import test from "node:test";
import assert from "node:assert/strict";
import { fromBase64, parsePcmAudio, encodeOpus24k } from "../src/tts.js";

function makeWav(samples) {
  const out = new Uint8Array(44 + samples.length * 2);
  const v = new DataView(out.buffer);
  const ascii = (p, s) => s.split("").forEach((c, i) => out[p+i] = c.charCodeAt(0));
  ascii(0, "RIFF"); v.setUint32(4, out.length - 8, true);
  ascii(8, "WAVE"); ascii(12, "fmt ");
  v.setUint32(16, 16, true); v.setUint16(20, 1, true);
  v.setUint16(22, 1, true); v.setUint32(24, 24000, true);
  v.setUint32(28, 48000, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
  ascii(36, "data"); v.setUint32(40, samples.length * 2, true);
  samples.forEach((n,i) => v.setInt16(44+i*2,n,true));
  return out;
}
test("PCM parser accepts valid WAV 24kHz mono", () => {
  assert.deepEqual(Array.from(parsePcmAudio(makeWav([0,200,-200,32767]))),[0,200,-200,32767]);
});
test("PCM parser accepts raw signed PCM and rejects MP3", () => {
  assert.deepEqual(Array.from(parsePcmAudio(new Uint8Array([0,0,255,127]),"audio/pcm")),[0,32767]);
  assert.throws(()=>parsePcmAudio(new Uint8Array([1,2,3,4]),"audio/mpeg"),/Unexpected audio/);
});
test("base64 decoder works",()=>{
  assert.deepEqual(Array.from(fromBase64("AAECAw==")),[0,1,2,3]);
});
test("24kHz PCM is encoded into real decodable raw Opus frames", async()=>{
  const pcm = new Int16Array(1440 * 2);
  for(let i=0;i<pcm.length;i++) pcm[i]=Math.round(Math.sin(i/20)*3000);
  const frames=[];
  await encodeOpus24k(pcm,packet=>{frames.push(packet)});
  assert.equal(frames.length,2);
  assert.ok(frames.every(f=>f.length>4&&f.length<4096));
});
