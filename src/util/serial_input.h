#ifndef SERIAL_INPUT_H
#define SERIAL_INPUT_H

#include "esp_err.h"
#include <stddef.h>

esp_err_t serial_input_init(void);
int serial_input_read_line(const char *prompt, char *buf, size_t max_len, uint32_t timeout_ms);
int serial_input_read_int(const char *prompt, int min_val, int max_val, uint32_t timeout_ms);

#endif
