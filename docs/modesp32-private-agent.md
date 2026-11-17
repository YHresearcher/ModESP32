# ModESP32 — Private Agent Configuration (V1.1)

## Current implementation

This branch adds a **build-time opt-in** private backend transport. No separate inference server, LLM, ASR, or TTS service has been deployed by this change.

The firmware uses the existing XiaoZhi-compatible **WebSocket/Opus** wire protocol. A private server must implement it; a generic OpenAI HTTP API URL is **not** compatible.

In custom mode, firmware skips XiaoZhi OTA/activation, ignores stale MQTT/WebSocket credentials in NVS, and rejects a missing/invalid private WebSocket address. There is no silent fallback to XiaoZhi.

## Enable the private agent

1. Install the ESP-IDF version required by this repository (see root README).
2. Run `idf.py menuconfig`.
3. Under **ModESP32 Assistant**, enable **Use custom agent backend (disable XiaoZhi)**.
4. Set **Custom agent WebSocket endpoint** to your private server's **ws://** or **wss://** URL, e.g. `wss://your-domain.example/ws` (**placeholder only**).
5. Save; build with the repository's established board config and flashing procedure.
6. Confirm serial logs show `Private agent: bypass XiaoZhi OTA/activation` and `Connecting to configured ModESP32 agent`. If no server exists, audio conversation will not work.

Do not place API keys in Git or in a WebSocket query string. This initial transport override does not implement backend authentication. Do not expose an unauthenticated service publicly. Use a trusted LAN gateway for development, or add secure per-device provisioning and TLS before production.

## Proposed Chibi agent identity (server-side, not firmware-side)

- Agent ID: `chibi-vi-v1`
- Display name: `Chibi`
- Preferred language: Vietnamese (`vi-VN`), proper accents
- Interaction: voice-first, face-only UI, no on-screen button dependency, physical BOOT fallback
- Persona: friendly, concise, direct, never claim to have run device actions without tool confirmation
- LLM: configurable by server operator; avoid storing model API keys on ESP32
- ASR: Vietnamese-capable service selected by backend operator
- TTS: Vietnamese voice selected by backend operator; stream output as protocol-compatible Opus frames
- Wake phrase: `Hey Chibi` requires a suitable local wake-word model; not implemented by setting a text prompt

### Example backend agent profile (not consumed by firmware)

```json
{
  "agent_id": "chibi-vi-v1",
  "name": "Chibi",
  "locale": "vi-VN",
  "system_prompt": "Bạn là Chibi, trợ lý giọng nói trên ModESP32. Trả lời bằng tiếng Việt có dấu, ngắn gọn, tự nhiên và hữu ích. Không tuyên bố đã điều khiển thiết bị nếu chưa có xác nhận thực thi.",
  "asr": {"provider": "TO_CONFIGURE", "language": "vi-VN"},
  "llm": {"provider": "TO_CONFIGURE", "model": "TO_CONFIGURE"},
  "tts": {"provider": "TO_CONFIGURE", "language": "vi-VN"}
}
```

## Acceptance checks

- [ ] CI builds selected ESP32-S3 board with `CONFIG_MODESP32_CUSTOM_AGENT=y`
- [ ] Blank endpoint: serial log rejects it and does not connect to xiaozhi.me
- [ ] Old saved XiaoZhi token is not sent in private mode
- [ ] Server handshake succeeds against actual compatible backend
- [ ] Microphone capture, ASR, Vietnamese reply, Opus audio playback verified on physical device
- [ ] BOOT fallback and wake word tested on device
- [ ] TFT image inversion, colors, Vietnamese fonts verified on device

CI build passing is not physical acceptance. The endpoint, backend authentication, server deployment and third-party AI credentials are still unconfigured.
