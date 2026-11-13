# WeActStudio.LogicAnalyzerV2

[English](README.md)

![WeAct Studio Logic Analyzer V2](Images/1.png)

200 MHz · 16 通道逻辑分析仪。主控 CH32H417 双核，USB 3.0 Type-C。  
一块板三个 APP，按键切换。

## 三个 APP

| 槽 | 烧录地址         | APP           | 说明                                      |
| - | ------------ | ------------- | --------------------------------------- |
| 0 | `0x08006000` | 逻辑分析仪         | 200 MHz，16 通道                           |
| 1 | `0x08052000` | USB 转串口 + DAP | 3 路 CDC 最高 9 Mbps + CMSIS-DAP v2 SWD 探针 |
| 2 | `0x0809E000` | 自写 APP        | 304 KB 槽位，SDK 编译                        |

每个槽 304 KB；前面 24 KB 是 IAP（`0x08000000`），最后 24 KB 是配置区（`0x080EA000`）。

## 规格

| 项       | 参数                                                     |
| ------- | ------------------------------------------------------ |
| 通道      | 16 路（D0–D15），可配 2 / 4 / 8 / 16                         |
| 采样率     | USB 3.0：4–200 MHz；USB 2.0：16 通道 4–20 MHz、8 通道 4–40 MHz |
| 采样时长    | 10 ms 起，上限由电脑内存决定，按 18 GB 数据量换算：200 MHz / 16 通道约 45 s，8 通道约 90 s     |
| 输入电平    | 1.0–3.3 V 可调，阈值 0.6–1.5 V                              |
| 协议解码    | 132 个（UART / I²C / SPI / CAN 等）                        |
| 供电 / 接口 | USB 3.0 Type-C，Vin 5 V                                 |
| 尺寸      | 42.5 × 26.5 × 13 mm（带外壳）                               |

## 快速开始

