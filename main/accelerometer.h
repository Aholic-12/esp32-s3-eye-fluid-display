#ifndef ACCELEROMETER_H
#define ACCELEROMETER_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c.h"
#include "esp_err.h"

// 加速度计句柄类型
typedef void *accelerometer_handle_t;

// ESP32-S3-EYE 板载 / 扩展 I2C
#define ACCEL_I2C_PORT          I2C_NUM_0
#define ACCEL_I2C_FREQ          (400 * 1000)
#define ACCEL_I2C_SDA_PIN       4
#define ACCEL_I2C_SCL_PIN       5

/**
 * 初始化加速度计
 * 支持自动检测常见的 I2C 加速度计；未检测到时回退到模拟数据
 */
accelerometer_handle_t accelerometer_init(void);

/**
 * 读取加速度计数据，单位 g
 */
esp_err_t accelerometer_read(accelerometer_handle_t accel, float *x, float *y, float *z);

/**
 * 获取单例句柄
 */
accelerometer_handle_t accelerometer_get_handle(void);

/**
 * 是否检测到真实硬件加速度计
 */
bool accelerometer_is_ready(accelerometer_handle_t accel);

/**
 * 获取当前传感器名称
 */
const char *accelerometer_get_name(accelerometer_handle_t accel);

/**
 * 关闭加速度计
 */
void accelerometer_deinit(accelerometer_handle_t accel);

#endif // ACCELEROMETER_H
