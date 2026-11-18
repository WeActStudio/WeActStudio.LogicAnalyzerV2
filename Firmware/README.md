# Firmware

The three APPs coexist in flash. Hold KEY for about two seconds and release to switch to the next slot; hold KEY while powering on to enter IAP and select the boot slot or write an image.

## Files

| File | Slot | Load address | Description |
|---|---|---|---|
| `SLOT0_CH32H417_Logic_Analyzer_APP_v0.2.bin` | 0 | `0x08006000` | Logic analyser, current version |
| `SLOT1_CH32H417_USB2UART_DAP_APP.bin` | 1 | `0x08052000` | USB-serial + DAP |
| `SLOT1_CH32H417_USB2UART_DAP_APP_fix_keil5.36.bin` | 1 | `0x08052000` | Same as above, USB product name changed to WeAct Studio CMSIS-DAP for Keil 5.36 |
| `SLOT2_CH32H417_Example_APP.bin` | 2 | `0x0809E000` | Teaching example, single CDC channel printing board information |
| `dll_fix_keil.zip` | — | — | DLLs for Keil 5.36 to recognise DAP; copy into the Keil installation's `ARM\BIN` |

## Logic analyser versions

The version is reported in the `sw` byte of the `0xAC` reply: `0x01` for v0.1 and `0x02` for v0.2. The v0.1 image is kept in `legacy version/` for rollback.

v0.2 changes over v0.1:

- Indicator brightness can be set from the host software, with five levels: Dim, Default, Bright, Brighter and Maximum.
- Short-press counts are reported in the reply to `0xA4` (get capture parameters), which the host uses to start and stop captures with the board key.
- Microsoft OS 1.0 compatible descriptors (WCID) were added. Windows uses the CH375 driver by default. To switch to WinUSB, uninstall the device in Device Manager with "Delete the driver software for this device" checked, then replug; firmware 0.2 binds WinUSB automatically. v0.1 has no compatible descriptors and still needs Zadig for WinUSB.

Indicator brightness and key start/stop require host software 0.2.0.0 or newer.

## Flashing

In the host software, choose **Help → FW Update** and select the image and target slot.

With several boards connected, the command-line tool must be given the target board's full serial number with `-d`. Without it the tool picks the first board it finds. Use `--dry-run` to check the write range first.
