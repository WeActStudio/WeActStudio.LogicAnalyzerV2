/********************************** (C) COPYRIGHT  *******************************
 * File Name          : ws2812_pwm.h
 * Author             : WeAct Studio
 * Version            : V1.0.0
 * Description        : WS2812 驱动对外接口（TIM5 CH2 + DMA，板载 1 颗灯）。
 *                      WS2812 driver interface (TIM5 CH2 + DMA, one LED).
 *********************************************************************************
 * Copyright (c) 2025 WeAct Studio.
 *******************************************************************************/

#ifndef __WS2812_PWM_H
#define __WS2812_PWM_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "ch32h417.h"
#include "debug.h"

#define Pixel_NUM (1)
#define CODE_0      (32)
#define CODE_1      (75)
#define PERIOD      (100)
#define RESET_LEN    (75)
#define TIM_UP_DMA_CH   DMA1_Channel5
#define TIM_UP_DMA_IRQn   DMA1_Channel5_IRQn
#define COLOR_BUFFER_LEN (((Pixel_NUM)*(3*8))+RESET_LEN)

void ws2812_setPixelColor(uint16_t id, uint8_t r, uint8_t g, uint8_t b);
void ws2812_cal_code(uint8_t code0, uint8_t code1, uint16_t period);
void ws2812_init(uint8_t code0, uint8_t code1, uint16_t period);
void ws2812_reinit(uint8_t code0, uint8_t code1, uint16_t period);
void ws2812_sync();
uint8_t ws2812_is_finished(void);

#ifdef __cplusplus
}
#endif

#endif