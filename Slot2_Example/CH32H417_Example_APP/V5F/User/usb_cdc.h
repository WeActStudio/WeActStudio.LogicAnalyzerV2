/********************************** (C) COPYRIGHT  *******************************
 * File Name          : usb_cdc.h
 * Author             : WeAct Studio
 * Version            : V1.0.0
 * Description        : 单路 CDC 的收发胶水层（本 example 的 V5F 用）。
 *                      Glue layer of the single CDC port (V5F side only).
 *
 * WCH 的 USBHS 例程把 CDC 直接焊在一路真实串口上（UART.c 那一层）。本工程
 * **不映射任何串口** —— 这个 CDC 只往外打印工程信息，所以那一层整个拿掉，
 * 只留下 USB 中断和主循环之间这几个共享变量。
 *
 * The WCH USBHS example wires the CDC straight to a real UART (the UART.c
 * layer).  This project maps **no UART at all** - the CDC only prints project
 * information - so that layer is gone and only these few variables are shared
 * between the USB interrupt and the main loop.
 *******************************************************************************/
#ifndef __USB_CDC_H
#define __USB_CDC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32h417.h"
#include "usb_desc.h"

/* 端点映射（与 usb_desc.c 的配置描述符一致）
 * Endpoint map (matches the configuration descriptor in usb_desc.c):
 *   EP3 IN  - CDC 通知端点（interrupt），本工程不发任何通知
 *   EP2 OUT - 主机 -> 设备
 *   EP2 IN  - 设备 -> 主机（工程信息就走这里）                              */
#define DEF_CDC_BULK_MPS          DEF_USB_EP2_HS_SIZE

/* 一次 IN 传输还没发完 / an IN transfer is still in flight */
extern volatile uint8_t  cdc_tx_busy;

/* 收到一包主机数据，等主循环处理 / a host packet is waiting for the loop   */
extern volatile uint8_t  cdc_rx_pending;
extern volatile uint16_t cdc_rx_len;
extern __attribute__((aligned(4))) uint8_t CDC_Rx_Buf[ DEF_USB_EP2_HS_SIZE ];

/* GET/SET_LINE_CODING 的 7 个字节。后面没有真实串口，这些字节不影响任何东西，
 * 但 Windows 坚持要 GET_LINE_CODING 读回它刚写下去的值，所以必须存着。
 * The 7 bytes of GET/SET_LINE_CODING.  Nothing is behind the port, but Windows
 * insists on reading back what it just wrote, so they have to be kept.       */
extern uint8_t cdc_line_coding[ 7 ];

/* 把一段数据从 EP2 IN 发出去；返回 1 = 已启动，0 = 现在发不了（没枚举/忙）
 * Send a block on EP2 IN.  Returns 1 when started, 0 when it cannot (not
 * enumerated, or the previous transfer has not completed yet).               */
uint8_t CDC_Send( const uint8_t *buf, uint16_t len );

/* 同一件事，但允许超过一个 bulk 包：按 MPS-1 分片，片间等上一包发完（最多 200 ms）。
 * 返回真正发出的字节数，小于 len 表示这一页被截断了（比如主机不读）。
 * The same, but longer than one bulk packet: split into MPS-1 pieces and wait
 * for each one to finish (bounded at 200 ms).  Returns the bytes actually sent;
 * less than len means the page was cut short, e.g. the host is not reading.  */
uint16_t CDC_SendChunked( const uint8_t *buf, uint16_t len );

/* EP2 IN 是否空闲 / is the bulk IN endpoint idle?                           */
uint8_t CDC_TxIdle( void );

#ifdef __cplusplus
}
#endif

#endif
