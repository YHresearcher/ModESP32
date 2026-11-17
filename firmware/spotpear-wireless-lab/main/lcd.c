// Spotpear ESP32-AI-1.28 v1.0 (non-touch) GC9A01 bring-up (ESP-IDF 5.3).
// SpotPear ESP32-AI-1.28 v1.0 (non-touch) pin map, NOT the BOX touchscreen.
// Source: tomorrow56/xiaozhi-esp32 at fed7f146, main/boards/spotpear/spotpear-esp32-ai-1.28/config.h
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"

#define LCD_W 240
#define LCD_H 240
#define LCD_SPI_HOST SPI3_HOST
#define LCD_PIN_MOSI 17
#define LCD_PIN_CLK 14
#define LCD_PIN_CS 13
#define LCD_PIN_DC 10
#define LCD_PIN_RST 18
#define LCD_PIN_BL 3
#define RGB565(r,g,b) (uint16_t)((((r)&0xf8)<<8)|(((g)&0xfc)<<3)|((b)>>3))

static const char *TAG = "SPOTPEAR_LCD";
static esp_lcd_panel_handle_t panel;
static uint16_t *framebuffer;

// Small uppercase 5x7 glyphs; each byte stores one column, low bit at top.
// Undefined glyphs render as blank, avoiding a heavyweight font dependency.
typedef struct { char ch; uint8_t col[5]; } glyph_t;
static const glyph_t font[] = {
  {'A',{0x7e,0x11,0x11,0x11,0x7e}}, {'B',{0x7f,0x49,0x49,0x49,0x36}},
  {'C',{0x3e,0x41,0x41,0x41,0x22}}, {'D',{0x7f,0x41,0x41,0x22,0x1c}},
  {'E',{0x7f,0x49,0x49,0x49,0x41}}, {'F',{0x7f,0x09,0x09,0x09,0x01}},
  {'G',{0x3e,0x41,0x49,0x49,0x7a}}, {'H',{0x7f,0x08,0x08,0x08,0x7f}},
  {'I',{0x00,0x41,0x7f,0x41,0x00}}, {'J',{0x20,0x40,0x41,0x3f,0x01}},
  {'K',{0x7f,0x08,0x14,0x22,0x41}}, {'L',{0x7f,0x40,0x40,0x40,0x40}},
  {'M',{0x7f,0x02,0x0c,0x02,0x7f}}, {'N',{0x7f,0x04,0x08,0x10,0x7f}},
  {'O',{0x3e,0x41,0x41,0x41,0x3e}}, {'P',{0x7f,0x09,0x09,0x09,0x06}},
  {'Q',{0x3e,0x41,0x51,0x21,0x5e}}, {'R',{0x7f,0x09,0x19,0x29,0x46}},
  {'S',{0x46,0x49,0x49,0x49,0x31}}, {'T',{0x01,0x01,0x7f,0x01,0x01}},
  {'U',{0x3f,0x40,0x40,0x40,0x3f}}, {'V',{0x1f,0x20,0x40,0x20,0x1f}},
  {'W',{0x7f,0x20,0x18,0x20,0x7f}}, {'X',{0x63,0x14,0x08,0x14,0x63}},
  {'Y',{0x03,0x04,0x78,0x04,0x03}}, {'Z',{0x61,0x51,0x49,0x45,0x43}},
  {'0',{0x3e,0x51,0x49,0x45,0x3e}}, {'1',{0x00,0x42,0x7f,0x40,0x00}},
  {'2',{0x42,0x61,0x51,0x49,0x46}}, {'3',{0x21,0x41,0x45,0x4b,0x31}},
  {'4',{0x18,0x14,0x12,0x7f,0x10}}, {'5',{0x27,0x45,0x45,0x45,0x39}},
  {'6',{0x3c,0x4a,0x49,0x49,0x30}}, {'7',{0x01,0x71,0x09,0x05,0x03}},
  {'8',{0x36,0x49,0x49,0x49,0x36}}, {'9',{0x06,0x49,0x49,0x29,0x1e}},
  {'-',{0x08,0x08,0x08,0x08,0x08}}, {'.',{0,0,0x60,0,0}},
  {'/',{0x40,0x30,0x08,0x06,0x01}}, {' ',{0,0,0,0,0}}
};
static const uint8_t *glyph_for(char c) {
    for (unsigned i=0; i<sizeof(font)/sizeof(font[0]); ++i)
        if(font[i].ch==c) return font[i].col;
    return font[sizeof(font)/sizeof(font[0])-1].col;
}
static inline uint16_t spi_rgb565(uint16_t color) { return (uint16_t)((color << 8) | (color >> 8)); }
static void pixel(int x,int y,uint16_t color) {
    if (x>=0 && y>=0 && x<LCD_W && y<LCD_H) framebuffer[y*LCD_W+x]=spi_rgb565(color);
}
static void filled_circle(int cx,int cy,int r,uint16_t color) {
    for (int y=-r;y<=r;++y) {
        for (int x=-r;x<=r;++x) {
            if (x*x+y*y<=r*r) pixel(cx+x,cy+y,color);
        }
    }
}
static void text_at(const char *s,int x,int y,int scale,uint16_t color) {
    for (; *s; ++s,x+=6*scale) {
        const uint8_t *cols=glyph_for(*s);
        for (int col=0;col<5;++col)
          for (int row=0;row<7;++row)
            if (cols[col] & (1u<<row))
              for(int dy=0;dy<scale;++dy)
                for(int dx=0;dx<scale;++dx)
                  pixel(x+col*scale+dx,y+row*scale+dy,color);
    }
}
static void center_text(const char *s,int y,int scale,uint16_t color) {
    int len=(int)strlen(s);
    text_at(s,(LCD_W-(len*6-1)*scale)/2,y,scale,color);
}

