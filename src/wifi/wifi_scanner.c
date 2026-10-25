#include "wifi_scanner.h"

#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_scanner";

static EventGroupHandle_t s_scan_event_group;
#define SCAN_DONE_BIT BIT0

static const char *auth_mode_to_str(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2_PSK";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENT";
    default:                        return "UNKNOWN";
    }
}

static void scan_done_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(s_scan_event_group, SCAN_DONE_BIT);
    }
}

esp_err_t wifi_scanner_init(void)
{
    s_scan_event_group = xEventGroupCreate();
    if (s_scan_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                                         &scan_done_handler, NULL, NULL);
    if (ret != ESP_OK) {
        vEventGroupDelete(s_scan_event_group);
        s_scan_event_group = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t wifi_scanner_start(wifi_scan_list_t *out_list)
{
    if (out_list == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_list, 0, sizeof(wifi_scan_list_t));
    xEventGroupClearBits(s_scan_event_group, SCAN_DONE_BIT);

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Scanning...");

    EventBits_t bits = xEventGroupWaitBits(s_scan_event_group, SCAN_DONE_BIT,
                                            pdTRUE, pdTRUE, pdMS_TO_TICKS(15000));
    if (!(bits & SCAN_DONE_BIT)) {
        ESP_LOGE(TAG, "Scan timed out");
        esp_wifi_scan_stop();
        return ESP_ERR_TIMEOUT;
    }

    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t fetch_count = (ap_count > WIFI_SCAN_MAX_AP) ? WIFI_SCAN_MAX_AP : ap_count;
    wifi_ap_record_t ap_records[WIFI_SCAN_MAX_AP];

    ret = esp_wifi_scan_get_ap_records(&fetch_count, ap_records);
    if (ret != ESP_OK) {
        return ret;
    }

    out_list->count = fetch_count;
    for (uint16_t i = 0; i < fetch_count; i++) {
        memcpy(out_list->entries[i].ssid, ap_records[i].ssid, sizeof(ap_records[i].ssid));
        out_list->entries[i].ssid[32] = '\0';
        out_list->entries[i].rssi = ap_records[i].rssi;
        out_list->entries[i].auth_mode = ap_records[i].authmode;
        out_list->entries[i].channel = ap_records[i].primary;
        memcpy(out_list->entries[i].bssid, ap_records[i].bssid, 6);
    }

    ESP_LOGI(TAG, "Found %d networks", fetch_count);
    return ESP_OK;
}

void wifi_scanner_print_results(const wifi_scan_list_t *list)
{
    if (list == NULL || list->count == 0) {
        ESP_LOGW(TAG, "No networks found");
        return;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=======================================================================");
    ESP_LOGI(TAG, " #  | SSID                             | RSSI | CH | Auth          | BSSID");
    ESP_LOGI(TAG, "-----------------------------------------------------------------------");

    for (uint16_t i = 0; i < list->count; i++) {
        const wifi_scan_result_t *e = &list->entries[i];
        const char *ssid = (e->ssid[0] == '\0') ? "<hidden>" : (const char *)e->ssid;

        ESP_LOGI(TAG, " %2d | %-32s | %4d | %2d | %-13s | %02X:%02X:%02X:%02X:%02X:%02X",
                 i + 1, ssid, e->rssi, e->channel,
                 auth_mode_to_str(e->auth_mode),
                 e->bssid[0], e->bssid[1], e->bssid[2],
                 e->bssid[3], e->bssid[4], e->bssid[5]);
    }

    ESP_LOGI(TAG, "=======================================================================");
}

void wifi_scanner_deinit(void)
{
    if (s_scan_event_group != NULL) {
        vEventGroupDelete(s_scan_event_group);
        s_scan_event_group = NULL;
    }
}
