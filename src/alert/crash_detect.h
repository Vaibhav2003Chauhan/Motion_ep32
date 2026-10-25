#ifndef CRASH_DETECT_H
#define CRASH_DETECT_H

#include "esp_err.h"
#include "lis3dh/lis3dh.h"

typedef enum {
    CRASH_STATE_NORMAL,
    CRASH_STATE_DETECTING,
    CRASH_STATE_CONFIRMED,
    CRASH_STATE_COOLDOWN,
} crash_state_t;

typedef struct {
    crash_state_t state;
    uint8_t       confirm_count;
    float         last_tilt;
    float         threshold_deg;
    uint32_t      last_alert_tick;
} crash_ctx_t;

void crash_detect_init(crash_ctx_t *ctx);
void crash_detect_set_threshold(crash_ctx_t *ctx, float threshold_deg);
bool crash_detect_check(crash_ctx_t *ctx, const lis3dh_data_t *data);

#endif