esp_err_t spotpear_lcd_init(void) {
    ESP_LOGI(TAG,"Initializing GC9A01 SPI; MOSI=%d SCLK=%d CS=%d DC=%d RST=%d BL=%d",
      LCD_PIN_MOSI,LCD_PIN_CLK,LCD_PIN_CS,LCD_PIN_DC,LCD_PIN_RST,LCD_PIN_BL);
    // ESP32-AI-1.28 v1.0: GPIO3 is an active-HIGH backlight control.\n    // Keep BL continuously enabled for LCD diagnosis (no deliberate blanking).
    gpio_config_t bl_cfg={
       .pin_bit_mask=1ULL<<LCD_PIN_BL,.mode=GPIO_MODE_OUTPUT,
       .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,
       .intr_type=GPIO_INTR_DISABLE
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg),TAG,"BL GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_BL,1),TAG,"BL on at startup");\n    ESP_LOGI(TAG,"LCD_BL_ENABLED: gpio=%d output=%d (expected 1)", LCD_PIN_BL, gpio_get_level(LCD_PIN_BL));

    spi_bus_config_t bus={
        .mosi_io_num=LCD_PIN_MOSI,.miso_io_num=-1,.sclk_io_num=LCD_PIN_CLK,
        .quadwp_io_num=-1,.quadhd_io_num=-1,
        .max_transfer_sz=LCD_W*LCD_H*2+8,
    };
    esp_err_t e=spi_bus_initialize(LCD_SPI_HOST,&bus,SPI_DMA_CH_AUTO);
    if(e!=ESP_OK)return e;

    esp_lcd_panel_io_handle_t io=NULL;
    esp_lcd_panel_io_spi_config_t io_cfg={
      .dc_gpio_num=LCD_PIN_DC,.cs_gpio_num=LCD_PIN_CS,
      .pclk_hz=10*1000*1000,.lcd_cmd_bits=8,.lcd_param_bits=8,
      .spi_mode=0,.trans_queue_depth=2,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,&io_cfg,&io),TAG,"panel io");
    esp_lcd_panel_dev_config_t panel_cfg={
      .reset_gpio_num=LCD_PIN_RST,
      .rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR,
      .bits_per_pixel=16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(io,&panel_cfg,&panel),TAG,"gc9a01");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel),TAG,"panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel),TAG,"panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel,true),TAG,"panel invert");
    // Hardware-tested V1.0 reference uses no mirroring.\n    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel,false,false),TAG,"panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel,true),TAG,"panel on");
    framebuffer=heap_caps_malloc(LCD_W*LCD_H*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!framebuffer) framebuffer=heap_caps_malloc(LCD_W*LCD_H*2,MALLOC_CAP_8BIT);
    if(!framebuffer) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG,"GC9A01 initialized; framebuffer ready");
    return ESP_OK;
}
esp_err_t spotpear_lcd_show_home(void) {
    if(!panel||!framebuffer) return ESP_ERR_INVALID_STATE;
    const uint16_t bg=RGB565(10,22,37), outer=RGB565(24,49,69),
                   bright=RGB565(44,218,166), white=RGB565(239,247,251),
                   muted=RGB565(137,176,193);
    for(int i=0;i<LCD_W*LCD_H;++i)framebuffer[i]=spi_rgb565(bg);
    filled_circle(120,120,116,outer);
    filled_circle(120,120,110,bg);
    filled_circle(120,75,24,bright);
    filled_circle(120,75,17,bg);
    filled_circle(120,75,9,bright);
    center_text("SPOTPEAR",111,2,white);
    center_text("WIRELESS LAB",137,1,bright);
    center_text("WIFI  BLE",165,1,muted);
    center_text("USB READY",185,1,white);
    // RGB565 values are byte-swapped in the buffer for MSB-first LCD SPI transfer.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel,0,0,LCD_W,LCD_H,framebuffer),TAG,"LCD draw");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_BL,1),TAG,"BL on");
    ESP_LOGI(TAG,"LCD_HOME_SHOWN");
    return ESP_OK;
}

