# ModESP32 face-only firmware — acceptance checklist

Status: **experimental hardware preview**, not a finished anime mascot and not a stable release.

## Included in current code
- Main screen without TALK / VOL / SET touch buttons. The **physical BOOT (GPIO0)** remains the talk fallback.
- Existing XiaoZhi emotion renderer centered, not the previously rejected geometric LVGL robot. **Approved anime Chibi sprite is not yet embedded**.
- Vietnamese UTF-8 status messages and firmware build locale `vi-VN`. Actual accent rendering still needs a screen inspection.
- Enabled **existing ESP-SR WakeNet model `hiesp`**, whose trigger is **“Hi ESP”**. This is **not** “Hey Chibi”, and the success of detection has not been hardware verified.
- ModMic serial diagnostic reports signal level (`MIC_LEVEL samples=... peak=... avg_abs=...`), never recording speech content.
- Activation / Wi-Fi configuration screens remain visible while provisioning.
- XPT2046 driver still uses provisional mirror X, **no persistent Y-axis calibration**. Touch precision problem is not resolved in this build.

## Hardware acceptance tests (must be done on device)
1. **Cold boot:** after ESP32-S3 joins Wi-Fi, main screen is face-only. Wi-Fi activation code must still show when provisioning is required.
2. **Audio fallback:** tap physical BOOT; Serial should show connection/listening. Speak a simple sentence and check ASR server receives it. Audio output alone does not prove mic works.
3. **Mic amplitude:** read Serial Monitor at 115200 baud. While listening/wake is active, compare `MIC_LEVEL peak/avg_abs` when silent vs speaking. `0` on both suggests mic/input channel/wiring/driver issue.
4. **WakeNet:** without touching anything, say **“Hi ESP”** repeatedly at different distances. Count true detections / missed detections / false wakes. Do not advertise “Hey Chibi” yet.
5. **Speech pipeline:** confirm microphone capture, server ASR, response TTS, speaker. Server `xiaozhi.me` controls current voice language; this firmware does not install external cloud TTS.
6. **Vietnamese text:** verify “Đang chờ gọi”, “Đang lắng nghe”, “Đang trả lời” display with full glyphs; if broken, fix embedded font mapping before stable release.
7. **Reboot/memory:** power cycle five times, check crash logs, audio latency and heap trends.
8. **Touch:** use previous experimental 5-point test build to collect real coordinate pairs before changing XPT2046 mapping; never guess Y correction.

## Rollback
Do not overwrite/delete the known functional touch test firmware. Restore
[mod-esp32s3-touch-dev-ea1ab677c874](https://github.com/YHresearcher/ModESP32/releases/tag/mod-esp32s3-touch-dev-ea1ab677c874)
if the current experimental build fails.

## Unfinished work
- True approved image asset for anime face + expression sprites.
- Hardware-measured XPT2046 calibration and persistence in NVS.
- Validated microphone signal path and natural wake detection.
- Custom trigger “Hey Chibi” (needs an appropriate model; `hiesp` is only a diagnostic fallback).
- Full Vietnamese TTS using cloud infrastructure (not solved by firmware-only work).
