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
#include <driver/spi_common.h>
#include <driver/gpio.h>
#ifdef CONFIG_CHIBI_XPT2046_TOUCH
#include <esp_lcd_touch_xpt2046.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#endif

#define TAG "ModEsp32S3St7789v"

class ModEsp32S3St7789vBoard : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_ = nullptr;
#ifdef CONFIG_CHIBI_XPT2046_TOUCH
    esp_lcd_touch_handle_t touch_ = nullptr;
    lv_indev_t* touch_indev_ = nullptr;
#endif
    void InitializeBacklight() {
        if (DISPLAY_BACKLIGHT_GPIO == GPIO_NUM_NC) {
            ESP_LOGW(TAG, "No LCD backlight GPIO configured; verify LED power/wiring");
            return;
        }
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << DISPLAY_BACKLIGHT_GPIO;
        cfg.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_BACKLIGHT_GPIO, DISPLAY_BACKLIGHT_ACTIVE_LEVEL));
    }
#ifdef CONFIG_CHIBI_XPT2046_TOUCH
    void InitializeTouch() {
        // Use existing LCD SPI2 bus. Resistive touch refresh is handled
        // by esp_lvgl_port; do not block the audio/GUI task with touch reads.
        esp_lcd_panel_io_spi_config_t tp_io_cfg =
            ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(TOUCH_SPI_CS_PIN);
        esp_lcd_panel_io_handle_t tp_io = nullptr;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &tp_io_cfg, &tp_io));
        esp_lcd_touch_config_t tp_cfg = {};
        tp_cfg.x_max = DISPLAY_WIDTH;
        tp_cfg.y_max = DISPLAY_HEIGHT;
        tp_cfg.rst_gpio_num = GPIO_NUM_NC;
        tp_cfg.int_gpio_num = TOUCH_IRQ_PIN;
        tp_cfg.flags.swap_xy = DISPLAY_SWAP_XY;
        tp_cfg.flags.mirror_x = DISPLAY_MIRROR_X;
        tp_cfg.flags.mirror_y = DISPLAY_MIRROR_Y;
        ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io, &tp_cfg, &touch_));
        lvgl_port_touch_cfg_t touch_cfg = {};
        touch_cfg.disp = lv_display_get_default();
        touch_cfg.handle = touch_;
        touch_indev_ = lvgl_port_add_touch(&touch_cfg);
        if (touch_indev_ == nullptr) {
            ESP_LOGE(TAG, "Failed to register touch input");
        } else {
            ESP_LOGI(TAG, "XPT2046 touch registered; verify calibration/orientation");
        }
    }
#endif

    void InitializeDisplay() {
        spi_bus_config_t bus = {};
        bus.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        bus.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        bus.miso_io_num = DISPLAY_SPI_MISO_PIN;
        bus.quadwp_io_num = GPIO_NUM_NC;
        bus.quadhd_io_num = GPIO_NUM_NC;
        bus.max_transfer_sz = DISPLAY_WIDTH * 40 * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t io = nullptr;
        esp_lcd_panel_io_spi_config_t io_cfg = {};
        io_cfg.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_cfg.dc_gpio_num = DISPLAY_DC_PIN;
        io_cfg.spi_mode = 0;
        io_cfg.pclk_hz = 20 * 1000 * 1000;
        io_cfg.trans_queue_depth = 10;
        io_cfg.lcd_cmd_bits = 8;
        io_cfg.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io));

        esp_lcd_panel_handle_t panel = nullptr;
        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = DISPLAY_RST_PIN;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, 
#ifdef CONFIG_CHIBI_LCD_INVERT
            true
#else
            false
#endif
        ));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
        display_ = new SpiLcdDisplay(io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        InitializeBacklight();
#ifdef CONFIG_CHIBI_XPT2046_TOUCH
        InitializeTouch();
#endif
        ESP_LOGI(TAG, "ST7789V panel initialized; backlight pin=%d touch=%d",
                 static_cast<int>(DISPLAY_BACKLIGHT_GPIO),
#ifdef CONFIG_CHIBI_XPT2046_TOUCH
                 1
#else
                 0
#endif
        );
    }

public:
    ModEsp32S3St7789vBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplay();
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

DECLARE_BOARD(ModEsp32S3St7789vBoard);
