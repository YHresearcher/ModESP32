idf_component_register(SRCS "main.c" "lcd.c" INCLUDE_DIRS "." REQUIRES esp_wifi esp_event esp_netif nvs_flash bt driver esp_lcd esp_driver_gpio esp_driver_spi)
