/********************************** (C) COPYRIGHT  *******************************
 * File Name          : hardware.c
 * Author             : WeAct Studio (teaching example for this board)
 * Version            : V1.0.0
 * Description        : 第三个 APP（example）的板级驱动。
 *                      Board support for the third APP (the example image).
 *********************************************************************************
 * Copyright (c) 2025 WeAct Studio.  Derived from the WCH CH32H417 EVT examples.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/* ============================================================================
 * 目录 / Contents
 * ----------------------------------------------------------------------------
 *   1. 毫秒时基 TIM2              millisecond time base
 *   2. 对外 IO（D0..D15）          the IOs that leave the board
 *   3. DAC 设置 IO 电压            setting the IO voltage with the DAC
 *   4. WS2812                     the single RGB LED
 *   5. 按键 KEY（PE1）             the key
 *   6. 配置区：读写与禁写地址       config area - read/write and the NO-WRITE list
 *   7. 跳转 APP / 切换槽           jumping to another APP
 *   8. Hardware()                 start-up and main loop
 *
 * 本工程是**给读者改着玩的**模板，双核：V3F（本文件）管按键、WS2812、IO 电压
 * DAC 和配置区；V5F 管 USB，用一个 CDC 口打印工程信息（见 V5F/User/hardware.c）。
 * 想看"真实产品"怎么写，去看另外两个 APP。
 *
 * This project is a template meant to be modified.  Dual core: V3F (this file)
 * owns the key, the WS2812, the IO-voltage DAC and the config area; V5F owns
 * the USB and prints the project information on a CDC port (see
 * V5F/User/hardware.c).  The other two APPs are the production-grade examples.
 * ==========================================================================*/

#include "hardware.h"
#include "ch32h417_gpio.h"
#include "debug.h"
#include "ws2812_pwm.h"

/* ============================================================================
 * 1. 毫秒时基 / millisecond time base
 * ==========================================================================*/

__IO uint32_t sys_tick_ms = 0;
__IO uint32_t key_hold_ms = 0;        /* 按键按住的时长，给 LED 做反馈用 */

/* 长按阈值：按住这么久松手 = 切 APP 槽（LED 与按键两处都要用，所以放这里）
 * Hold the key this long and release -> switch APP slot.  Used by both the LED
 * and the key, hence defined here.                                          */
#define DEF_EX_KEY_LONG_MS       2000u
#define DEF_EX_KEY_DEBOUNCE_MS     50u

void TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInitStructure.TIM_Period = 10 - 1;
    TIM_TimeBaseInitStructure.TIM_Prescaler = SystemCoreClock / 10000 - 1;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    NVIC_SetPriority(TIM2_IRQn, 0x10);
    NVIC_EnableIRQ(TIM2_IRQn);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET) {
        sys_tick_ms++;
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}

