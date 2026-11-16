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

#define TAG "ModS3Touch"

// Chibi v1.0-RC: LVGL-only artwork; no GPU/3D renderer or external sprite dependency.
// Safe recovery: the original ST7789V firmware is a separate unchanged build profile.
class TouchSpiDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* face_overlay_ = nullptr;
    lv_obj_t* face_eyes_ = nullptr;
    lv_obj_t* face_mouth_ = nullptr;
    lv_obj_t* face_status_ = nullptr;
    lv_obj_t* face_subtitle_ = nullptr;
    lv_obj_t* settings_overlay_ = nullptr;
    lv_obj_t* volume_overlay_ = nullptr;
    lv_obj_t* volume_label_ = nullptr;
    lv_obj_t* test_overlay_ = nullptr;
    lv_obj_t* test_button_ = nullptr;
    lv_obj_t* test_label_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    unsigned test_step_ = 0;
    bool auto_opened_ = false;
    bool user_closed_ = false;
    DeviceState last_state_ = kDeviceStateUnknown;

    static constexpr int kPoints[5][2] = {{6,48},{190,48},{98,132},{6,242},{190,242}};
    static constexpr const char* kNames[5] = {"Tren trai","Tren phai","Giua","Duoi trai","Duoi phai"};

    static lv_obj_t* Panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius) {
        auto* obj = lv_obj_create(parent);
        lv_obj_set_pos(obj,x,y);
        lv_obj_set_size(obj,w,h);
        lv_obj_set_style_radius(obj,radius,0);
        lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
        lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
        lv_obj_set_style_border_width(obj,0,0);
        lv_obj_set_style_pad_all(obj,0,0);
        lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE); // Decorative shapes never intercept touches.
        lv_obj_set_scrollbar_mode(obj,LV_SCROLLBAR_MODE_OFF);
        return obj;
    }
    static lv_obj_t* Label(lv_obj_t* parent,const char* text,int x,int y,uint32_t color) {
        auto* label=lv_label_create(parent);
        lv_label_set_text(label,text);
        lv_obj_set_style_text_color(label,lv_color_hex(color),0);
        lv_obj_set_pos(label,x,y);
        return label;
    }
    static lv_obj_t* Button(lv_obj_t* parent,const char* caption,int x,int y,int w,int h,
                            uint32_t color,lv_event_cb_t cb,void* user) {
        auto* btn=lv_button_create(parent);
        lv_obj_set_pos(btn,x,y);
        lv_obj_set_size(btn,w,h);
        lv_obj_set_style_radius(btn,10,0);
        lv_obj_set_style_bg_color(btn,lv_color_hex(color),0);
        auto* label=lv_label_create(btn);
        lv_label_set_text(label,caption);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn,cb,LV_EVENT_CLICKED,user);
        return btn;
    }
    static void OpenFace(lv_event_t* e) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(e));
        if (!self || !self->face_overlay_) return;
        self->user_closed_=false;
        lv_obj_remove_flag(self->face_overlay_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->face_overlay_);
    }
    static void CloseFace(lv_event_t* e) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(e));
        if (!self || !self->face_overlay_) return;
        self->user_closed_=true;
        lv_obj_add_flag(self->face_overlay_,LV_OBJ_FLAG_HIDDEN);
    }
    static void Talk(lv_event_t*) { Application::GetInstance().ToggleChatState(); }
    static void OpenVolume(lv_event_t* e) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(e));
        if (!self || !self->volume_overlay_) return;
        self->ShowVolume();
        lv_obj_remove_flag(self->volume_overlay_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->volume_overlay_);
    }
    // A callback can distinguish + and - without unsafe temporary lambda captures.
    static void VolumeMinus(lv_event_t* e) { AdjustVolume(e,-10); }
    static void VolumePlus(lv_event_t* e) { AdjustVolume(e,10); }
    static void AdjustVolume(lv_event_t* e,int delta) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(e));
        if(!self)return;
        auto* codec=Board::GetInstance().GetAudioCodec();
        if(!codec)return;
        int v=codec->output_volume()+delta;
        if(v<10)v=10;
        if(v>100)v=100;
        codec->SetOutputVolume(v);
        self->ShowVolume();
    }
    void ShowVolume() {
        if(!volume_label_)return;
        auto* codec=Board::GetInstance().GetAudioCodec();
        lv_label_set_text_fmt(volume_label_,"%d%%",codec?codec->output_volume():0);
    }
    static void OpenSettings(lv_event_t* e) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(e));
        if(!self || !self->settings_overlay_)return;
        lv_obj_remove_flag(self->settings_overlay_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->settings_overlay_);
    }
    static void ClosePanel(lv_event_t* e) {
        auto* overlay=static_cast<lv_obj_t*>(lv_event_get_user_data(e));
        if(overlay)lv_obj_add_flag(overlay,LV_OBJ_FLAG_HIDDEN);
    }
    static void Start5Point(lv_event_t* e) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(e));
        if(!self || !self->test_overlay_)return;
        self->test_step_=0;
        self->MoveTarget();
        lv_obj_remove_flag(self->test_overlay_,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->test_overlay_);
        ESP_LOGI("Touch5Point","TOUCH_5PT_START: all-screen capture, calibration NOT saved");
    }
    static void Capture5Point(lv_event_t* e) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_event_get_user_data(e));
        if(!self || self->test_step_>=5)return;
        lv_point_t pt={};
        if(lv_indev_active())lv_indev_get_point(lv_indev_active(),&pt);
        ESP_LOGI("Touch5Point","TOUCH_5PT step=%u label=%s x=%ld y=%ld",
                 self->test_step_+1,kNames[self->test_step_],
                 static_cast<long>(pt.x),static_cast<long>(pt.y));
        if(++self->test_step_==5){
            lv_obj_add_flag(self->test_overlay_,LV_OBJ_FLAG_HIDDEN);
            ESP_LOGI("Touch5Point","TOUCH_5PT_COMPLETE: verify coordinates; not stored");
        }else self->MoveTarget();
    }
    void MoveTarget() {
        if(test_step_>=5 || !test_label_ || !test_button_)return;
        lv_obj_set_pos(test_button_,kPoints[test_step_][0],kPoints[test_step_][1]);
        lv_label_set_text_fmt(test_label_,"%u/5 %s",test_step_+1,kNames[test_step_]);
    }
    static void OnTick(lv_timer_t* timer) {
        auto* self=static_cast<TouchSpiDisplay*>(lv_timer_get_user_data(timer));
        if(self)self->UpdateState();
    }
    void UpdateState() {
        if(!face_overlay_ || !face_status_)return;
        auto state=Application::GetInstance().GetDeviceState();
        if(state==kDeviceStateWifiConfiguring || state==kDeviceStateActivating ||
           state==kDeviceStateUpgrading || state==kDeviceStateFatalError){
            // Never hide the activation code or Wi-Fi credentials behind the mascot.
            lv_obj_add_flag(face_overlay_,LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if(!auto_opened_ && !user_closed_ && state==kDeviceStateIdle){
            auto_opened_=true;
            lv_obj_remove_flag(face_overlay_,LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(face_overlay_);
        }
        if(state==last_state_)return;
        last_state_=state;
        const char* title="READY";
        const char* sub="Cham TALK de bat dau";
        const char* eyes="o  o";
        const char* mouth="w";
        uint32_t color=0x61E4FC;
        switch(state){
          case kDeviceStateListening:
            title="LISTENING";sub="Dang lang nghe...";eyes="O  O";mouth="o";break;
          case kDeviceStateSpeaking:
            title="SPEAKING";sub="Chibi dang tra loi";eyes="^  ^";mouth="O";break;
          case kDeviceStateConnecting:
            title="CONNECTING";sub="Dang ket noi";eyes="o  -";mouth=".";color=0xF7B16C;break;
          case kDeviceStateAudioTesting:
            title="AUDIO TEST";sub="Dang kiem tra am thanh";eyes="o  o";mouth=".";break;
          case kDeviceStateNotifying:
            title="NOTIFY";sub="Thong bao moi";eyes="^  ^";mouth="w";break;
          default:break;
        }
        lv_label_set_text(face_status_,title);
        lv_label_set_text(face_subtitle_,sub);
        lv_label_set_text(face_eyes_,eyes);
        lv_label_set_text(face_mouth_,mouth);
        lv_obj_set_style_text_color(face_status_,lv_color_hex(color),0);
    }
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    ~TouchSpiDisplay() override {
        if(refresh_timer_){lv_timer_delete(refresh_timer_);refresh_timer_=nullptr;}
        // GUI objects belong to the LVGL display and are destroyed with it.
    }
    lv_display_t* GetLvglHandle() const {return display_;}
    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        if(face_overlay_ || !lvgl_port_lock(1000))return;
        lv_obj_t* screen=lv_screen_active();

        // Small launcher; original UI remains available with BACK.
        Button(screen,"CHIBI",9,267,96,46,0x156A88,OpenFace,this);
        face_overlay_=Panel(screen,0,0,240,320,0x071B30,0);
        lv_obj_add_flag(face_overlay_,LV_OBJ_FLAG_CLICKABLE);
        Label(face_overlay_,"XiaoZhi AI   CHIBI",14,13,0xC7EBFF);
        Button(face_overlay_,"X",187,2,47,43,0x24506C,CloseFace,this);

        // Chibi avatar is drawn natively with LVGL: small, fast and no full-screen JPEG.
        Panel(face_overlay_,50,66,140,137,0x0A466D,68);
        // Cat ears / headphones / hair cap / blush cheeks.
        Panel(face_overlay_,62,53,47,50,0xA2CFFA,16);
        Panel(face_overlay_,131,53,47,50,0xA2CFFA,16);
        Panel(face_overlay_,70,63,29,32,0x40DDF8,12);
        Panel(face_overlay_,141,63,29,32,0x40DDF8,12);
        Panel(face_overlay_,59,80,122,117,0xB7DAFF,53);
        Panel(face_overlay_,71,108,98,80,0xFFE2D5,40);
        Panel(face_overlay_,52,112,20,48,0x1B5B90,9);
        Panel(face_overlay_,168,112,20,48,0x1B5B90,9);
        Panel(face_overlay_,80,101,80,22,0x9CC9F2,13);
        face_eyes_=Label(face_overlay_,"o  o",97,128,0x154F8D);
        face_mouth_=Label(face_overlay_,"w",116,156,0xC86D62);
        Panel(face_overlay_,78,161,17,8,0xFFADC0,4);
        Panel(face_overlay_,146,161,17,8,0xFFADC0,4);
        Label(face_overlay_,"AI",55,130,0x63E4F3);

        face_status_=Label(face_overlay_,"READY",88,208,0x61E4FC);
        face_subtitle_=Label(face_overlay_,"Cham TALK de bat dau",44,234,0xB6CDDE);
        Button(face_overlay_,"TALK",7,266,72,48,0xCB712A,Talk,this);
        Button(face_overlay_,"VOL",84,266,72,48,0x205477,OpenVolume,this);
        Button(face_overlay_,"SET",161,266,72,48,0x205477,OpenSettings,this);

        volume_overlay_=Panel(screen,0,0,240,320,0x081E33,0);
        lv_obj_add_flag(volume_overlay_,LV_OBJ_FLAG_CLICKABLE);
        Label(volume_overlay_,"AM LUONG",61,30,0xE5F5FF);
        volume_label_=Label(volume_overlay_,"60%",94,131,0x69E6FF);
        Button(volume_overlay_,"-",22,198,88,51,0x275975,VolumeMinus,this);
        Button(volume_overlay_,"+",130,198,88,51,0xC66E30,VolumePlus,this);
        Button(volume_overlay_,"BACK",48,268,144,45,0x1C4D70,ClosePanel,volume_overlay_);
        lv_obj_add_flag(volume_overlay_,LV_OBJ_FLAG_HIDDEN);

        settings_overlay_=Panel(screen,0,0,240,320,0x081E33,0);
        lv_obj_add_flag(settings_overlay_,LV_OBJ_FLAG_CLICKABLE);
        Label(settings_overlay_,"SETTINGS",66,25,0xE8F6FF);
        Label(settings_overlay_,"Voice: XiaoZhi server",17,91,0xB6D6E9);
        Label(settings_overlay_,"Ngon ngu: server TTS",17,124,0xB6D6E9);
        Label(settings_overlay_,"Wi-Fi: dung portal goc",17,157,0xB6D6E9);
        Button(settings_overlay_,"5-POINT TOUCH",19,202,202,50,0x255D82,Start5Point,this);
        Button(settings_overlay_,"BACK",48,267,144,45,0xC66E30,ClosePanel,settings_overlay_);
        lv_obj_add_flag(settings_overlay_,LV_OBJ_FLAG_HIDDEN);

        test_overlay_=Panel(screen,0,0,240,320,0x06182C,0);
        lv_obj_add_flag(test_overlay_,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(test_overlay_,Capture5Point,LV_EVENT_PRESSED,this);
        test_label_=Label(test_overlay_,"1/5",14,12,0xB4E6FF);
        test_button_=Panel(test_overlay_,6,48,44,44,0xFF8929,22);
        Label(test_button_,"+",16,14,0xFFFFFF);
        lv_obj_remove_flag(test_button_,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(test_overlay_,LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(face_overlay_,LV_OBJ_FLAG_HIDDEN);
        refresh_timer_=lv_timer_create(OnTick,350,this);
        UpdateState();
        lvgl_port_unlock();
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
