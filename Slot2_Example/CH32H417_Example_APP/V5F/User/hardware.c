/********************************** (C) COPYRIGHT  *******************************
 * File Name          : hardware.c
 * Author             : WeAct Studio (teaching example for this board)
 * Version            : V1.0.0
 * Description        : example APP 的 V5F 侧：一个 CDC 口，周期性打印工程信息。
 *                      V5F side of the example APP: one CDC port that prints
 *                      the project information over and over.
 *
 * 这个 CDC **没有映射任何真实串口** —— 它只是往外打日志，主机发进来的字节只
 * 用来触发"再打一页"。想做真正的 USB 转串口，去看 CH32H417_USB2UART_APP：
 * 那边把三路 USART 接到 D0..D15 上。
 *
 * This CDC maps **no real UART** - it only prints.  Bytes from the host just
 * trigger "print one more page".  For a real USB-to-UART bridge see
 * CH32H417_USB2UART_APP, which wires three USARTs to D0..D15.
 *******************************************************************************/
#include "hardware.h"
#include "ch32h417_usbhs_device.h"
#include "usb_cdc.h"
#include "example_cfg.h"
#include "eeprom.h"
#include "stdio.h"

/* ---- USB 中断与主循环之间共享的状态 / state shared with the USB interrupt -- */
volatile uint8_t  cdc_tx_busy;
volatile uint8_t  cdc_rx_pending;
volatile uint16_t cdc_rx_len;
__attribute__((aligned(4))) uint8_t CDC_Rx_Buf[ DEF_USB_EP2_HS_SIZE ];

/* GET_LINE_CODING 的默认答案：115200-8N1。后面没有串口，改不改都无所谓，
 * 但 Windows 会读回它刚写下去的值，所以必须是可写的 RAM。
 * Default answer for GET_LINE_CODING: 115200-8N1.  Nothing is behind the port,
 * but Windows reads back what it wrote, so this has to live in RAM.          */
uint8_t cdc_line_coding[ 7 ] = { 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08 };

/* 发送缓冲：IN 传输进行中时 USB 控制器正在读它，所以不能是栈上的临时变量。
 * The USB controller reads this buffer while the transfer is in flight, so it
 * cannot be a temporary on the stack.                                        */
static uint8_t  cdc_tx_buf[ DEF_CDC_BULK_MPS ];

/* 信息页整页有两百多字节（十一行），超过一个 bulk 包。缓冲区只留一个包大小的
 * 时候 INFO_ADD 会把尾巴截掉、CDC_Send() 又因为超了 MPS-1 直接拒收 —— 症状是
 * "打开端口一个字都没有"。所以这里给足两包，发送按 CDC_SendChunked() 分片。
 * The page is longer than one bulk packet.  With a one-packet buffer INFO_ADD
 * clips the tail and CDC_Send() then refuses the lot, so the port looks mute;
 * hence two packets of room and a chunked sender.                            */
#define DEF_INFO_BUF_SIZE       ( 2u * DEF_CDC_BULK_MPS )
static uint8_t  cdc_info_buf[ DEF_INFO_BUF_SIZE ];

/* 粗略毫秒计数：1 ms 一步，只用来定时打印。
 * Rough millisecond counter - one step per millisecond, used for timing only. */
static uint32_t ms_tick;
static uint32_t tx_start_ms;
static uint32_t last_info_ms;

#define DEF_INFO_PERIOD_MS        2000u     /* 多久打一页 / page period      */
#define DEF_TX_TIMEOUT_MS         1000u     /* IN 看门狗 / IN watchdog       */

/*********************************************************************
 * @fn      CDC_TxIdle
 *
 * @brief   1 = EP2 IN 空闲 / 1 when the bulk IN endpoint is free
 */
uint8_t CDC_TxIdle(void)
{
    return ( cdc_tx_busy == 0u ) ? 1u : 0u;
}

