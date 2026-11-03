/********************************** (C) COPYRIGHT  *******************************
 * File Name          : eeprom.c
 * Version            : V3.0.0
 * Description        : EEPROM emulation on the on-chip code flash (A/B pages).
 *
 * See eeprom.h for why both pages exist and what a record costs.  Short version
 * of V3:
 *
 *   - One record = four words, written one FLASH_ProgramWord() at a time, so
 *     a write costs 16 bytes instead of the 256 byte page V2 burned.
 *   - There is no page header.  Every record carries a sequence number and a
 *     read takes the one with the highest number, looking at BOTH pages at
 *     once.  Ownership therefore follows the data instead of a status word,
 *     and a half-finished copy is simply a set of newer records that have not
 *     been written yet - nothing has been lost when the power comes back.
 *   - A write whose value the key already holds costs nothing at all.
 *   - EE_Init is READ ONLY: not one erase, not one program.  It runs before
 *     there is any USB to report a problem with, so it must not be able to
 *     hang.  Cleaning up is EE_Write()'s job - it rotates when it runs out of
 *     room, and by then the board is up.  The one time an image wants the area
 *     rewritten in the new spacing it calls EE_Migrate() itself, right after
 *     EE_Init(); that too is nothing but the rotation EE_Write() would do.
 *******************************************************************************/

#include "eeprom.h"
#include "ch32h417_flash.h"
#include <string.h>

/* Keys that can be carried across a page rotation.  One page holds
 * pagesize/16 slots, far more than any image needs for settings - this only
 * has to be larger than the number of distinct keys in use.                   */
#define EE_MAX_KEYS        32u

typedef struct
{
    uint32_t key;
    uint32_t val;
    uint32_t seq;
} ee_item_t;

static ee_ctx_t  ee;

/* Only ever touched with the interrupts of this core off.  It lives in .bss
 * and not on the stack: V3F runs with a 1 KB stack.                            */
static ee_item_t ee_items[EE_MAX_KEYS];                 /* used when rotating */

/*********************************************************************
 * @fn      ee_crc16
 *
 * @brief   CRC16-CCITT over the first three words of a record.
 */
