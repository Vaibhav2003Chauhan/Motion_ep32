#include "serial_input.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/uart.h"

static const char *TAG = "serial_input";

#define UART_PORT    UART_NUM_0
#define UART_BUF_SZ  256

static bool s_initialized = false;

esp_err_t serial_input_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    uart_config_t uart_cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    esp_err_t ret = uart_param_config(UART_PORT, &uart_cfg);
    if (ret != ESP_OK) return ret;

    ret = uart_driver_install(UART_PORT, UART_BUF_SZ, 0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    s_initialized = true;
    return ESP_OK;
}

static void serial_input_flush(void)
{
    uint8_t discard;
    while (uart_read_bytes(UART_PORT, &discard, 1, pdMS_TO_TICKS(20)) > 0) {
    }
}

int serial_input_read_line(const char *prompt, char *buf, size_t max_len, uint32_t timeout_ms)
{
    if (buf == NULL || max_len < 2) {
        return -1;
    }

    serial_input_flush();

    if (prompt != NULL) {
        printf("%s", prompt);
        fflush(stdout);
    }

    size_t pos = 0;
    uint32_t elapsed = 0;
    const uint32_t poll_ms = 50;

    while (pos < max_len - 1 && elapsed < timeout_ms) {
        uint8_t ch;
        int len = uart_read_bytes(UART_PORT, &ch, 1, pdMS_TO_TICKS(poll_ms));

        if (len <= 0) {
            elapsed += poll_ms;
            continue;
        }

        elapsed = 0;

        if (ch == '\r' || ch == '\n') {
            printf("\n");
            break;
        }

        if (ch == '\b' || ch == 127) {
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        if (ch >= 32 && ch < 127) {
            buf[pos++] = (char)ch;
            printf("%c", ch);
            fflush(stdout);
        }
    }

    buf[pos] = '\0';

    if (elapsed >= timeout_ms && pos == 0) {
        ESP_LOGW(TAG, "Input timed out");
        return -1;
    }

    return (int)pos;
}

int serial_input_read_int(const char *prompt, int min_val, int max_val, uint32_t timeout_ms)
{
    char buf[16];

    while (1) {
        int len = serial_input_read_line(prompt, buf, sizeof(buf), timeout_ms);
        if (len < 0) {
            return -1;
        }

        int val = atoi(buf);
        if (val >= min_val && val <= max_val) {
            return val;
        }

        printf("  Invalid. Enter a number between %d and %d.\n", min_val, max_val);
    }
}
