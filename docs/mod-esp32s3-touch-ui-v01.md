# ModESP32 · Touch & UI v0.1 (experimental)

## Confirmed hardware (2026-11-16)
- ESP32-S3 N16R8; flash 16 MB, PSRAM 8 MB, 3.3 V quad flash.
- ST7789V profile actually displays XiaoZhi UI on the user's 2.8-inch 240×320 LCD. Backlight illuminated after user connected LED to 3V3. **This does not certify LED drive for long-term use**; verify module resistor/backlight circuit.
- Module pin labels include `T_CLK`, `T_DIN`, `T_DO`, `T_CS`, `T_IRQ`. This suggests SPI resistive touch, likely XPT2046 compatible; controller identity is not proven.

## Wiring planned for the touch diagnostic build
| Touch pin | ESP32-S3 | Notes |
| --- | --- | --- |
| T_CLK | GPIO12 | Shared LCD SPI clock |
| T_DIN | GPIO11 | Shared LCD MOSI |
| T_DO | GPIO8 | Shared LCD MISO |
| T_CS | GPIO9 | Touch-only chip select |
| T_IRQ | GPIO7 | Input / optional interrupt |
| GND / VCC | On LCD module | Verify voltage and circuit; no guessed external wiring |

The existing ST7789V LCD pins stay unchanged; the **separate experimental firmware** adds the XPT2046 driver and LVGL pointer input. Keep the original proven `mod-esp32s3-st7789v-28` firmware release available as a recovery image. ESP32 BOOT physical button remains a fallback.

## Firmware variants
- `mod-esp32s3-st7789v-28`: original display firmware, touch not initialized, unchanged.
- `mod-esp32s3-st7789v-touch-28`: experimental touch diagnostic, same display/audio pins and an additional XPT2046 SPI input.
- The touchscreen profile is opt-in on the website and may remain unavailable while CI fails or no public release manifest exists.
- Never choose ILI9341 merely to enable touch: LCD controller and touch controller are separate chips.

## How to test
1. Unplug all power, confirm wiring and module pinout, reconnect.
2. Select ST7789V → Touch Diagnostic in the web flasher; verify binary SHA-256, save configuration if necessary, and explicitly approve flashing.
3. Connect Serial Monitor at 115200. Look for `TOUCH_READY` or `TOUCH_INIT_FAILED`.
4. On the display choose the 46 px `TOUCH TEST` button and tap 5 orange targets in sequence. Log `TOUCH_5PT step=...` contains reported position; `TOUCH_SAMPLE x=... y=...` reports the pointer location.
5. If taps do not register, first check `T_DO`, `T_CS`, common GND, and correct SPI wires. If taps register but coordinates are reversed or offset, use the 5-point worksheet on the website. **It is a diagnostic estimator, not a raw ADC calibration or a persistent calibration setting.**
6. Reflash original proven ST7789V if touch build fails; do not modify eFuses or erase the full flash unnecessarily.

## Touch-first 240×320 UI guidelines
- Preserve XiaoZhi's existing state machine and `SetEmotion()`/LVGL emotion display.
- Large touch targets of at least 44×44 px; a 46px on-screen diagnostic button is provided in the experimental variant.
- Avoid gesture-only navigation; BOOT button always works.
- Mascot states derive from real device status: idle, listening, thinking, speaking, Wi-Fi setup/disconnected, error and sleep. No fake listening status.
- Do not treat the 2.8-inch device as a web browser; the Wi-Fi setup portal stays accessible via `192.168.4.1`.
- Calibrate rotation first, then adjust UI layout. Do not save incorrect calibration while hardware is unverified.

## Future hardware-validation gate
The XPT2046 controller compatibility, IRQ behavior, rotated display coordinates, bounce/pressure noise and backlight electrical safety require testing on the physical device. CI green only proves build and software checks, not those hardware behaviors.
