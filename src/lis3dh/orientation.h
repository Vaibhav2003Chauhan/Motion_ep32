#ifndef ORIENTATION_H
#define ORIENTATION_H

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    float m[3][3];
} rotation_matrix_t;

typedef struct {
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    rotation_matrix_t R;
} orientation_ctx_t;

// Load from NVS if saved, else use the provided defaults and save them
esp_err_t orientation_init(orientation_ctx_t *ctx, float default_roll, float default_pitch, float default_yaw);

// Update angles at runtime — rebuilds matrix and persists to NVS
esp_err_t orientation_update(orientation_ctx_t *ctx, float roll_deg, float pitch_deg, float yaw_deg);

// Transform sensor-frame vector to world-frame
void orientation_apply(const orientation_ctx_t *ctx, const float sensor[3], float world[3]);

void orientation_print(const orientation_ctx_t *ctx);

#endif
