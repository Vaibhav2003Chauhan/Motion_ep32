#include "shell.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "util/serial_input.h"
#include "config/version.h"
#include "wifi/wifi_scanner.h"

static const char *TAG = "shell";

static QueueHandle_t s_cmd_queue;
static const shell_status_t *s_status;
static wifi_scan_list_t s_last_scan;
static bool s_has_scan = false;

static const char *state_to_str(wifi_state_t state)
{
    switch (state) {
    case WIFI_STATE_DISCONNECTED: return "Disconnected";
    case WIFI_STATE_SCANNING:     return "Scanning";
    case WIFI_STATE_CONNECTING:   return "Connecting";
    case WIFI_STATE_CONNECTED:    return "Connected";
    default:                      return "Unknown";
    }
}

static void print_help(void)
{
    printf("\n");
    printf("  Commands:\n");
    printf("  ----------------------------------------\n");
    printf("  scan        - Scan for WiFi networks\n");
    printf("  connect     - Connect to a scanned network\n");
    printf("  disconnect  - Disconnect from WiFi\n");
    printf("  status      - Show connection status\n");
    printf("  version     - Show firmware version\n");
    printf("  reboot      - Restart the device\n");
    printf("  exit        - Exit serial monitor\n");
    printf("  help        - Show this help\n");
    printf("  ----------------------------------------\n\n");
}

static void handle_scan(void)
{
    shell_cmd_t cmd = { .id = CMD_SCAN };
    xQueueSend(s_cmd_queue, &cmd, portMAX_DELAY);

    printf("Scanning... please wait.\n");
    vTaskDelay(pdMS_TO_TICKS(5000));

    esp_err_t ret = wifi_scanner_start(&s_last_scan);
    if (ret == ESP_OK) {
        s_has_scan = true;
        wifi_scanner_print_results(&s_last_scan);
    } else {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(ret));
    }
}

static void handle_connect(void)
{
    if (!s_has_scan || s_last_scan.count == 0) {
        printf("No scan results. Run 'scan' first.\n");
        return;
    }

    printf("\n");
    int choice = serial_input_read_int("Select network (number): ",
                                        1, s_last_scan.count, 30000);
    if (choice < 0) {
        printf("Selection cancelled.\n");
        return;
    }

    const wifi_scan_result_t *selected = &s_last_scan.entries[choice - 1];
    printf("Selected: \"%s\"\n", selected->ssid);

    shell_cmd_t cmd = { .id = CMD_CONNECT };
    strncpy(cmd.arg1, selected->ssid, sizeof(cmd.arg1) - 1);

    if (selected->auth_mode != WIFI_AUTH_OPEN) {
        int len = serial_input_read_line("Enter password (min 8 chars): ", cmd.arg2, sizeof(cmd.arg2), 60000);
        if (len < 0) {
            printf("No password entered.\n");
            return;
        }
        if (len < 8) {
            printf("Password too short. WPA2 requires at least 8 characters.\n");
            return;
        }
        printf("Password: %s\n", cmd.arg2);
    }

    xQueueSend(s_cmd_queue, &cmd, portMAX_DELAY);
    printf("Connecting to \"%s\"...\n", selected->ssid);

    for (int i = 0; i < 40; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_status->state == WIFI_STATE_CONNECTED) {
            printf("  Connected! IP: %s | RSSI: %d dBm\n", s_status->ip, s_status->rssi);
            return;
        }
        if (s_status->state == WIFI_STATE_DISCONNECTED && i > 2) {
            printf("  Connection failed.\n");
            return;
        }
    }
    printf("  Connection timed out.\n");
}

static void handle_status(void)
{
    printf("\n");
    printf("  State : %s\n", state_to_str(s_status->state));
    if (s_status->state == WIFI_STATE_CONNECTED) {
        printf("  SSID  : %s\n", s_status->ssid);
        printf("  IP    : %s\n", s_status->ip);
        printf("  RSSI  : %d dBm\n", s_status->rssi);
    }
    printf("\n");
}

esp_err_t shell_init(QueueHandle_t cmd_queue, const shell_status_t *status)
{
    if (cmd_queue == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cmd_queue = cmd_queue;
    s_status = status;

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

void shell_task(void *param)
{
    char line[64];

    printf("\n");
    printf("  %s v%s — Type 'help' for commands.\n\n", FW_PROJECT_NAME, FW_VERSION_STR);

    while (1) {
        int len = serial_input_read_line("esp32> ", line, sizeof(line), 600000);
        if (len <= 0) {
            continue;
        }

        if (strcmp(line, "scan") == 0) {
            handle_scan();
        } else if (strcmp(line, "connect") == 0) {
            handle_connect();
        } else if (strcmp(line, "disconnect") == 0) {
            shell_cmd_t cmd = { .id = CMD_DISCONNECT };
            xQueueSend(s_cmd_queue, &cmd, portMAX_DELAY);
            printf("Disconnecting...\n");
        } else if (strcmp(line, "status") == 0) {
            handle_status();
        } else if (strcmp(line, "version") == 0) {
            printf("  %s v%s\n", FW_PROJECT_NAME, FW_VERSION_STR);
        } else if (strcmp(line, "reboot") == 0) {
            printf("Rebooting...\n");
            vTaskDelay(pdMS_TO_TICKS(500));
            shell_cmd_t cmd = { .id = CMD_REBOOT };
            xQueueSend(s_cmd_queue, &cmd, portMAX_DELAY);
        } else if (strcmp(line, "exit") == 0) {
            printf("Exiting shell. Use Ctrl+] to close monitor.\n");
            vTaskDelete(NULL);
        } else if (strcmp(line, "help") == 0) {
            print_help();
        } else {
            printf("  Unknown command: '%s'. Type 'help'.\n", line);
        }
    }
}
