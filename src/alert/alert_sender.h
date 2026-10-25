#ifndef ALERT_SENDER_H
#define ALERT_SENDER_H

#include "esp_err.h"

esp_err_t alert_sender_init(void);
esp_err_t alert_send_crash(float tilt_angle, float ax, float ay, float az);

#endif
