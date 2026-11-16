import {validateMergedImage, parseChecksum, redactSecrets, classifyLog} from "./diagnostics.mjs";
const $ = id => document.getElementById(id);
const repo = "YHresearcher/ModESP32";
const MAX_LOG = 300000;
let firmwareUrl = null, manifestUrl = null, selectedFile = null, validated = false;
let port = null, reader = null, reading = false, serialText = "";
let selectionSerial = 0;
function message(text) { $("reportStatus").textContent = text; }
function append(chunk) {
  serialText = (serialText + chunk).slice(-MAX_LOG);
  $("log").textContent = serialText || "[No data]";
  $("log").scrollTop = $("log").scrollHeight;
  const findings = classifyLog(serialText);
  $("diagnostics").textContent = findings.length ? "Gợi ý: " + findings.map(f => f.code + " – " + f.message).join(" | ") : "Chưa nhận diện được lỗi qua các mẫu log hiện tại.";
}
function resetManifest() {
  if (firmwareUrl) URL.revokeObjectURL(firmwareUrl);
  if (manifestUrl) URL.revokeObjectURL(manifestUrl);
  firmwareUrl = null; manifestUrl = null;
  validated = false;
  $("installer").manifest = "about:blank";
  updateFlashAvailability();
}
function updateFlashAvailability() {
  const ready = validated && $("confirmBoard").checked && $("confirmHardware").checked && "serial" in navigator && window.isSecureContext;
  $("flashButton").disabled = !ready;
  $("flashHelp").textContent = ready
    ? "Sẵn sàng mở ESP Web Tools. Kiểm tra lại cổng USB trước khi tiến hành."
    : "Cần chọn file .bin hợp lệ và xác nhận cả hai ô. HTTPS + Web Serial là bắt buộc.";
}
$("compat").textContent = "serial" in navigator && window.isSecureContext
  ? "✓ Web Serial sẵn sàng trên HTTPS. Hãy đóng các ứng dụng đang giữ cổng USB."
  : "⚠ Trình duyệt không có Web Serial hoặc không chạy trên HTTPS; dùng Chrome/Edge desktop.";
