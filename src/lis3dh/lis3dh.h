#ifndef __LIS3DH_H__
#define __LIS3DH_H__

#include "esp_err.h"
#include <stdint.h>

// -------------------- Interface Selection --------------------
// Define LIS3DH_USE_SPI via build flag (-DLIS3DH_USE_SPI) to use SPI
// Default: I2C

#ifdef LIS3DH_USE_SPI
#define LIS3DH_SPI_HOST    SPI2_HOST
#define LIS3DH_PIN_MOSI    23
#define LIS3DH_PIN_MISO    19
#define LIS3DH_PIN_SCLK    18
#define LIS3DH_PIN_CS      5
#define LIS3DH_SPI_FREQ    (1 * 1000 * 1000)
#else
#define LIS3DH_I2C_PORT    I2C_NUM_0
#define LIS3DH_PIN_SDA     21
#define LIS3DH_PIN_SCL     22
#define LIS3DH_I2C_FREQ    (400 * 1000)
#define LIS3DH_I2C_ADDR    (0x18)
#endif

// -------------------- Register Map --------------------

#define LIS3DH_REG_WHO_AM_I      (0x0F)

#define LIS3DH_REG_CTRL_REG0     (0x1E)
#define LIS3DH_REG_TEMP_CFG      (0x1F)
#define LIS3DH_REG_CTRL_REG1     (0x20)
#define LIS3DH_REG_CTRL_REG2     (0x21)
#define LIS3DH_REG_CTRL_REG3     (0x22)
#define LIS3DH_REG_CTRL_REG4     (0x23)
#define LIS3DH_REG_CTRL_REG5     (0x24)
#define LIS3DH_REG_CTRL_REG6     (0x25)

#define LIS3DH_REG_REFERENCE     (0x26)
#define LIS3DH_REG_STATUS        (0x27)

#define LIS3DH_REG_OUT_X_L       (0x28)
#define LIS3DH_REG_OUT_X_H       (0x29)
#define LIS3DH_REG_OUT_Y_L       (0x2A)
#define LIS3DH_REG_OUT_Y_H       (0x2B)
#define LIS3DH_REG_OUT_Z_L       (0x2C)
#define LIS3DH_REG_OUT_Z_H       (0x2D)

#define LIS3DH_REG_INT1_CFG      (0x30)
#define LIS3DH_REG_INT1_SRC      (0x31)
#define LIS3DH_REG_INT1_THS      (0x32)
#define LIS3DH_REG_INT1_DUR      (0x33)

#define LIS3DH_REG_INT2_CFG      (0x34)
#define LIS3DH_REG_INT2_SRC      (0x35)
#define LIS3DH_REG_INT2_THS      (0x36)
#define LIS3DH_REG_INT2_DUR      (0x37)

#define LIS3DH_REG_OUTADC1_L     (0x08)
#define LIS3DH_REG_OUTADC1_H     (0x09)
#define LIS3DH_REG_OUTADC2_L     (0x0A)
#define LIS3DH_REG_OUTADC2_H     (0x0B)
#define LIS3DH_REG_OUTADC3_L     (0x0C)
#define LIS3DH_REG_OUTADC3_H     (0x0D)

#define LIS3DH_WHO_AM_I_VAL      (0x33)

// -------------------- Scale / Mode --------------------

#define LIS3DH_SCALE_2G          (2)
#define LIS3DH_SCALE_4G          (4)
#define LIS3DH_SCALE_8G          (8)
#define LIS3DH_SCALE_16G         (16)

#define LIS3DH_MODE_LOW_POWER    (1)
#define LIS3DH_MODE_NORMAL       (0)

#define LIS3DH_NUM_AXIS          (3)
#define LIS3DH_X_IDX             (0)
#define LIS3DH_Y_IDX             (1)
#define LIS3DH_Z_IDX             (2)

// -------------------- Config Struct --------------------

typedef struct lis3dh_ctrl_reg_config {
    uint8_t ctrl_reg0;
    uint8_t temp_cfg_reg;
    uint8_t ctrl_reg1;
    uint8_t ctrl_reg2;
    uint8_t ctrl_reg3;
    uint8_t ctrl_reg4;
    uint8_t ctrl_reg5;
    uint8_t ctrl_reg6;
    uint8_t int1_cfg;
    uint8_t int1_ths;
    uint8_t int1_dur;
    uint8_t int2_cfg;
    uint8_t int2_ths;
    uint8_t int2_dur;
} lis3dh_ctrl_reg_config_t;

// -------------------- Data Structs --------------------

typedef struct {
    int16_t raw[LIS3DH_NUM_AXIS];
    float   g[LIS3DH_NUM_AXIS];
    float   ms2[LIS3DH_NUM_AXIS];
    float   roll;
    float   pitch;
    float   tilt;
} lis3dh_data_t;

// -------------------- API --------------------

#include "orientation.h"

esp_err_t lis3dh_init(void);
esp_err_t lis3dh_read_who_am_i(uint8_t *who);
esp_err_t lis3dh_read_reg_val(uint8_t reg, uint8_t *val);
esp_err_t lis3dh_read_data(lis3dh_data_t *data);
void      lis3dh_set_orientation(orientation_ctx_t *ctx);

#endif