static uint16_t ee_crc16(const uint32_t *w, uint32_t nwords)
{
    uint16_t      crc = 0xFFFF;
    const uint8_t *p = (const uint8_t *)w;
    uint32_t      n = nwords * 4u;

    while(n--)
    {
        crc ^= (uint16_t)(*p++) << 8;
        for(uint32_t i = 0; i < 8; i++)
        {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/*********************************************************************
 * @fn      ee_slot_num / ee_slot_addr
 *
 * @brief   Geometry of one page, given its size in bytes.  A slot is one
 *          record - 16 bytes, four words.
 */
static uint32_t ee_slot_num(uint32_t pagesz)
{
    return pagesz / EE_SLOT_SIZE;
}

static uint32_t ee_slot_addr(uint32_t page, uint32_t idx)
{
    return page + idx * EE_SLOT_SIZE;
}

/*********************************************************************
 * @fn      ee_slot_empty
 *
 * @brief   1 = no word of the slot has ever been programmed, so it is free and
 *          each of its four words may be programmed once.
 *
 *          Erased storage answers EE_ERASED_WORD, not 0xFF - see eeprom.h.
 *          That is exactly what makes "programmed with 0xFF" distinguishable
 *          from "never touched", which ee_has_legacy() below relies on.
 */
static uint8_t ee_slot_empty(uint32_t addr)
{
    for(uint32_t i = 0; i < (EE_SLOT_SIZE / 4u); i++)
    {
        if(((vu32 *)addr)[i] != EE_ERASED_WORD)
        {
            return 0;
        }
    }
    return 1;
}

/*********************************************************************
 * @fn      ee_slot_read
 *
 * @brief   Takes the record apart.  0 when the 16 bytes are not a complete
 *          record: either still erased, only partly programmed (power dropped
 *          while it was being written) or foreign data left behind by another
 *          layout.
 *
 *          The CRC is what makes half written records harmless - it is the last
 *          word down and covers the three before it, so a write that was cut
 *          short never adds up.  Records like that are simply skipped, and
 *          because a read never stops at them nothing else is disturbed.
 */
static uint8_t ee_slot_read(uint32_t addr, uint32_t *seq, uint32_t *key, uint32_t *val)
{
    const uint32_t *w = (const uint32_t *)addr;
    uint16_t        c, nc;

    if((w[0] & 0xFFFF0000u) != EE_BLK_MAGIC)
    {
        return 0;
    }

    c = ee_crc16(w, 3);
    nc = (uint16_t)~c;

    if(w[3] != (((uint32_t)nc << 16) | c))
    {
        return 0;
    }

    *seq = w[0] & 0x0000FFFFu;
    *key = w[1];
    *val = w[2];

    return 1;
}

/*********************************************************************
 * @fn      ee_page_scan
 *
 * @brief   Counts a page's records, free blocks and leftovers, and reports the
 *          newest sequence number found.
 */
static void ee_page_scan(uint32_t page, uint32_t pagesz,
                         uint32_t *valid, uint32_t *free, uint32_t *maxseq)
{
    uint32_t n = ee_slot_num(pagesz);
    uint32_t s, k, v;

    *valid  = 0;
    *free   = 0;
    *maxseq = 0;

    for(uint32_t i = 0; i < n; i++)
    {
        uint32_t a = ee_slot_addr(page, i);

        if(ee_slot_empty(a))
        {
            (*free)++;
        }
        else if(ee_slot_read(a, &s, &k, &v))
        {
            (*valid)++;
            if(s > *maxseq)
            {
                *maxseq = s;
            }
        }
        else
        {
            /* Junk: a block nobody claims.  It can never be programmed again,
             * so it only costs a slot - nothing else looks at it.           */
        }
    }
}

/*********************************************************************
 * @fn      ee_lookup
 *
 * @brief   Newest value of key over BOTH pages.
 *
 *          Looking at both pages is what removes the need for a page header,
 *          and therefore removes the window a rotation would otherwise have:
 *          a key whose newer record has been copied already is read from the
 *          new page, one that has not been reached yet still reads correctly
 *          from the old one.
 */
static uint8_t ee_lookup(uint32_t pagesz, uint32_t key, uint32_t *val)
{
    uint32_t n = ee_slot_num(pagesz);
    uint32_t best = 0, s, k, v;
    uint8_t  found = 0;

    for(uint32_t p = 0; p < 2u; p++)
    {
        uint32_t page = EE_CFG_PHYS + p * pagesz;

        for(uint32_t i = 0; i < n; i++)
        {
            uint32_t a = ee_slot_addr(page, i);

            if(!ee_slot_read(a, &s, &k, &v))
            {
                continue;
            }
            if((k == key) && (!found || (s > best)))
            {
                best = s;
                *val = v;
                found = 1;
            }
        }
    }

    return found ? EE_OK : EE_ERR;
}

/*********************************************************************
 * @fn      ee_collect
 *
 * @brief   Latest value of every key over both pages, for a rotation.
 *          Returns how many keys there are; EE_MAX_KEYS is the limit.
 */
static uint32_t ee_collect(uint32_t pagesz)
{
    uint32_t n = ee_slot_num(pagesz);
    uint32_t cnt = 0;
    uint32_t s, k, v;

    for(uint32_t p = 0; p < 2u; p++)
    {
        uint32_t page = EE_CFG_PHYS + p * pagesz;

        for(uint32_t i = 0; i < n; i++)
        {
            uint32_t a = ee_slot_addr(page, i);
            uint32_t j;

            if(!ee_slot_read(a, &s, &k, &v))
            {
                continue;
            }

            for(j = 0; j < cnt; j++)
            {
                if(ee_items[j].key == k)
                {
                    if(s > ee_items[j].seq)
                    {
                        ee_items[j].seq = s;
                        ee_items[j].val = v;
                    }
                    break;
                }
            }
            if((j == cnt) && (cnt < EE_MAX_KEYS))
            {
                ee_items[cnt].key = k;
                ee_items[cnt].val = v;
                ee_items[cnt].seq = s;
                cnt++;
            }
        }
    }
    return cnt;
}

/*********************************************************************
 * @fn      ee_wait_ready
 *
 * @brief   Waits for the flash controller to stop being busy.
 *
 *          Every driver call in ch32h417_flash.c waits on SR_BSY with a bare
 *          `while(FLASH->STATR & SR_BSY);` - no limit at all.  A controller
 *          that has stopped answering therefore hangs the caller forever, and
 *          when that happens on the way up it takes the whole board with it:
 *          no USB, no bootloader HID, nothing left but a power cycle.  That is
 *          exactly how the board looked the two times this bit us.
 *
 *          Asking first does not make a stuck erase unstick, but it keeps a
 *          second operation from being piled on top of a bad state, and it
 *          turns "the board is dead" into "the storage is not usable" - the
 *          USB stays up and the failure can actually be seen.
 *
 * @return  1 - ready, 0 - still busy, do not start anything
 */
#define EE_SR_BSY                 0x00000001u
#define EE_READY_TRIES            1000000u

static uint8_t ee_wait_ready(void)
{
    uint32_t guard = EE_READY_TRIES;

    while((FLASH->STATR & EE_SR_BSY) != 0u)
    {
        if(--guard == 0u)
        {
            return 0;
        }
    }
    return 1;
}

/*********************************************************************
 * @fn      ee_erase_page
 *
 * @brief   Erases one physical page.  Erasing needs KEYR and MODEKEYR, so the
 *          lock bit alone does not close the key registers.
 */
static uint8_t ee_erase_page(uint32_t page)
{
    if(!ee_wait_ready())
    {
        return EE_ERR;                                  /* do not pile on     */
    }

    FLASH_Unlock_Fast();
    if(FLASH_ErasePage(page) != FLASH_COMPLETE)
    {
        FLASH_Lock_Fast();
        FLASH_Lock();
        return EE_ERR;
    }
    FLASH_Lock_Fast();
    FLASH_Lock();

    /* Read one word per block back.  It doubles as a sanity check that the
     * page really is storage: an erase outside the part "succeeds" just as
     * happily, but nothing could ever be written there.                     */
    for(uint32_t i = 0, n = ee_slot_num(ee.page); i < n; i++)
    {
        if(*(vu32 *)ee_slot_addr(page, i) != EE_ERASED_WORD)
        {
            return EE_ERR;
        }
    }
    return EE_OK;
}

/*********************************************************************
 * @fn      ee_find_free
 *
 * @brief   First still erased block of a page.
 */
static uint8_t ee_find_free(uint32_t page, uint32_t pagesz, uint32_t *addr)
{
    uint32_t n = ee_slot_num(pagesz);

    for(uint32_t i = 0; i < n; i++)
    {
        uint32_t a = ee_slot_addr(page, i);

        if(ee_slot_empty(a))
        {
            *addr = a;
            return EE_OK;
        }
    }
    return EE_ERR;
}

/*********************************************************************
 * @fn      ee_write_slot
 *
 * @brief   Programs one 16 byte record, one word at a time, and reads it back.
 *
 *          4 bytes is the smallest unit this flash accepts, so this is what
 *          "min write unit = 4 bytes" comes down to: a record costs 16 bytes
 *          instead of the 256 byte burst FLASH_ProgramPage_Fast() would take,
 *          and the part was measured tolerating exactly this - every word of a
 *          block programmed once, each read back, after a single erase.
 *
 *          Order matters more than it looks.  The CRC goes down LAST and covers
 *          the three words before it, so at no point does a partially written
 *          record add up: power lost after any of the first three words leaves
 *          16 bytes that ee_slot_read() rejects.  They cost a slot, nothing
 *          else, and nothing has to be repaired afterwards.
 *
 *          Word programming runs in the ordinary mode, plain FLASH_Unlock() -
 *          the same unlock the bootloader uses for the UID copy, which is the
 *          path this was learned from.  It reports a status per word, unlike
 *          the page burst, but the read back stays anyway: it is the only check
 *          that the data really landed.
 */
static uint8_t ee_write_slot(uint32_t addr, uint32_t seq, uint32_t key, uint32_t val)
{
    uint32_t  w[4];
    uint16_t  c, nc;

    w[0] = EE_BLK_MAGIC | (seq & 0x0000FFFFu);
    w[1] = key;
    w[2] = val;
    c = ee_crc16(w, 3);
    nc = (uint16_t)~c;
    w[3] = ((uint32_t)nc << 16) | c;

    if(!ee_wait_ready())                                /* see ee_wait_ready  */
    {
        return EE_ERR;
    }

    FLASH_Unlock();
    for(uint32_t i = 0; i < (EE_SLOT_SIZE / 4u); i++)
    {
        if(FLASH_ProgramWord(addr + i * 4u, w[i]) != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return EE_ERR;
        }
    }
    FLASH_Lock();

    for(uint32_t i = 0; i < (EE_SLOT_SIZE / 4u); i++)
    {
        if(*(vu32 *)(addr + i * 4u) != w[i])
        {
            return EE_ERR;
        }
    }
    return EE_OK;
}

/*********************************************************************
 * @fn      ee_rotate
 *
 * @brief   Frees the pair up again: copy the live values into the spare page
 *          and erase the one that was in use.
 *
 *          Order is deliberate.  The copy lands in the spare page first, so
 *          until the last block of the old page is gone every key is readable
 *          from one of the two - there is no instant at which the values exist
 *          only in RAM.  A rotation cut short simply leaves a partly filled
 *          page that start-up reads as "the newer records", with the rest still
 *          coming from the old one.
 *
 *          reset_seq restarts the numbering, which is what keeps a long lived
 *          device away from the 0xFFFF wrap around.
 */
static uint8_t ee_rotate(uint8_t reset_seq)
{
    uint32_t n, dst = ee.bak, old = ee.cur;
    uint32_t seq = reset_seq ? 0u : ee.seq;

    n = ee_collect(ee.page);
    if(n > ee_slot_num(ee.page))
    {
        return EE_ERR;                                  /* would not fit      */
    }

    if(ee_erase_page(dst) != EE_OK)
    {
        return EE_ERR;
    }
    for(uint32_t i = 0; i < n; i++)
    {
        seq++;
        if(ee_write_slot(ee_slot_addr(dst, i), seq,
                        ee_items[i].key, ee_items[i].val) != EE_OK)
        {
            return EE_ERR;
        }
    }
    if(ee_erase_page(old) != EE_OK)
    {
        return EE_ERR;
    }

    ee.cur = dst;
    ee.bak = old;
    ee.seq = (uint16_t)seq;

    return EE_OK;
}

/*********************************************************************
 * @fn      ee_has_legacy
 *
 * @brief   1 = the pair holds records written by firmware that spaced them
 *          256 bytes apart.
 *
 *          Those four words read exactly like ours - the layout never changed,
 *          only the distance between records - so an old area is still readable
 *          and nothing in it is wrong.  What gives it away is its filler: such
 *          a firmware programmed the 240 bytes behind every record with 0xFF.
 *
 *          And that is detectable, because PROGRAMMED 0xFF and ERASED do not
 *          look alike on this part.  Erased (or never touched) answers every
 *          read with 0xE339E339; a word that was programmed with 0xFF reads
 *          back 0xFFFFFFFF, with its ECC written alongside.  This layout never
 *          writes 0xFF anywhere, so a 0xFFFFFFFF in either page can only have
 *          come from there.
 *
 *          Slots that hold a valid record are skipped, so a value that happens
 *          to be 0xFFFFFFFF can never be mistaken for filler.
 */
static uint8_t ee_has_legacy(void)
{
    uint32_t n = ee_slot_num(ee.page);
    uint32_t s, k, v;

    for(uint32_t p = 0; p < 2u; p++)
    {
        uint32_t page = EE_CFG_PHYS + p * ee.page;

        for(uint32_t i = 0; i < n; i++)
        {
            uint32_t a = ee_slot_addr(page, i);

            if(ee_slot_read(a, &s, &k, &v))             /* one of our records  */
            {
                continue;
            }
            for(uint32_t j = 0; j < (EE_SLOT_SIZE / 4u); j++)
            {
                if(((vu32 *)a)[j] == 0xFFFFFFFFu)
                {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/*********************************************************************
 * @fn      EE_Migrate
 *
 * @brief   Rewrites the pair at today's spacing, keeping every value.
 *
 *          Called once, right after EE_Init().  It is the rotation EE_Write()
 *          would do the moment it ran out of room, done early and while there
 *          is still plenty of space, so the old filler is gone and every one of
 *          the ~1000 slots is usable again:
 *
 *            - the values are read from BOTH pages first, so what survives is
 *              exactly what a read would have returned;
 *            - they land in the spare page while both still exist, so at no
 *              instant do the settings live only in RAM - the reason this is
 *              not a plain erase and rewrite;
 *            - only then is the old page erased.
 *
 *          Nothing happens on an area that is already migrated: no filler, no
 *          erase, no write.  And if the rotation fails - an erase refused, the
 *          power dropped mid copy - next start-up simply tries again, because
 *          the old records are still there and still readable.
 *
 * ⚠️ This erases and programs, so it must NOT be folded into EE_Init().  See
 *   EE_Init's own note for why that line matters; this is kept as a separate
 *   call simply so the read-only contract of EE_Init stays true.
 *
 * @return  EE_OK - nothing to do, or done; EE_ERR - could not, retry next boot
 */
uint8_t EE_Migrate(void)
{
    uint8_t res;

    if(!ee.ready)
    {
        return EE_NOINIT;
    }

    if(!ee_has_legacy())
    {
        return EE_OK;                                   /* already current     */
    }

    __disable_irq();
    res = ee_rotate(1u);
    __enable_irq();

    return res;
}

/*********************************************************************
 * @fn      EE_Init
 *
 * @brief   Measures the pages and picks the one to append to.  Run before any
 *          read or write.
 *
 *          ⚠️ READ ONLY.  Not one erase, not one program.
 *
 *          That is the whole point, and it is not paranoia: EE_Init() runs on
 *          the way up, before USB, before the second core, before there is any
 *          way to say what went wrong.  Any erase or program in there is a bet
 *          that the flash controller happens to be answering right now - lose
 *          it and the board comes up silent, no USB and no bootloader HID, and
 *          only a power cycle gets it back.  That is exactly how this board
 *          died twice, once in V1 (page header written here) and once again
 *          later with a page wipe in here.
 *
 *          Nothing needs cleaning up here anyway.  A block nobody claims -
 *          leftovers of an older layout, or one whose write was cut short - is
 *          simply skipped by every scan and costs a slot; it is never mistaken
 *          for data.  When the pair really does run out of room, the rotation
 *          inside EE_Write() wipes it, and by then the board is up and running
 *          and a failure is something that can be reported instead of a brick.
 */
void EE_Init(void)
{
    uint32_t a, b, va, vb, fa, fb, sa, sb;

    memset(&ee, 0, sizeof(ee));

    /* Bit 28 of FLASH_CFGR0: 1 = dual bank, 8 KB erase pages, 960 KB usable. */
    ee.page = ((*(vu32 *)FLASH_CFGR0_BASE) & (1u << 28)) ? 8192u : 4096u;

    /* Single bank parts only accept addresses below FLASH_BASE + 0x78000, so
     * this config area does not exist there.  Report the storage as unusable
     * rather than failing every access.                                      */
    if((ee.page == 4096u) && ((EE_CFG_PHYS + 2u * ee.page) > (FLASH_BASE + 0x78000u)))
    {
        ee.ready = 0;
        return;
    }

    a = EE_CFG_PHYS;
    b = EE_CFG_PHYS + ee.page;

    ee_page_scan(a, ee.page, &va, &fa, &sa);
    ee_page_scan(b, ee.page, &vb, &fb, &sb);

    /* Whichever page holds the newest record is the one to append to; on a
     * blank part that is page A.                                            */
    if(sb > sa)
    {
        ee.cur = b;
        ee.bak = a;
    }
    else
    {
        ee.cur = a;
        ee.bak = b;
    }
    ee.seq = (uint16_t)((sa > sb) ? sa : sb);

    ee.ready = 1;
}

/*********************************************************************
 * @fn      EE_Ready
 */
uint8_t EE_Ready(void)
{
    return ee.ready;
}

/*********************************************************************
 * @fn      EE_PageSize
 */
uint32_t EE_PageSize(void)
{
    return ee.page;
}

/*********************************************************************
 * @fn      EE_Seq
 *
 * @brief   Sequence number of the newest record, 0 on a blank area.  Handy to
 *          see whether a write really landed.
 */
uint32_t EE_Seq(void)
{
    return ee.ready ? (uint32_t)ee.seq : 0u;
}

/*********************************************************************
 * @fn      EE_Inspect
 *
 * @brief   How full the pair is, derived from the flash alone.
 *
 *          Same idea as EE_Peek: no context, no repair, safe from either core
 *          and usable without EE_Init.  Handy to see whether a write really
 *          landed and how many are left before the next rotation.
 */
uint8_t EE_Inspect(uint32_t *seq, uint32_t *free)
{
    uint32_t page = ((*(vu32 *)FLASH_CFGR0_BASE) & (1u << 28)) ? 8192u : 4096u;
    uint32_t va, vb, fa, fb, sa, sb;

    if((page == 4096u) && ((EE_CFG_PHYS + 2u * page) > (FLASH_BASE + 0x78000u)))
    {
        return EE_NOINIT;
    }

    ee_page_scan(EE_CFG_PHYS,         page, &va, &fa, &sa);
    ee_page_scan(EE_CFG_PHYS + page,  page, &vb, &fb, &sb);

    if(seq)
    {
        *seq = (sa > sb) ? sa : sb;
    }
    if(free)
    {
        *free = fa + fb;
    }
    return EE_OK;
}

/*********************************************************************
 * @fn      EE_Read
 *
 * @brief   Newest value of key.  EE_ERR when the key was never written.
 */
uint8_t EE_Read(uint32_t key, uint32_t *val)
{
    if(!ee.ready)
    {
        return EE_NOINIT;
    }
    return ee_lookup(ee.page, key, val);
}

/*********************************************************************
 * @fn      EE_Peek
 *
 * @brief   Read a key without needing EE_Init and without touching any state.
 *
 *          This is what an APP uses to display a setting: it never repairs,
 *          formats or erases anything, so it is safe from the second core too.
 */
uint8_t EE_Peek(uint32_t key, uint32_t *val)
{
    uint32_t page = ((*(vu32 *)FLASH_CFGR0_BASE) & (1u << 28)) ? 8192u : 4096u;

    if((page == 4096u) && ((EE_CFG_PHYS + 2u * page) > (FLASH_BASE + 0x78000u)))
    {
        return EE_NOINIT;
    }

    return ee_lookup(page, key, val);
}

/*********************************************************************
 * @fn      EE_Write
 *
 * @brief   Appends one record, rotating the pair when both pages are full.
 *
 *          Two guarantees matter here and both come from V2:
 *
 *            - No block is ever programmed twice.  Append only ever goes to a
 *              block that still reads as erased, and a rotation writes into a
 *              page that has just been erased.  That is the rule V1 broke.
 *            - Writing what a key already holds costs nothing, so a setting
 *              rewritten with the same value every start-up does not wear out
 *              the storage.
 *
 *          Interrupts are off for the whole operation: erasing and programming
 *          stop the flash from answering a fetch.  Only the calling core is
 *          affected, which is why this runs on V3F - V5F keeps serving USB.
 */
uint8_t EE_Write(uint32_t key, uint32_t val)
{
    uint32_t old, a = 0;
    uint8_t  res;

    if(!ee.ready)
    {
        return EE_NOINIT;
    }

    if(EE_Read(key, &old) == EE_OK && old == val)
    {
        return EE_OK;                                   /* nothing to do      */
    }

    __disable_irq();

    /* Keep well clear of the wrap around: renumber while there is still room
     * to write the values back.                                             */
    if((ee.seq >= 0xFF00u) && (ee_rotate(1u) != EE_OK))
    {
        __enable_irq();
        return EE_ERR;
    }

    if(ee_find_free(ee.cur, ee.page, &a) != EE_OK)
    {
        if(ee_find_free(ee.bak, ee.page, &a) != EE_OK)
        {
            if(ee_rotate(0u) != EE_OK)
            {
                __enable_irq();
                return EE_ERR;
            }
            if(ee_find_free(ee.cur, ee.page, &a) != EE_OK)
            {
                __enable_irq();
                return EE_ERR;
            }
        }
    }

    ee.seq = (uint16_t)(ee.seq + 1u);
    res = ee_write_slot(a, ee.seq, key, val);

    /* Keep "page with the newest record" true, otherwise the next rotation
     * would wipe exactly the page everything was just written to.           */
    if(res == EE_OK)
    {
        if((a < ee.cur) || (a >= (ee.cur + ee.page)))
        {
            uint32_t t = ee.cur;
            ee.cur = ee.bak;
            ee.bak = t;
        }
    }

    __enable_irq();

    return res;
}

/*********************************************************************
 * @fn      EE_EraseAll
 *
 * @brief   Back to a blank config area.
 */
uint8_t EE_EraseAll(void)
{
    uint8_t res;

    if(!ee.ready)
    {
        return EE_NOINIT;
    }

    __disable_irq();
    res = ee_erase_page(ee.cur);
    if(res == EE_OK)
    {
        res = ee_erase_page(ee.bak);
    }
    if(res == EE_OK)
    {
        ee.seq = 0;
    }
    __enable_irq();

    return res;
}

/*********************************************************************
 * @fn      EE_Free
 *
 * @brief   Slots that may still be written, over both pages.  0 means the
 *          next write triggers a rotation (or fails if that one fails too).
 *
 *          A slot is one 16 byte record, so this is the number of writes left,
 *          not a number of bytes.
 */
uint32_t EE_Free(void)
{
    uint32_t va, vb, fa, fb, sa, sb;

    if(!ee.ready)
    {
        return 0;
    }

    ee_page_scan(ee.cur, ee.page, &va, &fa, &sa);
    ee_page_scan(ee.bak, ee.page, &vb, &fb, &sb);

    return fa + fb;
}