/* ============================================================================
 * 2. 对外 IO（D0 .. D15）/ The IOs that leave the board
 * ----------------------------------------------------------------------------
 * 板上丝印 D0..D15 就是这 16 个脚（取自原理图网络标号 ↔ 芯片引脚）。
 * The silkscreen D0..D15 on the board is exactly these 16 pins (net labels of
 * the schematic matched against the MCU pins).
 *
 *   丝印 Dn | 引脚 GPIO | 封装焊盘 | 备注 / note
 *   --------+-----------+----------+----------------------------------------
 *     D0    |  PD10     |   33     |
 *     D1    |  PD11     |   34     |
 *     D2    |  PD12     |   35     | CH0 RX  (USART1, AF14) 另一个 APP 用作串口
 *     D3    |  PD13     |   36     | CH0 TX  (USART1, AF14)      "
 *     D4    |  PD14     |   37     |
 *     D5    |  PD15     |   38     |
 *     D6    |  PF0      |   39     |
 *     D7    |  PF1      |   40     |
 *     D8    |  PF2      |   41     |
 *     D9    |  PC6      |   42     | CH1 TX  (USART4, AF7)       "
 *     D10   |  PC7      |   43     | CH1 RX  (USART4, AF7)       "
 *     D11   |  PC8      |   44     |
 *     D12   |  PC9      |   45     |
 *     D13   |  PA13     |   46     | CH2 TX  (USART3, AF4)       "
 *     D14   |  PA14     |   47     | CH2 RX  (USART3, AF4)       "
 *     D15   |  PA15     |   48     |
 *
 * 规律：Dn = 焊盘 33 + n。回环自测时短接 D2↔D3 / D9↔D10 / D13↔D14 就能测到
 * USB 转串口 APP 的三路串口。
 * Rule: Dn = pad 33 + n.  Shorting D2-D3 / D9-D10 / D13-D14 is how the three
 * UARTs of the USB-to-UART APP are looped back for a test.
 *
 * 不对外、但你要知道的脚 / On board, not on the header, but worth knowing:
 *   PE1   按键 KEY，按下为高（无外部上拉，内部下拉）
 *         the key, high when pressed (no external pull-up, internal pull-down)
 *   PC2   WS2812（原理图 U7，TIM5_CH2，AF2）—— 板上只有这一颗
 *         the one and only RGB LED (U7 on the schematic, TIM5_CH2, AF2)
 *   PA5   DAC2 输出，决定 D0..D15 的 IO 电平（见下一节）
 *         DAC2 output - it sets the IO level of D0..D15 (next section)
 *   PB8/PB9  USB2.0 DM/DP，同时是 SWDIO/SWCLK —— 所以 APP 一跑起来 SWD 就没了
 *         USB 2.0 DM/DP, shared with SWDIO/SWCLK, which is why SWD is off
 *         once an APP runs
 *
 * 用法 / Usage: 直接用 GPIO_Init() 初始化，例如下面把 D0（PD10）开成推挽输出。
 *              下面 Hardware() 里默认没有占用 D0..D15，它们留给读者。
 * ==========================================================================*/

static void D_io_demo_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOD, ENABLE);

    /* D0 = PD10: 推挽输出，2 MHz。改成 GPIO_Mode_IPU/IPD 就是输入。
     * D0 = PD10 as a 2 MHz push-pull output.  GPIO_Mode_IPU/IPD for input.  */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Medium;
    GPIO_Init(GPIOD, &GPIO_InitStructure);
}

/* ============================================================================
 * 3. DAC 设置 IO 电压 / Setting the IO voltage with the DAC
 * ----------------------------------------------------------------------------
 * D0..D15 的高电平由板上的电平转换电路决定，而这个电路的参考电压来自 **DAC2
 * (PA5)**。所以"改 IO 电压"= 改 DAC2 的输出，不是改 GPIO。
 * The high level of D0..D15 comes from the level shifter, and the shifter is
 * referenced to **DAC2 (PA5)**.  "Changing the IO voltage" therefore means
 * changing DAC2 - not touching the GPIOs.
 *
 * 取值范围 / Range: 1.0 V .. 3.3 V，代码里统一用"电压 x10"（10..33）表示，
 * 这样 2.6 V 这种无级值也能精确存进配置区。
 * Voltages are always carried as "volts x 10" (10..33) so that a value like
 * 2.6 V can be stored exactly in the config area.
 *
 * DAC_OUT() 的 0x8000 位 = 直连模式，低字节 = 电压 x10：
 *   0x8000 | 33 -> 3.3 V      0x8000 | 12 -> 1.2 V
 * 硬件会自动换算成 12 位 DAC 码（4095 - x10*4095/33）。
 * ==========================================================================*/

void Dac_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    DAC_InitTypeDef  DAC_InitType = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_DAC, ENABLE);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_5;      /* PA5 = DAC2          */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    DAC_InitType.DAC_Trigger      = DAC_Trigger_None;
    DAC_InitType.DAC_WaveGeneration = DAC_WaveGeneration_None;
    DAC_InitType.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
    DAC_InitType.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_2, &DAC_InitType);
    DAC_Cmd(DAC_Channel_2, ENABLE);
}

