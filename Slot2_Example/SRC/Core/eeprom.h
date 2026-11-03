/********************************** (C) COPYRIGHT  *******************************
 * File Name          : eeprom.h
 * Version            : V1.0.0
 * Description        : EEPROM emulation on the on-chip code flash (A/B pages).
 *******************************************************************************/

#ifndef __EEPROM_H
#define __EEPROM_H

#include "ch32h417.h"

/* ---------------------------------------------------------------------------
 * What this is
 * ------------
 * The part has no EEPROM and no backup registers, so non-volatile settings
 * (which APP slot is active, the level preset, ...) have to live in the code
 * flash.  The flash only programs 1 -> 0 and the smallest erase unit is a whole
 * page (4 KB or 8 KB depending on the part), so a byte cannot be rewritten.
 *
 * Classic fix: two pages, A and B, used as a rotating pair.
 *
 *   - Every write APPENDS a new record in the page currently in use.  Nothing
 *     is erased, so every earlier value stays readable until the pair runs out
 *     of room.
 *   - When both pages are full, the latest value of every key is copied into
 *     one page and the other is erased.
 *
 * The point of the second page is power-cut safety: at every instant at least
 * one page still holds every value.
 *
 * ⚠️⚠️ The one thing that is easy to get wrong here
 * ------------------------------------------------
 * An erased word does NOT read 0xFF.  Erased storage - or storage nobody has
 * touched since the part left the factory - answers every read with 0xE339E339.
 * Programming writes the data together with its ECC, and from then on the word
 * reads back exactly what was written, 0xFF included.
 *
 * That is what killed V1.  V1 asked "is this slot free?" by comparing against
 * 0xFFFFFFFF, so on this part no slot was ever free, every write looked like a
 * full pair and triggered a rotation, and each rotation erased and reprogrammed
 * the same page headers again and again.  A few dozen of those and both cores
 * stopped: USB silent, even the bootloader's HID gone.  It recovers on its own
 * once the part is reset; nothing in STATR reports anything.
 *
 * What V1's failure was NOT: a limit on how often a block may be programmed.
 * Measured on this very board (probe, below) a 256 byte block tolerates being
 * programmed 64 times after a single erase, one word at a time, the whole block
 * verified after every single write - all 64 checks passed.  So 16 byte records
 * appended with FLASH_ProgramWord() would work too - which is what this layout
 * now is, see "What a record costs" below.
 *
 * Verified on real silicon, 2026-10-02:
 *   0x08006000, inside the burned image              -> real code
 *   0x0802C000, past the image, never erased/written -> 0xE339E339
 *   probe page, one erase then 64 word programs in ONE block -> every read back
 *   exactly as written
 *
 * What a record costs
 * -------------------
 * 4 bytes - one word - is the smallest unit this flash can be programmed with,
 * and a record is four of them: magic|seq, key, value and a CRC that covers the
 * three before it.  16 bytes per record, 512 of them in an 8 KB page.
 *
 * V2 used to write each record as a whole 256 byte page with
 * FLASH_ProgramPage_Fast(), which cost 16x the space for no benefit: the part
 * has no need for it.  A probe on this very board showed a block tolerates
 * every one of its 64 words being programmed once between erases, so word-sized
 * appends are safe; they also take the ECC in together with the data, and every
 * word is read back because, unlike erase, programming reports nothing.
 *
 * And erase itself?  Still a whole 4 KB or 8 KB page and it cannot be made
 * smaller - that is not a choice in here, every erase Entry aligns its argument
 * down to a page boundary.  What got smaller is only what one WRITE consumes,
 * and that is where the density came from: 64 writes between erases became
 * 1024.
 *
 * Dropping the header also closes the rotation's power-cut window.  A copy
 * interrupted half way through is just a page holding some newer records - the
 * keys already copied read from it, the ones not reached yet still read from
 * the old page.  No value is ever held only in RAM.
 *
 * Who may call this
 * -----------------
 * Only V3F.  Programming and erasing run with the interrupts of the calling
 * core switched off, and V3F is the core that owns the LED, the key and the
 * DAC - a few tens of milliseconds without interrupts pass unnoticed there,
 * while V5F is left alone to keep the USB alive.
 *
 * The context below is a static, so every core would keep its own copy: a V5F
 * that called in as well would point at the page that was current before V3F
 * rotated the pair.  V5F must therefore not call anything that writes -
 * whatever it needs is mirrored over by V3F.
 *
 * EE_Peek() is the one exception: it touches no state at all and is safe from
 * either core.
 *
 * Record - four words, 16 bytes, written one word at a time:
 *
 *   +0   magic | seq : 0xE2A50000 | seq(16 bit)
 *   +4   key
 *   +8   value
 *   +12  crc         : low half = CRC16-CCITT of the first 12 bytes,
 *                      high half = its complement
 *
 * The CRC goes down LAST and covers everything before it, which is what makes a
 * half written record harmless: at any instant before it lands the 16 bytes do
 * not add up, so ee_blk_read() rejects them.  They cost a slot, nothing more.
 *
 * A record whose CRC does not match was interrupted while being written, or was
 * never ours; either way it is ignored and simply costs a slot.
 * ------------------------------------------------------------------------- */

