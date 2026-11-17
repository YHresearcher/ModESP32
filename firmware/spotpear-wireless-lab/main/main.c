// Spotpear Wireless Lab: isolated experimental firmware, not a XiaoZhi board variant.
// USB-console controlled, no unsafe raw injection or unsolicited connection attempts.
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

static const char *TAG = "SPOTPEAR_LAB";
static uint8_t own_addr_type;
static volatile bool ble_ready;
static volatile bool ble_scanning;
static volatile bool ble_advertising;
static bool ap_running;

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        ESP_LOGI(TAG, "BLE RSSI=%d addr=%02x:%02x:%02x:%02x:%02x:%02x",
            event->disc.rssi,
            event->disc.addr.val[5], event->disc.addr.val[4],
            event->disc.addr.val[3], event->disc.addr.val[2],
            event->disc.addr.val[1], event->disc.addr.val[0]);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ble_scanning = false;
        ESP_LOGI(TAG, "BLE scan complete");
    } else if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
        ble_advertising = false;
        ESP_LOGI(TAG, "BLE advertisement finished");
    }
    return 0;
}

static void ble_sync(void) {
    if (ble_hs_id_infer_auto(0, &own_addr_type) == 0) {
        ble_ready = true;
        ESP_LOGI(TAG, "BLE ready");
    }
}
static void nimble_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}
static void stop_ble(void) {
    if (ble_scanning) { ble_gap_disc_cancel(); ble_scanning = false; }
    if (ble_advertising) { ble_gap_adv_stop(); ble_advertising = false; }
}
static void stop_ap(void) {
    if (!ap_running) return;
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    ap_running = false;
}
static void wifi_scan(void) {
    stop_ap();
    wifi_scan_config_t cfg = {.show_hidden = true};
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Scan: %s", esp_err_to_name(err)); return; }
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    wifi_ap_record_t aps[20] = {0};
    uint16_t take = count > 20 ? 20 : count;
    err = esp_wifi_scan_get_ap_records(&take, aps);
    if (err != ESP_OK) return;
    ESP_LOGI(TAG, "Wi-Fi networks=%u showing=%u", count, take);
    for (unsigned i = 0; i < take; ++i) {
        ESP_LOGI(TAG, "%02u SSID=%s CH=%u RSSI=%d SECURITY=%d",
            i + 1, aps[i].ssid, aps[i].primary, aps[i].rssi, aps[i].authmode);
    }
}
static void wifi_test_ap(void) {
    stop_ap();
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);
    wifi_config_t cfg = {0};
    strcpy((char *)cfg.ap.ssid, "SPOTPEAR_LAB");
    strcpy((char *)cfg.ap.password, "LabOnly2026!");
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.max_connection = 1;
    cfg.ap.channel = 6;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ap_running = true;
    ESP_LOGI(TAG, "Own test AP started; type 'stop' to stop");
}
static void ble_scan(void) {
    if (!ble_ready) { ESP_LOGW(TAG, "BLE not ready"); return; }
    stop_ble();
    struct ble_gap_disc_params p = {0};
    p.passive = 1;
    p.filter_duplicates = 1;
    p.itvl = 0x50;
    p.window = 0x30;
    int rc = ble_gap_disc(own_addr_type, 5000, &p, gap_event, NULL);
    if (rc == 0) ble_scanning = true;
    else ESP_LOGE(TAG, "BLE scan failed: %d", rc);
}
static void ble_test_adv(void) {
    if (!ble_ready) { ESP_LOGW(TAG, "BLE not ready"); return; }
    stop_ble();
    struct ble_hs_adv_fields f = {0};
    const char *name = "SPOTPEAR_LAB";
    f.name = (const uint8_t *)name;
    f.name_len = strlen(name);
    f.name_is_complete = 1;
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) { ESP_LOGE(TAG, "Set BLE advertisement: %d", rc); return; }
    struct ble_gap_adv_params p = {0};
    p.conn_mode = BLE_GAP_CONN_MODE_NON;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, 15000, &p, gap_event, NULL);
    if (rc == 0) ble_advertising = true;
    else ESP_LOGE(TAG, "Start BLE advertisement: %d", rc);
}
void app_main(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    int rc = nimble_port_init();
    if (rc == 0) {
        ble_hs_cfg.sync_cb = ble_sync;
        nimble_port_freertos_init(nimble_task);
    } else ESP_LOGE(TAG, "NimBLE init: %d", rc);
    ESP_LOGI(TAG, "Spotpear Wireless Lab experimental USB console");
    // USB Serial/JTAG's default VFS stdin read is non-blocking; fgets() can
    // repeatedly return EOF and leave the stream in an error/EOF state.
    // Install the interrupt-driven USB driver and read raw bytes instead.
    usb_serial_jtag_driver_config_t usb_cfg = {
        .rx_buffer_size = 512,
        .tx_buffer_size = 512,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(TAG, "USB command RX ready (type help + Enter)");
    ESP_LOGI(TAG, "Commands: wifi | ble | ap | advertise | stop | help");
    char cmd[32] = {0};
    size_t used = 0;
    while (true) {
        char incoming;
        int count = usb_serial_jtag_read_bytes(&incoming, 1, pdMS_TO_TICKS(250));
        if (count <= 0) continue;
        if (incoming == '\r' || incoming == '\n') {
            if (used == 0) continue; // CRLF's second terminator
            cmd[used] = '\0';
            ESP_LOGI(TAG, "Command received: %s", cmd);
            used = 0;
        } else if (incoming == '\b' || incoming == 127) {
            if (used > 0) --used;
            continue;
        } else if (incoming >= 32 && incoming <= 126) {
            if (used < sizeof(cmd) - 1) cmd[used++] = incoming;
            else {
                used = 0;
                ESP_LOGW(TAG, "Command too long; cleared");
            }
            continue;
        } else continue;
        if (strcmp(cmd, "wifi") == 0) wifi_scan();
        else if (strcmp(cmd, "ble") == 0) ble_scan();
        else if (strcmp(cmd, "ap") == 0) wifi_test_ap();
        else if (strcmp(cmd, "advertise") == 0) ble_test_adv();
        else if (strcmp(cmd, "stop") == 0) { stop_ap(); stop_ble(); ESP_LOGI(TAG, "Stopped"); }
        else ESP_LOGI(TAG, "Commands: wifi | ble | ap | advertise | stop | help");
    }
}