static void DAC_OUT(uint16_t dac_value)
{
    uint16_t value;
    uint32_t temp;

    if ((dac_value & 0x8000u) == 0x8000u) {           /* 直连模式 / direct  */
        temp  = (uint32_t)(dac_value & 0xFFu) * 4095u / 33u;
        value = (uint16_t)(4095u - temp);
    } else {
        value = dac_value & 0x0FFFu;                  /* 分压模式 / divider */
    }

    DAC_SetChannel2Data(DAC_Align_12b_R, value);
}

/* 四档预设（电压 x10）/ the four presets (volts x 10) */
static const uint8_t ex_level_x10[EX_LEVEL_NUM] =
{
    EX_LEVEL_3V3, EX_LEVEL_2V5, EX_LEVEL_1V8, EX_LEVEL_1V2
};

/* 档位 -> LED 颜色 {R,G,B}：全部带红分量，和"收发活动色"（无红分量）区分开。
 * 这是板子上的配色约定：有红 = 电压档位，无红 = 数据收发。
 * Colours for the presets.  Every one of them carries a red component, which
 * is how the board tells "voltage" (red somewhere) from "traffic" (no red). */
static const uint8_t ex_level_color[EX_LEVEL_NUM][3] =
{
    {128, 128,   0},    /* 3.3 V  黄 yellow   */
    {128,  64,   0},    /* 2.5 V  橙 orange   */
    {128,   0, 128},    /* 1.8 V  品红 magenta*/
    {128,   0,   0},    /* 1.2 V  红 red      */
};

static uint8_t ex_level_now_x10 = EX_LEVEL_3V3;      /* 当前输出 / current  */
static uint8_t ex_level_dirty;                       /* 1 = 改过还没落盘    */
static uint32_t ex_level_idle_tick;                  /* 最后一次改动时刻    */

