# Chibi Cloud touch-board route — integration status

The board used for the approved Chibi face and verified LCD is
`mod-esp32s3-st7789v-touch-28`, **not**
`mod-esp32s3-st7789v-28`.

## Why the face appears but the AI is still XiaoZhi

- `mod-esp32s3-st7789v-touch-28` default build contains Chibi LVGL face and
  an optional local MCP persona tool, but **CONFIG_MODESP32_CUSTOM_AGENT is off**.
  It therefore uses the default XiaoZhi cloud, profile and voice.
- `mod-esp32s3-st7789v-28-chibi-cloud` points at Cloudflare but is the
  **non-touch** board variant, so it must not replace the approved touch LCD
  firmware simply to change the AI.
- The dedicated `mod-esp32s3-st7789v-touch-28-chibi-cloud` build now combines
  the approved touch display driver with the custom WebSocket configuration.
- The Cloudflare gateway uses Gemini for conversation, and Gemini TTS;
  it currently selects voice using `GEMINI_TTS_VOICE` (default `Kore`) and
  uses the hardcoded `SYSTEM` persona in `cloudflare/chibi/src/index.js`.
  The LVGL face and MCP tool do not modify these settings.
- Public firmware does **not** embed `CHIBI_DEVICE_TOKEN`. Custom agent uses
  `chibi/device_token` from device NVS, with optional locally compiled
  `CONFIG_MODESP32_AGENT_TOKEN` fallback. This must be provisioned securely
  before a cloud voice connection can succeed. Do not insert the token into
  GitHub public source, public Actions build, website JavaScript or released
  merged-binary. No provisioning UI has been implemented in this change.

## Debug decision table

| Observation | Interpretation |
|---|---|
| Boot log lacks `Chibi firmware build:` | Wrong firmware variant is flashed |
| `CHIBI_AUTH_MISSING` | Correct Chibi build, but no device token provisioned |
| HTTP 401 at cloud handshake | Token differs from Cloudflare secret |
| WebSocket connected, `listen.start`, but no `listen.stop` | Device VAD/turn-ending problem |
| `listen.stop`, but no STT/TTS | Investigate gateway request and logs |
| Answers in XiaoZhi voice after successful flash | Inspect firmware boot identity and actual WebSocket server; likely XiaoZhi firmware still installed |

## Acceptance gates

1. Compile `mod-esp32s3-st7789v-touch-28-chibi-cloud` under ESP-IDF 6.1.
2. Keep old touchscreen/display version for rollback; do not overwrite it
   until hardware pinout and calibration are confirmed.
3. Provision per-device token securely, verify connection only to
   `wss://chibi-esp32.hlthanh1992.workers.dev/ws`.
4. Speak a sentence, pause, verify `listen.stop` and STT, then Gemini TTS
   reply and device state `Idle`. Repeat across short/long utterances.
5. Provide server-side configuration for custom persona and TTS voice before
   claiming website/profile voice options are supported.

No hardware acceptance or audio voice selection has been claimed.
