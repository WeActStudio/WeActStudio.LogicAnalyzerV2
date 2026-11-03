/********************************** (C) COPYRIGHT  *******************************
* File Name          : debug.h
* Author             : WeAct Studio
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : This file contains all the functions prototypes for UART
*                      Printf , Delay functions.
*********************************************************************************
* Copyright (c) 2025 WeAct Studio.  Derived from the WCH CH32H417 EVT examples.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __DEBUG_H
#define __DEBUG_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stdio.h"
#include "ch32h417.h"


/* UART Printf Definition */
#define DEBUG_UART1    1
#define DEBUG_UART8    2
#define DEBUG_UART6    3

/* DEBUG UATR Definition */
#ifndef DEBUG

#ifdef Core_V3F
// #define DEBUG   DEBUG_UART1

#elif defined(Core_V5F)
// #define DEBUG   DEBUG_UART1

#endif

#endif

/* Run Core Definition */
#define Run_Core_V3F         0
#define Run_Core_V5F         1
#define Run_Core_V3FandV5F   2

/* 两个核都编、都跑：V3F 唤醒 V5F，V5F 拿到 HSEM 再把 V3F 叫醒。
 * Both cores are built and run: V3F wakes V5F, V5F takes HSEM and wakes V3F. */
#ifndef Run_Core
#define Run_Core   Run_Core_V3FandV5F
#endif

/* Core start addrress Definition */
#ifndef Core_V3F_StartAddr
#define Core_V3F_StartAddr   0x00000000
#endif

/* V5F 镜像的入口地址。它和 Common/Ld/V5F/Link_v5f.ld 的 FLASH ORIGIN、
 * 以及 Common/example_cfg.h 里的 EX_V5F_BASE 必须是同一个数。
 * Entry point of the V5F image.  It has to be the same number as the FLASH
 * ORIGIN in Common/Ld/V5F/Link_v5f.ld and as EX_V5F_BASE in
 * Common/example_cfg.h - otherwise V3F wakes the wrong address.             */
#ifndef Core_V5F_StartAddr
#define Core_V5F_StartAddr   0x000B8000
#endif

#if ((Run_Core == Run_Core_V3FandV5F) || (Run_Core == Run_Core_V5F)) && defined(Core_V5F) 
#define Func_Run_V5F 1
#define Func_Run_V3F 0
#elif ((Run_Core == Run_Core_V3FandV5F) || (Run_Core == Run_Core_V3F)) && defined(Core_V3F)
#define Func_Run_V5F 0
#define Func_Run_V3F 1
#else
#define Func_Run_V5F 0
#define Func_Run_V3F 0
#endif

#if(DEBUG)
  #define DBG_PRINT(format, ...)    printf(format, ##__VA_ARGS__)
#else
  #define DBG_PRINT(X...)
#endif

void Delay_Init(void);
void Delay_Us(uint32_t n);
void Delay_Ms(uint32_t n);
void USART_Printf_Init(uint32_t baudrate);

#ifdef __cplusplus
}
#endif

#endif 



