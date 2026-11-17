#pragma once
#include "esp_err.h"

// Display-only bring-up for the Spotpear ESP32-S3 1.28" BOX GC9A01.
// Pin mappings are board-specific; do not use for other round ESP32 boards.
esp_err_t spotpear_lcd_init(void);
esp_err_t spotpear_lcd_show_home(void);
