// Gemini speech synthesis -> PCM 24kHz mono -> raw Opus packets for ESP32.
// Browser-safe WebAssembly codec; no native Node modules or external ffmpeg.
import { createEncoder, createDecoder } from "libopus-wasm";

const SAMPLE_RATE = 24000;
const FRAME_MS = 60;
const FRAME_SAMPLES = SAMPLE_RATE * FRAME_MS / 1000;
const MAX_TTS_SAMPLES = SAMPLE_RATE * 25; // bound response duration / compute

export function fromBase64(base64) {
  if (typeof base64 !== "string" || !base64.length) throw new Error("Missing audio payload");
  const binary = atob(base64);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}
export function parsePcmAudio(bytes, mimeType = "") {
  if (!(bytes instanceof Uint8Array) || bytes.length < 2) throw new Error("Empty audio");
  let offset = 0, sampleRate = SAMPLE_RATE, channels = 1;
  const magic = String.fromCharCode(...bytes.slice(0, 4));
  if (magic === "RIFF") {
    if (bytes.length < 44 || String.fromCharCode(...bytes.slice(8, 12)) !== "WAVE") {
      throw new Error("Invalid WAV");
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let foundFormat = false, foundData = false;
    for (let pos = 12; pos + 8 <= bytes.length;) {
      const len = view.getUint32(pos + 4, true);
      const next = pos + 8 + len + (len & 1);
      if (next > bytes.length) throw new Error("Truncated WAV");
      const chunk = String.fromCharCode(...bytes.slice(pos, pos + 4));
      if (chunk === "fmt ") {
        const encoding = view.getUint16(pos + 8, true);
        channels = view.getUint16(pos + 10, true);
        sampleRate = view.getUint32(pos + 12, true);
        const bits = view.getUint16(pos + 22, true);
        if (encoding !== 1 || bits !== 16) throw new Error("Only PCM16 WAV supported");
        foundFormat = true;
      } else if (chunk === "data") {
        offset = pos + 8;
        bytes = bytes.slice(offset, offset + len);
        foundData = true;
        break;
      }
      pos = next;
    }
    if (!foundFormat || !foundData) throw new Error("Incomplete WAV");
  } else if (!/audio\/(pcm|l16)/i.test(mimeType)) {
    throw new Error("Unexpected audio MIME: " + mimeType);
  }
  if (sampleRate !== SAMPLE_RATE || channels !== 1 || bytes.length % 2 !== 0) {
    throw new Error("Expected 24kHz mono PCM16 output");
  }
  if (bytes.length / 2 > MAX_TTS_SAMPLES) throw new Error("TTS too long");
  // Explicit little-endian conversion to avoid Uint16 alignment issues.
  const pcm = new Int16Array(bytes.length / 2);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < pcm.length; i++) pcm[i] = view.getInt16(i * 2, true);
  return pcm;
}
export async function synthesizeGemini(env, text) {
  if (!env.GEMINI_API_KEY) throw new Error("GEMINI_API_KEY missing");
  const model = env.GEMINI_TTS_MODEL || "gemini-2.5-flash-preview-tts";
  const voice = env.GEMINI_TTS_VOICE || "Kore";
  const request = {
    contents: [{parts: [{text: "Nói bằng tiếng Việt tự nhiên, ấm áp và rõ ràng: " + text}]}],
    generationConfig: {
      responseModalities: ["AUDIO"],
      speechConfig: {voiceConfig: {prebuiltVoiceConfig: {voiceName: voice}}}
    }
  };
  const response = await fetch(
    "https://generativelanguage.googleapis.com/v1beta/models/" +
      encodeURIComponent(model) + ":generateContent",
    { method: "POST", headers: {
      "Content-Type": "application/json", "x-goog-api-key": env.GEMINI_API_KEY
    }, body: JSON.stringify(request) }
  );
  if (!response.ok) {
    // No API key, request, or sensitive text in diagnostics.
    throw new Error("Gemini TTS HTTP " + response.status + " (check TTS model/quota)");
  }
  const payload = await response.json();
  const block = (payload.candidates?.[0]?.content?.parts || []).find(p => p.inlineData?.data);
  if (!block) throw new Error("Gemini TTS returned no audio");
  return parsePcmAudio(fromBase64(block.inlineData.data), block.inlineData.mimeType || "audio/pcm");
}
export async function encodeOpus24k(pcm, send) {
  if (!(pcm instanceof Int16Array) || !pcm.length) throw new Error("No PCM to encode");
  const encoder = await createEncoder({
    sampleRate: SAMPLE_RATE, channels: 1, frameSize: FRAME_SAMPLES,
    bitrate: 24000
  });
  try {
    for (let i = 0; i < pcm.length; i += FRAME_SAMPLES) {
      const frame = new Int16Array(FRAME_SAMPLES);
      frame.set(pcm.subarray(i, i + FRAME_SAMPLES));
      const packet = encoder.encode(frame);
      await send(packet);
    }
  } finally {
    encoder.free();
  }
}
export async function measureOpusRms(packet, frameSamples = 960) {
  const decoder = await createDecoder({
    sampleRate: 16000, channels: 1
  });
  try {
    const pcm = decoder.decode(packet, {frameSize: frameSamples});
    let total = 0;
    for (const value of pcm) total += value * value;
    return Math.sqrt(total / Math.max(1, pcm.length));
  } finally {
    decoder.free();
  }
}
