// Build-time extraction of libopus-wasm's embedded binary into a real .wasm module.
// Cloudflare Workers forbids runtime compilation from ArrayBuffer but allows
// WebAssembly.Module imported by Wrangler at build time.
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
const src = readFileSync("node_modules/libopus-wasm/dist/index.js", "utf8");
const candidate = [...src.matchAll(/AGFzbQ[A-Za-z0-9+/=]{200,}/g)]
  .map(match => match[0]).sort((a,b)=>b.length-a.length)[0];
if (!candidate) throw new Error("No embedded libopus WASM base64 found; upstream package format changed");
const binary = Buffer.from(candidate, "base64");
if (binary.subarray(0,4).toString("hex") !== "0061736d") {
  throw new Error("Extracted bytes are not a WebAssembly binary");
}
mkdirSync("src/codec", {recursive:true});
writeFileSync("src/codec/opus.wasm", binary);
console.log("Extracted static libopus WASM: " + binary.length + " bytes");
