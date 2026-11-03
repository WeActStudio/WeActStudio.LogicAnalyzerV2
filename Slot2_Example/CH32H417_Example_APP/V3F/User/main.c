/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : WeAct Studio (example written for this board)
* Version            : V1.0.0
* Description        : 第三个 APP（example）的 V3F 入口。
*                      V3F entry point of the third APP (the example image).
*********************************************************************************
* Copyright (c) 2025 WeAct Studio.  Derived from the WCH CH32H417 EVT examples.
* Attention: This software (modified or not) and binary are used for
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

/* ============================================================================
 *  ENGLISH
 *  ---------------------------------------------------------------------------
 *  V3F side of the example application.  It is the **third** APP image and is
 *  linked for **APP slot 2** (`FLASH ORIGIN = 0x0009E000` in
 *  `Common/Ld/V3F/Link_v3f.ld`).
 *
 *  The chip is a dual core part.  This core owns the key, the WS2812, the DAC
 *  that sets the IO voltage and the config area; the V5F core (second project
 *  of the same solution) owns the USB and prints the project information on a
 *  CDC port - see `V5F/User/hardware.c`.
 *
 *  V3F starts alone after a reset and has to wake V5F up itself.  The sequence
 *  below is the one every WCH dual core project uses:
 *      - `NVIC_WakeUp_V5F()` releases the second core;
 *      - V3F then goes to STOP and waits for the HSEM interrupt;
 *      - V5F takes HSEM_ID0 and releases it again, which is what wakes V3F.
 *  Without the handshake V3F would start its peripherals while V5F is still
 *  coming up.
 *
 *  中文
 *  ---------------------------------------------------------------------------
 *  example 应用的 V3F 侧入口。它是**第三个** APP 镜像，链接到 **APP 槽 2**
 *  （`Common/Ld/V3F/Link_v3f.ld` 里 `FLASH ORIGIN = 0x0009E000`）。
 *
 *  芯片是双核的：V3F 管按键、WS2812、IO 电压 DAC 和配置区；V5F（同一解决方案
 *  的第二个工程）管 USB，用一个 CDC 口打印工程信息 —— 见 `V5F/User/hardware.c`。
 *
 *  复位后只有 V3F 在跑，V5F 要靠 V3F 唤醒。下面这段握手是所有 WCH 双核工程
 *  都用的写法：
 *      - `NVIC_WakeUp_V5F()` 放第二核出来；
 *      - V3F 随后进 STOP，等 HSEM 中断；
 *      - V5F 拿到 HSEM_ID0 再释放，这个动作把 V3F 唤醒。
 *  少了这段握手，V3F 会在 V5F 还没起来的时候就初始化外设。
 * ==========================================================================*/

#include "debug.h"
#include "hardware.h"

/*********************************************************************
 * @fn      VDDI8_IO_Change
 *
 * @brief   Turn off the internal 1.8v power supply.
 *          关掉内部 1.8 V 供电（本板 VIO18 由外部提供）。
 *
 * @return  none
 */
void VDDI8_IO_Change(void)
{
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR->CTLR |= PWR_CTLR_VIO_SWCR;
    PWR->CTLR &= ~PWR_CTLR_VSEL_VIO18;
    PWR->CTLR = PWR->CTLR | PWR_CTLR_VSEL_VIO18_MODE5;
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program. / 主程序
 *
 * @return  none
 */
int main(void)
{
    /* Disable SWD.  关掉 SWD：本板的 SWD 引脚与 USB2.0 复用，
     * 所以和另外两个 APP 一样，跑起来以后就只能靠 USB/串口观测。
     * Disable SWD - this board shares the SWD pins with USB 2.0, so like the
     * other two APPs there is no debugger once the APP is running.        */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_VIO1V8_IO_HSLV, DISABLE);

    /* Enable the IO function of PC6 pins (PC6 是 D9，本板上做了特殊处理) */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI->OR |= SWPMI_SWP_TBYP;

    /*  The current in the VDD12 power domain of the chip is relatively high.
     If the VDD12 power supply is externally provided and the internal LDO is turned off,
     the following function can be executed to reduce the chip's power consumption and
     alleviate overheating. Note that this operation should only be performed when the
     external 1.2V power supply has stabilized.*/
    PWR_VDD12ExternPower();
    VDDI8_IO_Change();

    SystemInit();
    SystemAndCoreClockUpdate();
    Delay_Init();

    DBG_PRINT("SystemClk:%d\r\n", SystemClock);
    DBG_PRINT("V3F SystemCoreClk:%d\r\n", SystemCoreClock);

    /* ---- 双核握手 / dual core handshake -----------------------------------
     * `Run_Core` in `Common/Debug/debug.h` selects the branch:
     *   Run_Core_V3FandV5F - wake V5F, wait for its HSEM, then run (used here)
     *   Run_Core_V3F       - V3F only, V5F never starts
     * Either way this core ends up in Hardware().                            */
#if (Run_Core == Run_Core_V3FandV5F)
    NVIC_WakeUp_V5F(Core_V5F_StartAddr);        /* 放第二核出来              */
    HSEM_ITConfig(HSEM_ID0, ENABLE);            /* V5F 释放信号量时唤醒我    */
    NVIC->SCTLR |= 1 << 4;                      /* 允许被事件唤醒            */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
    HSEM_ClearFlag(HSEM_ID0);
#endif

    Hardware();

    while(1)
    {
    }
}
