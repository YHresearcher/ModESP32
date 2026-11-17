// Extract libopus-wasm's embedded binary at build time, not at runtime.
// Wrangler imports the resulting .wasm file as a precompiled WebAssembly.Module.
import { readFileSync, readdirSync, statSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
const roots=["node_modules/libopus-wasm/dist"];
function* walk(dir){
  for(const item of readdirSync(dir)){
    const p=join(dir,item);
    if(statSync(p).isDirectory())yield* walk(p);
    else if(/\.(m?js)$/.test(p))yield p;
  }
}
let extracted;
for(const path of roots.flatMap(root=>[...walk(root)])){
  const src=readFileSync(path,"utf8");
  const matches=[...src.matchAll(/AGFzbQ[A-Za-z0-9+/=]{200,}/g)].sort((a,b)=>b[0].length-a[0].length);
  for(const match of matches){
    const binary=Buffer.from(match[0],"base64");
    if(binary.subarray(0,4).toString("hex")==="0061736d"){
      extracted=binary;
      console.log("Found libopus WASM in "+path);
      break;
    }
  }
  if(extracted)break;
}
if(!extracted)throw new Error("No libopus WASM base64 found in npm dist; upstream loader format changed");
mkdirSync("src/codec",{recursive:true});
writeFileSync("src/codec/opus.wasm",extracted);
console.log("Prepared static WASM module: "+extracted.byteLength+" bytes");
