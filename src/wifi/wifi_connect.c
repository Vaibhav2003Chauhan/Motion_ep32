#include "wifi_connect.h"
#include "config/wifi_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_connect";

static EventGroupHandle_t s_connect_event_group;
#define CONNECTED_BIT BIT0
#define FAIL_BIT      BIT1

static int s_retry_count = 0;
static bool s_connected = false;

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_count < WIFI_MAX_RETRY) {
            s_retry_count++;
            ESP_LOGW(TAG, "Retry %d/%d...", s_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Max retries reached");
            xEventGroupSetBits(s_connect_event_group, FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_connect_event_group, CONNECTED_BIT);
    }
}

esp_err_t wifi_connect_init(void)
{
    s_connect_event_group = xEventGroupCreate();
    if (s_connect_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret;

    ret = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &event_handler, NULL, NULL);
    if (ret != ESP_OK) return ret;

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, NULL, NULL);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

esp_err_t wifi_connect_start(const char *ssid, const char *password)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_retry_count = 0;
    xEventGroupClearBits(s_connect_event_group, CONNECTED_BIT | FAIL_BIT);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    }
    wifi_cfg.sta.threshold.authmode = (password != NULL && password[0] != '\0')
                                      ? WIFI_AUTH_WPA2_PSK
                                      : WIFI_AUTH_OPEN;

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(ret));
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(s_connect_event_group,
                                            CONNECTED_BIT | FAIL_BIT,
                                            pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & CONNECTED_BIT) {
        return ESP_OK;
    } else if (bits & FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to \"%s\"", ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Connection timed out");
    return ESP_ERR_TIMEOUT;
}

bool wifi_connect_is_connected(void)
{
    return s_connected;
}

esp_err_t wifi_connect_disconnect(void)
{
    esp_err_t ret = esp_wifi_disconnect();
    s_connected = false;
    return ret;
}
