#include "alert_sender.h"
#include "config/alert_config.h"
#include "wifi/wifi_manager.h"
#include "wifi/wifi_connect.h"
#include "wifi/wifi_scanner.h"
#include "util/serial_input.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "alert";

static bool s_wifi_ready = false;

// GTS Root R4 CA — cross-signed by GlobalSign, used by smtp.gmail.com
static const char gmail_root_ca[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX\n"
"MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
"CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx\n"
"NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
"GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0\n"
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube\n"
"Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e\n"
"WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd\n"
"BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd\n"
"BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN\n"
"l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw\n"
"Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v\n"
"Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG\n"
"SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ\n"
"odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY\n"
"+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs\n"
"kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep\n"
"8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1\n"
"vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl\n"
"-----END CERTIFICATE-----\n";

// ==================== WiFi Interactive Connect ====================

static esp_err_t wifi_interactive_connect(void)
{
    ESP_LOGI(TAG, "Scanning for WiFi networks...");

    wifi_scan_list_t scan_list;
    esp_err_t ret = wifi_scanner_start(&scan_list);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (scan_list.count == 0) {
        ESP_LOGE(TAG, "No WiFi networks found");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_scanner_print_results(&scan_list);

    // User selects network
    printf("\n");
    int choice = serial_input_read_int(
        "Select network (1-%d): ", 1, scan_list.count, WIFI_INPUT_TIMEOUT_MS);
    if (choice < 0) {
        ESP_LOGE(TAG, "No network selected (timeout)");
        return ESP_ERR_TIMEOUT;
    }

    const wifi_scan_result_t *selected = &scan_list.entries[choice - 1];
    ESP_LOGI(TAG, "Selected: \"%s\" (RSSI:%d, %s)",
             selected->ssid, selected->rssi,
             selected->auth_mode == WIFI_AUTH_OPEN ? "OPEN" : "SECURED");

    // Get password if network is secured
    char password[65] = {0};
    if (selected->auth_mode != WIFI_AUTH_OPEN) {
        printf("\n");
        int len = serial_input_read_line(
            "Enter password: ", password, sizeof(password), WIFI_INPUT_TIMEOUT_MS);
        if (len < 0) {
            ESP_LOGE(TAG, "No password entered (timeout)");
            return ESP_ERR_TIMEOUT;
        }
        if (len == 0) {
            ESP_LOGE(TAG, "Empty password for secured network");
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Connect with retry
    for (int attempt = 1; attempt <= WIFI_CONNECT_RETRY_MAX; attempt++) {
        ESP_LOGI(TAG, "Connecting to \"%s\" (attempt %d/%d)...",
                 selected->ssid, attempt, WIFI_CONNECT_RETRY_MAX);

        ret = wifi_connect_start(selected->ssid,
                                  selected->auth_mode == WIFI_AUTH_OPEN ? NULL : password);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Connected to \"%s\"", selected->ssid);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Attempt %d failed: %s", attempt, esp_err_to_name(ret));

        if (attempt < WIFI_CONNECT_RETRY_MAX) {
            ESP_LOGI(TAG, "Retrying in %d seconds...", WIFI_CONNECT_RETRY_DELAY / 1000);
            vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_RETRY_DELAY));
        }
    }

    ESP_LOGE(TAG, "All %d connection attempts failed", WIFI_CONNECT_RETRY_MAX);
    return ESP_FAIL;
}

// ==================== SMTP (Email) ====================
#if ALERT_ENABLE_EMAIL

static void base64_encode(const char *input, char *output, int out_len)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, j = 0;
    int len = strlen(input);
    while (i < len) {
        uint32_t a = i < len ? (uint8_t)input[i++] : 0;
        uint32_t b = i < len ? (uint8_t)input[i++] : 0;
        uint32_t c = i < len ? (uint8_t)input[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        if (j + 4 >= out_len) break;
        output[j++] = b64[(triple >> 18) & 0x3F];
        output[j++] = b64[(triple >> 12) & 0x3F];
        output[j++] = (i > len + 1) ? '=' : b64[(triple >> 6) & 0x3F];
        output[j++] = (i > len)     ? '=' : b64[triple & 0x3F];
    }
    output[j] = '\0';
}

static int smtp_tls_write(esp_tls_t *tls, const char *data)
{
    int len = strlen(data);
    int written = 0;
    while (written < len) {
        int ret = esp_tls_conn_write(tls, data + written, len - written);
        if (ret < 0) {
            ESP_LOGE(TAG, "SMTP write failed: %d", ret);
            return -1;
        }
        written += ret;
    }
    return written;
}

static int smtp_tls_read(esp_tls_t *tls, char *buf, int buf_len)
{
    int ret = esp_tls_conn_read(tls, (unsigned char *)buf, buf_len - 1);
    if (ret > 0) {
        buf[ret] = '\0';
    } else if (ret == 0) {
        buf[0] = '\0';
        ESP_LOGW(TAG, "SMTP read: connection closed by server");
    } else {
        buf[0] = '\0';
        ESP_LOGE(TAG, "SMTP read failed: %d", ret);
    }
    return ret;
}

static bool smtp_check_response(const char *buf, const char *expected_code, const char *step)
{
    if (strncmp(buf, expected_code, 3) != 0) {
        ESP_LOGE(TAG, "SMTP %s failed — expected %s, got: %.50s", step, expected_code, buf);
        return false;
    }
    return true;
}

static esp_err_t alert_send_email(const char *body)
{
    ESP_LOGI(TAG, "Connecting to SMTP %s:%d...", ALERT_SMTP_SERVER, ALERT_SMTP_PORT);

    esp_tls_cfg_t tls_cfg = {
        .cacert_buf  = (const unsigned char *)gmail_root_ca,
        .cacert_bytes = sizeof(gmail_root_ca),
    };
    esp_tls_t *tls = esp_tls_init();
    if (tls == NULL) {
        ESP_LOGE(TAG, "TLS init failed — out of memory");
        return ESP_ERR_NO_MEM;
    }

    int ret = esp_tls_conn_new_sync(ALERT_SMTP_SERVER, strlen(ALERT_SMTP_SERVER),
                                     ALERT_SMTP_PORT, &tls_cfg, tls);
    if (ret != 1) {
        ESP_LOGE(TAG, "TLS connect failed — check server/port/network");
        esp_tls_conn_destroy(tls);
        return ESP_ERR_INVALID_STATE;
    }

    char buf[512];
    char b64[256];
    esp_err_t result = ESP_FAIL;

    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "220", "greeting")) goto cleanup;
    ESP_LOGI(TAG, "SMTP connected: %.60s", buf);

    if (smtp_tls_write(tls, "EHLO esp32\r\n") < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "250", "EHLO")) goto cleanup;

    if (smtp_tls_write(tls, "AUTH LOGIN\r\n") < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "334", "AUTH LOGIN")) goto cleanup;

    base64_encode(ALERT_EMAIL_SENDER, b64, sizeof(b64));
    snprintf(buf, sizeof(buf), "%s\r\n", b64);
    if (smtp_tls_write(tls, buf) < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "334", "username")) goto cleanup;

    base64_encode(ALERT_EMAIL_PASSWORD, b64, sizeof(b64));
    snprintf(buf, sizeof(buf), "%s\r\n", b64);
    if (smtp_tls_write(tls, buf) < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "235", "auth")) {
        ESP_LOGE(TAG, "Auth failed — check email/app-password in alert_config.h");
        goto cleanup;
    }
    ESP_LOGI(TAG, "SMTP auth OK");

    snprintf(buf, sizeof(buf), "MAIL FROM:<%s>\r\n", ALERT_EMAIL_SENDER);
    if (smtp_tls_write(tls, buf) < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "250", "MAIL FROM")) goto cleanup;

    snprintf(buf, sizeof(buf), "RCPT TO:<%s>\r\n", ALERT_EMAIL_RECEIVER);
    if (smtp_tls_write(tls, buf) < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "250", "RCPT TO")) {
        ESP_LOGE(TAG, "Receiver rejected — check ALERT_EMAIL_RECEIVER");
        goto cleanup;
    }

    if (smtp_tls_write(tls, "DATA\r\n") < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "354", "DATA")) goto cleanup;

    snprintf(buf, sizeof(buf),
             "From: %s\r\n"
             "To: %s\r\n"
             "Subject: %s\r\n"
             "Content-Type: text/plain; charset=UTF-8\r\n"
             "\r\n"
             "%s\r\n"
             ".\r\n",
             ALERT_EMAIL_SENDER, ALERT_EMAIL_RECEIVER, ALERT_EMAIL_SUBJECT, body);
    if (smtp_tls_write(tls, buf) < 0) goto cleanup;
    ret = smtp_tls_read(tls, buf, sizeof(buf));
    if (ret <= 0 || !smtp_check_response(buf, "250", "message accept")) goto cleanup;

    ESP_LOGI(TAG, "Email sent successfully to %s", ALERT_EMAIL_RECEIVER);
    result = ESP_OK;

    smtp_tls_write(tls, "QUIT\r\n");
    smtp_tls_read(tls, buf, sizeof(buf));