/* 取电压最接近的那一档（无级调压时用来上色） */
static uint8_t ex_level_index(uint8_t x10)
{
    uint8_t best = 0, best_d = 0xFF, i;

    for (i = 0; i < EX_LEVEL_NUM; i++) {
        uint8_t a = ex_level_x10[i];
        uint8_t d = (a > x10) ? (uint8_t)(a - x10) : (uint8_t)(x10 - a);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

/* 真正输出到 DAC / apply a voltage to the DAC */
static void ex_level_apply_x10(uint8_t x10)
{
    if (x10 < EX_LEVEL_MIN) { x10 = EX_LEVEL_MIN; }
    if (x10 > EX_LEVEL_MAX) { x10 = EX_LEVEL_MAX; }

    ex_level_now_x10  = x10;
    ex_level_dirty    = 1;
    ex_level_idle_tick = sys_tick_ms;

    DAC_OUT((uint16_t)(0x8000u | (uint16_t)x10));
}

static void ex_level_next(void)
{
    uint8_t idx = ex_level_index(ex_level_now_x10);

    idx++;
    if (idx >= EX_LEVEL_NUM) { idx = 0; }
    ex_level_apply_x10(ex_level_x10[idx]);
}

/* 停手 3 秒后才落盘：按一下键就擦一次 flash 是最省事、也是最伤的写法。
 * The change is stored 3 s after the last key press - writing on every single
 * press is the easiest and the most damaging way to do it.                   */
#define DEF_EX_LEVEL_SAVE_IDLE_MS   3000u

static void ex_level_autosave(void)
{
    if (ex_level_dirty == 0u) { return; }
    if ((uint32_t)(sys_tick_ms - ex_level_idle_tick) < DEF_EX_LEVEL_SAVE_IDLE_MS) { return; }

    ex_level_dirty = 0;
    (void)EE_Write(EE_KEY_LEVEL, (uint32_t)ex_level_now_x10);
}

/* ============================================================================
 * 4. WS2812 / The single RGB LED
 * ----------------------------------------------------------------------------
 * 板上只有一颗（U7，PC2，TIM5_CH2 + DMA1_Channel5）。驱动在 ws2812_pwm.c：
 *   ws2812_init(CODE_0, CODE_1, PERIOD);   // 上电调一次
 *   ws2812_setPixelColor(0, r, g, b);      // 只改缓冲区，不发数据
 *   ws2812_sync();                         // 真正发出去（DMA，约 1 ms）
 *   ws2812_is_finished();                  // 上一帧发完了吗
 *
 * ⚠️ 只有等 ws2812_is_finished() 为真才能再次 sync()，否则会打断上一帧的波形，
 *    LED 显示错色。所以主循环里的写法是"发完了才更新颜色"。
 * ⚠️ 亮度别开满：直视很刺眼，而且整机电流也跟着上去。本例锁在 20%。
 * ==========================================================================*/

#define DEF_EX_LED_BREATHE_MS    2000u
#define DEF_EX_LED_BRIGHT_MIN       4u      /* 百分比 / percent */
#define DEF_EX_LED_BRIGHT_MAX      20u      /* 上限，别改大 / upper limit */

static uint8_t ex_led_buf[3];

static uint8_t ex_scale(uint8_t val, uint8_t br)
{
    return (uint8_t)(((uint16_t)val * br) / 100u);
}

static void ex_led_show(uint8_t r, uint8_t g, uint8_t b, uint8_t br)
{
    ex_led_buf[0] = ex_scale(r, br);
    ex_led_buf[1] = ex_scale(g, br);
    ex_led_buf[2] = ex_scale(b, br);

    ws2812_setPixelColor(0, ex_led_buf[0], ex_led_buf[1], ex_led_buf[2]);
    ws2812_sync();
}

/* 常规画面：当前档位色，缓慢呼吸 / normal picture: preset colour, breathing */
static void ex_led_process(void)
{
    static uint32_t last_ms = 0;
    uint16_t t, half;
    uint8_t  br, idx;

    if (ws2812_is_finished() == 0u) { return; }         /* 上一帧还没发完    */
    if ((uint32_t)(sys_tick_ms - last_ms) < 20u) { return; }  /* 别刷太勤  */
    last_ms = sys_tick_ms;

    /* 长按 KEY 期间显示白色：提示"松手就切到下一个 APP 槽" */
    if (key_hold_ms >= DEF_EX_KEY_LONG_MS) {
        ex_led_show(128, 128, 128, DEF_EX_LED_BRIGHT_MAX);
        return;
    }

    half = (uint16_t)(DEF_EX_LED_BREATHE_MS / 2u);
    t    = (uint16_t)(sys_tick_ms % DEF_EX_LED_BREATHE_MS);
    if (t < half) {
        br = (uint8_t)(DEF_EX_LED_BRIGHT_MIN +
             (uint16_t)(DEF_EX_LED_BRIGHT_MAX - DEF_EX_LED_BRIGHT_MIN) * t / half);
    } else {
        br = (uint8_t)(DEF_EX_LED_BRIGHT_MAX -
             (uint16_t)(DEF_EX_LED_BRIGHT_MAX - DEF_EX_LED_BRIGHT_MIN) *
             (uint16_t)(t - half) / half);
    }

    idx = ex_level_index(ex_level_now_x10);
    ex_led_show(ex_level_color[idx][0], ex_level_color[idx][1], ex_level_color[idx][2], br);
}

/* ============================================================================
 * 5. 按键 KEY（PE1）/ The key
 * ----------------------------------------------------------------------------
 * 按下为高（内部下拉）。短按 = 换一档 IO 电压；长按 ≥ 2 s = 切到下一个 APP 槽。
 * High when pressed (internal pull-down).  Short press = next IO level,
 * long press (>= 2 s) = jump to the next APP slot.
 * ==========================================================================*/

void key_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOE, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOE, &GPIO_InitStructure);
}