for (const id of ["confirmBoard","confirmHardware"]) $(id).addEventListener("change", updateFlashAvailability);
for (const id of ["firmware","checksum"]) $(id).addEventListener("change", validateSelectedFile);
async function validateSelectedFile() {
  const request = ++selectionSerial;
  resetManifest();
  selectedFile = $("firmware").files[0] ?? null;
  $("firmwareHash").textContent = "";
  if (!selectedFile) { $("fileStatus").textContent = "Chưa chọn firmware"; return; }
  $("fileStatus").textContent = "Đang xác minh file firmware...";
  try {
    const header = new Uint8Array(await selectedFile.slice(0, 32).arrayBuffer());
    const issues = validateMergedImage(header, selectedFile.name, selectedFile.size);
    if (issues.length) throw Error(issues.join(" "));
    const full = await selectedFile.arrayBuffer();
    const sha = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", full)), n => n.toString(16).padStart(2,"0")).join("");
    if (request !== selectionSerial) return;
    const checksum = $("checksum").files[0];
    if (checksum) {
      const expected = parseChecksum(await checksum.text());
      if (!expected) throw Error("SHA256SUMS không có digest hợp lệ cho merged-binary.bin.");
      if (expected !== sha) throw Error("SHA-256 KHÔNG khớp! Không được flash firmware này.");
    }
    if (request !== selectionSerial) return;
    firmwareUrl = URL.createObjectURL(selectedFile);
    const manifest = {
      name: "ModESP32 ESP32-S3 DIY",
      version: "experimental",
      new_install_prompt_erase: false,
      builds: [{chipFamily: "ESP32-S3", improv: false, parts: [{path: firmwareUrl, offset: 0}]}]
    };
    manifestUrl = URL.createObjectURL(new Blob([JSON.stringify(manifest)], {type:"application/json"}));
    // ESP Web Tools documents assigning a Blob URL directly to the manifest property.
    $("installer").manifest = manifestUrl;
    validated = true;
    $("fileStatus").textContent = selectedFile.name + " · " + (selectedFile.size / 1048576).toFixed(2) + " MiB · " + (checksum ? "SHA256 verified" : "SHA256 chưa đối chiếu, chỉ kiểm tra cấu trúc");
    $("firmwareHash").textContent = "SHA-256: " + sha;
    updateFlashAvailability();
  } catch (e) {
    if (request !== selectionSerial) return;
    resetManifest();
    $("fileStatus").textContent = "Không thể flash: " + (e?.message || String(e));
  }
}
$("connect").addEventListener("click", async () => {
  if (!("serial" in navigator) || !window.isSecureContext) return message("Cần Web Serial và HTTPS.");
  if (port) return;
  try {
    port = await navigator.serial.requestPort();
    await port.open({baudRate: Number($("baud").value)});
    $("serialState").textContent = "Đã kết nối";
    $("connect").disabled = true;
    $("disconnect").disabled = false;
    reading = true;
    const decoder = new TextDecoder();
    while (reading && port.readable) {
      reader = port.readable.getReader();
      try {
        while (reading) {
          const {value, done} = await reader.read();
          if (done) break;
          if (value) append(decoder.decode(value, {stream:true}));
        }
      } catch (e) {
        if (reading) append("\n[SERIAL ERROR] " + String(e) + "\n");
      } finally {
        reader.releaseLock();
        reader = null;
      }
      if (!reading) break;
      // Avoid a tight loop when the serial readable stream closes.
      if (!port.readable) break;
    }
  } catch (e) {
    if (e.name !== "NotFoundError") append("\n[CONNECT ERROR] " + String(e) + "\n");
  } finally {
    await closeSerial();
  }
});
async function closeSerial() {
  reading = false;
  try { if (reader) await reader.cancel(); } catch {}
  try { if (port) await port.close(); } catch {}
  port = null;
  $("connect").disabled = false;
  $("disconnect").disabled = true;
  $("serialState").textContent = "Đã ngắt";
}
$("disconnect").addEventListener("click", () => {
  reading = false;
  if (reader) reader.cancel().catch(() => {});
});
$("clear").addEventListener("click", () => {
  serialText = "";
  $("log").textContent = "[ModESP32] Đã xóa log.\n";
  $("diagnostics").textContent = "Chưa có log để phân tích.";
});
$("download").addEventListener("click", () => {
  const url = URL.createObjectURL(new Blob([serialText || "[No serial output]\n"], {type:"text/plain;charset=utf-8"}));
  const a = document.createElement("a");
  a.href = url;
  a.download = "modesp32-serial-" + new Date().toISOString().replace(/[:.]/g,"-") + ".txt";
  a.click();
  setTimeout(() => URL.revokeObjectURL(url),1000);
  message("Đã tải log gốc về máy; file có thể chứa dữ liệu nhạy cảm.");
});
function updateShare() { $("issue").disabled = !$("confirmShare").checked || !$("reportPreview").value.trim(); }
$("confirmShare").addEventListener("change", updateShare);
$("reportPreview").addEventListener("input", () => { $("confirmShare").checked = false; updateShare(); });
$("prepare").addEventListener("click", () => {
  const findings = classifyLog(serialText);
  const summary = findings.length ? findings.map(x => "- " + x.code + ": " + x.message).join("\n") : "- Chưa phát hiện mẫu lỗi rõ ràng";
  $("reportPreview").value = "Device: ESP32-S3 N16R8; LCD: ST7789V; Audio: I2S/ MAX98357A\n\nFindings:\n" + summary + "\n\nSerial excerpt (review manually):\n" + redactSecrets(serialText).slice(-2200);
  $("confirmShare").checked = false;
  updateShare();
  message("Đã tạo bản xem trước. Hãy xóa token, SSID, nội dung riêng tư nếu vẫn còn.");
});
$("copy").addEventListener("click", async () => {
  const val = $("reportPreview").value.trim();
  if (!val) return message("Tạo bản xem trước trước khi copy.");
  try {
    await navigator.clipboard.writeText(val);
    message("Đã copy bản xem trước. Chỉ chia sẻ sau khi đã tự kiểm tra dữ liệu.");
  } catch (e) { message("Không copy được: " + e.message); }
});
$("issue").addEventListener("click", () => {
  if ($("issue").disabled) return;
  const text = $("reportPreview").value.slice(0,2700);
  const url = "https://github.com/" + repo + "/issues/new?title=" +
    encodeURIComponent("[ModESP32 S3] Diagnostic report") + "&body=" + encodeURIComponent(
      "## Symptoms\n(Ghi triệu chứng và cách tái hiện)\n\n## Diagnostics (user reviewed)\n\n" + text +
      "\n\nPlease remove any sensitive device or personal details before publishing.");
  window.open(url, "_blank", "noopener,noreferrer");
  message("Đã mở bản nháp GitHub Issue. Chỉ khi bạn xác nhận Submit trên GitHub mới có dữ liệu được đăng.");
});
