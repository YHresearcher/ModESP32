# Spotpear Wireless Lab (experimental)

Independent ESP-IDF subproject inside **YHresearcher/ModESP32**. The XiaoZhi firmware at the repository root is deliberately untouched.

## Supported at source level
- ESP32-S3 radio-based passive 2.4 GHz Wi-Fi scan
- BLE passive discovery via NimBLE
- Own WPA2 test SoftAP (SPOTPEAR_LAB)
- Own BLE test advertisement (SPOTPEAR_LAB), 15 seconds
- Stop radio tests using USB console

No deauth flood, beacon spam, identity spoofing or third-party access. Use only with systems you own or are authorized to test.

## Critical limitation

This is a **USB-console-first integration baseline**, not a fully validated Spotpear BOX GUI firmware. It does not yet initialize the GC9A01 round LCD, buttons, speaker, microphone or microSD. Those require confirmed board revision and separate testing. Build success is not hardware validation.

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
