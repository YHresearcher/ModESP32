# Chibi Cloud — ESP32-S3 N16R8 firmware activation

Cloud gateway (live): `wss://chibi-esp32.hlthanh1992.workers.dev/ws`.

The cloud-only end-to-end test passed in GitHub Actions run
[44035904254](https://github.com/YHresearcher/ModESP32/actions/runs/44035904254):
16 kHz Opus input -> Gemini transcription + chat -> Vietnamese Gemini TTS ->
101 binary 24 kHz Opus packets.

**Cloud testing is not a substitute for physical-board acceptance.**

## Hardware variant

Target build name: `mod-esp32s3-st7789v-28-chibi-cloud`.
This uses board `mod-esp32s3-st7789v-28`, ESP32-S3 N16R8,
ST7789V 240×320 display, XPT2046, I2S microphone and speaker.
Pin mappings are in `main/boards/mod-esp32s3-st7789v-28/config.h`.
Physical wiring and display inversion/touch orientation are not yet verified.

## Important: secrets MUST NOT be committed

The Cloudflare Worker requires a bearer token set as `CHIBI_DEVICE_TOKEN`
in GitHub Actions secrets. GitHub masks this; it cannot be retrieved from
the GitHub secret after saving. You must use the same value that you
privately saved during setup, or rotate the Worker secret to a new value
you control. **Do not paste it into a chat or public issue.**

The CI variant intentionally leaves `CONFIG_MODESP32_AGENT_TOKEN` empty;
a firmware built this way will NOT authenticate with Cloudflare.
Do not call it flash-ready for authenticated use or distribute it as such.

## Private local build + flash

1. Install ESP-IDF 6.1 per repository README (ESP-IDF 5.x is unsupported).
2. In a private local clone of this branch, run:
```bash
git clone --branch feat/chibi-cloudflare-free-v1 https://github.com/YHresearcher/ModESP32.git
cd ModESP32
source "$IDF_PATH/export.sh"
python3 scripts/build.py mod-esp32s3-st7789v-28 --name mod-esp32s3-st7789v-28-chibi-cloud
```
3. Use `idf.py menuconfig` in the build's correctly selected SDK configuration. Under **ModESP32 Assistant** verify Custom Agent is on, URL is the Cloudflare WSS endpoint, and enter the *private* bearer token. Rebuild after changing the configuration. Because `scripts/build.py` may regenerate `sdkconfig`, validate that the final image actually contains the custom token by examining the effective config **locally only** (never print or upload secrets).
4. Flash with `idf.py -p /dev/cu.usbmodemXXXX flash monitor` once the appropriate build directory/board configuration is selected. Substitute the actual USB serial port; do not blindly flash the wrong partition or hardware variant.
5. Observe serial logs: custom transport, WebSocket v1 hello, authenticated connection. Press the physical BOOT button to begin a short voice test if the wake word is not yet available.
6. Verify actual Vietnamese STT text, returned TTS audio, speaker playback, mic and touch orientation. Do NOT claim wake word `Hey Chibi` is functional until a custom offline wake model has been installed and tested.

A future secure provisioning portal can eliminate the need to include the token in a local firmware image. Until then, build locally with care and keep images containing the token private.

## Failure diagnosis

- **HTTP 401 / unauthorized:** token mismatch, missing header, or wrong Cloudflare account.
- **WS hello times out:** wrong URL, TLS, network issue or protocol version mismatch.
- **Gemini quota 429 / upstream 503:** API free-tier capacity/quota, not necessarily firmware.
- **No spoken reply:** confirm Opus 24 kHz output, I2S speaker wiring and hardware audio codec.
- **No trigger:** wake word `Hey Chibi` requires separate offline model; use physical trigger first.

## Deployment artifacts and status

The cloud backend is live. The firmware branch and its custom hardware variant
require CI compilation and physical ESP32-S3 validation before being released.