/*********************************************************************
 * @fn      CDC_Send
 *
 * @brief   从 EP2 IN 发一段数据出去。
 *          Start a bulk IN transfer on EP2.
 *
 * @return  1 = 已启动，0 = 现在发不了（还没枚举 / 上一包还没发完 / 长度非法）
 *          1 = started, 0 = cannot right now (not enumerated, busy, bad len)
 *
 * @note    ⚠️ 长度必须小于 MPS：正好发满一个最大包时，主机端类驱动会当成
 *          "后面还有"而把数据扣在自己缓冲里，终端上一字节都看不到。
 *          ⚠️ The length must stay below the MPS: a transfer of exactly the
 *          maximum packet size reads as "more to come" to the host class
 *          driver, which then sits on the bytes instead of showing them.
 */
uint8_t CDC_Send( const uint8_t *buf, uint16_t len )
{
    if( USBHS_DevConfig == 0u )                 /* 还没枚举完 / not configured */
    {
        return 0u;
    }
    if( cdc_tx_busy != 0u )                     /* 上一包还没发完 / busy      */
    {
        return 0u;
    }
    if( ( len == 0u ) || ( len > (uint16_t)( DEF_CDC_BULK_MPS - 1u ) ) )
    {
        return 0u;
    }

    memcpy( cdc_tx_buf, buf, len );
    cdc_tx_busy  = 1u;
    tx_start_ms  = ms_tick;

    USBHSD->UEP2_TX_DMA  = (uint32_t)cdc_tx_buf;
    USBHSD->UEP2_TX_LEN  = len;
    USBHSD->UEP2_TX_CTRL = ( USBHSD->UEP2_TX_CTRL & ~USBHS_UEP_T_RES_MASK )
                           | USBHS_UEP_T_RES_ACK;
    return 1u;
}

/* 主机一直不来读，IN 完成中断就永远不会到，cdc_tx_busy 会一直挂着。
 * 超过 1 s 就当这次传输作废，下一页照常发。
 * If the host never reads, the IN-done interrupt never arrives and cdc_tx_busy
 * would stay set.  Release it after 1 s so the next page still goes out.     */
static void cdc_tx_watchdog(void)
{
    if( ( cdc_tx_busy != 0u ) &&
        ( (uint32_t)( ms_tick - tx_start_ms ) >= (uint32_t)DEF_TX_TIMEOUT_MS ) )
    {
        cdc_tx_busy = 0u;
    }
}

/*********************************************************************
 * @fn      CDC_SendChunked
 *
 * @brief   发一整段（可以超过一个 bulk 包），返回真正发出去的字节数。
 *
 *          Send a whole block, longer than one bulk packet allowed.
 *
 *          为什么不能简单调 CDC_Send()：它硬性拒收 len > MPS-1（见上），而这一页
 *          有五百多字节，于是整页被静默丢掉 —— 打开端口一个字都读不到就是这个。
 *          Why not just call CDC_Send(): it refuses anything above MPS-1 outright,
 *          and this page is over five hundred bytes, so the whole page was being
 *          dropped silently - which is exactly "the port opens and says nothing".
 *
 *          每片取 MPS-1（511）而不是 MPS（512）：正好填满一个包的 IN 会被 usbser
 *          扣着等短包才交给应用层，整页要拖到很久才成块到达；511 < 512 本身就是短包，
 *          每片都能立刻交上去。USB2UART 那边也是这么发的。
 *          Each piece is MPS-1 (511), never MPS: an IN that exactly fills the
 *          endpoint is held back by usbser until a short packet shows up, so a
 *          full page would arrive late and in lumps.  511 < 512 is a short packet
 *          by itself, so every piece is delivered as it goes.
 *
 *          片与片之间等上一包真的发完（EP2 IN 只有一个，缓冲区也只有一份，重叠就
 *          覆盖了在途数据）。等待有上限：主机一直不读时不能把主循环卡死 ——
 *          返回已发字节数，让这一页就此作废，下个周期照常。
 *          Pieces wait for the previous one to finish (there is a single EP2 IN
 *          and a single buffer - overlapping would overwrite data in flight).
 *          The wait is bounded: if the host never reads, the page is abandoned
 *          rather than hanging the main loop.
 *
 * @return  bytes handed to the host; < len means the page was cut short
 */
