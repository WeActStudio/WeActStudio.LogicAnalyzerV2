/********************************** (C) COPYRIGHT  *******************************
 * File Name          : example_cfg.h
 * Author             : WeAct Studio
 * Version            : V1.0.0
 * Description        : example APP 两个核共用的常量。
 *                      Constants shared by the two cores of the example APP.
 *
 * V3F 与 V5F 是两个独立的工程，但看的是同一块板子、同一份配置区、同一张槽表，
 * 所以这些数字只能有一份定义，放在 Common/ 下两边都 include。
 *
 * V3F and V5F are two separate projects looking at the same board, the same
 * config area and the same slot table, so these numbers live here, once.
 *******************************************************************************/
#ifndef __EXAMPLE_CFG_H
#define __EXAMPLE_CFG_H

#include "ch32h417.h"
#include "eeprom.h"

/* ---------------------------------------------------------------------------
 * APP 基地址 / APP base address
 * ---------------------------------------------------------------------------
 * 三个槽（见 SRC/Core/eeprom.h 的 APP_SLOT_ADDR(n)）：
 *   slot 0 : 0x006000 .. 0x052000   逻辑分析仪 APP
 *   slot 1 : 0x052000 .. 0x09E000   USB 转串口 APP
 *   slot 2 : 0x09E000 .. 0x0EA000   本 example（当前）
 *
 * ⚠️ 换槽要同时改三处，改漏一个"板子就安静得像坏了"：
 *   1. Common/Ld/V3F/Link_v3f.ld 的 FLASH ORIGIN
 *   2. Common/Ld/V5F/Link_v5f.ld 的 FLASH ORIGIN
 *   3. 烧录命令的 -b（0x08000000 + EX_APP_BASE）
 *
 * ⚠️ Moving to another slot means editing three places; miss one and the board
 *    just sits there looking dead:
 *   1. FLASH ORIGIN in Common/Ld/V3F/Link_v3f.ld
 *   2. FLASH ORIGIN in Common/Ld/V5F/Link_v5f.ld
 *   3. the `-b` of the burn command (0x08000000 + EX_APP_BASE)               */
#define EX_APP_SLOT               2u
#define EX_APP_BASE               APP_SLOT_ADDR( EX_APP_SLOT )   /* 0x0009E000 */
#define EX_APP_BASE_PHYS          ( FLASH_BASE + EX_APP_BASE )   /* 0x0809E000 */

/* V5F 镜像放在同一槽里、V3F 之后，偏移量与另外两个 APP 一致（0x1A000）。
 * The V5F image sits in the same slot, 0x1A000 after V3F - the same offset the
 * other two APPs use.                                                        */
#define EX_V5F_OFFSET_IN_SLOT     0x0001A000u
#define EX_V5F_BASE               ( EX_APP_BASE + EX_V5F_OFFSET_IN_SLOT ) /* 0x000B8000 */
#define EX_V5F_BASE_PHYS          ( FLASH_BASE + EX_V5F_BASE )            /* 0x080B8000 */

/* ---------------------------------------------------------------------------
 * 请求 IAP / 切 APP 槽：写在 SRAM 末尾的一个字
 * Requesting the bootloader or another APP slot: one word at the top of SRAM
 * ---------------------------------------------------------------------------
 * 0x2017FFF0 在 IAP 和两个 APP 里都无人使用（链接脚本把 RAM 顶端 16 字节空出来），
 * 所以它能活过一次软复位。IAP 上电后读它：
 *   0x69617021        -> 停在 IAP 等升级
 *   0x736C7400 | slot -> 记住该槽并跳过去
 *
 * ⚠️ 这三个常量与 IAP 工程 CH32H417_Logic_Analyzer_IAP/Common/iap.h 是同一份约定，
 *    改一边必须改另一边，否则请求会被当成噪声忽略（现象：按了没反应）。
 *
 * ⚠️ These three constants are the same convention as
 *    CH32H417_Logic_Analyzer_IAP/Common/iap.h - change both sides together.  */
#define IapRamAddr                ( 0x2017FFF0u )
#define IapRamNum                 ( 0x69617021u )   /* 进 IAP              */
#define IapSlotRamNum             ( 0x736C7400u )   /* 切槽，低字节 = 槽号 */

/* ---------------------------------------------------------------------------
 * 主机命令：藏在 SET_LINE_CODING 的波特率字段里
 * Host commands, carried in the SET_LINE_CODING baud-rate field
 * ---------------------------------------------------------------------------
 * 这个 CDC 后面没有真实串口，波特率本来毫无意义，正好拿它当命令通道 ——
 * 和 USB 转串口 APP 用的是同一套约定，所以同一份脚本对两个固件都能用。
 * 2048 这个数没有任何终端会用（真实波特率上限 9 M，但 2048  Baud 没人这么设），
 * 所以不会误触发。
 *
 * 只有 2048 = 进 IAP。切槽不用命令：按键长按 2 s 就行，那才是这个教学工程想演示的。
 *
 * This CDC has no real UART behind it, so the baud rate means nothing anyway -
 * which makes it a fine command channel, and the same convention the USB-to-UART
 * image uses, so one script drives both.  2048 is a rate no terminal ever picks,
 * so it cannot trigger by accident.
 *
 * Only 2048 = enter the bootloader.  Switching slots needs no command: holding
 * KEY for 2 s is the gesture this teaching image exists to demonstrate.        */
#define DEF_EX_IAP_BAUD                   2048u

/* ---------------------------------------------------------------------------
 * IO 电平档位（电压 x10）/ IO level presets (volts x 10)
 * ---------------------------------------------------------------------------
 * 与 USB 转串口 APP 用同一套取值范围和同一个键 EE_KEY_LEVEL，
 * 所以在两个 APP 之间切来切去，电平不会跳回去。
 * Same range and the same key (EE_KEY_LEVEL) as the USB-to-UART APP, so the
 * level survives switching between the two images.                           */
#define EX_LEVEL_NUM              4u
#define EX_LEVEL_3V3              33u
#define EX_LEVEL_2V5              25u
#define EX_LEVEL_1V8              18u
#define EX_LEVEL_1V2              12u
#define EX_LEVEL_MIN              10u               /* 1.0 V */
#define EX_LEVEL_MAX              33u               /* 3.3 V */

/* ---------------------------------------------------------------------------
 * 配置区键 / config area keys
 * ---------------------------------------------------------------------------
 * 命名空间 0x03（EE_NS_APP3，见 eeprom.h）是留给本 example 的。
 * Namespace 0x03 (EE_NS_APP3 in eeprom.h) is reserved for this example.      */
#define EE_KEY_EXAMPLE_BOOT       ( EE_NS_APP3 | 0x00000001u )   /* 上电次数  */

#endif /* __EXAMPLE_CFG_H */
