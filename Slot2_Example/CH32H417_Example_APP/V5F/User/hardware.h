/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.h
* Author             : WeAct Studio
* Version            : V1.0.0
* Description        : V5F 侧的板级接口（example APP）。
*                      V5F board interface of the example APP.
*
* V5F 负责 USB：一个 CDC 口，周期性打印工程信息。按键、WS2812、IO 电压 DAC 和
* 配置区的擦写都在 V3F，见 V3F/User/hardware.c。
*
* V5F owns the USB: one CDC port that prints the project information over and
* over.  The key, the WS2812, the IO-voltage DAC and every write to the config
* area live on V3F - see V3F/User/hardware.c.
*********************************************************************************/
#ifndef __HARDWARE_H
#define __HARDWARE_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32h417.h"
#include "debug.h"

void Hardware(void);

/* Called from the control endpoint when the host sets the line coding: the
 * baud rate field doubles as a command channel, because this CDC has no real
 * UART behind it.  Only DEF_EX_IAP_BAUD (2048) does anything; everything else
 * is stored and read back unchanged so Windows stays happy.
 *
 * It does NOT reset there - that runs from the main loop instead.  Resetting
 * inside the EP0 handler would drop off the bus before the control transfer
 * had been acknowledged, and the host would see a malformed transfer.        */
void EX_OnLineCoding( uint32_t baud );

#ifdef __cplusplus
}
#endif

#endif
