#include <math.h>
#include "orientation.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "orientation";

#define DEG2RAD(d) ((d) * (float)M_PI / 180.0f)

#define NVS_NAMESPACE   "orient"
#define NVS_KEY_ROLL    "roll"
#define NVS_KEY_PITCH   "pitch"
#define NVS_KEY_YAW     "yaw"

// -------------------- NVS helpers --------------------

static esp_err_t nvs_load_angles(float *roll, float *pitch, float *yaw)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;

    int16_t r, p, y;
    ret = nvs_get_i16(h, NVS_KEY_ROLL, &r);
    if (ret != ESP_OK) { nvs_close(h); return ret; }
    ret = nvs_get_i16(h, NVS_KEY_PITCH, &p);
    if (ret != ESP_OK) { nvs_close(h); return ret; }
    ret = nvs_get_i16(h, NVS_KEY_YAW, &y);
    if (ret != ESP_OK) { nvs_close(h); return ret; }

    nvs_close(h);
    *roll  = (float)r;
    *pitch = (float)p;
    *yaw   = (float)y;
    return ESP_OK;
}

static esp_err_t nvs_save_angles(float roll, float pitch, float yaw)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_i16(h, NVS_KEY_ROLL,  (int16_t)roll);
    nvs_set_i16(h, NVS_KEY_PITCH, (int16_t)pitch);
    nvs_set_i16(h, NVS_KEY_YAW,   (int16_t)yaw);
    ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Orientation saved to NVS: %.0f° %.0f° %.0f°", roll, pitch, yaw);
    }
    return ret;
}

// -------------------- Rotation matrix --------------------

static void build_rotation_matrix(orientation_ctx_t *ctx)
{
    float cr = cosf(DEG2RAD(ctx->roll_deg));
    float sr = sinf(DEG2RAD(ctx->roll_deg));
    float cp = cosf(DEG2RAD(ctx->pitch_deg));
    float sp = sinf(DEG2RAD(ctx->pitch_deg));
    float cy = cosf(DEG2RAD(ctx->yaw_deg));
    float sy = sinf(DEG2RAD(ctx->yaw_deg));

    // R = Rz(yaw) * Ry(pitch) * Rx(roll)  — ZYX intrinsic
    ctx->R.m[0][0] = cy * cp;
    ctx->R.m[0][1] = cy * sp * sr - sy * cr;
    ctx->R.m[0][2] = cy * sp * cr + sy * sr;

    ctx->R.m[1][0] = sy * cp;
    ctx->R.m[1][1] = sy * sp * sr + cy * cr;
    ctx->R.m[1][2] = sy * sp * cr - cy * sr;

    ctx->R.m[2][0] = -sp;
    ctx->R.m[2][1] = cp * sr;
    ctx->R.m[2][2] = cp * cr;
}

// -------------------- Public API --------------------

esp_err_t orientation_init(orientation_ctx_t *ctx, float default_roll, float default_pitch, float default_yaw)
{
    if (ctx == NULL) return ESP_ERR_INVALID_ARG;

    // Init NVS flash (safe to call multiple times)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    float r, p, y;
    ret = nvs_load_angles(&r, &p, &y);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded orientation from NVS: %.0f° %.0f° %.0f°", r, p, y);
        ctx->roll_deg  = r;
        ctx->pitch_deg = p;
        ctx->yaw_deg   = y;
    } else {
        ESP_LOGI(TAG, "No orientation in NVS — using defaults: %.0f° %.0f° %.0f°",
                 default_roll, default_pitch, default_yaw);
        ctx->roll_deg  = default_roll;
        ctx->pitch_deg = default_pitch;
        ctx->yaw_deg   = default_yaw;
        nvs_save_angles(default_roll, default_pitch, default_yaw);
    }

    build_rotation_matrix(ctx);

    ESP_LOGI(TAG, "Mounting orientation: Roll=%.0f°  Pitch=%.0f°  Yaw=%.0f°",
             ctx->roll_deg, ctx->pitch_deg, ctx->yaw_deg);
    return ESP_OK;
}

esp_err_t orientation_update(orientation_ctx_t *ctx, float roll_deg, float pitch_deg, float yaw_deg)
{
    if (ctx == NULL) return ESP_ERR_INVALID_ARG;

    ctx->roll_deg  = roll_deg;
    ctx->pitch_deg = pitch_deg;
    ctx->yaw_deg   = yaw_deg;
    build_rotation_matrix(ctx);
    nvs_save_angles(roll_deg, pitch_deg, yaw_deg);

    ESP_LOGI(TAG, "Orientation updated: Roll=%.0f°  Pitch=%.0f°  Yaw=%.0f°",
             roll_deg, pitch_deg, yaw_deg);
    return ESP_OK;
}

void orientation_apply(const orientation_ctx_t *ctx, const float sensor[3], float world[3])
{
    for (int i = 0; i < 3; i++) {
        world[i] = ctx->R.m[i][0] * sensor[0]
                 + ctx->R.m[i][1] * sensor[1]
                 + ctx->R.m[i][2] * sensor[2];
    }
}

void orientation_print(const orientation_ctx_t *ctx)
{
    ESP_LOGI(TAG, "Rotation matrix:");
    for (int r = 0; r < 3; r++) {
        ESP_LOGI(TAG, "  [%+.4f  %+.4f  %+.4f]",
                 ctx->R.m[r][0], ctx->R.m[r][1], ctx->R.m[r][2]);
    }
}
