"""Chibi private voice gateway, XiaoZhi-compatible WebSocket protocol v1.

Development server with optional device bearer authentication. Requires ffmpeg and Python 3.11+.
For a public cloud deployment set CHIBI_DEVICE_TOKEN and enable TLS (wss://).
"""
import asyncio
import hmac
import io
import json
import logging
import os
import subprocess
import uuid
import wave
from dataclasses import dataclass, field

import edge_tts
import httpx
import numpy as np
import opuslib
from faster_whisper import WhisperModel
from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

HOST = os.getenv("CHIBI_HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", os.getenv("CHIBI_PORT", "8765")))
DEVICE_TOKEN = os.getenv("CHIBI_DEVICE_TOKEN", "")
MODEL = os.getenv("GEMINI_MODEL", "gemini-2.5-flash")
ASR_MODEL = os.getenv("WHISPER_MODEL", "base")
VOICE = os.getenv("CHIBI_VOICE", "vi-VN-HoaiMyNeural")
MAX_AUDIO_SECONDS = 20
SYSTEM = (
    "Bạn là Chibi, trợ lý AI thân thiện trên thiết bị ESP32. "
    "Luôn trả lời bằng tiếng Việt có dấu, tự nhiên, rõ ràng và ngắn gọn. "
    "Không tự nhận đã điều khiển thiết bị khi chưa nhận được xác nhận."
)
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("chibi")
asr = None


@dataclass
class Session:
    sid: str = field(default_factory=lambda: str(uuid.uuid4()))
    decoder: object = field(default_factory=lambda: opuslib.Decoder(16000, 1))
    buffer: bytearray = field(default_factory=bytearray)
    listening: bool = False
    busy: bool = False
    voice_ms: int = 0
    silence_ms: int = 0
    history: list = field(default_factory=list)


async def event(ws, session, kind, **fields):
    await ws.send(json.dumps({"type": kind, "session_id": session.sid, **fields}, ensure_ascii=False))


def wav_bytes(pcm):
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(pcm)
    return out.getvalue()


def recognize(pcm):
    if not pcm:
        return ""
    segments, _ = asr.transcribe(io.BytesIO(wav_bytes(pcm)), language="vi",
                                 vad_filter=True, beam_size=3)
    return " ".join(s.text.strip() for s in segments).strip()


async def answer(question, history):
    api_key = os.getenv("GEMINI_API_KEY")
    if not api_key:
        raise RuntimeError("GEMINI_API_KEY is missing")
    contents = history[-8:] + [{"role": "user", "parts": [{"text": question}]}]
    endpoint = "https://generativelanguage.googleapis.com/v1beta/models/" + MODEL + ":generateContent"
    payload = {"systemInstruction": {"parts": [{"text": SYSTEM}]},
               "contents": contents, "generationConfig": {"maxOutputTokens": 400}}
    async with httpx.AsyncClient(timeout=45) as client:
        response = await client.post(endpoint, headers={"x-goog-api-key": api_key},
                                     json=payload)
        response.raise_for_status()
    candidates = response.json().get("candidates") or []
    parts = (candidates[0].get("content") or {}).get("parts", []) if candidates else []
    text = "".join(p.get("text", "") for p in parts).strip()
    if not text:
        raise RuntimeError("Gemini returned no text")
    history.extend([{"role": "user", "parts": [{"text": question}]},
                    {"role": "model", "parts": [{"text": text}]}])
    return text


async def generate_tts_pcm(text):
    # Edge TTS currently needs access to Microsoft's speech service.
    data = bytearray()
    async for chunk in edge_tts.Communicate(text, VOICE).stream():
        if chunk["type"] == "audio":
            data.extend(chunk["data"])
    if not data:
        raise RuntimeError("TTS returned no audio")
    proc = await asyncio.create_subprocess_exec(
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", "pipe:0",
        "-f", "s16le", "-ac", "1", "-ar", "24000", "pipe:1",
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE)
    pcm, err = await proc.communicate(bytes(data))
    if proc.returncode != 0:
        raise RuntimeError("ffmpeg: " + err.decode(errors="replace")[:200])
    return pcm


