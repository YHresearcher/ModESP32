// Workers-compatible Opus loader: Cloudflare only accepts statically imported
// WebAssembly.Module objects, not bytes compiled dynamically at request time.
import opusModule from "./codec/opus.wasm";
import handler from "./index.js";

const instantiate = WebAssembly.instantiate.bind(WebAssembly);
WebAssembly.instantiate = (source, imports) => {
  if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
    return Promise.resolve(instantiate(opusModule, imports))
      .then(instance => ({module: opusModule, instance}));
  }
  return instantiate(source, imports);
};
export default handler;
