#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "accelerometer.h"

static const char *TAG = "accelerometer";

#define MPU6050_ADDR                0x68
#define MPU6050_REG_WHO_AM_I        0x75
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_ACCEL_CONFIG    0x1C
#define MPU6050_REG_ACCEL_XOUT_H    0x3B

#define LIS2DH12_ADDR_LOW           0x18
#define LIS2DH12_ADDR_HIGH          0x19
#define LIS2DH12_REG_WHO_AM_I       0x0F
#define LIS2DH12_REG_CTRL1          0x20
#define LIS2DH12_REG_CTRL4          0x23
#define LIS2DH12_REG_OUT_X_L        0x28

typedef enum {
    ACCEL_SENSOR_NONE = 0,
    ACCEL_SENSOR_MPU6050,
    ACCEL_SENSOR_LIS2DH12,
} accel_sensor_type_t;

typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    bool initialized;
    accel_sensor_type_t sensor_type;
    float scale;
} accelerometer_t;

static accelerometer_t s_accel;

static esp_err_t i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = ACCEL_I2C_SDA_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = ACCEL_I2C_SCL_PIN,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = ACCEL_I2C_FREQ,
    };

    esp_err_t ret = i2c_param_config(ACCEL_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_driver_install(ACCEL_I2C_PORT, conf.mode, 0, 0, 0);
    if (ret == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return ret;
}

static esp_err_t i2c_read_register(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(ACCEL_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_write_register(uint8_t addr, uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(ACCEL_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static bool probe_register(uint8_t addr, uint8_t reg, uint8_t expected)
{
    uint8_t value = 0;
    if (i2c_read_register(addr, reg, &value, 1) != ESP_OK) {
        return false;
    }
    return value == expected;
}

static esp_err_t mpu6050_init(uint8_t addr)
{
    ESP_RETURN_ON_ERROR(i2c_write_register(addr, MPU6050_REG_PWR_MGMT_1, 0x01), TAG, "wake MPU6050 failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(i2c_write_register(addr, MPU6050_REG_ACCEL_CONFIG, 0x00), TAG, "config MPU6050 failed");
    return ESP_OK;
}

static esp_err_t lis2dh12_init(uint8_t addr)
{
    ESP_RETURN_ON_ERROR(i2c_write_register(addr, LIS2DH12_REG_CTRL1, 0x57), TAG, "config LIS2DH12 CTRL1 failed");
    ESP_RETURN_ON_ERROR(i2c_write_register(addr, LIS2DH12_REG_CTRL4, 0x88), TAG, "config LIS2DH12 CTRL4 failed");
    return ESP_OK;
}

accelerometer_handle_t accelerometer_init(void)
{
    memset(&s_accel, 0, sizeof(s_accel));
    s_accel.i2c_port = ACCEL_I2C_PORT;

    ESP_LOGI(TAG, "init i2c on SDA=%d SCL=%d", ACCEL_I2C_SDA_PIN, ACCEL_I2C_SCL_PIN);
    if (i2c_init() != ESP_OK) {
        ESP_LOGW(TAG, "I2C init failed, fallback to simulated data");
        return &s_accel;
    }

    if (probe_register(MPU6050_ADDR, MPU6050_REG_WHO_AM_I, 0x68) || probe_register(MPU6050_ADDR, MPU6050_REG_WHO_AM_I, 0x69)) {
        if (mpu6050_init(MPU6050_ADDR) == ESP_OK) {
            s_accel.sensor_type = ACCEL_SENSOR_MPU6050;
            s_accel.i2c_addr = MPU6050_ADDR;
            s_accel.scale = 16384.0f;
            s_accel.initialized = true;
            ESP_LOGI(TAG, "detected MPU6050 @ 0x%02X", s_accel.i2c_addr);
            return &s_accel;
        }
    }

    const uint8_t lis_addr[] = {LIS2DH12_ADDR_LOW, LIS2DH12_ADDR_HIGH};
    for (size_t i = 0; i < sizeof(lis_addr); i++) {
        if (probe_register(lis_addr[i], LIS2DH12_REG_WHO_AM_I, 0x33) && lis2dh12_init(lis_addr[i]) == ESP_OK) {
            s_accel.sensor_type = ACCEL_SENSOR_LIS2DH12;
            s_accel.i2c_addr = lis_addr[i];
            s_accel.scale = 1000.0f;
            s_accel.initialized = true;
            ESP_LOGI(TAG, "detected LIS2DH12 @ 0x%02X", s_accel.i2c_addr);
            return &s_accel;
        }
    }

    ESP_LOGW(TAG, "no supported hardware accelerometer found, use simulated data");
    return &s_accel;
}

esp_err_t accelerometer_read(accelerometer_handle_t accel, float *x, float *y, float *z)
{
    accelerometer_t *handle = (accelerometer_t *)accel;
    if (handle == NULL || x == NULL || y == NULL || z == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->initialized) {
        static int sim_count = 0;
        sim_count++;
        *x = 0.55f * sinf(sim_count * 0.07f);
        *y = 0.55f * cosf(sim_count * 0.09f);
        *z = 1.0f;
        return ESP_OK;
    }

    if (handle->sensor_type == ACCEL_SENSOR_MPU6050) {
        uint8_t data[6];
        ESP_RETURN_ON_ERROR(i2c_read_register(handle->i2c_addr, MPU6050_REG_ACCEL_XOUT_H, data, sizeof(data)), TAG, "read MPU6050 failed");

        int16_t ax = (int16_t)((data[0] << 8) | data[1]);
        int16_t ay = (int16_t)((data[2] << 8) | data[3]);
        int16_t az = (int16_t)((data[4] << 8) | data[5]);

        *x = ax / handle->scale;
        *y = ay / handle->scale;
        *z = az / handle->scale;
        return ESP_OK;
    }

    if (handle->sensor_type == ACCEL_SENSOR_LIS2DH12) {
        uint8_t data[6];
        ESP_RETURN_ON_ERROR(i2c_read_register(handle->i2c_addr, LIS2DH12_REG_OUT_X_L | 0x80, data, sizeof(data)), TAG, "read LIS2DH12 failed");

        int16_t ax = (int16_t)(((int16_t)data[1] << 8) | data[0]) >> 4;
        int16_t ay = (int16_t)(((int16_t)data[3] << 8) | data[2]) >> 4;
        int16_t az = (int16_t)(((int16_t)data[5] << 8) | data[4]) >> 4;

        *x = ax / handle->scale;
        *y = ay / handle->scale;
        *z = az / handle->scale;
        return ESP_OK;
    }

    return ESP_FAIL;
}

accelerometer_handle_t accelerometer_get_handle(void)
{
    return &s_accel;
}

bool accelerometer_is_ready(accelerometer_handle_t accel)
{
    accelerometer_t *handle = (accelerometer_t *)accel;
    return handle != NULL && handle->initialized;
}

const char *accelerometer_get_name(accelerometer_handle_t accel)
{
    accelerometer_t *handle = (accelerometer_t *)accel;
    if (handle == NULL || !handle->initialized) {
        return "SIM";
    }
    switch (handle->sensor_type) {
    case ACCEL_SENSOR_MPU6050:
        return "MPU6050";
    case ACCEL_SENSOR_LIS2DH12:
        return "LIS2DH12";
    default:
        return "SIM";
    }
}

void accelerometer_deinit(accelerometer_handle_t accel)
{
    accelerometer_t *handle = (accelerometer_t *)accel;
    if (handle == NULL) {
        return;
    }
    if (i2c_driver_delete(ACCEL_I2C_PORT) == ESP_OK) {
        ESP_LOGI(TAG, "accelerometer deinit done");
    }
    handle->initialized = false;
}