uint16_t CDC_SendChunked( const uint8_t *buf, uint16_t len )
{
    uint16_t sent = 0u;
    uint32_t guard;

    while( sent < len )
    {
        uint16_t chunk = (uint16_t)( len - sent );

        if( chunk > (uint16_t)( DEF_CDC_BULK_MPS - 1u ) )
        {
            chunk = (uint16_t)( DEF_CDC_BULK_MPS - 1u );
        }

        if( CDC_Send( buf + sent, chunk ) == 0u )
        {
            /* 端点还占着（或者设备都没枚举）：等它，但最多 200 ms —— 比 1 s 的
             * 看门狗短，所以不会等到看门狗把在途传输当掉。 */
            for( guard = 0u; guard < 200u; guard++ )
            {
                if( CDC_TxIdle() != 0u )
                {
                    break;
                }
                Delay_Ms( 1u );
            }
            if( CDC_TxIdle() == 0u )
            {
                break;                              /* 主机不读，收手        */
            }
            if( CDC_Send( buf + sent, chunk ) == 0u )
            {
                break;                              /* 还是发不出去          */
            }
        }

        sent = (uint16_t)( sent + chunk );
    }

    return sent;
}

/* ---------------------------------------------------------------------------
 * 工程信息页 / the project information page
 *
 * ⚠️ V5F 读配置区只能用 EE_Peek()。EE_Read()/EE_Write() 都要动 V3F 的那份
 *    上下文（当前页、seq），两个核各自留一份静态变量，V5F 记的会是 V3F 轮转
 *    之前的过期页 —— 读出来的可能是已经被覆盖的老值。EE_Peek() 一不碰上下文、
 *    二只做只读扫描，所以两核都安全。
 *
 * ⚠️ On V5F the only way to read the config area is EE_Peek().  EE_Read() and
 *    EE_Write() both touch the static context (current page, seq) and every
 *    core keeps its own copy, so V5F would hold the page that was current
 *    before V3F rotated the pair.  EE_Peek() touches no state at all.
 * -------------------------------------------------------------------------*/
#define INFO_ADD( ... )                                                      \
    do {                                                                     \
        int r = snprintf( (char *)buf + n, ( n < cap ) ? ( cap - n ) : 0u,   \
                          __VA_ARGS__ );                                      \
        if( r > 0 )                                                          \
        {                                                                    \
            n += (uint16_t)r;                                                \
            if( n > cap ) { n = cap; }                                       \
        }                                                                    \
    } while( 0 )

static uint16_t info_build( uint8_t *buf, uint16_t cap, uint32_t up_s )
{
    uint32_t v;
    uint32_t boots = 0u;
    uint32_t level = EX_LEVEL_3V3;
    uint32_t slot  = EX_APP_SLOT;
    uint16_t n = 0u;

    (void)EE_Peek( EE_KEY_EXAMPLE_BOOT, &boots );
    if( EE_Peek( EE_KEY_LEVEL, &v ) == EE_OK )
    {
        if( ( v >= EX_LEVEL_MIN ) && ( v <= EX_LEVEL_MAX ) ) { level = v; }
    }
    if( EE_Peek( EE_KEY_ACTIVE_SLOT, &v ) == EE_OK )
    {
        if( v < APP_SLOT_NUM ) { slot = v; }
    }

    INFO_ADD( "\r\n==== CH32H417 Example APP ====\r\n" );
    INFO_ADD( "vendor   : WeAct Studio\r\n" );
    INFO_ADD( "image    : slot %u, V3F @ 0x%06X, V5F @ 0x%06X\r\n",
              (unsigned)EX_APP_SLOT, (unsigned)EX_APP_BASE, (unsigned)EX_V5F_BASE );
    INFO_ADD( "burn     : CLI -f CH32H417_Example_APP.bin -b %08X\r\n",
              (unsigned)EX_APP_BASE_PHYS );
    INFO_ADD( "clocks   : V5F %u MHz, up %u s\r\n",
              (unsigned)( SystemCoreClock / 1000000u ), (unsigned)up_s );
    INFO_ADD( "IO level : %u.%u V  (DAC2 on PA5, config key 0x%08X)\r\n",
              (unsigned)( level / 10u ), (unsigned)( level % 10u ),
              (unsigned)EE_KEY_LEVEL );
    INFO_ADD( "boot cnt : %u  (config key 0x%08X)\r\n",
              (unsigned)boots, (unsigned)EE_KEY_EXAMPLE_BOOT );
    INFO_ADD( "active   : APP slot %u  (config key 0x%08X)\r\n",
              (unsigned)slot, (unsigned)EE_KEY_ACTIVE_SLOT );
    INFO_ADD( "key PE1  : short = next IO level, long 2 s = next APP slot\r\n" );
    INFO_ADD( "speed    : SET_LINE_CODING %u = IAP bootloader (FE17)\r\n",
              (unsigned)DEF_EX_IAP_BAUD );
    INFO_ADD( "IOs      : D0..D15 = PD10..PD15, PF0..PF2, PC6..PC9, PA13..PA15\r\n" );
    INFO_ADD( "this port: no UART behind it - send any byte to print again\r\n" );
    return n;
}

