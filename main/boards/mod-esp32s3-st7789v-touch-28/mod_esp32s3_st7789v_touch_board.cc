#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_xpt2046.h>
#include <esp_lvgl_port.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>
#include <cstring>
#include <cstdint>
#include "board.h"
#include "chibi_face_b64.h"
#include "jpeg_to_image.h"
#include <mbedtls/base64.h>
#include <vector>

#define TAG "ModS3Touch"

// Face-only UI: embedded approved Chibi JPEG with XiaoZhi emotion fallback.
// The agent/persona is configured by the connected cloud service, not LVGL.
class TouchSpiDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* face_background_ = nullptr;
    lv_obj_t* face_image_ = nullptr;
    lv_image_dsc_t face_dsc_ = {};
    uint8_t* face_rgb_ = nullptr;
    lv_obj_t* face_status_ = nullptr;
    lv_timer_t* face_timer_ = nullptr;
    DeviceState last_state_ = kDeviceStateUnknown;
    bool face_visible_ = false;

    // Decode the approved JPEG once at boot. The decoded RGB565 data stays
    // alive for LVGL; 160x162x2 = ~52 KiB, suitable for N16R8 PSRAM.
    // If memory or JPEG decoding fails, use the upstream emoji renderer.
    void SetupFaceImage(lv_obj_t* screen) {
        const size_t b64_len = strlen(kChibiFaceJpegBase64);
        std::vector<uint8_t> jpeg(b64_len * 3 / 4 + 8);
        size_t jpeg_len = 0;
        if (mbedtls_base64_decode(jpeg.data(), jpeg.size(), &jpeg_len,
                                  reinterpret_cast<const unsigned char*>(kChibiFaceJpegBase64),
                                  b64_len) != 0) {
            ESP_LOGE(TAG, "CHIBI_ASSET_FAIL: base64");
            return;
        }
        uint8_t* rgb = nullptr;
        size_t rgb_len = 0, width = 0, height = 0, stride = 0;
        if (jpeg_to_image(jpeg.data(), jpeg_len, &rgb, &rgb_len, &width,
                          &height, &stride) != ESP_OK || !rgb) {
            ESP_LOGE(TAG, "CHIBI_ASSET_FAIL: jpeg decoder");
            return;
        }
        face_rgb_ = rgb;
        face_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        face_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        face_dsc_.header.w = static_cast<uint32_t>(width);
        face_dsc_.header.h = static_cast<uint32_t>(height);
        face_dsc_.header.stride = static_cast<uint32_t>(stride);
        face_dsc_.data_size = rgb_len;
        face_dsc_.data = face_rgb_;
        face_image_ = lv_image_create(screen);
        lv_image_set_src(face_image_, &face_dsc_);
        lv_obj_align(face_image_, LV_ALIGN_CENTER, 0, -23);
        lv_obj_remove_flag(face_image_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(face_image_, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "CHIBI_ASSET_READY: %ux%u RGB565 bytes=%u",
                 static_cast<unsigned>(width), static_cast<unsigned>(height),
                 static_cast<unsigned>(rgb_len));
    }
    static void Tick(lv_timer_t* timer) {
        auto* self = static_cast<TouchSpiDisplay*>(lv_timer_get_user_data(timer));
        if (self) self->UpdateFace();
    }
    void UpdateFace() {
        if (!face_background_) return;
        const DeviceState state = Application::GetInstance().GetDeviceState();
        // These modes require the original XiaoZhi UI (activation code, Wi-Fi,
        // upgrades, critical errors), not an unresponsive full-screen face.
        const bool show_face = state == kDeviceStateIdle ||
            state == kDeviceStateConnecting || state == kDeviceStateListening ||
            state == kDeviceStateSpeaking || state == kDeviceStateNotifying;
        if (show_face != face_visible_) {
            face_visible_ = show_face;
            if (show_face) {
                lv_obj_remove_flag(face_background_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(face_background_);
                // Approved face takes precedence; original emotions are fallback.
                if (face_image_) {
                    lv_obj_remove_flag(face_image_, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_move_foreground(face_image_);
                    if (emoji_label_) lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
                    if (emoji_image_) lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
                } else {
                    if (emoji_label_) { lv_obj_align(emoji_label_, LV_ALIGN_CENTER, 0, -20); lv_obj_move_foreground(emoji_label_); }
                    if (emoji_image_) { lv_obj_align(emoji_image_, LV_ALIGN_CENTER, 0, -20); lv_obj_move_foreground(emoji_image_); }
                }
                lv_obj_move_foreground(face_status_);
                lv_obj_remove_flag(face_status_, LV_OBJ_FLAG_HIDDEN);
                if (top_bar_) lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
                if (status_bar_) lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
                if (content_) lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(face_background_, LV_OBJ_FLAG_HIDDEN);
                if (face_image_) lv_obj_add_flag(face_image_, LV_OBJ_FLAG_HIDDEN);
                if (face_status_) lv_obj_add_flag(face_status_, LV_OBJ_FLAG_HIDDEN);
                if (top_bar_) lv_obj_remove_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
                if (status_bar_) lv_obj_remove_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
                if (content_) lv_obj_remove_flag(content_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (show_face && face_image_) {
            // A server emotion update may have re-exposed the default image.
            if (emoji_label_) lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
            if (emoji_image_) lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        } else if (show_face) {
            ShowEmotion();
        }
        if (state == last_state_) return;
        last_state_ = state;
        if (!face_status_) return;
        const char* status = "Đang chờ gọi 'Hi ESP'...";
        switch (state) {
            case kDeviceStateConnecting: status = "Đang kết nối..."; break;
            case kDeviceStateListening: status = "Đang lắng nghe..."; break;
            case kDeviceStateSpeaking: status = "Đang trả lời..."; break;
            case kDeviceStateNotifying: status = "Có thông báo mới"; break;
            default: break;
        }
        lv_label_set_text(face_status_, status);
        lv_obj_set_style_text_color(face_status_,
            lv_color_hex(state == kDeviceStateListening ? 0x4FFFE0 :
                         state == kDeviceStateSpeaking ? 0xFFE3A6 : 0xC7E9FF), 0);
        ESP_LOGI("ChibiFace", "FACE_STATE=%d WAKE_PHRASE=Hi ESP; face-only prototype", (int)state);
    }
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    ~TouchSpiDisplay() override {
        if (face_timer_) {
            lv_timer_delete(face_timer_);
            face_timer_ = nullptr;
        }
    }
    lv_display_t* GetLvglHandle() const { return display_; }
    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        if (face_background_ || !lvgl_port_lock(1000)) return;
        auto* screen = lv_screen_active();
        face_background_ = lv_obj_create(screen);
        lv_obj_set_size(face_background_, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_set_pos(face_background_, 0, 0);
        lv_obj_set_style_pad_all(face_background_, 0, 0);
        lv_obj_set_style_border_width(face_background_, 0, 0);
        lv_obj_set_style_radius(face_background_, 0, 0);
        lv_obj_set_style_bg_color(face_background_, lv_color_hex(0x081B30), 0);
        lv_obj_remove_flag(face_background_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(face_background_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(face_background_, LV_OBJ_FLAG_HIDDEN);
        SetupFaceImage(screen);
        // Hide the old three-button control panel. BOOT GPIO0 remains a
        // functional fallback if the microphone or wake model fails.
        face_status_ = lv_label_create(screen);
        lv_label_set_text(face_status_, "Đang chờ gọi 'Hi ESP'...");
        lv_obj_set_style_text_color(face_status_, lv_color_hex(0xC7E9FF), 0);
        lv_obj_set_style_text_align(face_status_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(face_status_, DISPLAY_WIDTH - 12);
        lv_obj_align(face_status_, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_obj_add_flag(face_status_, LV_OBJ_FLAG_HIDDEN);
        face_timer_ = lv_timer_create(Tick, 350, this);
        UpdateFace();
        lvgl_port_unlock();
    }
};

// Board-specific microphone level diagnostics. No raw audio or transcripts are
// logged. A nonzero amplitude proves electrical input, not ASR correctness.
class ModDiagnosticAudioCodec : public NoAudioCodecSimplex {
public:
    using NoAudioCodecSimplex::NoAudioCodecSimplex;
    bool InputData(std::vector<int16_t>& data) override {
        if (!NoAudioCodecSimplex::InputData(data)) return false;
        if (++frames_ % 100 == 0 && !data.empty()) {
            uint32_t peak = 0;
            uint64_t sum_abs = 0;
            for (int16_t v : data) {
                uint32_t abs_v = v == INT16_MIN ? 32768u :
                    static_cast<uint32_t>(v < 0 ? -static_cast<int32_t>(v) : v);
                if (abs_v > peak) peak = abs_v;
                sum_abs += abs_v;
            }
            ESP_LOGI("ModMic", "MIC_LEVEL samples=%u peak=%u avg_abs=%u (not audio content)",
                     static_cast<unsigned>(data.size()),
                     static_cast<unsigned>(peak),
                     static_cast<unsigned>(sum_abs / data.size()));
        }
        return true;
    }
private:
    uint32_t frames_ = 0;
};

// Experimental firmware: keep the original, proven ST7789V target unchanged.
class ModEsp32S3TouchBoard : public WifiBoard {
private:
    Button boot_button_;
    TouchSpiDisplay* display_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    lv_indev_t* indev_ = nullptr;

    static void TouchEvent(lv_event_t* event) {
        auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
        if (!indev) return;
        lv_point_t point = {};
        lv_indev_get_point(indev, &point);
        ESP_LOGI(TAG, "TOUCH_SAMPLE x=%ld y=%ld irq=%d (verify rotation/calibration)",
                 static_cast<long>(point.x), static_cast<long>(point.y),
                 gpio_get_level(TOUCH_IRQ_PIN));
    }

    void InitializeDisplayAndTouch() {
        spi_bus_config_t bus = {};
        bus.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        bus.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        bus.miso_io_num = DISPLAY_SPI_MISO_PIN;
        bus.quadwp_io_num = GPIO_NUM_NC;
        bus.quadhd_io_num = GPIO_NUM_NC;
        bus.max_transfer_sz = DISPLAY_WIDTH * 40 * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t lcd_io = nullptr;
        esp_lcd_panel_io_spi_config_t lcd_io_cfg = {};
        lcd_io_cfg.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        lcd_io_cfg.dc_gpio_num = DISPLAY_DC_PIN;
        lcd_io_cfg.spi_mode = 0;
        lcd_io_cfg.pclk_hz = 20 * 1000 * 1000;
        lcd_io_cfg.trans_queue_depth = 10;
        lcd_io_cfg.lcd_cmd_bits = 8;
        lcd_io_cfg.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &lcd_io_cfg, &lcd_io));

        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = DISPLAY_RST_PIN;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(lcd_io, &panel_cfg, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        // This ST7789V module showed a photographic-negative effect with INVON.
        // Use normal color mode. If this board revision is opposite, change
        // DISPLAY_COLOR_INVERT in config.h; do not change RGB565 data.
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_COLOR_INVERT));
        ESP_LOGI(TAG, "DISPLAY_COLOR_INVERT=%d", static_cast<int>(DISPLAY_COLOR_INVERT));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
        display_ = new TouchSpiDisplay(lcd_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY);
        ESP_LOGI(TAG, "ST7789V display initialized; starting experimental XPT2046 touch");

        gpio_config_t irq_cfg = {};
        irq_cfg.pin_bit_mask = 1ULL << TOUCH_IRQ_PIN;
        irq_cfg.mode = GPIO_MODE_INPUT;
        irq_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
        ESP_ERROR_CHECK(gpio_config(&irq_cfg));

        esp_lcd_panel_io_spi_config_t touch_io_cfg = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_SPI_CS_PIN);
        esp_err_t err = esp_lcd_new_panel_io_spi(SPI2_HOST, &touch_io_cfg, &touch_io_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TOUCH_IO_FAILED err=%s; continuing without touch", esp_err_to_name(err));
            return;
        }
        esp_lcd_touch_config_t touch_cfg = {};
        touch_cfg.x_max = DISPLAY_WIDTH;
        touch_cfg.y_max = DISPLAY_HEIGHT;
        touch_cfg.rst_gpio_num = GPIO_NUM_NC;
        // IRQ is polled/logged manually; polling driver avoids false interrupts on unverified boards.
        touch_cfg.int_gpio_num = GPIO_NUM_NC;
        touch_cfg.flags.swap_xy = 0;
        // Current hardware reports right-hand BACK touches as left-hand TALK: horizontal axis reversed.
        // Apply provisional X inversion and verify with physical five-point test.
        touch_cfg.flags.mirror_x = 1;
        touch_cfg.flags.mirror_y = 0;

        err = esp_lcd_touch_new_spi_xpt2046(touch_io_, &touch_cfg, &touch_);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TOUCH_INIT_FAILED err=%s; continuing without touch", esp_err_to_name(err));
            return;
        }
        const lvgl_port_touch_cfg_t lv_touch_cfg = {
            .disp = display_->GetLvglHandle(),
            .handle = touch_,
        };
        indev_ = lvgl_port_add_touch(&lv_touch_cfg);
        if (indev_) {
            lv_indev_add_event_cb(indev_, TouchEvent, LV_EVENT_PRESSED, nullptr);
            ESP_LOGI(TAG, "TOUCH_READY: GPIO12 CLK, GPIO11 DIN, GPIO8 DO, GPIO9 CS, GPIO7 IRQ; tap 5 points to validate");
        } else {
            ESP_LOGE(TAG, "TOUCH_INDEV_FAILED (LCD and original BOOT button remain available)");
        }
    }

public:
    ModEsp32S3TouchBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplayAndTouch();
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    AudioCodec* GetAudioCodec() override {
        static ModDiagnosticAudioCodec audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }
};
DECLARE_BOARD(ModEsp32S3TouchBoard);
