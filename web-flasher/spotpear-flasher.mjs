// Isolated Spotpear Flipper Lab flasher. Never reuse older ModESP32 board profiles.
const byId = id => document.getElementById(id);
let imageUrl = null, manifestUrl = null, activeRequest = 0, verified = false;
const expectedTag = /^spotpear-wireless-dev-[a-f0-9]{12}$/;
const reset = () => {
  verified = false;
  byId("spotpearFlash").disabled = true;
  byId("spotpearInstaller").manifest = "about:blank";
  if (imageUrl) URL.revokeObjectURL(imageUrl);
  if (manifestUrl) URL.revokeObjectURL(manifestUrl);
  imageUrl = manifestUrl = null;
};
function updateButton() {
  byId("spotpearFlash").disabled = !(verified &&
    byId("spotpearBoardConfirm").checked &&
    byId("spotpearBackupConfirm").checked &&
    byId("spotpearExperimentalConfirm").checked &&
    window.isSecureContext && "serial" in navigator);
}
for (const id of ["spotpearBoardConfirm","spotpearBackupConfirm","spotpearExperimentalConfirm"]) {
  byId(id).addEventListener("change", updateButton);
}
async function load() {
  const request = ++activeRequest;
  reset();
  byId("spotpearStatus").textContent = "Đang kiểm tra phiên bản Spotpear trên GitHub…";
  try {
    const response = await fetch("/api/firmware/latest?display=spotpear", {cache:"no-store"});
    if (!response.ok) throw new Error("API firmware HTTP " + response.status);
    const meta = await response.json();
    if (request !== activeRequest) return;
    if (!meta.available || meta.board !== "spotpear-box128-wireless-lab" ||
        meta.chipFamily !== "ESP32-S3" || meta.display !== "spotpear" ||
        meta.channel !== "development" || meta.tested !== false ||
        !expectedTag.test(meta.tag) ||
        !/^[a-f0-9]{64}$/.test(meta.sha256) ||
        !Number.isSafeInteger(meta.size) || meta.size < 65536 || meta.size > 16777216 ||
        meta.firmwareUrl !== "/api/firmware/binary?tag=" + encodeURIComponent(meta.tag))
      throw new Error("Metadata không đúng profile Spotpear.");
    const binaryResponse = await fetch(meta.firmwareUrl, {cache:"no-store"});
    if (!binaryResponse.ok) throw new Error("Tải firmware lỗi HTTP " + binaryResponse.status);
    if (binaryResponse.redirected && new URL(binaryResponse.url).origin !== location.origin)
      throw new Error("Firmware bị chuyển đến host khác.");
    const bytes = await binaryResponse.arrayBuffer();
    if (request !== activeRequest) return;
    const header = new Uint8Array(bytes);
    if (bytes.byteLength !== meta.size || header[0] !== 0xe9)
      throw new Error("Merged image/size không hợp lệ tại offset 0.");
    const actual = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)),
      x => x.toString(16).padStart(2, "0")).join("");
    if (request !== activeRequest) return;
    if (actual !== meta.sha256) throw new Error("SHA-256 không khớp. Đã chặn Flash.");
    imageUrl = URL.createObjectURL(new Blob([bytes], {type:"application/octet-stream"}));
    manifestUrl = URL.createObjectURL(new Blob([JSON.stringify({
      name:"Spotpear Wireless Lab (USB experimental)",
      version:meta.tag,
      new_install_prompt_erase:false,
      builds:[{chipFamily:"ESP32-S3",improv:false,parts:[{path:imageUrl,offset:0}]}]
    })],{type:"application/json"}));
    byId("spotpearInstaller").manifest = manifestUrl;
    verified = true;
    byId("spotpearStatus").textContent = "Đã xác minh " + meta.tag +
      " · SHA-256 khớp · " + (bytes.byteLength/1048576).toFixed(2) +
      " MiB · CHƯA kiểm thử phần cứng; màn hình có thể không hiển thị.";
    updateButton();
  } catch (error) {
    if (request !== activeRequest) return;
    reset();
    byId("spotpearStatus").textContent = "Chưa có bản Spotpear tự động có thể nạp: " +
      (error?.message || String(error)) + ". Xem GitHub Actions hoặc thử lại sau khi phát hành.";
  }
}
byId("spotpearRefresh").addEventListener("click", load);
void load();