/*********************************************************************
 * @fn      Hardware
 *
 * @brief   V5F 主循环 / V5F main loop
 */
/* ---------------------------------------------------------------------------
 * 主机命令：波特率当命令使 / Host commands: the baud rate is the command
 * ---------------------------------------------------------------------------
 * 这个 CDC 后面没有真实串口，波特率存下来只是为了满足 Windows 的
 * GET_LINE_CODING 回读，真正被当命令用的只有 2048（见 example_cfg.h）。
 *
 * The line coding is stored only so Windows gets its own value back; the only
 * field that means anything here is 2048.                                   */
static volatile uint8_t ex_iap_request;

void EX_OnLineCoding( uint32_t baud )
{
    if( baud == (uint32_t)DEF_EX_IAP_BAUD )
    {
        ex_iap_request = 1u;
    }
}

/* 真正复位放在主循环，不在 EP0 中断里：控制传输还没被应答就掉线的话，
 * 主机看到的是一个残缺的传输，而不是"命令生效了"。
 * The reset itself runs from the main loop, never from the EP0 handler. */
static void ex_enter_iap(void)
{
    uint16_t len;
    uint32_t waited;

    len = (uint16_t)snprintf( (char *)cdc_info_buf, sizeof( cdc_info_buf ),
                              "\r\nentering IAP bootloader (FE17) ...\r\n" );
    (void)CDC_Send( cdc_info_buf, len );

    /* 给这行字一点时间发出去，最多 200 ms；发不出去也照样走，不留在这。 */
    for( waited = 0; waited < 200u; waited++ )
    {
        if( CDC_TxIdle() != 0u )
        {
            break;
        }
        Delay_Ms( 1 );
    }

    *(volatile uint32_t *)IapRamAddr = IapRamNum;
    __asm volatile( "fence" ::: "memory" );        /* 让这次写真的出去 */

    NVIC_SystemReset();

    while( 1 ) { }
}

void Hardware(void)
{
    uint16_t len;

    SystemAndCoreClockUpdate();
    Delay_Init();

    /* USBHS(USB 2.0) 设备初始化：EP1..EP2/EP3 与中断都在里面开好 */
    USBHS_Device_Init( ENABLE );

    while( 1 )
    {
        if( ex_iap_request != 0u )
        {
            ex_iap_request = 0u;
            ex_enter_iap();                     /* 不返回 / never returns */
        }

        /* 主机发来数据：回一行，并顺手刷新一页 */
        if( cdc_rx_pending != 0u )
        {
            uint16_t got = cdc_rx_len;

            cdc_rx_pending = 0u;
            len = (uint16_t)snprintf( (char *)cdc_info_buf, sizeof( cdc_info_buf ),
                                      "rx: %u bytes\r\n", (unsigned)got );
            (void)CDC_SendChunked( cdc_info_buf, len );
            last_info_ms = ms_tick;
        }

        /* 定时打一页工程信息（这一页超过一个 bulk 包，必须分片发） */
        if( (uint32_t)( ms_tick - last_info_ms ) >= (uint32_t)DEF_INFO_PERIOD_MS )
        {
            last_info_ms = ms_tick;
            len = info_build( cdc_info_buf, (uint16_t)sizeof( cdc_info_buf ),
                              ms_tick / 1000u );
            (void)CDC_SendChunked( cdc_info_buf, len );
        }

        cdc_tx_watchdog();

        Delay_Ms( 1 );
        ms_tick++;
    }
}
