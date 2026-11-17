# Chibi AI — agent configuration (server-side)

## What the firmware actually does
- The device remains compatible with the XiaoZhi MQTT/WebSocket/Opus conversation protocol.
- The firmware **does not own the LLM system prompt or cloud TTS voice**. A new face and an on-device persona text do NOT change the backend's selected agent.
- The board exposes the MCP tool `self.get_chibi_agent_profile` to an agent that chooses to call it. **The cloud must opt in**; this is not an automatic system-prompt override.

## Identity to use in XiaoZhi Console
Agent name: **Chibi AI**
Language: **vi-VN**
Voice preference: **natural Vietnamese female voice**, only if offered by the configured TTS provider.
System prompt:

> Bạn là Chibi, trợ lý AI đồng hành thân thiện trên thiết bị ModESP32. Luôn giao tiếp bằng tiếng Việt tự nhiên, có dấu đầy đủ, trừ khi người dùng yêu cầu ngôn ngữ khác. Xưng mình hoặc Chibi, gọi người dùng là bạn. Trả lời ngắn gọn, hữu ích, tránh đọc biểu tượng Markdown hoặc văn bản quá dài. Không tự nhận đã điều khiển phần cứng, tra cứu hay đặt lịch nếu chưa có xác nhận từ công cụ. Không tự nhận là trợ lý XiaoZhi mặc định; XiaoZhi là nền tảng kết nối và phát âm thanh. Nếu hệ thống TTS cho phép, ưu tiên giọng nữ tiếng Việt rõ ràng và nhẹ nhàng. Nếu kết nối mạng lỗi, giải thích ngắn gọn và hướng dẫn thử lại.

## Server setup and acceptance
1. Sign in to `https://xiaozhi.me/console/` and choose the agent associated with the device's existing activation/device ID. Do not create an unrelated agent without assigning the actual device.
2. Change the agent's name and system/persona prompt, and select Vietnamese language/voice if offered. Save.
3. If the public hosted XiaoZhi instance does not permit this configuration, use a **compatible alternative agent backend** (must implement the actual XiaoZhi audio protocol and provide a valid OTA-provided endpoint) or continue with the default XiaoZhi personality. No firmware-only patch can override an opaque hosted server's model prompt.
4. Reconnect the device and ask "Bạn là ai?" Expected: "Mình là Chibi..." rather than a default XiaoZhi description.
5. Test ten Vietnamese requests and listen for a natural female Vietnamese TTS voice. Naming a voice in the prompt alone cannot force TTS provider/voice selection.

## Color correction — ST7789
- Previous firmware called `esp_lcd_panel_invert_color(panel, true)`. The current board revision uses `DISPLAY_COLOR_INVERT false` in `config.h` to disable LCD inversion.
- This is the **reported negative-image fix**, pending visual device test. Do NOT modify the already working microphone and I²S GPIO pins.
- If only the JPEG retains wrong red/blue while native LVGL colors look correct, investigate RGB565 byte order and LCD RGB/BGR order separately. Do not blindly invert JPEG data.
- Keep a working old firmware available for rollback.

## Build vs hardware
CI success only validates compilation and firmware packaging. It does not establish display color correctness or whether the cloud agent settings have taken effect.
