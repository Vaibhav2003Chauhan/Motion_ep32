/************************************************************************/
/*! \file lis3dh.c
 *
 * Description: LIS3DH accelerometer driver — I2C / SPI
 *
 * **********************************************************************/

#include <string.h>
#include <math.h>
#include "lis3dh.h"
#include "orientation.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef LIS3DH_USE_SPI
#include "driver/spi_master.h"
#else
#include "driver/i2c.h"
#endif

static const char *TAG = "lis3dh";

// -------------------- Default Config (u546 pattern) --------------------

static lis3dh_ctrl_reg_config_t s_cfg = {
    .ctrl_reg0    = 0x10,   // CTRL_REG0: SDO/SA0 pull-up connected
    .temp_cfg_reg = 0xC0,   // TEMP_CFG: ADC + temp sensor enabled
    .ctrl_reg1    = 0x57,   // CTRL_REG1: 100Hz, all axes enabled
    .ctrl_reg2    = 0x00,   // CTRL_REG2: no high-pass filter
    .ctrl_reg3    = 0x00,   // CTRL_REG3: no interrupts on INT1
    .ctrl_reg4    = 0x88,   // CTRL_REG4: BDU enabled, ±2g, high-res
    .ctrl_reg5    = 0x00,   // CTRL_REG5: no reboot, no FIFO
    .ctrl_reg6    = 0x00,   // CTRL_REG6: no interrupts on INT2
    .int1_cfg     = 0x00,
    .int1_ths     = 0x00,
    .int1_dur     = 0x00,
    .int2_cfg     = 0x00,
    .int2_ths     = 0x00,
    .int2_dur     = 0x00,
};

static uint8_t s_current_scale = LIS3DH_SCALE_2G;
static orientation_ctx_t *s_orient = NULL;

void lis3dh_set_orientation(orientation_ctx_t *ctx)
{
    s_orient = ctx;
}

// ==================== SPI Bus Layer ====================
#ifdef LIS3DH_USE_SPI

static spi_device_handle_t s_spi;

static esp_err_t lis3dh_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg & 0x3F, val };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx };
    return spi_device_transmit(s_spi, &t);
}

static esp_err_t lis3dh_read_reg(uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = { reg | 0x80, 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = { .length = 16, .tx_buffer = tx, .rx_buffer = rx };
    esp_err_t ret = spi_device_transmit(s_spi, &t);
    *val = rx[1];
    return ret;
}

static esp_err_t lis3dh_read_burst(uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t tx[7] = { reg | 0xC0 };
    uint8_t rx[7] = {0};
    spi_transaction_t t = { .length = (1 + len) * 8, .tx_buffer = tx, .rx_buffer = rx };
    esp_err_t ret = spi_device_transmit(s_spi, &t);
    memcpy(buf, &rx[1], len);
    return ret;
}

static esp_err_t lis3dh_bus_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = LIS3DH_PIN_MOSI,
        .miso_io_num = LIS3DH_PIN_MISO,
        .sclk_io_num = LIS3DH_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t ret = spi_bus_initialize(LIS3DH_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = LIS3DH_SPI_FREQ,
        .mode = 0,
        .spics_io_num = LIS3DH_PIN_CS,
        .queue_size = 1,
    };
    return spi_bus_add_device(LIS3DH_SPI_HOST, &dev, &s_spi);
}

// ==================== I2C Bus Layer ====================
#else

