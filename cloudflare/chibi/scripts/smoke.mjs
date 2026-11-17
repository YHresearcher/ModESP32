// End-to-end cloud-only smoke test. Credentials must come from GitHub Actions secrets.
// Generates a short Vietnamese utterance via Gemini TTS, converts it to the ESP32
// input format (16 kHz mono, 60 ms Opus) and verifies the worker returns actual
// Opus TTS binary frames. Does not test the physical mic/speaker.
import WebSocket from "ws";
import { createEncoder } from "libopus-wasm";
import { parsePcmAudio, fromBase64 } from "../src/tts.js";

const endpoint = process.env.CHIBI_WS_URL;
const apiKey = process.env.GEMINI_API_KEY;
const token = process.env.CHIBI_DEVICE_TOKEN;
if (!endpoint || !apiKey || !token) throw new Error("Missing smoke test env vars");
if (!/^wss:\/\//.test(endpoint)) throw new Error("Expected secure WSS endpoint");

function assert(cond, reason) { if (!cond) throw new Error(reason); }
async function speechInput() {
  const response = await fetch("https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-preview-tts:generateContent", {
    method: "POST",
    headers: {"x-goog-api-key": apiKey, "Content-Type":"application/json"},
    body: JSON.stringify({
      contents: [{parts:[{text:"Nói rõ bằng tiếng Việt: Xin chào Chibi, hôm nay bạn thế nào?"}]}],
      generationConfig: {
        responseModalities:["AUDIO"],
        speechConfig:{voiceConfig:{prebuiltVoiceConfig:{voiceName:"Kore"}}}
      }
    })
  });
  if (!response.ok) throw new Error("Gemini TTS validation: HTTP " + response.status);
  const output = await response.json();
  const block = (output.candidates?.[0]?.content?.parts || []).find(p=>p.inlineData?.data)?.inlineData;
  assert(block, "Gemini TTS validation: no audio bytes");
  const pcm24 = parsePcmAudio(fromBase64(block.data), block.mimeType || "audio/pcm");
  const count = Math.floor(pcm24.length * 2 / 3);
  const pcm16 = new Int16Array(count);
  for (let i=0;i<count;i++) {
    const offset = i * 1.5;
    const a = Math.floor(offset), b = Math.min(a + 1, pcm24.length - 1);
    pcm16[i] = Math.round(pcm24[a] + (pcm24[b]-pcm24[a]) * (offset-a));
  }
  const enc = await createEncoder({sampleRate:16000,channels:1,frameSize:960,bitrate:24000});
  const packets=[];
  try {
    for(let i=0;i<pcm16.length;i+=960) {
      const block=new Int16Array(960);
      block.set(pcm16.subarray(i,i+960));
      packets.push(enc.encode(block));
    }
  } finally {enc.free()}
  return packets;
}
async function main() {
  const healthURL = endpoint.replace(/^wss:/, "https:").replace(/\/ws$/, "/health");
  const health = await fetch(healthURL);
  assert(health.ok, "Worker health endpoint HTTP " + health.status);
  const status = await health.json();
  assert(status.voice_output === true, "Worker is not on the voice-enabled version");
  console.log("Health: OK, voice-enabled Worker detected");
  const inputPackets = await speechInput();
  console.log("Gemini TTS real API: OK (" + inputPackets.length + " input Opus frames)");
  await new Promise((resolve,reject)=>{
    const ws = new WebSocket(endpoint, {headers: {Authorization:"Bearer "+token}});
    let gotHello=false, gotStt=false, gotTts=false, gotAudio=0, gotStop=false;
    const timer=setTimeout(()=>{ws.terminate();reject(new Error("Timeout; hello="+gotHello+", STT="+gotStt+", TTS="+gotTts+", binary="+gotAudio+", stop="+gotStop))},90000);
    const done=(e)=>{clearTimeout(timer);try{ws.close()}catch(_){};e?reject(e):resolve()};
    ws.on("error",done);
    ws.on("open",()=>ws.send(JSON.stringify({type:"hello",transport:"websocket",version:1,audio_params:{format:"opus",sample_rate:16000,channels:1,frame_duration:60}})));
    ws.on("message",async (raw,isBinary)=>{
      if(isBinary){gotAudio++;return}
      const obj=JSON.parse(raw.toString());
      if(obj.type==="hello"&&!gotHello){
        gotHello=true;
        ws.send(JSON.stringify({type:"listen",state:"start",mode:"manual",session_id:obj.session_id}));
        for(const packet of inputPackets)ws.send(packet);
        ws.send(JSON.stringify({type:"listen",state:"stop",session_id:obj.session_id}));
      }
      if(obj.type==="stt")gotStt=!!obj.text;
      if(obj.type==="tts"&&obj.state==="start")gotTts=true;
      if(obj.type==="tts"&&obj.state==="stop"){
        gotStop=true;
        if(gotHello&&gotStt&&gotTts&&gotAudio>0){
          console.log("Complete Cloud voice roundtrip: success; binary Opus packets="+gotAudio);
          done();
        } else done(new Error("Incomplete roundtrip; hello="+gotHello+", STT="+gotStt+", TTS="+gotTts+", binary="+gotAudio));
      }
      if(obj.type==="alert") {
        console.log("Cloud alert: "+(obj.message||"unknown").slice(0,150));
      }
    });
    ws.on("close",(code)=>{if(!gotStop)done(new Error("WebSocket closed before TTS complete: "+code))});
  });
}
main().catch(e=>{console.error("Cloud smoke FAILED: "+e.message);process.exitCode=1});
