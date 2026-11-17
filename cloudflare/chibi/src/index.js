// Chibi Cloudflare Free: authenticated XiaoZhi-protocol v1 Opus voice gateway.
import { synthesizeGemini, encodeOpus24k } from "./tts.js";
const MAX_AUDIO_BYTES = 256_000;
const MAX_FRAMES = 180; // up to 10.8s at 60ms per frame
const SYSTEM = "Bạn là Chibi, trợ lý giọng nói trên ESP32. Trả lời bằng tiếng Việt có dấu, ngắn gọn, thân thiện, chính xác. Không tuyên bố đã làm gì nếu chưa thực hiện.";
const utf8 = (text) => new TextEncoder().encode(text);

function checksum(buffer) {
  let crc = 0;
  for (const b of buffer) {
    crc ^= b << 24;
    for (let j = 0; j < 8; j++) crc = (crc & 0x80000000) ? ((crc << 1) ^ 0x04c11db7) >>> 0 : (crc << 1) >>> 0;
  }
  return crc >>> 0;
}
function page(packet, sequence, granule, eos = false) {
  const segCount = Math.floor(packet.length / 255) + 1;
  const lacing = new Uint8Array(segCount);
  let remaining = packet.length;
  for (let i = 0; i < segCount; i++) {
    lacing[i] = Math.min(remaining, 255);
    remaining -= lacing[i];
  }
  if (segCount > 255) throw new Error("Opus frame too large");
  const out = new Uint8Array(27 + segCount + packet.length);
  out.set(utf8("OggS"), 0); out[4] = 0; out[5] = (sequence === 0 ? 2 : 0) | (eos ? 4 : 0);
  const view = new DataView(out.buffer);
  view.setBigUint64(6, BigInt(granule), true);
  view.setUint32(14, 0x43484249, true); // fixed stream serial for this independently submitted utterance
  view.setUint32(18, sequence, true);
  out[26] = segCount;
  out.set(lacing, 27);
  out.set(packet, 27 + segCount);
  view.setUint32(22, checksum(out), true);
  return out;
}
function opusHead() {
  const out = new Uint8Array(19);
  out.set(utf8("OpusHead"), 0); out[8] = 1; out[9] = 1;
  const d = new DataView(out.buffer);
  d.setUint16(10, 0, true); d.setUint32(12, 16000, true);
  return out;
}
function opusTags() {
  const name = utf8("Chibi");
  const out = new Uint8Array(8 + 4 + name.length + 4);
  out.set(utf8("OpusTags")); const d = new DataView(out.buffer);
  d.setUint32(8, name.length, true); out.set(name, 12);
  d.setUint32(12 + name.length, 0, true);
  return out;
}
function muxOgg(frames, frameMs) {
  const packets = [page(opusHead(), 0, 0), page(opusTags(), 1, 0)];
  for (let i = 0; i < frames.length; i++) {
    packets.push(page(frames[i], i + 2, (i + 1) * frameMs * 48, i === frames.length - 1));
  }
  const total = packets.reduce((n, p) => n + p.length, 0);
  const output = new Uint8Array(total);
  let offset = 0;
  for (const p of packets) { output.set(p, offset); offset += p.length; }
  return output;
}
function toB64(bytes) {
  const parts = [];
  for (let i = 0; i < bytes.length; i += 8192) {
    parts.push(String.fromCharCode(...bytes.subarray(i, i + 8192)));
  }
  return btoa(parts.join(""));
}
async function gemini(env, parts, mode = "chat") {
  if (!env.GEMINI_API_KEY) throw new Error("GEMINI_API_KEY is missing");
  const primary = mode === "asr"
    ? (env.GEMINI_ASR_MODEL || "gemini-3.5-flash-lite")
    : (env.GEMINI_MODEL || "gemini-3.5-flash-lite");
  const fallback = mode === "chat"
    ? (env.GEMINI_CHAT_FALLBACK || "gemini-3.8-flash")
    : (env.GEMINI_ASR_FALLBACK || "gemini-3.8-flash");
  const models = [primary, ...(fallback && fallback !== primary ? [fallback] : [])];
  let lastCode = 503;
  for (const model of models) {
    for (let attempt = 0; attempt < 2; attempt++) {
      const response = await fetch(
        "https://generativelanguage.googleapis.com/v1beta/models/" +
        encodeURIComponent(model) + ":generateContent", {
          method: "POST",
          headers: {"Content-Type": "application/json", "x-goog-api-key": env.GEMINI_API_KEY},
          body: JSON.stringify({
            systemInstruction: {parts: [{text: SYSTEM}]},
            contents: [{role: "user", parts}],
            generationConfig: {maxOutputTokens: 500}
          })
        }
      );
      if (response.ok) {
        const result = await response.json();
        const text = (result.candidates?.[0]?.content?.parts || [])
          .map(x => x.text || "").join("").trim();
        if (!text) throw new Error("Gemini returned empty text");
        return text;
      }
      lastCode = response.status;
      if (lastCode !== 429 && lastCode !== 500 && lastCode !== 503) {
        throw new Error("Gemini HTTP " + lastCode);
      }
      // Low CPU use during upstream overload; retry only transient failures.
      if (attempt < 1) await new Promise(r => setTimeout(r, 750));
    }
  }
  throw new Error("Gemini HTTP " + lastCode);
}
function reply(ws, sid, type, body = {}) {
  try { ws.send(JSON.stringify({type, session_id: sid, ...body})); }
  catch (_) { /* socket closed */ }
}
function agentSession(ws, env) {
  const sid = crypto.randomUUID();
  let hello = false, listening = false, busy = false;
  let frameMs = 60, frames = [], size = 0;
  const reset = () => { frames = []; size = 0; listening = false; };
  async function finish() {
    if (busy || !frames.length) { reset(); return; }
    busy = true;
    const utterance = frames; frames = []; size = 0; listening = false;
    let stage = "audio";
    try {
      // Gemini can accept an actual Ogg/Opus container, not raw WebSocket Opus frames.
      const ogg = muxOgg(utterance, frameMs);
      stage = "speech-to-text";
      const transcript = await gemini(env, [
        {text: "Chép lại nguyên văn lời nói tiếng Việt trong âm thanh sau. Chỉ trả về nội dung được nói, có dấu."},
        {inlineData: {mimeType: "audio/ogg", data: toB64(ogg)}}
      ], "asr");
      reply(ws, sid, "stt", {text: transcript});
      stage = "chat";
      const answer = await gemini(env, [{text: "Người dùng nói: " + transcript + "\nHãy trả lời."}]);
      // Only announce speech once Gemini has successfully generated real PCM.
      // Failed TTS must not emit a synthetic success / fake Opus payload.
      stage = "text-to-speech";
      const pcm = await synthesizeGemini(env, answer);
      reply(ws, sid, "tts", {state: "start"});
      reply(ws, sid, "tts", {state: "sentence_start", text: answer});
      try {
        stage = "opus-encoder";
        await encodeOpus24k(pcm, async (packet) => {
          ws.send(packet);
          // Bound buffering and let network writes progress.
          if (ws.bufferedAmount > 128000) await new Promise(r => setTimeout(r, 25));
        });
      } finally {
        reply(ws, sid, "tts", {state: "stop"});
      }
    } catch (error) {
      console.error("Chibi turn failed", String(error));
      const reason = error instanceof Error ? error.message : "unknown";
      // Restrict diagnostics to model HTTP code / codec stage. Never show secrets or prompts.
      const safe = /^(Gemini (TTS )?HTTP \d+|Gemini TTS returned no audio|Only PCM16|Expected 24kHz|Unexpected audio MIME|Invalid WAV|Truncated WAV|Incomplete WAV|No PCM)/.test(reason) ? reason : "internal error";
      reply(ws, sid, "alert", {status: "Chibi", message: stage + ": " + safe, emotion: "sad"});
    } finally { busy = false; }
  }
  ws.addEventListener("message", (event) => {
    if (typeof event.data === "string") {
      let data;
      try { data = JSON.parse(event.data); } catch (_) { return; }
      if (data.type === "hello") {
        if (data.transport !== "websocket" || data.version !== 1) {
          ws.close(1002, "Only websocket version 1 is supported"); return;
        }
        const ms = Number(data.audio_params?.frame_duration ?? 60);
        if (![20, 40, 60].includes(ms)) {
          ws.close(1002, "Unsupported frame duration"); return;
        }
        frameMs = ms; hello = true;
        reply(ws, sid, "hello", {
          transport: "websocket",
          audio_params: {format: "opus", sample_rate: 24000, channels: 1, frame_duration: 60}
        });
      } else if (hello && data.type === "listen") {
        if (data.state === "start" && !busy) { reset(); listening = true; }
        if (data.state === "stop") void finish();
      } else if (data.type === "abort") { reset(); }
      return;
    }
    if (!hello || !listening || busy) return;
    void (async () => {
      const frame = event.data instanceof ArrayBuffer
        ? new Uint8Array(event.data)
        : new Uint8Array(await event.data.arrayBuffer());
      if (!listening || busy || !frame.length) return;
      if (frame.length > 4096 || size + frame.length > MAX_AUDIO_BYTES) {
        reset(); reply(ws, sid, "alert", {status:"Chibi", message:"Bản ghi quá dài."}); return;
      }
      frames.push(frame); size += frame.length;
      // A conservative frame cap prevents indefinite recording when the device
      // sends no listen.stop. Proper VAD is still required for conversational use.
      if (frames.length >= MAX_FRAMES) void finish();
    })();
  });
}
export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/health") {
      return Response.json({service:"chibi-cloudflare", status:"ok", stage:"voice-enabled", voice_output:true});
    }
    if (url.pathname !== "/ws") return new Response("Not found", {status:404});
    if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") {
      return new Response("WebSocket required", {status:426});
    }
    if (!env.CHIBI_DEVICE_TOKEN) return new Response("Server token is not configured", {status:503});
    const auth = request.headers.get("Authorization") || "";
    if (auth !== "Bearer " + env.CHIBI_DEVICE_TOKEN) return new Response("Unauthorized", {status:401});
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    server.binaryType = "arraybuffer";
    server.accept();
    agentSession(server, env);
    return new Response(null, {status:101, webSocket:client});
  }
};

export { muxOgg, checksum, opusHead, opusTags };
