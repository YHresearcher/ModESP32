import fs from "node:fs";
import path from "node:path";
const root="node_modules/libopus-wasm";
for(const file of ["dist/index.js","dist/wasm.js","dist/libopus.wasm","dist/libopus.generated.mjs"]){
 const p=path.join(root,file);
 if(fs.existsSync(p))console.log("file",file,fs.statSync(p).size);
}
const text=fs.readFileSync(path.join(root,"dist/index.js"),"utf8");
console.log("index chars",text.length);
for(const pattern of ["WebAssembly.instantiate","wasmBinary","base64","data:application/wasm","AGFzbQ","atob","Uint8Array.from"]){
 const at=text.indexOf(pattern);
 console.log(pattern,at,at>=0?text.slice(Math.max(0,at-110),at+200).replace(/[A-Za-z0-9+/]{70,}/g,"<base64>"):"");
}
console.log("long text literals", [...text.matchAll(/["'`]([A-Za-z0-9+/]{100,})["'`]/g)].slice(0,3).map(x=>x[1].length));
