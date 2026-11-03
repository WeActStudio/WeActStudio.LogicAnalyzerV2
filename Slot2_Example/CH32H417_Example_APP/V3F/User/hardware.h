/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.h
* Author             : WeAct Studio
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : This file contains all the functions prototypes for the
*                      hardware.
*
* Example APP (third image, APP slot 2) - V3F public interface.
* example APP（第三个镜像，APP 槽 2）—— V3F 对外接口。
*
* 与 V5F 共用的常量在 Common/example_cfg.h（APP 基址、IAP 请求魔数、电平档位、
* 配置区键），那里有完整的中英文说明。
* The constants shared with V5F live in Common/example_cfg.h (APP base address,
* IAP request magic, level presets, config keys) with the full explanation.
*********************************************************************************
* Copyright (c) 2025 WeAct Studio.  Derived from the WCH CH32H417 EVT examples.
* Attention: This software (modified or not) and binary are used for
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __HARDWARE_H
#define __HARDWARE_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "ch32h417.h"
#include "debug.h"
#include "eeprom.h"
#include "example_cfg.h"

void Hardware(void);

#ifdef __cplusplus
}
#endif

#endif
