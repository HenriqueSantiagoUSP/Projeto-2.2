#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mpu6050.h"
#include "esp_log.h"

static const char *TAG = "mpu6050";

#define REG_SMPLRT_DIV      0x19
#define REG_CONFIG          0x1A
#define REG_GYRO_CONFIG     0x1B
#define REG_ACCEL_CONFIG    0x1C
#define REG_FIFO_EN         0x23
#define REG_INT_ENABLE      0x38
#define REG_INT_STATUS      0x3A
#define REG_USER_CTRL       0x6A
#define REG_PWR_MGMT_1      0x6B
#define REG_FIFO_COUNT_H    0x72
#define REG_FIFO_R_W        0x74
#define REG_WHO_AM_I        0x75

#define WHO_AM_I_MPU6050    0x68
#define PWR_RESET           0x80
#define PWR_CLOCK_PLL_GX    0x01    /* awake, clocked from the X gyro PLL (steadier than the RC) */
#define CONFIG_DLPF_44HZ    0x03    /* accel 44 Hz, gyro 42 Hz; internal rate becomes 1 kHz */
#define GYRO_FS_500         0x08
#define ACCEL_FS_8G         0x10
#define USER_FIFO_EN        0x40
#define USER_FIFO_RESET     0x04
#define FIFO_EN_ACCEL_GYRO  0x78    /* XG, YG, ZG and the 3 accel axes, in that register order */
#define INT_FIFO_OFLOW      0x10

#define SAMPLE_BYTES        12      /* accel x,y,z then gyro x,y,z, big endian */
#define FIFO_SIZE           1024
#define BURST_SAMPLES       10      /* samples per I2C read */
#define I2C_TIMEOUT_MS      50

static esp_err_t write_reg(mpu6050_t *mpu, uint8_t reg, uint8_t value){
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(mpu->dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t read_regs(mpu6050_t *mpu, uint8_t reg, uint8_t *out, size_t len){
    return i2c_master_transmit_receive(mpu->dev, &reg, 1, out, len, I2C_TIMEOUT_MS);
}

static int16_t be16(const uint8_t *p){
    return (int16_t) ((uint16_t) p[0] << 8 | p[1]);
}

static esp_err_t fifo_reset(mpu6050_t *mpu){
    esp_err_t err = write_reg(mpu, REG_USER_CTRL, USER_FIFO_RESET);
    if (err != ESP_OK) return err;
    return write_reg(mpu, REG_USER_CTRL, USER_FIFO_EN);
}

esp_err_t mpu6050_init(mpu6050_t *mpu, i2c_master_bus_handle_t bus, uint8_t addr){
    esp_err_t err;
    /* On the bus once: the caller may try again and again while the sensor is unplugged. */
    if (mpu->dev == NULL) {
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 400000,
        };
        err = i2c_master_bus_add_device(bus, &dev_config, &mpu->dev);
        if (err != ESP_OK) return err;
    }

    uint8_t who = 0;
    if (read_regs(mpu, REG_WHO_AM_I, &who, 1) != ESP_OK) {
        /* Not plugged in: a state the caller reports, once. */
        ESP_LOGD(TAG, "no answer at 0x%02X", addr);
        return ESP_ERR_NOT_FOUND;
    }
    if (who != WHO_AM_I_MPU6050) {
        /* Clones answer 0x70/0x72 and usually work the same; say so, do not refuse. */
        ESP_LOGW(TAG, "WHO_AM_I is 0x%02X, expected 0x%02X", who, WHO_AM_I_MPU6050);
    }

    err = write_reg(mpu, REG_PWR_MGMT_1, PWR_RESET);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(100));

    const uint8_t setup[][2] = {
        { REG_PWR_MGMT_1,   PWR_CLOCK_PLL_GX },
        { REG_CONFIG,       CONFIG_DLPF_44HZ },
        { REG_SMPLRT_DIV,   1000 / MPU6050_SAMPLE_RATE_HZ - 1 },
        { REG_GYRO_CONFIG,  GYRO_FS_500 },
        { REG_ACCEL_CONFIG, ACCEL_FS_8G },
        { REG_INT_ENABLE,   INT_FIFO_OFLOW },
        { REG_FIFO_EN,      FIFO_EN_ACCEL_GYRO },
    };
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); i++) {
        err = write_reg(mpu, setup[i][0], setup[i][1]);
        if (err != ESP_OK) return err;
    }

    err = fifo_reset(mpu);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "+-8 g, +-500 dps, %d Hz into the FIFO", MPU6050_SAMPLE_RATE_HZ);
    }
    return err;
}

esp_err_t mpu6050_fifo_read(mpu6050_t *mpu, mpu6050_sample_t *out, size_t max, size_t *count, bool *overflow){
    *count = 0;
    *overflow = false;

    uint8_t status;
    esp_err_t err = read_regs(mpu, REG_INT_STATUS, &status, 1);
    if (err != ESP_OK) return err;

    uint8_t raw_count[2];
    err = read_regs(mpu, REG_FIFO_COUNT_H, raw_count, sizeof(raw_count));
    if (err != ESP_OK) return err;
    size_t bytes = (size_t) raw_count[0] << 8 | raw_count[1];

    /* Once it overflows, the FIFO no longer starts on a sample boundary: start over. */
    if ((status & INT_FIFO_OFLOW) || bytes >= FIFO_SIZE - SAMPLE_BYTES + 1) {
        err = fifo_reset(mpu);
        if (err != ESP_OK) return err;   /* could not even reset it: a bus problem, not an overflow */
        *overflow = true;
        return ESP_OK;
    }

    size_t available = bytes / SAMPLE_BYTES;
    if (available > max) {
        available = max;
    }

    uint8_t burst[BURST_SAMPLES * SAMPLE_BYTES];
    while (*count < available) {
        size_t n = available - *count;
        if (n > BURST_SAMPLES) {
            n = BURST_SAMPLES;
        }
        err = read_regs(mpu, REG_FIFO_R_W, burst, n * SAMPLE_BYTES);
        if (err != ESP_OK) {
            /*
             * The sensor may have sent part of the burst, moving its FIFO pointer: the next read
             * would start in the middle of a sample. Start the FIFO over (if the bus lets us).
             */
            fifo_reset(mpu);
            *count = 0;
            return err;
        }

        for (size_t i = 0; i < n; i++) {
            const uint8_t *p = &burst[i * SAMPLE_BYTES];
            out[*count] = (mpu6050_sample_t) {
                .ax = be16(p + 0), .ay = be16(p + 2), .az = be16(p + 4),
                .gx = be16(p + 6), .gy = be16(p + 8), .gz = be16(p + 10),
            };
            (*count)++;
        }
    }
    return ESP_OK;
}