/* 定义在下一节（第 7 节）/ defined further down, in section 7 */
static void app_slot_request_next(void);

static void key_process(void)
{
    static uint8_t  last = 0;
    static uint32_t t_change = 0;
    static uint32_t t_press  = 0;
    uint8_t  cur = (GPIOE->INDR & GPIO_Pin_1) ? 1u : 0u;
    uint32_t now = sys_tick_ms;

    if (cur != last) {
        if ((uint32_t)(now - t_change) < (uint32_t)DEF_EX_KEY_DEBOUNCE_MS) { return; }
        t_change = now;
        last = cur;

        if (cur != 0u) {                       /* 按下 / pressed             */
            t_press = now;
        } else {                               /* 松手 / released            */
            if ((uint32_t)(now - t_press) >= (uint32_t)DEF_EX_KEY_LONG_MS) {
                app_slot_request_next();       /* 长按 = 切槽（不会返回）    */
            } else {
                ex_level_next();               /* 短按 = 换电压档位          */
            }
            key_hold_ms = 0;
        }
    } else {
        t_change = now;
        if (cur != 0u) {
            key_hold_ms = (uint32_t)(now - t_press);
        }
    }
}

/* ============================================================================
 * 6. 配置区：读写与禁写地址 / Config area: read/write and the NO-WRITE list
 * ----------------------------------------------------------------------------
 * 芯片没有 EEPROM，掉电要留住的东西存在 code flash 末尾的**配置区**：
 *    0x080EA000 .. 0x080EC000   页 A   (8 KB)
 *    0x080EC000 .. 0x080EE000   页 B   (8 KB)
 *    0x080EE000 .. 0x080F0000   保留   (8 KB)  ← 不要碰
 * 实现在 SRC/Core/eeprom.c（A/B 双页 + 全局 seq 裁决，掉电不会丢）。
 *
 * 用法只有三个函数，别自己调 FLASH_Program*：
 *   EE_Init();                      上电调一次，纯只读，什么都不擦写
 *   EE_Read(key,  &val);            读，返回 EE_OK / EE_ERR(没这条) / EE_NOINIT
 *   EE_Write(key, val);             写，追加一条 256 B 记录；值没变则直接返回
 *   EE_Peek(key, &val);             只读版，V5F 也能调
 *                                   （V5F 要用配置区只能走它，见 V5F/User/hardware.c）
 *
 *   ⚠️ 只有 V3F 能读写。V5F 调 EE_Write 会读到 V3F 轮转之前的过期页。
 *   ⚠️ 每次上电都会先跑一遍 IAP，所以 IAP 里的 eeprom.c 版本和 APP 一样重要：
 *      改完 SRC/Core/eeprom.c 必须同时重烧 IAP（只有 SWD 能写 IAP 区）。
 *
 * 键的空间 / Key namespaces（高 8 位决定归属，互不干扰）：
 *   0x01000000  USB 转串口 APP      EE_KEY_LEVEL      串口电平（电压 x10）
 *   0x02000000  逻辑分析仪 APP
 *   0x03000000  本 example          EE_KEY_EXAMPLE_BOOT
 *   0xFF000000  IAP（占最后一段）    EE_KEY_ACTIVE_SLOT 当前激活的 APP 槽
 *
 * ---------------------------------------------------------------------------
 * ⚠️ 这些地址**绝对不能写**（写了就不是"改设置"，而是把固件戳个洞）：
 *
 *   0x00000000 .. 0x00006000   IAP 引导区。CLI 烧录工具也只能写 APP 区，
 *                              所以这里一旦坏掉只能 SWD 救。
 *                              其中 0x00005FF4 起的 12 字节是 UID 副本：
 *                              IAP 发现它和芯片 UID 不符会**擦掉自己**并复位，
 *                              表现是设备彻底没有 USB，只能 SWD 重烧。
 *   0x00006000 .. 0x000EA000   三个 APP 槽。烧进别的槽的固件就在这段里，
 *                              往这里写等于改别人的代码。
 *   0x000EE000 .. 0x000F0000   配置区保留段。留着给将来的第二组 A/B 页。
 *
 *   (上面是执行地址；给 FLASH_* 函数时要加 FLASH_BASE，例如 0x080EA000。)
 *
 * ⚠️ Addresses you must NEVER write:
 *   0x00000000..0x00006000  the bootloader (the CLI tool cannot even reach it,
 *                           so damaging it means SWD or nothing).  The 12 bytes
 *                           at 0x00005FF4 are the UID copy: if it stops
 *                           matching the chip UID the bootloader ERASES ITSELF
 *                           and resets - the board then looks completely dead.
 *   0x00006000..0x000EA000  the three APP slots, i.e. somebody else's code.
 *   0x000EE000..0x000F0000  reserved tail of the config area.
 *   (Execution addresses; add FLASH_BASE for the FLASH_* routines.)
 * ==========================================================================*/

