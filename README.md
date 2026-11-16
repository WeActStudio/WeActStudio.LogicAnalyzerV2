# WeActStudio.LogicAnalyzerV2

[中文](README_zh.md)

![WeAct Studio Logic Analyzer V2](Images/2.png)

200 MHz, 16-channel logic analyzer with a CH32H417 RISC-V dual-core and a USB 3.0 Type-C interface. Three apps are built in and are selected with a button.

## Three apps

| Slot | Flash address | App | Description |
|---|---|---|---|
| 0 | `0x08006000` | Logic analyzer | 200 MHz, 16 channels |
| 1 | `0x08052000` | USB-serial + DAP | 3× CDC up to 9 Mbps + CMSIS-DAP v2 SWD debugger |
| 2 | `0x0809E000` | Your own app | 304 KB slot, build with the SDK |

304 KB per slot; the first 24 KB is the IAP (`0x08000000`) and the last 24 KB is the config area
(`0x080EA000`).

## Specifications

| Item | Value |
|---|---|
| Channels | 16 (D0–D15), configurable as 2 / 4 / 8 / 16 |
| Sample rate | USB 3.0: 4–200 MHz; USB 2.0: 4–20 MHz at 16 channels, 4–40 MHz at 8 channels |
| Capture length | From 10 ms. The software uses an 18 GB data figure to define the length ceiling: about 45 s at 200 MHz / 16 channels, 90 s at 8 channels (the actual length is limited by free PC memory) |
| Input level | 1.0–3.3 V adjustable, threshold 0.6–1.5 V |
| Protocol decoders | 132 (UART / I²C / SPI / CAN and more) |
| Power / interface | USB 3.0 Type-C, Vin 5 V |
| Size | 42.5 × 26.5 × 13 mm (with enclosure) |

## Quick start

