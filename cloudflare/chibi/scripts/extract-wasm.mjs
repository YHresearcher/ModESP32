// Convert Emscripten's inline JS binary literal to a static .wasm module.
// Executed at BUILD TIME on GitHub Actions; no compilation from bytes on Worker.
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import vm from "node:vm";

const file = "node_modules/libopus-wasm/dist/generated/libopus.generated.mjs";
const src = readFileSync(file, "utf8");
const marker = "return binaryDecode(";
const at = src.indexOf(marker);
if (at === -1) throw new Error("Emscripten binaryDecode marker not found");
let start = at + marker.length;
while (/\s/.test(src[start])) start++;
const quote = src[start];
if (quote !== "'" && quote !== '"') throw new Error("Unexpected embedded binary literal");
let end = -1;
for (let i = start + 1; i < src.length; i++) {
  if (src[i] === "\\") { i++; continue; }
  if (src[i] === quote) { end = i; break; }
}
if (end === -1) throw new Error("Unterminated embedded libopus literal");
const literal = src.slice(start, end + 1);
// Only a string literal from a pinned npm dependency is interpreted at build time.
const decoded = vm.runInNewContext(literal, Object.create(null), {timeout:1000});
if (typeof decoded !== "string") throw new Error("Bad generated WASM literal");
const binary = Buffer.alloc(decoded.length);
for (let i=0;i<decoded.length;i++) {
  const code=decoded.charCodeAt(i);
  if(code>255)throw new Error("Non-byte character in generated WASM literal");
  binary[i]=code;
}
if(binary.subarray(0,4).toString("hex")!=="0061736d"){
  throw new Error("Extracted bytes are not WASM");
}
mkdirSync("src/codec",{recursive:true});
writeFileSync("src/codec/opus.wasm",binary);
console.log("Extracted Cloudflare-compatible libopus WASM: "+binary.length+" bytes");