static uint32_t ex_boot_count;

/* 每次上电把计数 +1 并存回去 —— 这一段就是"配置区读写"的最小示例。
 * 代价要清楚：一次上电消耗一条 256 B 记录，两块 8 KB 页共 64 条，
 * 写满会自动轮转（自动、掉电安全），所以长期跑没问题，但如果这是量产的
 * APP，关掉 DEF_EX_BOOTCNT_WRITE，只在真正需要时写。
 *
 * A minimal read-modify-write example: bump a boot counter and store it.
 * It costs one 256 B record per power-up; the 8 KB page pair holds 64 and
 * rotates automatically and safely, but a shipping APP should turn
 * DEF_EX_BOOTCNT_WRITE off and only write when it really has to.            */
#define DEF_EX_BOOTCNT_WRITE   1

static void ex_cfg_boot_count(void)
{
    uint32_t v = 0;

    if (EE_Ready() && (EE_Read(EE_KEY_EXAMPLE_BOOT, &v) == EE_OK)) {
        ex_boot_count = v + 1u;
    } else {
        ex_boot_count = 1u;                     /* 空白器件：从 1 开始算 */
    }

#if DEF_EX_BOOTCNT_WRITE
    (void)EE_Write(EE_KEY_EXAMPLE_BOOT, ex_boot_count);
#endif
}

/* 开机恢复上次记住的 IO 电压（与 USB 转串口 APP 共用同一个键）。
 * Restore the IO voltage (the USB-to-UART APP uses the same key).           */
static void ex_cfg_load_level(void)
{
    uint32_t v;

    if (EE_Ready() && (EE_Read(EE_KEY_LEVEL, &v) == EE_OK) &&
        (v >= EX_LEVEL_MIN) && (v <= EX_LEVEL_MAX)) {
        ex_level_apply_x10((uint8_t)v);
    } else {
        ex_level_apply_x10(EX_LEVEL_3V3);
    }

    ex_level_dirty = 0;                          /* 刚恢复的，不用再写回去 */
}

