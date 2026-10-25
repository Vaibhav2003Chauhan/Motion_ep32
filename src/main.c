#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config/version.h"
#include "config/alert_config.h"
#include "lis3dh/lis3dh.h"
#include "lis3dh/orientation.h"
#include "alert/crash_detect.h"
#include "alert/alert_sender.h"
#include "util/serial_input.h"
#include "ble/ble_config_service.h"

static const char *TAG = "main";

#define ORIENTATION_CHECK_TOLERANCE  20.0f
#define BLE_DEVICE_NAME  "LIS3DH-CrashMon"

static crash_ctx_t s_crash_ctx;
static orientation_ctx_t s_orient;
static bool s_alert_enabled = false;
static volatile float s_crash_threshold = CRASH_TILT_THRESHOLD_DEG;

// Mango pattern: flag set by serial/BLE, picked up by IMU task
static volatile bool s_calibrate_flag = false;
static float s_new_roll, s_new_pitch, s_new_yaw;

// BLE callback: orientation written from phone
static void on_ble_orient_write(float roll, float pitch, float yaw)
{
    s_new_roll  = roll;
    s_new_pitch = pitch;
    s_new_yaw   = yaw;
    s_calibrate_flag = true;
    ESP_LOGI(TAG, "BLE: orientation set to %.0f %.0f %.0f", roll, pitch, yaw);
}

// BLE callback: crash threshold written from phone
static void on_ble_threshold_write(float threshold_deg)
{
    s_crash_threshold = threshold_deg;
    crash_detect_set_threshold(&s_crash_ctx, threshold_deg);
    ESP_LOGI(TAG, "BLE: crash threshold set to %.1f°", threshold_deg);
}

static bool verify_orientation(const lis3dh_data_t *data)
{
    ESP_LOGI(TAG, "Expected orientation: X:0g  Y:0g  Z:1g  (flat, tilt ~0°)");
    ESP_LOGI(TAG, "Actual:  X:%+.3fg  Y:%+.3fg  Z:%+.3fg  Tilt:%.1f°",
             data->g[LIS3DH_X_IDX], data->g[LIS3DH_Y_IDX], data->g[LIS3DH_Z_IDX], data->tilt);

    if (data->tilt > ORIENTATION_CHECK_TOLERANCE) {
        ESP_LOGW(TAG, "Tilt %.1f° at startup — sensor may not be mounted flat!", data->tilt);
        return false;
    }
    ESP_LOGI(TAG, "Orientation OK — tilt %.1f° within tolerance", data->tilt);
    return true;
}

static void serial_cmd_task(void *param)
{
    char buf[64];

    ESP_LOGI(TAG, "Serial commands ready. Type 'help' for available commands.");

    while (1) {
        int len = serial_input_read_line("> ", buf, sizeof(buf), 60000);
        if (len <= 0) continue;

        if (strncmp(buf, "SET_ORIENT ", 11) == 0) {
            float r, p, y;
            if (sscanf(buf + 11, "%f %f %f", &r, &p, &y) == 3) {
                s_new_roll  = r;
                s_new_pitch = p;
                s_new_yaw   = y;
                s_calibrate_flag = true;
                ESP_LOGI(TAG, "CMD: SET_ORIENT %.0f %.0f %.0f — will apply next cycle", r, p, y);
            } else {
                printf("Usage: SET_ORIENT <roll> <pitch> <yaw>\n");
                printf("Example: SET_ORIENT 0 0 90\n");
            }
        } else if (strcmp(buf, "GET_ORIENT") == 0) {
            printf("Current orientation: Roll=%.0f  Pitch=%.0f  Yaw=%.0f\n",
                   s_orient.roll_deg, s_orient.pitch_deg, s_orient.yaw_deg);
            orientation_print(&s_orient);
        } else if (strcmp(buf, "help") == 0) {
            printf("Available commands:\n");
            printf("  SET_ORIENT <roll> <pitch> <yaw>  — Set mounting orientation (saved to NVS)\n");
            printf("  GET_ORIENT                       — Show current orientation & matrix\n");
            printf("  help                             — Show this help\n");
        } else {
            printf("Unknown command: '%s'. Type 'help'.\n", buf);
        }
    }
}

