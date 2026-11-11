#pragma once

/*
 * MPU-6050 accelerometer + gyroscope, sampled by the sensor itself into its FIFO.
 *
 * The sample rate comes from the chip (1 kHz internal rate / (1 + SMPLRT_DIV)), not
 * from a task delay, so the timing is hardware exact and the FIFO can be drained in
 * batches. The 1 KiB FIFO holds 85 samples (425 ms at 200 Hz); drain it well
 * before that.
 */

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define MPU6050_ADDR_DEFAULT        0x68    /* AD0 low */
#define MPU6050_SAMPLE_RATE_HZ      200
#define MPU6050_SAMPLE_PERIOD_MS    (1000 / MPU6050_SAMPLE_RATE_HZ)

/* Full scales: +-8 g (falls reach 6-8 g) and +-500 deg/s. */
#define MPU6050_ACCEL_LSB_PER_G     4096.0f
#define MPU6050_GYRO_LSB_PER_DPS    65.5f

typedef struct {
    i2c_master_dev_handle_t dev;
} mpu6050_t;

/* One FIFO sample, raw counts. */
typedef struct {
    int16_t ax, ay, az;
    int16_t gx, gy, gz;
} mpu6050_sample_t;

/*
 * Resets and configures the chip: ranges above, 44 Hz DLPF, 200 Hz, FIFO on.
 * ESP_ERR_NOT_FOUND when nothing answers at addr, without logging an error. It may be
 * called again (sensor plugged in later, or back after a glitch): mpu must start
 * zeroed, and the device is added to the bus only the first time.
 */
esp_err_t mpu6050_init(mpu6050_t *mpu, i2c_master_bus_handle_t bus, uint8_t addr);

/*
 * Reads up to max complete samples from the FIFO into out. On overflow the FIFO
 * is reset, *count is 0 and ESP_ERR_INVALID_STATE tells the caller samples were lost.
 */
esp_err_t mpu6050_fifo_read(mpu6050_t *mpu, mpu6050_sample_t *out, size_t max, size_t *count);
