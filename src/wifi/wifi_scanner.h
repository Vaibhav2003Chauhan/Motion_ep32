#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include "esp_err.h"
#include "esp_wifi_types.h"

#define WIFI_SCAN_MAX_AP 20

typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_auth_mode_t auth_mode;
    uint8_t channel;
    uint8_t bssid[6];
} wifi_scan_result_t;

typedef struct {
    wifi_scan_result_t entries[WIFI_SCAN_MAX_AP];
    uint16_t count;
} wifi_scan_list_t;

esp_err_t wifi_scanner_init(void);
esp_err_t wifi_scanner_start(wifi_scan_list_t *out_list);
void wifi_scanner_print_results(const wifi_scan_list_t *list);
void wifi_scanner_deinit(void);

#endif
