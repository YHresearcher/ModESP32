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

#define TAG "ModS3Touch"

class TouchSpiDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* test_overlay_ = nullptr;
    lv_obj_t* face_overlay_ = nullptr;
    lv_obj_t* face_label_ = nullptr;
    lv_obj_t* face_status_ = nullptr;
    const char* face_expression_ = "^  ^";
    lv_obj_t* test_button_ = nullptr;
    lv_obj_t* test_label_ = nullptr;
    unsigned test_step_ = 0;
    static constexpr int kPoints[5][2] = {{6,48},{190,48},{98,132},{6,242},{190,242}};
    static constexpr const char* kNames[5] = {"Tren trai","Tren phai","Giua","Duoi trai","Duoi phai"};

    static void OnFaceOpen(lv_event_t* event) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(event));
        if(self && self->face_overlay_)lv_obj_remove_flag(self->face_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    static void OnFaceClose(lv_event_t* event) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(event));
        if(self && self->face_overlay_)lv_obj_add_flag(self->face_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    static void OnFaceTalk(lv_event_t*) {
        Application::GetInstance().ToggleChatState();
    }
    static void OnFaceTest(lv_event_t* event) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(event));
        if(!self || !self->test_overlay_)return;
        self->test_step_=0;
        lv_obj_remove_flag(self->test_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->test_overlay_); // Must cover mascot overlay and its three buttons.
        self->MoveTarget();
        ESP_LOGI("Touch5Point", "TOUCH_5PT_START from mascot: tap 5 markers, all-screen capture");
    }
    void SetExpressionText(const char* emotion) {
        if(!face_label_ || !face_status_)return;
        const char* face="^  ^";
        const char* status="San sang";
        if(emotion && (std::strstr(emotion,"sad") || std::strstr(emotion,"cry"))) {
            face=";  ;";status="Buon";
        } else if(emotion && (std::strstr(emotion,"sleep") || std::strstr(emotion,"tired"))) {
            face="-  -";status="Nghi";
        } else if(emotion && (std::strstr(emotion,"think") || std::strstr(emotion,"confused"))) {
            face="o  -";status="Suy nghi";
        } else if(emotion && (std::strstr(emotion,"happy") || std::strstr(emotion,"smile") || std::strstr(emotion,"laugh"))) {
            face="^  ^";status="Vui ve";
        } else if(emotion && (std::strstr(emotion,"surprise") || std::strstr(emotion,"scared"))) {
            face="O  O";status="Bat ngo";
        }
        face_expression_=face;
        lv_label_set_text(face_label_, face_expression_);
        lv_label_set_text(face_status_,status);
    }
    static void OnStartTest(lv_event_t* event) {
        auto* self = static_cast<TouchSpiDisplay*>(lv_event_get_user_data(event));
        if (!self || !self->test_overlay_) return;
        self->test_step_ = 0;
        lv_obj_remove_flag(self->test_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->test_overlay_);
        self->MoveTarget();
        ESP_LOGI("Touch5Point", "TOUCH_5PT_START: all-screen capture, no target hit-test required; calibration NOT saved");
    }
    static void OnTarget(lv_event_t* event) {
        auto* self = static_cast<TouchSpiDisplay*>(lv_event_get_user_data(event));
        if (!self) return;
        lv_point_t point = {};
        if (lv_indev_active()) lv_indev_get_point(lv_indev_active(), &point);
        ESP_LOGI("Touch5Point", "TOUCH_5PT step=%u label=%s x=%ld y=%ld",
                 self->test_step_ + 1, kNames[self->test_step_],
                 static_cast<long>(point.x), static_cast<long>(point.y));
        ++self->test_step_;
        if (self->test_step_ >= 5) {
            lv_obj_add_flag(self->test_overlay_, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI("Touch5Point", "TOUCH_5PT_COMPLETE: geometry checked, no persistent calibration");
        } else {
            self->MoveTarget();
        }
    }
    void MoveTarget() {
        if (test_step_ >= 5 || !test_button_ || !test_label_) return;
        lv_obj_set_pos(test_button_, kPoints[test_step_][0], kPoints[test_step_][1]);
        lv_label_set_text_fmt(test_label_, "%u/5 %s", test_step_+1, kNames[test_step_]);
    }
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    lv_display_t* GetLvglHandle() const { return display_; }
    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        if (test_overlay_ || !lvgl_port_lock(1000)) return;
        lv_obj_t* screen = lv_screen_active();
        lv_obj_t* start = lv_button_create(screen);
        lv_obj_set_size(start, 106, 46);
        lv_obj_align(start, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
        lv_obj_set_style_bg_color(start, lv_color_hex(0x126CA8), 0);
        lv_obj_t* label = lv_label_create(start);
        lv_label_set_text(label, "TOUCH TEST");
        lv_obj_center(label);
        lv_obj_add_event_cb(start, OnStartTest, LV_EVENT_CLICKED, this);

        test_overlay_ = lv_obj_create(screen);
        lv_obj_set_size(test_overlay_, 240, 320);
        lv_obj_set_pos(test_overlay_, 0, 0);
        lv_obj_set_style_bg_color(test_overlay_, lv_color_hex(0x06182C), 0);
        lv_obj_set_style_bg_opa(test_overlay_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(test_overlay_, 0, 0);
        lv_obj_set_scrollbar_mode(test_overlay_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(test_overlay_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(test_overlay_, LV_OBJ_FLAG_SCROLLABLE);
        // Capture every press on this full-screen overlay, even when touch axes are wrong.
        lv_obj_add_event_cb(test_overlay_, OnTarget, LV_EVENT_PRESSED, this);
        test_label_ = lv_label_create(test_overlay_);
        lv_obj_set_style_text_color(test_label_, lv_color_hex(0xB4E6FF), 0);
        lv_obj_set_pos(test_label_, 14, 12);
        test_button_ = lv_button_create(test_overlay_);
        lv_obj_set_size(test_button_, 44, 44);
        lv_obj_set_style_radius(test_button_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(test_button_, lv_color_hex(0xFF8929), 0);
        lv_obj_t* dot = lv_label_create(test_button_);
        lv_label_set_text(dot, "+");
        lv_obj_center(dot);
        lv_obj_remove_flag(test_button_, LV_OBJ_FLAG_CLICKABLE);
        // Orange marker is visual only until touch mapping has been verified.
        lv_obj_add_flag(test_overlay_, LV_OBJ_FLAG_HIDDEN);

        // Optional experimental touch-first mascot preview. The proven XiaoZhi UI
        // stays the default, and the BOOT button always remains a fallback.
        lv_obj_t* face_open=lv_button_create(screen);
        lv_obj_set_size(face_open,106,46);
        lv_obj_align(face_open,LV_ALIGN_BOTTOM_LEFT,4,-4);
        lv_obj_set_style_bg_color(face_open,lv_color_hex(0x176F71),0);
        lv_obj_t* face_open_label=lv_label_create(face_open);
        lv_label_set_text(face_open_label,"AI FACE");
        lv_obj_center(face_open_label);
        lv_obj_add_event_cb(face_open,OnFaceOpen,LV_EVENT_CLICKED,this);

        face_overlay_=lv_obj_create(screen);
        lv_obj_set_size(face_overlay_,240,320);
        lv_obj_set_pos(face_overlay_,0,0);
        lv_obj_set_style_pad_all(face_overlay_,0,0);
        lv_obj_set_style_bg_color(face_overlay_,lv_color_hex(0x071B30),0);
        lv_obj_set_style_bg_opa(face_overlay_,LV_OPA_COVER,0);
        lv_obj_set_scrollbar_mode(face_overlay_,LV_SCROLLBAR_MODE_OFF);

        lv_obj_t* header=lv_label_create(face_overlay_);
        lv_obj_set_style_text_color(header,lv_color_hex(0xA9DEFF),0);
        lv_label_set_text(header,"XiaoZhi AI  |  TOUCH");
        lv_obj_align(header,LV_ALIGN_TOP_MID,0,13);

        lv_obj_t* face=lv_obj_create(face_overlay_);
        lv_obj_set_size(face,162,162);
        lv_obj_align(face,LV_ALIGN_CENTER,0,-24);
        lv_obj_set_style_bg_color(face,lv_color_hex(0x123C5B),0);
        lv_obj_set_style_border_color(face,lv_color_hex(0x53D8F0),0);
        lv_obj_set_style_border_width(face,3,0);
        lv_obj_set_style_radius(face,LV_RADIUS_CIRCLE,0);
        lv_obj_set_scrollbar_mode(face,LV_SCROLLBAR_MODE_OFF);

        face_label_=lv_label_create(face);
        lv_label_set_text(face_label_,"^  ^");
        lv_obj_set_style_text_color(face_label_,lv_color_hex(0x69EAFF),0);
        lv_obj_align(face_label_,LV_ALIGN_CENTER,0,-14);

        lv_obj_t* mouth=lv_label_create(face);
        lv_label_set_text(mouth,"u");
        lv_obj_set_style_text_color(mouth,lv_color_hex(0x69EAFF),0);
        lv_obj_align(mouth,LV_ALIGN_CENTER,0,16);
        face_status_=lv_label_create(face_overlay_);
        lv_label_set_text(face_status_,"San sang");
        lv_obj_set_style_text_color(face_status_,lv_color_hex(0xE5F5FF),0);
        lv_obj_align(face_status_,LV_ALIGN_CENTER,0,77);

        const char* labels[]={"TALK","5-PT","BACK"};
        lv_event_cb_t callbacks[]={OnFaceTalk,OnFaceTest,OnFaceClose};
        for(int i=0;i<3;i++){
            lv_obj_t* btn=lv_button_create(face_overlay_);
            lv_obj_set_size(btn,70,48);
            lv_obj_set_pos(btn,8+i*78,264);
            lv_obj_set_style_bg_color(btn,lv_color_hex(i==0?0xC86B24:0x1C5C83),0);
            lv_obj_t* text=lv_label_create(btn);
            lv_label_set_text(text,labels[i]);
            lv_obj_center(text);
            lv_obj_add_event_cb(btn,callbacks[i],LV_EVENT_CLICKED,this);
        }
        lv_obj_add_flag(face_overlay_,LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }
    bool SetEmotion(const char* emotion) override {
        bool ok=SpiLcdDisplay::SetEmotion(emotion);
        if(face_label_ && lvgl_port_lock(1000)){
            SetExpressionText(emotion);
            lvgl_port_unlock();
        }
        return ok;
    }
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
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
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
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }
};
DECLARE_BOARD(ModEsp32S3TouchBoard);
