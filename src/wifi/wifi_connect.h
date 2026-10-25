#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "esp_err.h"

esp_err_t wifi_connect_init(void);
esp_err_t wifi_connect_start(const char *ssid, const char *password);
bool wifi_connect_is_connected(void);
esp_err_t wifi_connect_disconnect(void);

#endif