/* Flash map of the 960 KB part (all boundaries are 8 KB aligned):
 *
 *   0x000000 .. 0x006000   IAP bootloader                  24 KB
 *                          (its last 12 B hold BOOT_UID_ADDR below)         
 *   0x006000 .. 0x052000   APP slot 0                     304 KB
 *   0x052000 .. 0x09E000   APP slot 1                     304 KB
 *   0x09E000 .. 0x0EA000   APP slot 2                     304 KB
 *   0x0EA000 .. 0x0EE000   config area, page A + page B    16 KB
 *   0x0EE000 .. 0x0F0000   spare                           8 KB
 */
#define APP_SLOT_SIZE        0x0004C000u                    /* 304 KB        */
#define APP_SLOT_NUM         3u
/* Address the CPU jumps to.  The flash is aliased at 0x00000000, so slot 0 is
 * reached at 0x6000 - do NOT add FLASH_BASE here.                           */
#define APP_SLOT_ADDR(n)     (0x00006000u + ((uint32_t)(n)) * APP_SLOT_SIZE)
#define APP_SLOT_DEFAULT     0u

/* Anti-clone copy of the chip UID, in the last 12 bytes of the bootloader's own
 * region - just below the APP entry at 0x00006000.
 *
 * The bootloader programs the UID here the first time it runs, and every image
 * refuses to start unless the copy still matches the chip, so a cloned flash
 * image dropped on a different chip stalls instead of running.
 *
 * ⚠️ ONE definition, used by all three images: the bootloader writes and checks
 * it, the APPs only check it, and they have to agree.  They did not once - the
 * APPs were still reading 0x4000 while the bootloader had moved here - and the
 * first time 0x4000 got blanked the APP sat in an endless reset loop: no USB
 * and no bootloader HID either, because the bootloader only ever got as far as
 * jumping to an APP that reset the chip again.  It is indistinguishable from a
 * dead board, which is what made it expensive.  The address now lives in this
 * one header for that reason; do not spell the number out again elsewhere.
 *
 * Keep the bootloader image below BOOT_UID_ADDR.  Its region is
 * 0x000000..0x006000 and the current build ends around 0x36EC, so there is
 * roughly 10 KB of room left.
 *
 * BOOT_UID_ADDR is the execution alias (what the CPU reads); BOOT_UID_PHYS is
 * what the flash controller wants (FLASH_BASE based, what FLASH_ProgramWord
 * takes).                                                                   */
#define BOOT_UID_ADDR        0x00005FF4u                    /* 0x6000 - 12   */
#define BOOT_UID_PHYS        (FLASH_BASE + BOOT_UID_ADDR)   /* 0x08005FF4    */
#define BOOT_UID_WORDS       3u                             /* UID is 12 B   */
#define CHIP_UID_ADDR        0x1FFFF7E8u                    /* factory UID   */

/* Config area.  Execution alias in EE_CFG_ADDR, address the flash controller
 * expects (FLASH_BASE based) in EE_CFG_PHYS.                                */
#define EE_CFG_ADDR          0x000EA000u
#define EE_CFG_PHYS          (FLASH_BASE + EE_CFG_ADDR)     /* 0x080EA000    */

/* Key namespaces.  The top byte of a key says who owns it, so one image can
 * never tread on another image's settings.  Segments 0x01..0xFE belong to the
 * application images, 0xFF - the LAST one - belongs to the bootloader.
 *
 * The IAP keys sit in the top segment on purpose: the application segments can
 * then grow upwards for as long as they like without ever colliding with them,
 * and "is this a system key" is a single mask test.
 *
 * A segment belongs to an image, not to a slot, so an image keeps its settings
 * when it is burned into another slot.                                      */
#define EE_NS_APP(n)         (((uint32_t)(n)) << 24u)       /* n = 1 .. 0xFE */
#define EE_NS_USB2UART       EE_NS_APP(1u)                  /* 0x01xxxxxx    */
#define EE_NS_LOGICANA       EE_NS_APP(2u)                  /* 0x02xxxxxx    */
#define EE_NS_APP3           EE_NS_APP(3u)                  /* 0x03xxxxxx    */
#define EE_NS_SYS            0xFF000000u                    /* IAP, last     */
#define EE_KEY_IS_SYS(key)   (((key) & 0xFF000000u) == EE_NS_SYS)

