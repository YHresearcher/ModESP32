# XiaoZhi Vietnamese voice backend — Mac M2 Pro

This is a deployable **configuration for upstream** [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server), not a replacement firmware and not an online-hosted service.

## Modules
- ESP32-S3: unchanged upstream XiaoZhi WebSocket/Opus audio client.
- ASR: Groq Whisper Large v3 Turbo, Vietnamese speech supported. Requires **your own Groq API key** and depends on the provider's limits.
- LLM: local Ollama `qwen2.5:3b` on macOS.
- TTS: upstream EdgeTTS adapter, `vi-VN-HoaiMyNeural` (Vietnamese female voice). This is an unofficial Edge service integration: service limits and availability may change.
- VAD: upstream SileroVAD. Audio flow and timing depend on network and server readiness.
- VieNeu-TTS: **not implemented** in this package; requires a separate compatible TTS provider adapter and model/license validation.

## Requirements
- Docker Desktop and Ollama running on a Mac, connected to same LAN as ESP32.
- Groq API key for transcription (Groq ASR is not guaranteed free).
- Mac always running while using this local backend.
- `ollama serve` reachable by Docker at `host.docker.internal:11434`. Confirm reachability; Ollama by default may bind only loopback, and Docker Desktop host routing can vary.
- The server image is an upstream moving `:server_latest` tag. Pin to a tested digest for production.

## Setup
```bash
cd deploy/vietnamese-voice
ollama pull qwen2.5:3b
# Optionally: export LAN_IP=192.168.x.x
bash setup.sh
docker compose up -d
docker compose logs -f --tail 80
```
Open `http://<Mac-LAN-IP>:8003/xiaozhi/ota/` from the same LAN to confirm the OTA URL provider. The ESP32 must receive a WebSocket endpoint from the appropriate OTA/provisioning configuration; **changing only this YAML does not redirect an ESP32 still provisioned for xiaozhi.me**. Do not overwrite the current XiaoZhi.me working firmware or credentials before confirming an alternate endpoint path.

## Important precautions
- Restrict ports 8000 and 8003 to the trusted home LAN. This sample disables authentication and MUST NOT be exposed to Internet. Configure firewall or bind ports to the Mac LAN IP when needed.
- Config file generated in `data/.config.yaml` contains API credentials and is ignored by Git.
- On-device voice/ASR and actual ESP32 end-to-end audio have **not** been validated by this package.
- Device fallback is a separate known-good firmware flash; no automatic failover to xiaozhi.me is claimed.
- If upstream config keys change, compare against `main/xiaozhi-server/config.yaml` and `core/providers/tts/edge.py`.
