# ModESP32 face-only firmware — acceptance checklist

Status: **Chibi AI v1.1 Voice Foundation Preview**, not a stable release. Owner reports microphone input working; fresh firmware face rendering and wake-word still need device acceptance.

## Included in current code
- Main screen without TALK / VOL / SET touch buttons. The **physical BOOT (GPIO0)** remains the talk fallback.
- Approved Chibi JPEG face is **embedded** as base64 firmware source, decoded once to RGB565 by the existing JPEG decoder, and displayed in LVGL. On memory/decode failure the existing XiaoZhi emotion renderer remains the fallback. One static face, not yet a multi-expression sprite animation.
- Vietnamese UTF-8 status messages and firmware build locale `vi-VN`. Actual accent rendering still needs a screen inspection.
- Enabled **existing ESP-SR WakeNet model `hiesp`**, whose trigger is **“Hi ESP”**. This is **not** “Hey Chibi”, and the success of detection has not been hardware verified.
- Owner confirms the microphone now works. ModMic still logs signal level (`MIC_LEVEL samples=... peak=... avg_abs=...`), never recording speech content; firmware-specific end-to-end ASR remains unverified.
- Activation / Wi-Fi configuration screens remain visible while provisioning.
- XPT2046 driver still uses provisional mirror X, **no persistent Y-axis calibration**. Touch precision problem is not resolved in this build.

## Hardware acceptance tests (must be done on device)
1. **Cold boot:** after ESP32-S3 joins Wi-Fi, main screen is face-only. Wi-Fi activation code must still show when provisioning is required.
2. **Audio fallback:** tap physical BOOT; Serial should show connection/listening. Speak a simple sentence and check ASR server receives it. Audio output alone does not prove mic works.
3. **Mic amplitude (regression):** microphone was previously confirmed working by the owner. On this newly built release, read Serial Monitor at 115200 baud, compare `MIC_LEVEL peak/avg_abs` silent vs speaking, and verify cloud ASR still recognizes a phrase. Nonzero PCM alone is insufficient.
4. **WakeNet:** without touching anything, say **“Hi ESP”** repeatedly at different distances. Count true detections / missed detections / false wakes. Do not advertise “Hey Chibi” yet.
5. **Speech pipeline:** confirm microphone capture, server ASR, response TTS, speaker. Server `xiaozhi.me` controls current voice language; this firmware does not install external cloud TTS.
6. **Approved mascot and Vietnamese text:** verify `CHIBI_ASSET_READY`, approved Chibi face appears (160x162 JPEG), no TALK/VOL/SET buttons; confirm “Đang chờ gọi”, “Đang lắng nghe”, “Đang trả lời” display with all Vietnamese accents. Fallback `CHIBI_ASSET_FAIL` requires diagnosing JPEG/heap; check Unicode font mapping before stable release.
7. **Reboot/memory:** power cycle five times, check crash logs, audio latency and heap trends.
8. **Touch:** use previous experimental 5-point test build to collect real coordinate pairs before changing XPT2046 mapping; never guess Y correction.

## Rollback
Do not overwrite/delete the known functional touch test firmware. Restore
[mod-esp32s3-touch-dev-ea1ab677c874](https://github.com/YHresearcher/ModESP32/releases/tag/mod-esp32s3-touch-dev-ea1ab677c874)
if the current experimental build fails.

## Unfinished work
- Animated multi-expression sprite set (currently approved static JPEG face only).
- Hardware-measured XPT2046 calibration and persistence in NVS.
- Validated full conversation and wake detection on this new firmware (microphone alone confirmed working previously).
- Custom trigger “Hey Chibi” (needs an appropriate model; `hiesp` is only a diagnostic fallback).
- Full Vietnamese TTS using cloud infrastructure (not solved by firmware-only work).

## Firmware preview release
- Voice build: `--language vi-VN --wake-word hiesp`; no custom wake-word model is bundled.
- Inspect GitHub Actions, prerelease SHA-256, and flash from the corresponding immutable development tag, not a mutable artifact named 'latest'.
- The existing XiaoZhi backend handles cloud TTS. Firmware changes do not select a Vietnamese female voice on the server; configure that in XiaoZhi Console if available.
- UI assets, wake responsiveness, TTS and HTTP/WebSocket recovery are not marked stable without on-board evidence.