cleanup:
    esp_tls_conn_destroy(tls);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Email send FAILED");
    }
    return result;
}

#endif // ALERT_ENABLE_EMAIL

// ==================== SMS (Textbelt) ====================
#if ALERT_ENABLE_SMS

static esp_err_t alert_send_sms(const char *message)
{
    ESP_LOGI(TAG, "Sending SMS to %s...", ALERT_SMS_PHONE);

    char post_data[512];
    snprintf(post_data, sizeof(post_data),
             "{\"phone\":\"%s\",\"message\":\"%s\",\"key\":\"%s\"}",
             ALERT_SMS_PHONE, message, ALERT_SMS_API_KEY);

    esp_http_client_config_t config = {
        .url = "https://textbelt.com/text",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "SMS HTTP client init failed — out of memory");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ESP_LOGI(TAG, "SMS sent successfully!");
        } else {
            ESP_LOGW(TAG, "SMS API returned HTTP %d — check phone/key", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "SMS HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

#endif // ALERT_ENABLE_SMS

// ==================== Public API ====================

esp_err_t alert_sender_init(void)
{
    ESP_LOGI(TAG, "--- Alert Sender Init ---");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = serial_input_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Serial input init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = wifi_scanner_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scanner init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = wifi_connect_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Interactive WiFi: scan → select → password → connect
    ret = wifi_interactive_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed — alerts will not work");
        return ret;
    }

    s_wifi_ready = true;

#if ALERT_ENABLE_EMAIL
    ESP_LOGI(TAG, "Email alerts : ENABLED → %s", ALERT_EMAIL_RECEIVER);
#else
    ESP_LOGI(TAG, "Email alerts : DISABLED");
#endif
#if ALERT_ENABLE_SMS
    ESP_LOGI(TAG, "SMS alerts   : ENABLED → %s (key: %s)", ALERT_SMS_PHONE,
             strcmp(ALERT_SMS_API_KEY, "textbelt") == 0 ? "free-1/day" : "paid");
#else
    ESP_LOGI(TAG, "SMS alerts   : DISABLED");
#endif

    ESP_LOGI(TAG, "Alert sender ready");
    return ESP_OK;
}

esp_err_t alert_send_crash(float tilt_angle, float ax, float ay, float az)
{
    if (!s_wifi_ready) {
        ESP_LOGE(TAG, "Alert sender not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_connect_is_connected()) {
        ESP_LOGE(TAG, "WiFi disconnected — cannot send alert");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, " SENDING CRASH ALERT                   ");
    ESP_LOGW(TAG, " Tilt=%.1f°  X=%.2fg  Y=%.2fg  Z=%.2fg", tilt_angle, ax, ay, az);
    ESP_LOGW(TAG, "========================================");

    char message[256];
    snprintf(message, sizeof(message),
             "CRASH ALERT!\n"
             "Tilt: %.1f deg\n"
             "Accel X: %.2fg  Y: %.2fg  Z: %.2fg\n"
             "Threshold: %.0f deg\n"
             "Device: LIS3DH ESP32",
             tilt_angle, ax, ay, az, CRASH_TILT_THRESHOLD_DEG);

    esp_err_t final_ret = ESP_OK;
    int success_count = 0;
    int total_count = 0;

#if ALERT_ENABLE_EMAIL
    total_count++;
    ESP_LOGI(TAG, "[EMAIL] Sending...");
    esp_err_t email_ret = alert_send_email(message);
    if (email_ret == ESP_OK) {
        success_count++;
        ESP_LOGI(TAG, "[EMAIL] OK");
    } else {
        ESP_LOGE(TAG, "[EMAIL] FAILED: %s", esp_err_to_name(email_ret));
        final_ret = email_ret;
    }
#endif

#if ALERT_ENABLE_SMS
    total_count++;
    char sms_msg[160];
    snprintf(sms_msg, sizeof(sms_msg),
             "CRASH ALERT! Tilt:%.1fdeg X:%.2fg Y:%.2fg Z:%.2fg",
             tilt_angle, ax, ay, az);
    ESP_LOGI(TAG, "[SMS] Sending...");
    esp_err_t sms_ret = alert_send_sms(sms_msg);
    if (sms_ret == ESP_OK) {
        success_count++;
        ESP_LOGI(TAG, "[SMS] OK");
    } else {
        ESP_LOGE(TAG, "[SMS] FAILED: %s", esp_err_to_name(sms_ret));
        final_ret = sms_ret;
    }
#endif

    ESP_LOGI(TAG, "Alert result: %d/%d sent successfully", success_count, total_count);
    return final_ret;
}