static esp_err_t lis3dh_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LIS3DH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(LIS3DH_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t lis3dh_read_reg(uint8_t reg, uint8_t *val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LIS3DH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LIS3DH_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(LIS3DH_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t lis3dh_read_burst(uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LIS3DH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg | 0x80, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LIS3DH_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(LIS3DH_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t lis3dh_bus_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = LIS3DH_PIN_SDA,
        .scl_io_num = LIS3DH_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = LIS3DH_I2C_FREQ,
    };
    esp_err_t ret = i2c_param_config(LIS3DH_I2C_PORT, &conf);
    if (ret != ESP_OK) return ret;
    return i2c_driver_install(LIS3DH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

#endif

// ==================== Common Driver ====================

esp_err_t lis3dh_read_who_am_i(uint8_t *who)
{
    return lis3dh_read_reg(LIS3DH_REG_WHO_AM_I, who);
}

esp_err_t lis3dh_read_reg_val(uint8_t reg, uint8_t *val)
{
    return lis3dh_read_reg(reg, val);
}

static esp_err_t lis3dh_write_verify(uint8_t reg, uint8_t val)
{
    esp_err_t ret = lis3dh_write_reg(reg, val);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write reg 0x%02X FAILED: %s", reg, esp_err_to_name(ret));
        return ret;
    }

    uint8_t readback = 0;
    ret = lis3dh_read_reg(reg, &readback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Readback reg 0x%02X FAILED: %s", reg, esp_err_to_name(ret));
        return ret;
    }
    if (readback != val) {
        ESP_LOGE(TAG, "Reg 0x%02X verify FAIL: wrote=0x%02X read=0x%02X", reg, val, readback);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "  Reg 0x%02X = 0x%02X [OK]", reg, readback);
    return ESP_OK;
}

static float lis3dh_get_sensitivity(void)
{
    switch (s_current_scale) {
        case LIS3DH_SCALE_2G:  return 1.0f;
        case LIS3DH_SCALE_4G:  return 2.0f;
        case LIS3DH_SCALE_8G:  return 4.0f;
        case LIS3DH_SCALE_16G: return 12.0f;
        default:               return 1.0f;
    }
}

static void lis3dh_decode_scale(uint8_t ctrl_reg4)
{
    uint8_t fs = (ctrl_reg4 >> 4) & 0x03;
    switch (fs) {
        case 0: s_current_scale = LIS3DH_SCALE_2G;  break;
        case 1: s_current_scale = LIS3DH_SCALE_4G;  break;
        case 2: s_current_scale = LIS3DH_SCALE_8G;  break;
        case 3: s_current_scale = LIS3DH_SCALE_16G; break;
    }
}

esp_err_t lis3dh_init(void)
{
    esp_err_t ret;

#ifdef LIS3DH_USE_SPI
    ESP_LOGI(TAG, "--- LIS3DH Init (SPI) ---");
    ESP_LOGI(TAG, "MOSI=%d  MISO=%d  SCLK=%d  CS=%d  FREQ=%d Hz",
             LIS3DH_PIN_MOSI, LIS3DH_PIN_MISO, LIS3DH_PIN_SCLK, LIS3DH_PIN_CS, LIS3DH_SPI_FREQ);
#else
    ESP_LOGI(TAG, "--- LIS3DH Init (I2C) ---");
    ESP_LOGI(TAG, "SDA=%d  SCL=%d  ADDR=0x%02X  FREQ=%d Hz",
             LIS3DH_PIN_SDA, LIS3DH_PIN_SCL, LIS3DH_I2C_ADDR, LIS3DH_I2C_FREQ);
#endif

    ret = lis3dh_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus init FAILED: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Bus init OK");

    vTaskDelay(pdMS_TO_TICKS(20));

    // ---- WHO_AM_I ----
    uint8_t who = 0;
    ret = lis3dh_read_who_am_i(&who);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read FAILED: %s  (check wiring!)", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WHO_AM_I = 0x%02X  (expected 0x%02X)", who, LIS3DH_WHO_AM_I_VAL);
    if (who != LIS3DH_WHO_AM_I_VAL) {
        ESP_LOGE(TAG, "WHO_AM_I MISMATCH — wrong device or bad connection");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "WHO_AM_I MATCH — LIS3DH confirmed");

    // ---- Write all control registers (u546 pattern) ----
    ESP_LOGI(TAG, "Writing control registers...");

    ret = lis3dh_write_verify(LIS3DH_REG_CTRL_REG0, s_cfg.ctrl_reg0);
    if (ret != ESP_OK) return ret;

    ret = lis3dh_write_verify(LIS3DH_REG_TEMP_CFG, s_cfg.temp_cfg_reg);
    if (ret != ESP_OK) return ret;

    ret = lis3dh_write_verify(LIS3DH_REG_CTRL_REG1, s_cfg.ctrl_reg1);
    if (ret != ESP_OK) return ret;

    ret = lis3dh_write_verify(LIS3DH_REG_CTRL_REG2, s_cfg.ctrl_reg2);
    if (ret != ESP_OK) return ret;

    ret = lis3dh_write_verify(LIS3DH_REG_CTRL_REG3, s_cfg.ctrl_reg3);
    if (ret != ESP_OK) return ret;

    ret = lis3dh_write_verify(LIS3DH_REG_CTRL_REG4, s_cfg.ctrl_reg4);
    if (ret != ESP_OK) return ret;

    ret = lis3dh_write_verify(LIS3DH_REG_CTRL_REG5, s_cfg.ctrl_reg5);
    if (ret != ESP_OK) return ret;

    ret = lis3dh_write_verify(LIS3DH_REG_CTRL_REG6, s_cfg.ctrl_reg6);
    if (ret != ESP_OK) return ret;

    lis3dh_decode_scale(s_cfg.ctrl_reg4);

    // ---- Read reference register to reset HP filter ----
    uint8_t ref = 0;
    lis3dh_read_reg(LIS3DH_REG_REFERENCE, &ref);

    ESP_LOGI(TAG, "Scale: ±%dg  Sensitivity: %.1f mg/digit", s_current_scale, lis3dh_get_sensitivity());
    ESP_LOGI(TAG, "--- LIS3DH READY ---");
    return ESP_OK;
}

esp_err_t lis3dh_read_data(lis3dh_data_t *data)
{
    // Check status register — data ready?
    uint8_t status = 0;
    esp_err_t ret = lis3dh_read_reg(LIS3DH_REG_STATUS, &status);
    if (ret != ESP_OK) return ret;

    if (!(status & 0x08)) {
        return ESP_ERR_NOT_FINISHED;
    }

    uint8_t raw[6];
    ret = lis3dh_read_burst(LIS3DH_REG_OUT_X_L, raw, 6);
    if (ret != ESP_OK) return ret;

    float sens = lis3dh_get_sensitivity();

    for (int i = 0; i < LIS3DH_NUM_AXIS; i++) {
        int16_t raw16 = (int16_t)(raw[i * 2 + 1] << 8 | raw[i * 2]) >> 4;
        data->raw[i] = raw16;
        data->g[i]   = raw16 * sens * 0.001f;
        data->ms2[i] = data->g[i] * 9.81f;
    }

    float ax, ay, az;

    if (s_orient != NULL) {
        float sensor[3] = { data->g[0], data->g[1], data->g[2] };
        float world[3];
        orientation_apply(s_orient, sensor, world);
        ax = world[0];
        ay = world[1];
        az = world[2];
    } else {
        ax = data->g[LIS3DH_X_IDX];
        ay = data->g[LIS3DH_Y_IDX];
        az = data->g[LIS3DH_Z_IDX];
    }

    data->roll  = atan2f(ay, az) * (180.0f / M_PI);
    data->pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * (180.0f / M_PI);
    data->tilt  = atan2f(sqrtf(ax * ax + ay * ay), az) * (180.0f / M_PI);

    return ESP_OK;
}
