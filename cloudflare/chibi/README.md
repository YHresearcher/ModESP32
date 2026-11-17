# Chibi Cloudflare Workers Free — Stage 1

This is a **working-code prototype**, not a deployed voice assistant. It contains:
- Token-protected WSS path `/ws` with the XiaoZhi WebSocket **v1** hello response.
- Ogg/Opus muxing from ESP32's raw 16 kHz mono Opus frames (20/40/60 ms only).
- Speech transcription and Vietnamese assistant reply using Gemini API.
- The assistant's reply appears as `tts.sentence_start` text on devices supporting this protocol.

**Not yet present:** Opus-encoded Vietnamese TTS playback, tested wake-word “Hey Chibi”, production-quality VAD, long conversation history, robust reconnect/timeout tests, real-device verification. The worker deliberately sends **no fake binary voice payload**. It only returns text until a compatible WASM Opus encoder is implemented.

## Run and deploy

Requires Node.js 20+, Cloudflare account, Gemini AI Studio API key (free-tier availability subject to provider quota).

```bash
cd cloudflare/chibi
npm install
npm run check
npm test
npx wrangler login
npx wrangler secret put GEMINI_API_KEY
npx wrangler secret put CHIBI_DEVICE_TOKEN
npm run deploy
```

When prompted for the device token, generate a long random unique value, e.g. `openssl rand -base64 32`. **Do not paste real secrets in chats, scripts, public repositories or URLs.** Wrangler secret values are stored in the Cloudflare account. Note that `wrangler secret put` can create a deployed Worker during initial provisioning; if it fails because the Worker doesn't exist yet, run `npm run deploy` first, and then set both secrets before connecting devices.

Expected deployment URL is shown by Wrangler; don't assume a URL until deployment completes. Example endpoint: `wss://chibi-esp32.<your-subdomain>.workers.dev/ws`.

## Firmware configuration

Build firmware from branch `fix/custom-agent-routing-v1` if that branch is the only one with the current custom-agent features; otherwise verify the feature exists on your selected branch.

In `idf.py menuconfig`:
- Enable **Use custom agent backend**.
- Set **Custom agent WebSocket endpoint** to **real** `wss://.../ws`.
- Set **Private agent bearer token** equal to `CHIBI_DEVICE_TOKEN`.

Do not put secrets into a committed `sdkconfig`. The ESP32 must support TLS and the protocol's 16 kHz mono Opus frame format.

## Safety and limits

- A static device token embedded in firmware is a prototype only. Use per-device credentials for production.
- Only connect to `wss://`, never `ws://` over the public Internet.
- Gemini audio and text are processed by Google. Free-tier limits vary and can change.
- Cloudflare Workers Free CPU/request quota applies; audio buffering/encoding, API usage and concurrency may be constrained.
- Raw packets are retained only in memory for the turn. No durable persistence is implemented.
- No actual Cloudflare account access or secret provisioning has been performed by this PR.

## Validation requirements

1. `npm run check`, `npm test`.
2. Deploy to Cloudflare; verify `GET /health`.
3. Test authenticated WS handshake; reject unauthenticated clients.
4. Verify Gemini audio transcription and text response on recorded speech with real API key.
5. **Before claiming complete voice assistant**, add and verify Opus TTS frames with ESP32 hardware.