// Auto-calibration: compute raw roll/pitch/yaw from g-values (no rotation matrix)
static void compute_raw_angles(const lis3dh_data_t *data, float *roll, float *pitch, float *yaw)
{
    float ax = data->g[LIS3DH_X_IDX];
    float ay = data->g[LIS3DH_Y_IDX];
    float az = data->g[LIS3DH_Z_IDX];

    *roll  = atan2f(ay, az) * (180.0f / (float)M_PI);
    *pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * (180.0f / (float)M_PI);
    *yaw   = s_orient.yaw_deg;  // yaw can't be derived from accelerometer alone
}

static void lis3dh_task(void *param)
{
    lis3dh_data_t data;

#if AUTO_CALIB_ENABLE
    float ref_roll = 0, ref_pitch = 0;
    uint32_t stable_count = 0;
    const uint32_t loop_ms = 500;
    const uint32_t stable_target = (AUTO_CALIB_STABLE_SEC * 1000) / loop_ms;
    bool first_sample = true;
#endif

    while (1) {
        // Mango pattern: check calibrate flag set by serial cmd task
        if (s_calibrate_flag) {
            orientation_update(&s_orient, s_new_roll, s_new_pitch, s_new_yaw);
            orientation_print(&s_orient);
            s_calibrate_flag = false;
#if AUTO_CALIB_ENABLE
            first_sample = true;
            stable_count = 0;
#endif
            ESP_LOGI(TAG, "Rotation matrix recalibrated (serial cmd)");
        }

        esp_err_t ret = lis3dh_read_data(&data);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "X:%+6.3fg  Y:%+6.3fg  Z:%+6.3fg  | Tilt:%+7.2f°  Roll:%+7.2f°  Pitch:%+7.2f°",
                     data.g[LIS3DH_X_IDX], data.g[LIS3DH_Y_IDX], data.g[LIS3DH_Z_IDX],
                     data.tilt, data.roll, data.pitch);

#if AUTO_CALIB_ENABLE
            // Auto-calibration: raw angles from sensor (without rotation matrix)
            float raw_roll, raw_pitch, raw_yaw;
            compute_raw_angles(&data, &raw_roll, &raw_pitch, &raw_yaw);

            if (first_sample) {
                ref_roll  = raw_roll;
                ref_pitch = raw_pitch;
                stable_count = 0;
                first_sample = false;
            } else {
                float drift_roll  = fabsf(raw_roll - ref_roll);
                float drift_pitch = fabsf(raw_pitch - ref_pitch);

                if (drift_roll < AUTO_CALIB_TOLERANCE_DEG &&
                    drift_pitch < AUTO_CALIB_TOLERANCE_DEG) {
                    stable_count++;

                    if (stable_count >= stable_target) {
                        ESP_LOGW(TAG, "AUTO-CALIB: Stable for %ds — updating orientation to Roll=%.1f Pitch=%.1f Yaw=%.0f",
                                 AUTO_CALIB_STABLE_SEC, raw_roll, raw_pitch, raw_yaw);
                        orientation_update(&s_orient, raw_roll, raw_pitch, raw_yaw);
                        orientation_print(&s_orient);
                        stable_count = 0;
                        first_sample = true;
                    }
                } else {
                    ref_roll  = raw_roll;
                    ref_pitch = raw_pitch;
                    stable_count = 0;
                }
            }
#endif

            // BLE status notification
            ble_device_status_t ble_status = {
                .roll  = data.roll,
                .pitch = data.pitch,
                .tilt  = data.tilt,
                .crash_state = s_crash_ctx.state,
            };
            ble_config_service_notify_status(&ble_status);

            if (s_alert_enabled && crash_detect_check(&s_crash_ctx, &data)) {
                alert_send_crash(data.tilt,
                                 data.g[LIS3DH_X_IDX],
                                 data.g[LIS3DH_Y_IDX],
                                 data.g[LIS3DH_Z_IDX]);
            }
        } else if (ret != ESP_ERR_NOT_FINISHED) {
            ESP_LOGE(TAG, "Accel read failed: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  %s  v%s", FW_PROJECT_NAME, FW_VERSION_STR);
    ESP_LOGI(TAG, "  Built: %s %s", FW_BUILD_DATE, FW_BUILD_TIME);
    ESP_LOGI(TAG, "========================================");

    // ---- Step 1: WiFi Connect ----
    ESP_LOGI(TAG, "[1/6] WiFi init & connect...");
    esp_err_t ret = alert_sender_init();
    if (ret == ESP_OK) {
        s_alert_enabled = true;
        ESP_LOGI(TAG, "[1/6] WiFi connected — alerts ENABLED");
    } else {
        ESP_LOGW(TAG, "[1/6] WiFi failed — alerts DISABLED (monitoring continues)");
    }

    // ---- Step 2: BLE GATT Service ----
    ESP_LOGI(TAG, "[2/6] BLE GATT service init...");
    ret = ble_config_service_init(BLE_DEVICE_NAME);
    if (ret == ESP_OK) {
        ble_config_service_set_orient_callback(on_ble_orient_write);
        ble_config_service_set_threshold_callback(on_ble_threshold_write);
        ESP_LOGI(TAG, "[2/6] BLE ready — device: '%s'", BLE_DEVICE_NAME);
    } else {
        ESP_LOGW(TAG, "[2/6] BLE init failed — BLE config disabled");
    }

    // ---- Step 3: LIS3DH Init ----
    ESP_LOGI(TAG, "[3/6] LIS3DH init...");
    ret = lis3dh_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, " LIS3DH INIT FAILED: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "========================================");
        return;
    }

    // ---- Step 4: Orientation rotation matrix ----
    ESP_LOGI(TAG, "[4/6] Building rotation matrix...");
    orientation_init(&s_orient, MOUNT_ROLL_DEG, MOUNT_PITCH_DEG, MOUNT_YAW_DEG);
    orientation_print(&s_orient);
    lis3dh_set_orientation(&s_orient);

    // ---- Step 5: Verify orientation ----
    ESP_LOGI(TAG, "[5/6] Verifying orientation...");
    vTaskDelay(pdMS_TO_TICKS(100));
    lis3dh_data_t test;
    ret = lis3dh_read_data(&test);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "First read failed: %s", esp_err_to_name(ret));
        return;
    }
    verify_orientation(&test);

    // ---- Step 6: Start crash monitoring ----
    ESP_LOGI(TAG, "[6/6] Starting crash monitoring...");
    crash_detect_init(&s_crash_ctx);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " BLE Device  : %s", BLE_DEVICE_NAME);
    ESP_LOGI(TAG, " Orientation : %.0f° %.0f° %.0f°", s_orient.roll_deg, s_orient.pitch_deg, s_orient.yaw_deg);
    ESP_LOGI(TAG, " Threshold   : tilt > %.0f°", CRASH_TILT_THRESHOLD_DEG);
    ESP_LOGI(TAG, " Alerts      : %s", s_alert_enabled ? "ENABLED (Email+SMS)" : "DISABLED (no WiFi)");
    ESP_LOGI(TAG, " ALL SYSTEMS GO — monitoring started    ");
    ESP_LOGI(TAG, "========================================");

    xTaskCreate(lis3dh_task, "lis3dh_task", 8192, NULL, 5, NULL);
    xTaskCreate(serial_cmd_task, "serial_cmd", 4096, NULL, 3, NULL);
}