/* Keys.  Add new settings here, the storage layer is generic.               */
#define EE_KEY_ACTIVE_SLOT   (EE_NS_SYS | 0x00000001u)      /* value: 0..2   */
#define EE_KEY_LEVEL         (EE_NS_USB2UART | 0x00000001u) /* value: 0..3   */

/* Return codes */
#define EE_OK                0u
#define EE_ERR               1u                             /* not found/full */
#define EE_NOINIT            2u                             /* area unusable  */

/* One record: four words, 16 bytes.  4 bytes is the smallest unit this flash
 * can be programmed with, so this is the smallest a record can be while still
 * carrying its own key, value and CRC.                                       */
#define EE_SLOT_SIZE         16u

/* Magic of a block.  It differs from the V1 record magic (0xA55A0000) on
 * purpose: anything an earlier firmware left behind then reads as foreign and
 * is wiped on start-up instead of being mistaken for one of our records.     */
#define EE_BLK_MAGIC         0xE2A50000u

/* What an erased word reads back as.
 *
 * ⚠️ It is NOT 0xFF.  Storage that has been erased (or never touched since the
 * chip left the factory) answers every read with 0xE339E339 - there is no ECC
 * written for it yet, and the flash reports that instead of pretending the
 * word is all ones.  Programming writes data AND its ECC, after which the word
 * reads back exactly what was written, 0xFF included - which is why the filler
 * below is 0xFF even though erased cells are not.
 *
 * Getting this wrong costs everything: with 0xFF as the "free" test no block
 * is ever recognised as empty, so every write looks like the pair is full and
 * tries to rotate, which erases and reprograms the same block over and over -
 * and that is what locks the controller up (see the top of this file).
 *
 * Verified on real silicon: address 0x0802C000, past the end of the burned
 * image and never erased nor programmed by anything here, reads 0xE339E339,
 * while 0x08006000 inside the image reads real code.                        */
#define EE_ERASED_WORD       0xE339E339u

typedef struct
{
    uint32_t page;                                          /* erase page size */
    uint32_t cur;                                           /* page in use     */
    uint32_t bak;                                           /* spare page      */
    uint16_t seq;                                           /* last seq used   */
    uint8_t  ready;                                         /* 1 = usable      */
} ee_ctx_t;

void     EE_Init(void);
uint8_t  EE_Ready(void);
uint8_t  EE_Read(uint32_t key, uint32_t *val);
uint8_t  EE_Peek(uint32_t key, uint32_t *val);              /* read only      */
uint8_t  EE_Inspect(uint32_t *seq, uint32_t *free);         /* read only      */
uint8_t  EE_Write(uint32_t key, uint32_t val);
uint8_t  EE_EraseAll(void);
uint8_t  EE_Migrate(void);                                  /* see below      */
uint32_t EE_Free(void);                                     /* free slots     */
uint32_t EE_Seq(void);                                      /* newest record  */
uint32_t EE_PageSize(void);                                 /* 4096 or 8192   */

/* ---------------------------------------------------------------------------
 * EE_Migrate - one time clean-up of an area written by an older record size
 *
 * Firmware that used 256 byte records leaves its four words looking exactly
 * like ours - the layout did not change, only the distance between records -
 * so an old area still READS fine and nothing is lost.  What it does waste is
 * space: the 240 filler bytes of every old record (programmed 0xFF, which is
 * NOT an erased word) occupy slots that can never be programmed again.
 *
 * Calling this once, right after EE_Init(), erases both pages and writes every
 * live key back at the new spacing.  Same net effect as wiping the area, with
 * the values kept - which matters for the settings that have to survive a
 * power cycle (the level preset, how often the board has booted).
 *
 * It costs nothing when there is nothing to do: no filler found, no erase.
 *
 * ⚠️ This is deliberately NOT inside EE_Init().  EE_Init() has to stay read
 * only - that contract is why it is safe to run on the way up - so this is a
 * separate step every image takes next to it.
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * Sizes, for the record
 *
 * 8 KB page -> 512 slots per page, 1024 over the pair.  A rotation gives back
 * everything except the keys themselves, so a device that keeps rewriting a
 * handful of settings gets a thousand writes per erase cycle instead of the
 * sixty the 256 byte records allowed:
 *
 *   writes between erases ~= 1024 - keys
 *
 * What must NEVER come back:
 *   - testing a free slot against 0xFFFFFFFF (V1).  Erased reads 0xE339E339,
 *     and getting it wrong makes every write rotate, which is what locked the
 *     part up.  Appending 16 B records with FLASH_ProgramWord() is fine - the
 *     probe showed a block tolerates 64 word programs - it was never the
 *     problem.
 *   - reading only the "current" page: that is what would turn an interrupted
 *     rotation into lost settings.  A read has to take the highest sequence
 *     number it finds across BOTH pages.
 * ------------------------------------------------------------------------- */

#endif /* __EEPROM_H */
