# Chibi Gateway — chạy thử trên Mac

Đây là backend **thử nghiệm**, không phải bản production. Nó thực hiện:
ESP32 (Opus 16 kHz) -> Whisper nhận diện tiếng Việt -> Gemini trả lời -> Edge TTS tiếng Việt -> Opus 24 kHz -> ESP32.

**Không gửi giọng nói đến server Xiaozhi** khi firmware được biên dịch ở chế độ Custom Agent và trỏ đúng server này. Tuy nhiên, dữ liệu vẫn được gửi tới Gemini và dịch vụ Edge TTS của Microsoft.

## 1. Chuẩn bị Mac

- Python 3.11 hoặc 3.12
- Homebrew / ffmpeg: `brew install ffmpeg`
- ESP32 và Mac cùng Wi-Fi/LAN
- Gemini API key từ Google AI Studio. Tài khoản Gemini Pro trên web không đồng nghĩa với API được cấp miễn phí hoặc không tính phí.

```bash
cd backend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
export GEMINI_API_KEY='PASTE_YOUR_KEY_HERE'
export GEMINI_MODEL='gemini-2.5-flash'
export CHIBI_HOST='0.0.0.0'
python chibi_server.py
```

Không commit key thật. Lần chạy đầu Whisper cần tải model; Mac phải có Internet.

## 2. Lấy địa chỉ Mac trong mạng LAN

```bash
ipconfig getifaddr en0
```

Ví dụ nếu IP của Mac là `192.168.1.50` (chỉ minh họa), endpoint là `ws://192.168.1.50:8765`.

## 3. Cấu hình ESP32 firmware

```bash
idf.py menuconfig
```

Trong **ModESP32 Assistant**:
- Enable **Use custom agent backend (disable XiaoZhi)**
- Điền endpoint `ws://<MAC_LAN_IP>:8765`
- Chọn đúng phần cứng và cấu hình chân cũ; build và flash theo hướng dẫn của repo

**Không được trỏ ESP32 tới `localhost`: đó là chính ESP32, không phải Mac.**

## 4. Giới hạn của prototype

- Không có cơ chế xác thực trên cổng WebSocket: chỉ chạy trong LAN tin cậy, KHÔNG mở port trên router hoặc công khai Internet.
- Chưa kiểm thử âm thanh/màn hình trực tiếp với phần cứng.
- Hỗ trợ giao thức Opus WebSocket **v1**; không hỗ trợ v2/v3, MQTT/UDP, AEC full duplex hay MCP tool calls.
- Tách lượt thoại bằng ngưỡng âm lượng đơn giản; tiếng ồn dễ gây kích hoạt sai, câu dài/ngập ngừng có thể bị cắt.
- Chưa có model wake word “Hey Chibi”, chưa xử lý Barge-in hoàn chỉnh.
- Edge TTS là kết nối bên thứ ba, không phải TTS offline hoặc API có SLA.
- Gemini/Whisper/TTS có độ trễ; không đảm bảo real-time.
- Không lưu khóa API trên ESP32. Server chạy trên Mac phải được mở để agent hoạt động.

## 5. Acceptance checks

1. Serial ESP32 xác nhận Custom Agent Mode, không gọi OTA Xiaozhi.
2. Máy Mac log thấy WebSocket hello và client kết nối.
3. Thử bằng nút vật lý trước, đừng đòi “Hey Chibi” ngay.
4. Nói tiếng Việt: hiển thị `stt` có dấu, thấy `tts` start/sentence_start/stop, nghe giọng nói.
5. Kiểm tra âm lượng, lỗi TTS, timeout mạng, kết nối lại và cách hiển thị TFT.
6. Sau khi kiểm thử thành công mới cân nhắc merge PR.

## 6. Bảo mật và triển khai dài hạn

Bản production nên dùng WSS/TLS, xác thực thiết bị bằng token provision riêng, giới hạn kết nối, quản lý khóa trên server, logging hạn chế dữ liệu nhạy cảm, và triển khai server ổn định luôn bật. Không mở port 8765 công khai.
