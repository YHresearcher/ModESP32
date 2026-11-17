import test from "node:test";
import assert from "node:assert/strict";
import {muxOgg, opusHead, opusTags, checksum} from "../src/index.js";

test("Ogg Opus header and tags are valid", () => {
  assert.equal(new TextDecoder().decode(opusHead().slice(0,8)), "OpusHead");
  assert.equal(opusHead().length, 19);
  assert.equal(new TextDecoder().decode(opusTags().slice(0,8)), "OpusTags");
});
test("muxOgg assembles header, tags and frame pages", () => {
  const audio = muxOgg([new Uint8Array([1,2,3]),new Uint8Array([4,5])], 60);
  const str = new TextDecoder("latin1").decode(audio);
  assert.equal(str.slice(0,4), "OggS");
  assert.equal(str.includes("OpusHead"), true);
  assert.equal(str.includes("OpusTags"), true);
  assert.equal((str.match(/OggS/g)||[]).length, 4);
});
test("checksum is unsigned integer", () => {
  const x = checksum(new Uint8Array([0,1,2,3]));
  assert.ok(Number.isInteger(x) && x >= 0 && x <= 0xffffffff);
});
