#ifndef SHELL_H
#define SHELL_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    CMD_SCAN,
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_STATUS,
    CMD_HELP,
    CMD_VERSION,
    CMD_REBOOT,
} shell_cmd_id_t;

typedef struct {
    shell_cmd_id_t id;
    char arg1[33];
    char arg2[64];
} shell_cmd_t;

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char ssid[33];
    char ip[16];
    int8_t rssi;
} shell_status_t;

esp_err_t shell_init(QueueHandle_t cmd_queue, const shell_status_t *status);
void shell_task(void *param);

#endif
