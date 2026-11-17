# Chibi Cloud — Google Cloud Run (ESP32-S3)

## Why Cloud Run?

Cloud Run supports standard WebSockets and public `wss://` domains. Each active WebSocket counts as an active request and is billed while connected; per-connection timeout is **up to 60 minutes**, so firmware needs retry/reconnect after expiry. This is not a guaranteed free always-on device.

This configuration runs Whisper small CPU ASR model weights pre-baked into container, Gemini via Google AI Studio API key, and Microsoft Edge TTS for Vietnamese speech. It is a prototype and has not yet passed CI/hardware validation.

## Deploy (Cloud Shell with gcloud)

Prerequisites: a Google Cloud project with billing enabled, Cloud Run / Cloud Build / Artifact Registry / Secret Manager APIs enabled, and Gemini API key created separately via Google AI Studio.

1. Open Cloud Shell, clone the repository containing this branch and enter the repository:
```bash
git clone -b fix/custom-agent-routing-v1 https://github.com/YHresearcher/ModESP32.git
cd ModESP32
gcloud config set project YOUR_GOOGLE_CLOUD_PROJECT_ID
gcloud services enable run.googleapis.com cloudbuild.googleapis.com artifactregistry.googleapis.com secretmanager.googleapis.com
```

2. Store Gemini key and a random device token **in Secret Manager** (never in Git, firmware URL or command history). Example interactive input using stdin:
```bash
read -rs -p 'Gemini API key: ' GEMINI_VALUE; echo
printf %s "$GEMINI_VALUE" | gcloud secrets create chibi-gemini-key --replication-policy=automatic --data-file=-
unset GEMINI_VALUE
python3 -c 'import secrets; print(secrets.token_urlsafe(32))'
```
Copy the generated device token somewhere private and configure it via `idf.py menuconfig`. To create its secret, use `read -rs -p 'Device token: ' DEVICE_VALUE`, then `printf %s "$DEVICE_VALUE" | gcloud secrets create chibi-device-token --replication-policy=automatic --data-file=-` and `unset DEVICE_VALUE`.

3. Grant the Cloud Run **runtime service account** access to both secrets (Secret Manager Secret Accessor). The exact account depends on your Google Cloud setup; use a dedicated runtime service account rather than broad default permissions.

4. Deploy the Dockerfile from `backend/`. Example:
```bash
gcloud run deploy chibi-gateway \
  --source backend \
  --region asia-southeast1 \
  --allow-unauthenticated \
  --memory 2Gi --cpu 2 \
  --timeout 3600 \
  --concurrency 10 \
  --max-instances 1 \
  --min-instances 0 \
  --set-env-vars WHISPER_MODEL=base,GEMINI_MODEL=gemini-2.5-flash \
  --set-secrets GEMINI_API_KEY=chibi-gemini-key:latest,CHIBI_DEVICE_TOKEN=chibi-device-token:latest
```

**Important:** `--allow-unauthenticated` means requests can reach the Cloud Run URL without Google IAM auth. The gateway checks a separate **Bearer** token during WebSocket handshake; use a long random token, a distinct secret per environment, and never expose it in URLs. This is a prototype authentication design, not production-grade device provisioning.

5. Find Cloud Run HTTPS service URL in its Console (for example `https://chibi-gateway-<hash>-as.a.run.app`). Replace its scheme with `wss://` while preserving the host (no extra path):
```text
wss://chibi-gateway-<hash>-as.a.run.app
```
This is only an example, not a deployed endpoint.

6. ESP32 build, in `idf.py menuconfig` → **ModESP32 Assistant**:
- Enable `MODESP32_CUSTOM_AGENT`.
- Set `MODESP32_AGENT_WS_URL` to your actual Cloud Run WSS URL.
- Set `MODESP32_AGENT_TOKEN` to the secret device token.
- Select the board/hardware configuration matching the wired ESP32-S3.
- Build/flash after reviewing diffs and running CI; don't publish `sdkconfig` with device token.

7. Acceptance: inspect Cloud Run logs and ESP32 serial logs; test hello/v1 handshake, mic-to-ASR Vietnamese text, Gemini response, Opus TTS output, network reconnect, timeout recovery and TFT. Only then consider release.

## Limitations and cost

- The gateway handles WebSocket v1 only. Device v2/v3 and MQTT/UDP aren't implemented.
- The prototype uses RMS voice-end detection, not a robust VAD. It has no complete local 'Hey Chibi' wake model.
- Edge TTS is a third-party online service without guaranteed availability; use a supported commercial TTS API for production.
- Cloud Run billing is based on active connection time, memory, CPU and traffic. Each open WebSocket keeps compute billable. Set budget alerts.
- Cloud Run may close a connection after its configured request timeout (at most 3600 s). ESP32 must reconnect.
- Avoid storing utterance contents in durable logs; this prototype currently logs ASR and agent reply for debugging and should disable this in production.
- Cloud Run scale-to-zero can mean model cold start; consider model API-based ASR if startup latency is unacceptable.
