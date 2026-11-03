/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WeAct Studio
 * Version            : V1.0.0
 * Date               : 2025/05/07
 * Description        : Main program body for V5F.
 *
 * V5F 是被 V3F 叫醒的那一核：它一上来的第一件事是把 HSEM_ID0 拿走再放掉，
 * 这个动作产生一个中断，把停在 STOP 里的 V3F 唤醒。之后 V5F 进 Hardware()，
 * 把 USB CDC 跑起来。
 *
 * V5F is the core V3F wakes up.  Its very first job is to take HSEM_ID0 and
 * release it again - that is what raises the interrupt that pulls V3F out of
 * STOP.  After that it drops into Hardware() and runs the USB CDC port.
 *********************************************************************************
 * Copyright (c) 2025 WeAct Studio.  Derived from the WCH CH32H417 EVT examples.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "debug.h"
#include "hardware.h"

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    SystemAndCoreClockUpdate();
    Delay_Init();

    DBG_PRINT( "V5F SystemCoreClk:%d\r\n", SystemCoreClock );

#if (Run_Core == Run_Core_V3FandV5F)
    HSEM_FastTake( HSEM_ID0 );
    HSEM_ReleaseOneSem( HSEM_ID0, 0 );      /* 唤醒 V3F / wakes V3F up */

    Hardware();                             /* USB CDC 在这里跑起来 */

#elif (Run_Core == Run_Core_V3F)

#elif (Run_Core == Run_Core_V5F)
    Hardware();
#endif

    while( 1 )
    {
    }
}
