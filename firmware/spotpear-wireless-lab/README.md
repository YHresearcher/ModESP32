# Spotpear Wireless Lab (experimental)

Independent ESP-IDF subproject inside **YHresearcher/ModESP32**. The XiaoZhi firmware at the repository root is deliberately untouched.

## Supported at source level
- ESP32-S3 radio-based passive 2.4 GHz Wi-Fi scan
- BLE passive discovery via NimBLE
- Own WPA2 test SoftAP (SPOTPEAR_LAB)
- Own BLE test advertisement (SPOTPEAR_LAB), 15 seconds
- Stop radio tests using USB console

No deauth flood, beacon spam, identity spoofing or third-party access. Use only with systems you own or are authorized to test.

## LCD bring-up (experimental)

This build initializes a 240×240 **GC9A01** round LCD at boot, runs an automatic red/green/blue/white full-screen diagnostic (~4.4 seconds), then draws a standalone **SPOTPEAR / WIRELESS LAB / USB READY** splash screen. The command `lcd` repeats this test without resetting the device. The logs `LCD_DIAG_SEND`, `LCD_DIAG_TX_SUBMITTED` and `LCD_DIAG_DONE` confirm submission to SPI only, not actual LCD display. Confirmed reference for Spotpear ESP32-AI-1.28 v1.0 *non-touch* PCB from `tomorrow56/xiaozhi-esp32` at `fed7f146`, `main/boards/spotpear/spotpear-esp32-ai-1.28/config.h`: MOSI=GPIO17, SCLK=GPIO14, CS=GPIO13, DC=GPIO10, RST=GPIO18, BL=GPIO3 **active-high**. The previously assumed 2/4/5/47/38/42 mapping belongs to a different BOX touchscreen board; do not reuse it on this PCB. Display uses the Espressif `esp_lcd_gc9a01` component and SPI3 at 10 MHz. These pins are not universal for ESP32-S3 round-screen devices; verify your board revision first.

**Important:** This is an LCD hardware test and USB console firmware, not yet a two-button menu. It does not implement buttons, speaker, microphone or microSD. Appearance, color order, backlight polarity and physical function still require testing on the actual board. Build success is not hardware validation.

USB console commands (`help`, `wifi`, `ble`, `ap`, `advertise`, `stop`, `lcd`) now use interrupt-driven USB Serial/JTAG RX and log `Command received: ...` when Enter terminates a command. If the LCD fails, the firmware should report `LCD init failed` on USB and continue radio initialization.

## Build

Requires ESP-IDF 5.3.2 (kept independent of XiaoZhi root which uses ESP-IDF 6.1).

```bash
cd firmware/spotpear-wireless-lab
idf.py set-target esp32s3
idf.py build
```

Back up the *entire original flash* before experimenting and confirm flash/PSRAM sizes and the device's chip revision. Use the original backup to recover.

## Architecture / isolation
This directory has its own CMakeLists.txt, main and sdkconfig defaults. It is not registered as a XiaoZhi board and does not change its build scripts or OTA output.

## Status
**Experimental / not hardware-tested.** GitHub Actions checks compilation only.