async def process_turn(ws, s):
    if s.busy or not s.buffer:
        return
    s.busy = True
    s.listening = False
    pcm = bytes(s.buffer)
    s.buffer.clear()
    s.voice_ms = s.silence_ms = 0
    try:
        question = await asyncio.to_thread(recognize, pcm)
        if not question:
            await event(ws, s, "tts", state="stop")
            return
        log.info("ASR: %s", question)
        await event(ws, s, "stt", text=question)
        reply = await answer(question, s.history)
        log.info("Chibi: %s", reply)
        pcm_out = await generate_tts_pcm(reply)
        await event(ws, s, "tts", state="start")
        await event(ws, s, "tts", state="sentence_start", text=reply)
        encoder = opuslib.Encoder(24000, 1, opuslib.APPLICATION_VOIP)
        frame_samples = 1440  # 60ms, 24kHz, 16-bit mono
        frame_bytes = frame_samples * 2
        for offset in range(0, len(pcm_out), frame_bytes):
            chunk = pcm_out[offset:offset + frame_bytes]
            if len(chunk) < frame_bytes:
                chunk += bytes(frame_bytes - len(chunk))
            await ws.send(encoder.encode(chunk, frame_samples))
            await asyncio.sleep(0.06)
        await event(ws, s, "tts", state="stop")
    except ConnectionClosed:
        raise
    except Exception as exc:
        log.exception("Agent turn failed")
        await event(ws, s, "alert", status="Chibi", message=str(exc)[:120],
                    emotion="sad")
        await event(ws, s, "tts", state="stop")
    finally:
        s.busy = False


async def handler(ws):
    expected = "Bearer " + DEVICE_TOKEN
    provided = ws.request.headers.get("Authorization", "")
    if DEVICE_TOKEN and not hmac.compare_digest(provided, expected):
        await ws.close(code=1008, reason="Unauthorized device")
        return
    s = Session()
    peer = ws.remote_address
    log.info("Connected: %s", peer)
    task = None
    try:
        async for msg in ws:
            if isinstance(msg, str):
                try:
                    data = json.loads(msg)
                except json.JSONDecodeError:
                    continue
                kind = data.get("type")
                if kind == "hello":
                    if data.get("transport") != "websocket" or data.get("version") != 1:
                        await ws.close(code=1002, reason="Only websocket protocol v1 is supported")
                        return
                    await event(ws, s, "hello", transport="websocket",
                                audio_params={"format": "opus", "sample_rate": 24000,
                                              "channels": 1, "frame_duration": 60})
                elif kind == "listen":
                    state = data.get("state")
                    if state == "start":
                        if not s.busy:
                            s.listening = True
                            s.buffer.clear()
                            s.voice_ms = s.silence_ms = 0
                    elif state == "stop" and not s.busy and s.buffer:
                        task = asyncio.create_task(process_turn(ws, s))
                    # state=detect is a wake-word announcement, not an utterance end.
                elif kind == "abort":
                    s.listening = False
                    s.buffer.clear()
                    if task and not task.done():
                        task.cancel()
            elif s.listening and not s.busy:
                try:
                    pcm = s.decoder.decode(msg, 960)  # 60ms at 16kHz
                except opuslib.OpusError:
                    continue
                s.buffer.extend(pcm)
                samples = np.frombuffer(pcm, dtype="<i2").astype(np.float32)
                rms = float(np.sqrt(np.mean(samples * samples)))
                if rms > 500:
                    s.voice_ms += 60
                    s.silence_ms = 0
                elif s.voice_ms >= 480:
                    s.silence_ms += 60
                if (s.voice_ms >= 480 and s.silence_ms >= 1200 or
                    len(s.buffer) >= MAX_AUDIO_SECONDS * 16000 * 2):
                    task = asyncio.create_task(process_turn(ws, s))
    except ConnectionClosed:
        pass
    finally:
        if task and not task.done():
            task.cancel()
        log.info("Disconnected: %s", peer)


async def main():
    global asr
    log.info("Loading Whisper model %s", ASR_MODEL)
    asr = WhisperModel(ASR_MODEL, device="cpu", compute_type="int8")
    if HOST == "0.0.0.0" and not DEVICE_TOKEN:
        raise RuntimeError("CHIBI_DEVICE_TOKEN required on a public-facing listener")
    log.info("Starting Chibi on ws://%s:%d", HOST, PORT)
    async with serve(handler, HOST, PORT, max_size=2**20, ping_interval=20):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
