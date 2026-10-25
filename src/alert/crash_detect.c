#include "crash_detect.h"
#include "config/alert_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "crash";

void crash_detect_init(crash_ctx_t *ctx)
{
    ctx->state          = CRASH_STATE_NORMAL;
    ctx->confirm_count  = 0;
    ctx->last_tilt      = 0.0f;
    ctx->threshold_deg  = CRASH_TILT_THRESHOLD_DEG;
    ctx->last_alert_tick = 0;
    ESP_LOGI(TAG, "Crash detect init — threshold=%.1f° confirm=%d cooldown=%ds",
             ctx->threshold_deg, CRASH_CONFIRM_COUNT, CRASH_COOLDOWN_SEC);
}

void crash_detect_set_threshold(crash_ctx_t *ctx, float threshold_deg)
{
    ctx->threshold_deg = threshold_deg;
    ESP_LOGI(TAG, "Crash threshold updated: %.1f°", threshold_deg);
}

bool crash_detect_check(crash_ctx_t *ctx, const lis3dh_data_t *data)
{
    ctx->last_tilt = data->tilt;
    uint32_t now = xTaskGetTickCount();

    switch (ctx->state) {

    case CRASH_STATE_NORMAL:
        if (data->tilt > ctx->threshold_deg) {
            ctx->confirm_count = 1;
            ctx->state = CRASH_STATE_DETECTING;
            ESP_LOGW(TAG, "Tilt %.1f° > %.1f° — detecting (%d/%d)",
                     data->tilt, ctx->threshold_deg, ctx->confirm_count, CRASH_CONFIRM_COUNT);
        }
        break;

    case CRASH_STATE_DETECTING:
        if (data->tilt > ctx->threshold_deg) {
            ctx->confirm_count++;
            ESP_LOGW(TAG, "Tilt %.1f° — confirming (%d/%d)",
                     data->tilt, ctx->confirm_count, CRASH_CONFIRM_COUNT);
            if (ctx->confirm_count >= CRASH_CONFIRM_COUNT) {
                ctx->state = CRASH_STATE_CONFIRMED;
                ctx->last_alert_tick = now;
                ESP_LOGE(TAG, "*** CRASH CONFIRMED *** tilt=%.1f°", data->tilt);
                return true;
            }
        } else {
            ctx->confirm_count = 0;
            ctx->state = CRASH_STATE_NORMAL;
            ESP_LOGI(TAG, "Tilt back to %.1f° — false alarm", data->tilt);
        }
        break;

    case CRASH_STATE_CONFIRMED:
        ctx->state = CRASH_STATE_COOLDOWN;
        break;

    case CRASH_STATE_COOLDOWN:
        if ((now - ctx->last_alert_tick) > pdMS_TO_TICKS(CRASH_COOLDOWN_SEC * 1000)) {
            ctx->state = CRASH_STATE_NORMAL;
            ctx->confirm_count = 0;
            ESP_LOGI(TAG, "Cooldown expired — monitoring again");
        }
        break;
    }

    return false;
}