1. 安装 [U3LogicAnalyzer](#软件下载)，Type-C 线连板子。
2. 选采样率与时长，按「开始」（或空格）采集。
3. 需要解码时打开「解码器」，选协议。

固件升级：「帮助 → 固件升级」。

## 管脚

| 端子  | MCU 引脚 | 逻辑分析仪 | USB 转串口 + DAP |
| --- | ------ | ----- | ------------- |
| D0  | PD10   | CH0   | —             |
| D1  | PD11   | CH1   | —             |
| D2  | PD12   | CH2   | USART1 RX     |
| D3  | PD13   | CH3   | USART1 TX     |
| D4  | PD14   | CH4   | —             |
| D5  | PD15   | CH5   | —             |
| D6  | PF0    | CH6   | SWCLK         |
| D7  | PF1    | CH7   | nRESET        |
| D8  | PF2    | CH8   | SWDIO         |
| D9  | PC6    | CH9   | USART4 TX     |
| D10 | PC7    | CH10  | USART4 RX     |
| D11 | PC8    | CH11  | —             |
| D12 | PC9    | CH12  | —             |
| D13 | PA13   | CH13  | USART3 TX     |
| D14 | PA14   | CH14  | USART3 RX     |
| D15 | PA15   | CH15  | —             |

串口回环跳线：CH0 D2↔D3、CH1 D9↔D10、CH2 D13↔D14。

板内信号：

| 信号     | 引脚  | 说明                          |
| ------ | --- | --------------------------- |
| KEY    | PE1 | 内部下拉，按下为高                   |
| WS2812 | PC2 | TIM5 CH2 + DMA              |
| 串口电平基准 | PA5 | DAC 通道 2，1.0–3.3 V          |
| USB    | —   | Type-C 直连芯片 USB 3.0（SuperSpeed）口，不占 D0–D15 端子 |

## 按键

| 当前 APP        | 短按                              | 长按 2 s 松手 |
| ------------- | ------------------------------- | --------- |
| 逻辑分析仪         | —                               | 下一个槽      |
| USB 转串口 + DAP | 换 IO 电平：3.3 → 2.5 → 1.8 → 1.2 V | 下一个槽      |
| Example       | 同上                              | 下一个槽      |
| IAP           | 前进一个槽，立即写配置区                    | —         |

**进 IAP**：按住 KEY 上电，指示灯亮起后松开。灯慢闪 = 在选槽，什么都不按就停在升级模式  
（USB 上是 `1A86:FE17`），可以用升级工具烧固件。这是唯一的救砖通道。

**IAP 内切 APP**：

- 灯慢闪时每按一次前进一格，立即写进配置区，断电不丢。
- 最后一次后静默 3 s 跳进该槽运行；灯常亮 = 已定。

## 指示灯

板上只有一颗 WS2812（PC2），所有状态都靠颜色表达，亮度压得很低，不刺眼：

| 场景          | 显示                                             |
| ----------- | ---------------------------------------------- |
| 上电          | 绿 / 蓝 / 红 = 槽 0 / 1 / 2；选槽时同色慢闪，定下后常亮          |
| 长按到点        | 白色，松手就切槽                                       |
| 逻辑分析仪       | 采集时呼吸，未采集时常亮；颜色由 16 路输入电平混合，全低为暗黄              |
| 串口 + DAP 空闲 | 呼吸，颜色即 IO 电平：3.3 V 亮黄、2.5 V 紫、1.8 V 品红、1.2 V 红 |
| 串口收发        | 绿 = 收、蓝 = 发、青 = 同时，闪 50 ms 后回到电平色              |
| DAP 通信      | 红绿交替闪，停手 0.5 s 回到电平色                           |

## 软件切换

| 方式     | 操作                                          |
| ------ | ------------------------------------------- |
| 按键     | 长按 2 s，指示灯变白后松手                             |
| 串口命令   | 波特率设成 `2049` / `2050` / `2051`（槽 0 / 1 / 2） |
| 烧录     | 写入哪个槽就启动哪个槽                                 |
| IAP 菜单 | 按住 KEY 上电，灯亮后松开，灯开始闪烁；按一次前进一格，停手 3 s 跳进该槽   |

上电灯色即当前槽：绿 = 0、蓝 = 1、红 = 2。

## USB 转串口 + DAP

槽 1 的 APP：3 路 USB 转串口 + 一个 CMSIS-DAP v2 探针，两者可同时用。短按 KEY 换 IO 电平  
（3.3 → 2.5 → 1.8 → 1.2 V），停手 3 s 自动记住。

| 通道  | 端子              | USB 接口 |
| --- | --------------- | ------ |
| CH0 | D3 TX / D2 RX   | MI_00  |
| CH1 | D9 TX / D10 RX  | MI_02  |
| CH2 | D13 TX / D14 RX | MI_04  |

COM 号顺序不固定，认通道看设备属性里的「接口」MI_xx，或在任一端口把波特率设成 `2034`  
读状态报告——写着 `open` 的那行就是它。波特率最高 9 Mbps。

**转义命令**：把**波特率**设成下面这些值即触发，不用发任何数据。

| 值             | 作用                               |
| ------------- | -------------------------------- |
| `2010`–`2033` | 无级设 IO 电平 1.0–3.3 V，0.1 V 步进，不保存 |
| `2034`        | 打印状态报告：版本、三路开关与波特率、流量、电平、当前槽     |
| `2035`        | 把当前电平存为开机默认                      |
| `2048`        | 进 IAP 升级模式                       |
| `2049`–`2051` | 切到槽 0 / 1 / 2                    |

**DAP 探针**：CMSIS-DAP v2，WinUSB 免驱，SWD 三线（D6 CLK / D8 DIO / D7 nRESET）。  
没有 JTAG、没有 SWO。

```bash
pyocd list                                        # 认出 "WeAct Studio USB UART DAP"
pyocd gdbserver --frequency 8000000 --port 3333
```

OpenOCD 用 `interface/cmsis-dap.cfg` + `transport select swd`；目标固件在跑时要用  
connect-under-reset 才能连上。

**Keil MDK 5.36** 自带的驱动打不开 CMSIS-DAP v2 探针，µVision 里看不到调试器，ARM 在后续 MDK 版本里才修好。两种修法任选其一：

| 修法 | 做法 |
|---|---|
| 换 dll | 先把 Keil 安装目录的 `ARM\BIN` 备份一份，用 `Firmware/dll_fix_keil.zip` 里的 `CMSIS_DAP.dll`、`CMSIS_AGDI.dll`、`CMSIS_AGDI_V8M.dll` 覆盖同名文件，重启 µVision。 |
| 升级槽 1 | 烧 `Firmware/SLOT1_CH32H417_USB2UART_DAP_APP_fix_keil5.36.bin`。能看到的差别只有 USB 产品名，改成 `WeAct Studio CMSIS-DAP` 之后 5.36 就认得出来；`pyocd list` 和设备管理器里显示的名字也跟着变。 |

然后在工程选项里选 **CMSIS-DAP Debugger**，Port 选 **SW**。

## 软件下载

| 软件                                 | 用途                              | 下载                                                                       |
| ---------------------------------- | ------------------------------- | ------------------------------------------------------------------------ |
| **U3LogicAnalyzer**                | 上位机：采集、波形、协议解码（Windows / Linux） | <https://github.com/WeActStudio/WeActStudio.U3LogicAnalyzer/releases>    |
| **CH32H417_LogicAnalyzer_Upgrade** | 固件升级工具，GUI + CLI                | <https://github.com/WeActStudio/CH32H417_LogicAnalyzer_Upgrade/releases> |
| **WeActStudio.DAPFlasher**         | CMSIS-DAP 探针烧录工具                | <https://github.com/WeActStudio/WeActStudio.DAPFlasher/releases>         |

## 文档

| 文档                                       | 内容     |
| ---------------------------------------- | ------ |
| [U3LogicAnalyzer 使用说明](Doc/usage.zh_CN.md) | 完整使用手册 |
| [U3LogicAnalyzer Usage](Doc/usage.md)      | 英文版    |
| [3D 模型（STEP）](Hardware/WeAct-LogicAnalyzerV2_v10%203D.step) | 结构模型，可用 CAD 软件打开 |

## 目录

```
├── Firmware/       # 固件镜像 + Keil 5.36 的 dll 修复包
├── Slot2_Example/  # 槽 2 示例工程（SDK）
├── Hardware/       # 3D 模型（STEP）
├── Images/         # 产品图
└── Doc/            # 使用说明
```

## 注意

- 采样点数 = 采样率 × 时长，16 通道每点 2 字节（8 通道 1 字节），数据全在电脑内存里。软件按 18 GB 数据换算上限：200 MHz / 16 通道 45 s、8 通道 90 s，4 MHz 可到 37 分钟。可用内存不足时上位机会提前停止采集。
- 同一 USB 控制器下别的设备频繁进出可能打断采集，可降到 8 通道 200 MHz 或 16 通道 100 MHz。
- 输入电平必须与实际接入的信号一致；设错会让阈值落在错误的位置，正常的高电平被判成低。
