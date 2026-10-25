#include "wifi_manager.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"

static const char *TAG = "wifi_mgr";

static bool s_initialized = false;

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret;

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop creation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

void wifi_manager_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    s_initialized = false;
    ESP_LOGI(TAG, "WiFi deinitialized");
}