/* ============================================================================
 * 7. 跳转 APP / 切换槽 / Jumping to another APP
 * ----------------------------------------------------------------------------
 * 上电永远先跑 IAP，由 IAP 决定跳到哪个槽。想换槽有四条路：
 *
 *  a) 本例用的（推荐）：往 SRAM 请求字写一个魔数再软复位
 *         *(volatile uint32_t *)IapRamAddr = IapSlotRamNum | slot;
 *         NVIC_SystemReset();
 *     IAP 醒来读到它，把选择写进配置区，然后跳过去。
 *
 *  b) 直接改配置区再复位（效果一样，本例没用）：
 *         EE_Write(EE_KEY_ACTIVE_SLOT, slot);  NVIC_SystemReset();
 *     不推荐：这次改动立刻持久化，而 (a) 由 IAP 统一落盘，语义更干净。
 *
 *  c) 在 USB 转串口 APP 里往 COM 口设波特率 2049 / 2050 / 2051
 *     = 切到槽 0 / 1 / 2，不必烧录。
 *
 *  d) 重新烧录：CLI -f <bin> -b 0809E000
 *     烧哪个槽，IAP 就跑哪个槽（烧完自动记住）。
 *
 * ⚠️ "烧哪个槽就跑哪个槽"意味着镜像的链接基址必须和烧录地址一致，否则跳过去
 *    就是一段地址全错的代码（现象：板子安静得像坏了）。换槽要同时改两处：
 *    链接脚本 FLASH ORIGIN（Common/Ld/V3F/Link_v3f.ld）和烧录命令的 -b。
 *
 * The bootloader always runs first and picks the slot.  Four ways to change it:
 *  a) what this example does: write the request word in SRAM and reset;
 *  b) write EE_KEY_ACTIVE_SLOT yourself and reset (same result, not used here);
 *  c) in the USB-to-UART APP, set baud rate 2049/2050/2051 on a COM port;
 *  d) burn the image again with `CLI -f <bin> -b 0809E000`.
 *
 * ⚠️ The image has to be LINKED for the slot it is burned into, otherwise the
 *    jump lands in code whose every address is wrong.  Change both the FLASH
 *    ORIGIN and the `-b` together.
 * ==========================================================================*/

static void app_slot_request_next(void)
{
    uint32_t slot = 0;
    uint32_t v;

    if (EE_Ready() && (EE_Read(EE_KEY_ACTIVE_SLOT, &v) == EE_OK)) {
        slot = v;
    }
    slot = (slot + 1u) % (uint32_t)APP_SLOT_NUM;

    /* 白灯常亮一下，让人看得出是要切槽而不是灯坏了 */
    ex_led_show(128, 128, 128, DEF_EX_LED_BRIGHT_MAX);
    Delay_Ms(200);

    *(volatile uint32_t *)IapRamAddr = IapSlotRamNum | (slot & 0xFFu);
    NVIC_SystemReset();

    while (1) { }
}

/* ============================================================================
 * 8. 上电与主循环 / Start-up and main loop
 * ==========================================================================*/

void Hardware(void)
{
    SystemAndCoreClockUpdate();

    /* EE_Init() 必须最先跑：它探测擦除页大小、挑出有效页。
     * ⚠️ 它是纯只读的，一次擦、一次写都不做 —— 别往里加擦写。
     *    它跑在启动路径上，比 USB、比第二个核、比任何能报错的通道都早，
     *    里面任何擦写都是"赌 flash 控制器此刻正好在应答"，赌输了整块板子
     *    就是静默的（USB 没有、bootloader HID 也没有，只能断电）。
     * EE_Init() comes first.  It is read-only by design - never add an erase
     * or a program to it.                                                    */
    EE_Init();

    /* 旧记录间隔是 256 B，四个字跟现在一样所以还能读，但那 240 B 填充占着
     * 永不能再编程的槽。EE_Migrate() 轮转一次把它们腾出来，值保留；
     * 已经迁移过的区域不会发生任何擦写。 */
    (void)EE_Migrate();

    TIM2_Init();
    key_init();
    Dac_Init();
    ws2812_init(CODE_0, CODE_1, PERIOD);

    /* 配置区：读出上电次数、恢复上次记住的 IO 电压 */
    ex_cfg_boot_count();
    ex_cfg_load_level();

    /* D0（PD10）开成输出，主循环里每 500 ms 翻一次 —— 接个万用表或示波器
     * 就能确认"板子活着"。用完可以删掉，D0..D15 默认都是留给读者的。 */
    D_io_demo_init();

    while (1) {
        static uint32_t t_toggle = 0;

        key_process();
        ex_level_autosave();
        ex_led_process();

        if ((uint32_t)(sys_tick_ms - t_toggle) >= 500u) {
            t_toggle = sys_tick_ms;
            GPIOD->OUTDR ^= GPIO_Pin_10;             /* D0 翻转 / toggle D0 */
        }
    }
}