1. Install [U3LogicAnalyzer](#downloads) and connect the device with a Type-C cable.
2. Select the sample rate and capture length, then press **Start** (or the space bar) to capture.
3. To decode a protocol, open **Decoders** and select it.

Firmware update: **Help → FW Update**.

## Pinout

| Terminal | MCU pin | Logic analyzer | USB-serial + DAP |
|---|---|---|---|
| D0 | PD10 | CH0 | — |
| D1 | PD11 | CH1 | — |
| D2 | PD12 | CH2 | USART1 RX |
| D3 | PD13 | CH3 | USART1 TX |
| D4 | PD14 | CH4 | — |
| D5 | PD15 | CH5 | — |
| D6 | PF0 | CH6 | SWCLK |
| D7 | PF1 | CH7 | nRESET |
| D8 | PF2 | CH8 | SWDIO |
| D9 | PC6 | CH9 | USART4 TX |
| D10 | PC7 | CH10 | USART4 RX |
| D11 | PC8 | CH11 | — |
| D12 | PC9 | CH12 | — |
| D13 | PA13 | CH13 | USART3 TX |
| D14 | PA14 | CH14 | USART3 RX |
| D15 | PA15 | CH15 | — |

Loopback jumpers: CH0 D2↔D3, CH1 D9↔D10, CH2 D13↔D14.

On-board signals:

| Signal | Pin | Description |
|---|---|---|
| KEY | PE1 | Internal pull-down, high when pressed |
| WS2812 | PC2 | TIM5 CH2 + DMA |
| UART level reference | PA5 | DAC channel 2, 1.0–3.3 V |
| USB | — | Type-C connects directly to the on-chip USB 3.0 (SuperSpeed) port and does not use terminals D0–D15 |

## Button

| Running app | Short press | Hold 2 s, then release |
|---|---|---|
| Logic analyzer | — | Switches to the next slot: USB-serial + DAP |
| USB-serial + DAP | Switches the IO level: 3.3 → 2.5 → 1.8 → 1.2 V | Switches to the next slot: Your own app |
| Your own app | Same as above | Switches to the next slot: Logic analyzer |
| IAP | Advances one slot, written immediately | — |

**Entering IAP**: hold KEY while powering on, then release once the LED lights up. A slow blink means the board is waiting for a slot selection; if the button is left untouched until the timeout expires, it stays in upgrade mode (USB ID `1A86:FE17`), where the upgrade tool can flash firmware. This is the only recovery path.

**Switching apps inside IAP**:

- While the LED blinks, each press advances one slot and writes the selection immediately, so it survives a power loss.
- After the last press, 3 s of silence boots that slot; a steady LED confirms the selection.

## LED

A single WS2812 (PC2) reports every state through colour; it is kept dim deliberately:

| State | Display |
|---|---|
| Power-on | Green, blue and red denote slots 0, 1 and 2; the LED blinks in that colour while a slot is being selected and stays lit once set |
| Hold threshold reached | White; release to switch the slot |
| Logic analyzer | Breathing while capturing, steady when idle; the colour is a mix of the 16 input levels, dark yellow when all are low |
| USB-serial + DAP idle | Breathing in the IO level colour: 3.3 V yellow, 2.5 V purple, 1.8 V magenta, 1.2 V red |
| UART traffic | Green for RX, blue for TX, cyan for both; a 50 ms flash, then back to the level colour |
| DAP traffic | Red and green alternate; back to the level colour 0.5 s after the last command |

## Switching apps

| Method | Procedure |
|---|---|
| Button | Hold for 2 s until the LED turns white, then release |
| UART command | Set the baud rate to `2049` / `2050` / `2051` (slot 0 / 1 / 2) |
| Flashing | Writing a slot makes it the boot slot |
| IAP slot select | Hold KEY while powering on and release once the LED lights up; the LED then blinks, each press advances one slot, and 3 s of idle time boots that slot |

The LED colour at power-on shows the active slot: green for 0, blue for 1, red for 2.

## USB-serial + DAP

The slot 1 app provides three USB-to-serial ports and a CMSIS-DAP v2 debugger, both usable at the same time. A short press on the button switches the IO level (3.3 → 2.5 → 1.8 → 1.2 V); the setting is saved 3 s later.

| Channel | Terminals | USB interface |
|---|---|---|
| CH0 | D3 TX / D2 RX | MI_00 |
| CH1 | D9 TX / D10 RX | MI_02 |
| CH2 | D13 TX / D14 RX | MI_04 |

COM port numbers are not stable. Identify a port by its MI_xx interface number in the device properties, or set the baud rate to `2034` on any port and read the status report: the entry marked `open` is that port. The maximum baud rate is 9 Mbps.

**Escape commands**: set the baud rate to one of the values below; no data needs to be sent.

| Value | Effect |
|---|---|
| `2010`–`2033` | Set the IO level continuously from 1.0 to 3.3 V in 0.1 V steps, not saved |
| `2034` | Status report: version, three ports and baud rates, traffic, level, active slot |
| `2035` | Save the current level as the power-on default |
| `2048` | Enter IAP upgrade mode |
| `2049`–`2051` | Switch to slot 0 / 1 / 2 |

**DAP**: CMSIS-DAP v2 debugger, driver-free over WinUSB, SWD on three pins (D6 CLK / D8 DIO / D7 nRESET). Neither JTAG nor SWO is supported.

```bash
pyocd list                                        # shows "WeAct Studio USB UART DAP"
pyocd gdbserver --frequency 8000000 --port 3333
```

With OpenOCD, use `interface/cmsis-dap.cfg` and `transport select swd`; when the target firmware is running, connect-under-reset is required.

**Keil MDK 5.36** ships a driver that is too old to recognise CMSIS-DAP v2 debuggers, so µVision lists no debugger; ARM fixed this in a later MDK release. **Either fix below is sufficient on its own:**

| Fix | Procedure |
|---|---|
| Replace the DLLs | Back up the Keil installation's `ARM\BIN`, overwrite `CMSIS_DAP.dll`, `CMSIS_AGDI.dll` and `CMSIS_AGDI_V8M.dll` with the copies in `Firmware/dll_fix_keil.zip`, then restart µVision. |
| Update slot 1 | Flash `Firmware/SLOT1_CH32H417_USB2UART_DAP_APP_fix_keil5.36.bin`. The only visible difference is the USB product string, now `WeAct Studio CMSIS-DAP`, which version 5.36 accepts; `pyocd list` and the device manager report the same name. |

Then select **CMSIS-DAP Debugger** and Port **SW** in the project options.

## Downloads

| Software | Purpose | Download |
|---|---|---|
| **U3LogicAnalyzer** | PC software: capture, waveform, decoding (Windows / Linux) | <https://github.com/WeActStudio/WeActStudio.U3LogicAnalyzer/releases> |
| **CH32H417_LogicAnalyzer_Upgrade** | Firmware upgrade tool, with a GUI and a CLI | <https://github.com/WeActStudio/CH32H417_LogicAnalyzer_Upgrade/releases> |
| **WeActStudio.DAPFlasher** | Flashing tool for CMSIS-DAP debuggers | <https://github.com/WeActStudio/WeActStudio.DAPFlasher/releases> |

## Documentation

| Document | Contents |
|---|---|
| [U3LogicAnalyzer usage](Doc/usage.md) | Full user guide |
| [使用说明](Doc/usage.zh_CN.md) | Chinese version |
| [3D model (STEP)](Hardware/WeAct-LogicAnalyzerV2_v10%203D.step) | Mechanical model, open with a CAD tool |

## Layout

```
├── Firmware/       # firmware images + the Keil 5.36 DLL fix
├── Slot2_Example/  # slot 2 example app (SDK)
├── Hardware/       # 3D model (STEP)
├── Images/         # product images
└── Doc/            # usage guide
```

## Notes

- Samples = rate × duration, with 2 bytes per sample at 16 channels and 1 byte at 8 channels; all data is held in PC memory. The software applies an 18 GB data ceiling: 45 s at 200 MHz / 16 channels, 90 s at 8 channels, and up to 37 min at 4 MHz. A capture stops early when free memory runs low.
- Other devices on the same USB controller can interrupt a capture when they are plugged in, unplugged, opened or closed frequently; reduce to 8 channels at 200 MHz or 16 channels at 100 MHz.
- The input level must match the signal actually applied; a wrong setting misplaces the threshold, so genuine highs read as low.

```
/*---------------------------------------
- WeAct Studio Official Link
- taobao: WeActStudio.taobao.com
- aliexpress 1: WeActStudio.aliexpress.com
- aliexpress 2: WeActStudioOne.aliexpress.com
- github: github.com/WeActStudio
- gitee: gitee.com/WeActStudio
- blog: blog.weactstudio.com
---------------------------------------*/
```
