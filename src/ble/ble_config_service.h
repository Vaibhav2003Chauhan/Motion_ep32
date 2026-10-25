#ifndef BLE_CONFIG_SERVICE_H
#define BLE_CONFIG_SERVICE_H

#include "esp_err.h"
#include <stdint.h>

// Device status pushed via BLE notify
typedef struct {
    float roll;
    float pitch;
    float tilt;
    uint8_t crash_state;  // 0=NORMAL, 1=DETECTING, 2=CONFIRMED, 3=COOLDOWN
} __attribute__((packed)) ble_device_status_t;

esp_err_t ble_config_service_init(const char *device_name);
void      ble_config_service_notify_status(const ble_device_status_t *status);

// Callback: called from BLE context when orientation is written
typedef void (*ble_orient_write_cb_t)(float roll, float pitch, float yaw);
void ble_config_service_set_orient_callback(ble_orient_write_cb_t cb);

// Callback: called from BLE context when crash threshold is written
typedef void (*ble_threshold_write_cb_t)(float threshold_deg);
void ble_config_service_set_threshold_callback(ble_threshold_write_cb_t cb);

#endif