/*
 * LCD Diagnostic V0.2: deterministic full-frame RGB565 tests without glyphs,
 * GUI layers or any Wi-Fi dependencies. No panel readback on write-only SPI.
 * Keep the same DMA-capable framebuffer alive across transfers and allow
 * each async SPI transfer time to finish before changing its contents.
 */
esp_err_t spotpear_lcd_diagnostic(void) {
    if (!panel || !framebuffer) return ESP_ERR_INVALID_STATE;
    const struct { const char *name; uint16_t rgb; } tests[] = {
        {"RED", RGB565(255, 0, 0)},
        {"GREEN", RGB565(0, 255, 0)},
        {"BLUE", RGB565(0, 0, 255)},
        {"WHITE", RGB565(255, 255, 255)}
    };
    ESP_LOGI(TAG, "LCD_DIAG_BEGIN: SPI=10MHz mode=0 panel=GC9A01");
    for (unsigned k = 0; k < sizeof(tests)/sizeof(tests[0]); ++k) {
        const uint16_t value = spi_rgb565(tests[k].rgb);
        for (int i = 0; i < LCD_W * LCD_H; ++i) framebuffer[i] = value;
        ESP_LOGI(TAG, "LCD_DIAG_SEND: %s", tests[k].name);
        esp_err_t e = esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, framebuffer);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "LCD_DIAG_FAILED: %s err=%s", tests[k].name, esp_err_to_name(e));
            return e;
        }
        ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_BL, 1), TAG, "backlight on");\n        ESP_LOGI(TAG, "LCD_DIAG_BL_STATUS: gpio=%d output=%d (expected 1)", LCD_PIN_BL, gpio_get_level(LCD_PIN_BL));
        // 115200 bytes at 10 MHz is ~92 ms; leave margin for queued SPI DMA.
        vTaskDelay(pdMS_TO_TICKS(1100));
        ESP_LOGI(TAG, "LCD_DIAG_TX_SUBMITTED: %s (display not read back)", tests[k].name);
    }
    ESP_LOGI(TAG, "LCD_DIAG_DONE: four colors transmitted");
    return ESP_OK;
}
