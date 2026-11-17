import fs from "node:fs";
import path from "node:path";
const root="node_modules/libopus-wasm/dist";
function walk(dir){
 for(const e of fs.readdirSync(dir)){
   const p=path.join(dir,e);
   if(fs.statSync(p).isDirectory()) walk(p);
   else {
     const len=fs.statSync(p).size;
     console.log("codec-file",p,len);
     if(/\.(m?js)$/.test(p)){
       const t=fs.readFileSync(p,"utf8");
       for(const pat of ["instantiate", "wasmBinary", "base64", "AGFzbQ", "atob", "data:application", "wasmBinaryFile"]){
          const i=t.indexOf(pat);
          if(i>=0){
            const snippet=t.slice(Math.max(0,i-60),i+160).replace(/[A-Za-z0-9+/]{60,}/g,"<base64>");
            console.log("codec-ref",pat,i,snippet);
          }
       }
       console.log("codec-head",t.slice(0,180).replace(/[A-Za-z0-9+/]{60,}/g,"<base64>"));
     }
   }
 }
}
walk(root);
