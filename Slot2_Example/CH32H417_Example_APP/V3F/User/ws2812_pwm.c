/********************************** (C) COPYRIGHT  *******************************
 * File Name          : ws2812_pwm.c
 * Author             : WeAct Studio
 * Version            : V1.0.0
 * Description        : WS2812 驱动：TIM5 CH2 + DMA，板载只有 1 颗灯（原理图 U7）。
 *                      WS2812 driver: TIM5 CH2 + DMA, one LED on the board.
 *********************************************************************************
 * Copyright (c) 2025 WeAct Studio.
 *******************************************************************************/

#include "ws2812_pwm.h"
#include "string.h"

uint16_t color_buf[COLOR_BUFFER_LEN];
static uint8_t code_0 = 0;
static uint8_t code_1 = 0;
static uint16_t prescaler_value = 0;

void ws2812_setPixelColor(uint16_t id, uint8_t r, uint8_t g, uint8_t b)
{
    int i = 0, j = id * 24u;
    if (id >= Pixel_NUM) {
        return;
    }

    for (i = 0; i < 8; i++) {
        if (g & (0x80 >> i)) {
            color_buf[j] = code_1;
        } else {
            color_buf[j] = code_0;
        }
        j++;
    }
    for (i = 0; i < 8; i++) {
        if (r & (0x80 >> i)) {
            color_buf[j] = code_1;
        } else {
            color_buf[j] = code_0;
        }
        j++;
    }
    for (i = 0; i < 8; i++) {
        if (b & (0x80 >> i)) {
            color_buf[j] = code_1;
        } else {
            color_buf[j] = code_0;
        }
        j++;
    }
}

void ws2812_cal_code(uint8_t code0, uint8_t code1, uint16_t period)
{
    prescaler_value = (uint16_t) (SystemCoreClock / 800000) - 1;
    uint32_t temp = prescaler_value * (uint32_t)code0 / (uint32_t)period;
    code_0 = temp;
    temp = prescaler_value * (uint32_t)code1 / (uint32_t)period;
    code_1 = temp;
}

void ws2812_init(uint8_t code0, uint8_t code1, uint16_t period)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    TIM_OCInitTypeDef TIM_OCInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};

    ws2812_cal_code(code0, code1, period);

    memset(color_buf, 0, sizeof(color_buf));

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC | RCC_HB2Periph_AFIO, ENABLE);
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_TIM5, ENABLE);
    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);

    GPIO_PinAFConfig(GPIOC,GPIO_PinSource2,GPIO_AF2);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_High;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    TIM_TimeBaseInitStructure.TIM_Period = prescaler_value;
    TIM_TimeBaseInitStructure.TIM_Prescaler = 0;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit( TIM5, &TIM_TimeBaseInitStructure);

    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_Pulse = 0;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_Low;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OC2Init( TIM5, &TIM_OCInitStructure);

    TIM_OC2PreloadConfig( TIM5, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig( TIM5, ENABLE);
    TIM_CtrlPWMOutputs(TIM5, ENABLE);
    TIM_DMACmd(TIM5, TIM_DMA_Update, ENABLE);
    TIM_Cmd( TIM5, ENABLE);

    DMA_InitTypeDef DMA_InitStructure = {0};

    DMA_DeInit( TIM_UP_DMA_CH);
    DMA_Cmd( TIM_UP_DMA_CH, DISABLE);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) &TIM5->CH2CVR;
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t) color_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = COLOR_BUFFER_LEN;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init( TIM_UP_DMA_CH, &DMA_InitStructure);

    DMA_Cmd( TIM_UP_DMA_CH, DISABLE);

    DMA_ITConfig( TIM_UP_DMA_CH, DMA_IT_TC, ENABLE);

    NVIC_SetPriority (TIM_UP_DMA_IRQn, 0);
    NVIC_EnableIRQ (TIM_UP_DMA_IRQn);

    DMA_MuxChannelConfig(DMA_MuxChannel5,30);

    DMA_Cmd( TIM_UP_DMA_CH, ENABLE);
    while(DMA_GetCurrDataCounter(TIM_UP_DMA_CH)!=0);
}

void ws2812_reinit(uint8_t code0, uint8_t code1, uint16_t period)
{
    ws2812_cal_code(code0, code1, period);
    memset(color_buf, 0, sizeof(color_buf));

    TIM_Cmd(TIM5, DISABLE);

    TIM_SetAutoreload(TIM5, prescaler_value);
    TIM_SetCounter(TIM5, 0);
    TIM_SetCompare2(TIM5, 0);

    DMA_SetCurrDataCounter(TIM_UP_DMA_CH, COLOR_BUFFER_LEN);
    DMA_Cmd(TIM_UP_DMA_CH, ENABLE);
    TIM_Cmd(TIM5, ENABLE);
    while(DMA_GetCurrDataCounter(TIM_UP_DMA_CH)!=0);
}

void ws2812_sync()
{
    while(DMA_GetCurrDataCounter(TIM_UP_DMA_CH)!=0);

    DMA_SetCurrDataCounter(TIM_UP_DMA_CH, COLOR_BUFFER_LEN);
    DMA_Cmd(TIM_UP_DMA_CH, ENABLE);
    TIM_Cmd(TIM5, ENABLE);
}

uint8_t ws2812_is_finished(void)
{
    return ((TIM5->CTLR1 & TIM_CEN) == 0x00);
}

void DMA1_Channel5_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void DMA1_Channel5_IRQHandler(void)
{
    if (DMA_GetFlagStatus(DMA1, DMA1_FLAG_TC5)) {
        TIM_Cmd(TIM5, DISABLE);
        DMA_Cmd(TIM_UP_DMA_CH, DISABLE);
        DMA_ClearFlag(DMA1, DMA1_FLAG_TC5);
    }
}