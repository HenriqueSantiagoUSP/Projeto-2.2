/*
 * Hardware test of the single PCB (room hub + wearable): one firmware that reads every part
 * the board carries and shows on the serial console whether each one works, so a freshly
 * soldered board is checked with a single flash. No Wi-Fi, no MQTT, no rules: raw readings,
 * redrawn every second. How to read the panel: firmware/hwtest/README.md.
 *
 * The pins come straight from pcb_pins.h (the board is the same for both firmwares); the
 * MPU-6050 driver is the wearable's and the CAN transport the shared one; the LM35 chain fit,
 * the PIR pulls and warm-up and the reed settle time come from the hub's headers, so nothing
 * of the real firmwares is copied here. The judgement of each part is in hwtest_checks,
 * which has host tests.
 */

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "can.h"
#include "can_ids.h"
#include "hcsr501.h"
#include "hwtest_checks.h"
#include "indicators.h"
#include "lm35.h"
#include "mpu6050.h"
#include "pcb_pins.h"
#include "protocol_can.h"
#include "reedswitch.h"
#include "rtos_err.h"
#include "sys_events.h"
#include "task_config.h"

static const char *TAG = "hwtest";

#define INPUT_POLL_MS         5
#define BUTTON_SETTLE_MS      30      /* as the wearable's button_gesture */
#define LM35_READ_PERIOD_MS   100
#define LM35_BURST            8       /* calibrated readings averaged per value */
#define LM35_WINDOW           100     /* 10 s of values: the noise is their spread */
#define MPU_READ_PERIOD_MS    100     /* ~20 samples at 200 Hz, well inside the 85-sample FIFO */
#define MPU_RETRY_MS          2000
#define MPU_BATCH             32
#define CAN_PERIOD_MS         1000
#define CAN_BURST_PERIOD_MS   5       /* key c: a busy bus, to see if it reaches the LM35 */
#define UI_PERIOD_MS          1000
#define KEY_POLL_MS           50
#define HELP_SHOWN_MS         15000
#define LOG_LINES             4
#define LOG_LINE_LEN          120
#define PANEL_MAX             4096

/* What this test sets on the MPU-6050 beyond the driver: a pulse on INT for every sample. */
#define MPU_REG_INT_ENABLE    0x38
#define MPU_INT_DATA_READY    0x01
#define MPU_INT_FIFO_OFLOW    0x10    /* what the driver enables, kept */
#define MPU_ADDR_ALT          0x69    /* AD0 high */

/* ---------------------------------------------------------------------------
 * State shared by the reading tasks and the panel (s_lock, held for a few lines)
 * ------------------------------------------------------------------------- */

typedef struct {
    input_track_t pir, door1, door2, button;
    bool          pir_warm;         /* warm-up over and the tracker restarted */

    bool          adc_ok;
    const char   *adc_error;
    window_t      lm35;             /* mV */

    bool          mpu_found;
    uint8_t       mpu_addr;
    uint32_t      mpu_samples;      /* since boot */
    float         ax, ay, az, gx, gy, gz, amag;   /* means of the last batch: g and deg/s */
    bool          mpu_seen_rest;

    bool          can_ok;
    uint32_t      can_sent, can_refused, can_echo, can_other;
    bool          can_burst;

    char          i2c_scan[80];
} hw_state_t;

static SemaphoreHandle_t s_lock;
static hw_state_t s_st;
static float s_lm35_storage[LM35_WINDOW];
static atomic_uint_fast32_t s_int_pulses;   /* MPU INT edges, counted by the ISR */

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_adc_channel;
static i2c_master_bus_handle_t s_bus;

static const lm35_chain_t LM35_CHAIN = {
    .slope_mv_per_c = LM35_CHAIN_SLOPE_MV_PER_C,
    .offset_mv = LM35_CHAIN_OFFSET_MV,
    .min_c = LM35_MIN_VALID_C,
    .max_c = LM35_MAX_VALID_C,
};

static uint32_t now_ms(void){
    return (uint32_t) (esp_timer_get_time() / 1000);
}

static void lock(void){
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void){
    xSemaphoreGive(s_lock);
}

/* ---------------------------------------------------------------------------
 * Logs: kept for the panel instead of scrolling it away
 * ------------------------------------------------------------------------- */

static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_log_ring[LOG_LINES][LOG_LINE_LEN];
static int s_log_next;
static char s_log_line[LOG_LINE_LEN];
static size_t s_log_len;
static bool s_log_escape;

/* esp_log may hand a line over in several pieces: they are joined until the newline. */
static int log_capture(const char *fmt, va_list args){
    char piece[LOG_LINE_LEN];
    int n = vsnprintf(piece, sizeof(piece), fmt, args);
    taskENTER_CRITICAL(&s_log_mux);
    for (const char *p = piece; *p != '\0'; p++) {
        char c = *p;
        if (s_log_escape) {                 /* skip colour codes: ESC [ ... m */
            s_log_escape = c != 'm';
            continue;
        }
        if (c == '\x1b') {
            s_log_escape = true;
        } else if (c == '\n') {
            s_log_line[s_log_len] = '\0';
            memcpy(s_log_ring[s_log_next], s_log_line, s_log_len + 1);
            s_log_next = (s_log_next + 1) % LOG_LINES;
            s_log_len = 0;
        } else if (c != '\r' && s_log_len < LOG_LINE_LEN - 1) {
            s_log_line[s_log_len++] = c;
        }
    }
    taskEXIT_CRITICAL(&s_log_mux);
    return n;
}

/* ---------------------------------------------------------------------------
 * Digital inputs
 * ------------------------------------------------------------------------- */

static void config_input(gpio_num_t pin, gpio_pullup_t pull_up, gpio_pulldown_t pull_down, gpio_int_type_t intr){
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up,
        .pull_down_en = pull_down,
        .intr_type = intr,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d not configured: %s", pin, esp_err_to_name(err));
        return;
    }
    /* Hardware glitch filter, as the real firmwares (slide "ESP32 - GPIO"). */
    gpio_pin_glitch_filter_config_t filter_config = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = pin,
    };
    gpio_glitch_filter_handle_t filter;
    if (gpio_new_pin_glitch_filter(&filter_config, &filter) == ESP_OK) {
        gpio_glitch_filter_enable(filter);
    }
}

static void IRAM_ATTR mpu_int_isr(void *arg){
    (void) arg;
    atomic_fetch_add_explicit(&s_int_pulses, 1, memory_order_relaxed);
}

/* Polled: a test has no deadline, and the settle logic lives in hwtest_checks. */
static void inputs_task(void *arg){
    (void) arg;
    while (1) {
        uint32_t now = now_ms();
        int pir = gpio_get_level(PCB_PIN_PIR);
        int door1 = gpio_get_level(PCB_PIN_DOOR1);
        int door2 = gpio_get_level(PCB_PIN_DOOR2);
        int button = gpio_get_level(PCB_PIN_BUTTON);

        lock();
        input_track_feed(&s_st.pir, pir, now);
        input_track_feed(&s_st.door1, door1, now);
        input_track_feed(&s_st.door2, door2, now);
        input_track_feed(&s_st.button, button, now);
        if (!s_st.pir_warm && now >= HCSR501_WARMUP_MS) {
            /* What it did while warming up proves nothing. */
            input_track_restart(&s_st.pir);
            s_st.pir_warm = true;
        }
        unlock();
        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}

/* ---------------------------------------------------------------------------
 * LM35 (ADC1, calibrated, as lm35.c)
 * ------------------------------------------------------------------------- */

static esp_err_t adc_init(void){
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(PCB_PIN_LM35, &unit, &s_adc_channel);
    if (err != ESP_OK) return err;
    if (unit != ADC_UNIT_1) return ESP_ERR_INVALID_ARG;

    adc_oneshot_unit_init_cfg_t unit_config = { .unit_id = unit };
    err = adc_oneshot_new_unit(&unit_config, &s_adc);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, s_adc_channel, &channel_config);
    if (err != ESP_OK) return err;
    /* The chain was fitted against real millivolts: the panel shows the ones TP4 measures. */
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = s_adc_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali);
}

static void analog_task(void *arg){
    (void) arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        int sum = 0;
        int n = 0;
        esp_err_t err = ESP_OK;
        for (int i = 0; i < LM35_BURST; i++) {
            int mv;
            err = adc_oneshot_get_calibrated_result(s_adc, s_cali, s_adc_channel, &mv);
            if (err != ESP_OK) break;
            sum += mv;
            n++;
        }
        lock();
        if (n == LM35_BURST) {
            window_push(&s_st.lm35, (float) sum / (float) n);
            s_st.adc_error = NULL;
        } else {
            s_st.adc_error = esp_err_to_name(err);
        }
        unlock();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LM35_READ_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------------
 * MPU-6050 (the wearable's driver) and its INT pin
 * ------------------------------------------------------------------------- */

static uint8_t mpu_probe(void){
    if (i2c_master_probe(s_bus, MPU6050_ADDR_DEFAULT, 50) == ESP_OK) return MPU6050_ADDR_DEFAULT;
    if (i2c_master_probe(s_bus, MPU_ADDR_ALT, 50) == ESP_OK) return MPU_ADDR_ALT;
    return 0;
}

static void mpu_task(void *arg){
    (void) arg;
    mpu6050_t mpu = { 0 };   /* zeroed once: a later init reuses the device on the bus */
    bool ready = false;

    while (1) {
        if (!ready) {
            uint8_t addr = mpu_probe();
            esp_err_t err = addr ? mpu6050_init(&mpu, s_bus, addr) : ESP_ERR_NOT_FOUND;
            if (err == ESP_OK) {
                const uint8_t int_enable[2] = { MPU_REG_INT_ENABLE, MPU_INT_DATA_READY | MPU_INT_FIFO_OFLOW };
                err = i2c_master_transmit(mpu.dev, int_enable, sizeof(int_enable), 50);
            }
            lock();
            s_st.mpu_found = err == ESP_OK;
            s_st.mpu_addr = addr;
            unlock();
            if (err != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(MPU_RETRY_MS));
                continue;
            }
            ready = true;
        }

        mpu6050_sample_t batch[MPU_BATCH];
        size_t count = 0;
        bool overflow = false;
        esp_err_t err = mpu6050_fifo_read(&mpu, batch, MPU_BATCH, &count, &overflow);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "MPU-6050 stopped answering (%s): trying again", esp_err_to_name(err));
            ready = false;
            lock();
            s_st.mpu_found = false;
            unlock();
            continue;
        }
        if (count > 0) {
            float sx = 0, sy = 0, sz = 0, sgx = 0, sgy = 0, sgz = 0, smag = 0, sgmag = 0;
            for (size_t i = 0; i < count; i++) {
                float ax = batch[i].ax / MPU6050_ACCEL_LSB_PER_G;
                float ay = batch[i].ay / MPU6050_ACCEL_LSB_PER_G;
                float az = batch[i].az / MPU6050_ACCEL_LSB_PER_G;
                float gx = batch[i].gx / MPU6050_GYRO_LSB_PER_DPS;
                float gy = batch[i].gy / MPU6050_GYRO_LSB_PER_DPS;
                float gz = batch[i].gz / MPU6050_GYRO_LSB_PER_DPS;
                sx += ax; sy += ay; sz += az;
                sgx += gx; sgy += gy; sgz += gz;
                smag += sqrtf(ax * ax + ay * ay + az * az);
                sgmag += sqrtf(gx * gx + gy * gy + gz * gz);
            }
            float n = (float) count;
            lock();
            s_st.mpu_samples += (uint32_t) count;
            s_st.ax = sx / n; s_st.ay = sy / n; s_st.az = sz / n;
            s_st.gx = sgx / n; s_st.gy = sgy / n; s_st.gz = sgz / n;
            s_st.amag = smag / n;
            if (mpu_at_rest(s_st.amag, sgmag / n)) {
                s_st.mpu_seen_rest = true;
            }
            unlock();
        }
        if (overflow) {
            ESP_LOGW(TAG, "MPU-6050 FIFO overflowed: samples lost");
        }
        vTaskDelay(pdMS_TO_TICKS(MPU_READ_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------------
 * CAN: a presence frame, read back through the transceiver in bench mode
 * ------------------------------------------------------------------------- */

static void on_can_frame(const can_rx_event_t *event, const can_id_info_t *info, void *ctx){
    (void) info;
    (void) ctx;
    bool own = event->id == CAN_MSG_ID(CAN_GROUP_PROJECT, CAN_NIBBLE_PRESENCE);
    lock();
    if (own) {
        s_st.can_echo++;
    } else {
        s_st.can_other++;
    }
    unlock();
}

static void can_task(void *arg){
    (void) arg;
    while (1) {
        lock();
        uint8_t presence = s_st.pir.stable == 1;
        bool burst = s_st.can_burst;
        unlock();

        /* The same frame the hub sends, so the test is harmless on the room's real bus. */
        esp_err_t err = can_transmit(CAN_NIBBLE_PRESENCE, &presence, CAN_LEN_PRESENCE);
        lock();
        if (err == ESP_OK) {
            s_st.can_sent++;
        } else {
            s_st.can_refused++;
        }
        unlock();
        vTaskDelay(pdMS_TO_TICKS(burst ? CAN_BURST_PERIOD_MS : CAN_PERIOD_MS));
    }
}

/* ---------------------------------------------------------------------------
 * Keys (console UART) and I2C scan
 * ------------------------------------------------------------------------- */

#if CONFIG_ESP_CONSOLE_UART
static bool s_keys = false;

static void keys_init(void){
    /* With the driver installed, printf goes through it too (uart_vfs_dev_use_driver). */
    if (uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) == ESP_OK) {
        uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
        s_keys = true;
    } else {
        ESP_LOGW(TAG, "keys off: the console UART driver did not start");
    }
}

static int key_read(TickType_t wait){
    uint8_t c;
    if (!s_keys) {
        vTaskDelay(wait);
        return -1;
    }
    return uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &c, 1, wait) == 1 ? c : -1;
}
#else
static void keys_init(void){
    ESP_LOGW(TAG, "keys off: the console is not a UART");
}

static int key_read(TickType_t wait){
    vTaskDelay(wait);
    return -1;
}
#endif

static void i2c_scan(void){
    char out[sizeof(s_st.i2c_scan)];
    size_t used = (size_t) snprintf(out, sizeof(out), "I2C:");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_bus, addr, 20) == ESP_OK && used < sizeof(out)) {
            used += (size_t) snprintf(out + used, sizeof(out) - used, " 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        snprintf(out, sizeof(out), "I2C: nenhum endereco respondeu (o MPU-6050 e 0x68)");
    }
    lock();
    memcpy(s_st.i2c_scan, out, sizeof(out));
    unlock();
}

static void restart_checks(void){
    lock();
    input_track_restart(&s_st.door1);
    input_track_restart(&s_st.door2);
    input_track_restart(&s_st.button);
    if (s_st.pir_warm) {
        input_track_restart(&s_st.pir);
    }
    s_st.mpu_seen_rest = false;
    s_st.can_sent = s_st.can_refused = s_st.can_echo = s_st.can_other = 0;
    window_clear(&s_st.lm35);
    unlock();
}

/* ---------------------------------------------------------------------------
 * Panel
 * ------------------------------------------------------------------------- */

static char s_panel[PANEL_MAX];
static size_t s_panel_len;

/* Appends to the panel; a panel too long is cut, never overrun. */
static void vout(const char *fmt, va_list args){
    size_t room = sizeof(s_panel) - s_panel_len;
    if (room <= 1) {
        return;
    }
    int n = vsnprintf(s_panel + s_panel_len, room, fmt, args);
    if (n > 0) {
        s_panel_len = (size_t) n < room ? s_panel_len + (size_t) n : sizeof(s_panel) - 1;
    }
}

static void out(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);
    vout(fmt, args);
    va_end(args);
}

static const char *mark(check_state_t state){
    switch (state) {
        case CHECK_OK:   return "[ OK  ]";
        case CHECK_FAIL: return "[FALHA]";
        default:         return "[ ... ]";
    }
}

static void line(const char *name, const char *where, check_verdict_t v, const char *fmt, ...){
    out("%s %-9s %-7s ", mark(v.state), name, where);
    va_list args;
    va_start(args, fmt);
    vout(fmt, args);
    va_end(args);
    out("\n");
    if (v.why != NULL) {
        out("                          -> %s\n", v.why);
    }
}

static const char *level_name(int level, const char *high, const char *low){
    return level < 0 ? "?" : (level ? high : low);
}

typedef struct {
    hw_state_t       st;
    window_summary_t lm35;
    float            mpu_rate_hz;   /* -1 while not measured over a full interval */
    float            int_rate_hz;
    uint32_t         now;
} snapshot_t;

static void render(const snapshot_t *s, bool panel, bool help){
    static const char *const status_text[] = {
        [SUMMARY_WAITING] = "azul (esperando acao)",
        [SUMMARY_ALL_OK] = "verde (tudo OK)",
        [SUMMARY_FAILED] = "vermelho (falha)",
    };
    const hw_state_t *st = &s->st;

    check_verdict_t v[8];
    v[0] = st->adc_ok ? check_lm35(&LM35_CHAIN, &s->lm35)
                      : (check_verdict_t) { CHECK_FAIL, "o ADC do GPIO 4 nao iniciou: veja os ultimos avisos" };
    v[1] = check_pir(&st->pir, s->now, HCSR501_WARMUP_MS);
    v[2] = check_both_levels(&st->door1, "feche e abra o contato (ima no reed, ou um fio entre os dois pinos do J3)");
    v[3] = check_both_levels(&st->door2, "feche e abra o contato (ima no reed, ou um fio entre os dois pinos do J4)");
    v[4] = check_both_levels(&st->button, "aperte e solte o botao");
    v[5] = check_mpu(st->mpu_found, s->mpu_rate_hz, MPU6050_SAMPLE_RATE_HZ, st->mpu_seen_rest);
    v[6] = check_mpu_int(st->mpu_found, s->int_rate_hz, MPU6050_SAMPLE_RATE_HZ);
    v[7] = check_can(st->can_ok, st->can_sent, st->can_echo);
    size_t ok = 0;
    summary_t summary = check_summary(v, 8, &ok);
    indicators_set_status(summary == SUMMARY_ALL_OK ? IND_STATUS_OK
                          : summary == SUMMARY_FAILED ? IND_STATUS_FAULT : IND_STATUS_BOOT);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    s_panel_len = 0;
    if (panel) {
        out("\x1b[H\x1b[J");   /* cursor home, clear: the panel is redrawn in place */
    } else {
        out("\n------------------------------------------------------------------------\n");
    }
    out("=== Teste de hardware da PCB v1 ===   t = %" PRIu32 " s   [%u/8 OK]   LED: %s\n",
        s->now / 1000, (unsigned) ok, status_text[summary]);
    out("ESP32-S3 rev v%d.%d | MAC " MACSTR " | ESP-IDF %s | LED da DevKit v%s (GPIO %d)\n\n",
        chip.revision / 100, chip.revision % 100, MAC2STR(mac), esp_get_idf_version(),
#ifdef CONFIG_PCB_DEVKIT_V1_1
        "1.1",
#else
        "1.0",
#endif
        PCB_PIN_STATUS_LED);

    if (s->lm35.n > 0) {
        line("LM35", "GPIO 4", v[0], "%4.0f mV  %5.2f C  ruido %.2f C  (%.0f a %.0f mV em %u leituras; TP4 deve medir o mesmo)",
             s->lm35.mean, lm35_celsius(&LM35_CHAIN, s->lm35.mean), s->lm35.std / LM35_CHAIN.slope_mv_per_c,
             s->lm35.min, s->lm35.max, (unsigned) s->lm35.n);
    } else {
        line("LM35", "GPIO 4", v[0], "%s", st->adc_error ? st->adc_error : "--");
    }
    uint32_t warm_left = s->now < HCSR501_WARMUP_MS ? (HCSR501_WARMUP_MS - s->now + 999) / 1000 : 0;
    if (warm_left > 0) {
        line("PIR", "J2", v[1], "%-9s faltam %" PRIu32 " s de aquecimento",
             level_name(st->pir.stable, "detectado", "livre"), warm_left);
    } else {
        line("PIR", "J2", v[1], "%-9s detectou %" PRIu32 "x", level_name(st->pir.stable, "detectado", "livre"),
             st->pir.rises);
    }
    /* Doors: the reed closes to GND, so high is open (internal pull-up). */
    line("Porta 1", "J3", v[2], "%-9s abriu %" PRIu32 "x, fechou %" PRIu32 "x",
         level_name(st->door1.stable, "aberta", "fechada"), st->door1.rises, st->door1.falls);
    line("Porta 2", "J4", v[3], "%-9s abriu %" PRIu32 "x, fechou %" PRIu32 "x",
         level_name(st->door2.stable, "aberta", "fechada"), st->door2.rises, st->door2.falls);
    line("Botao", "SW1", v[4], "%-9s apertou %" PRIu32 "x", level_name(st->button.stable, "solto", "apertado"),
         st->button.falls);
    if (st->mpu_found) {
        line("MPU-6050", "J5", v[5], "0x%02X  %3.0f amostras/s  |a| %.2f g  a (%+.2f %+.2f %+.2f) g  w (%+6.1f %+6.1f %+6.1f) dps",
             st->mpu_addr, s->mpu_rate_hz < 0 ? 0.0f : s->mpu_rate_hz, st->amag, st->ax, st->ay, st->az,
             st->gx, st->gy, st->gz);
    } else {
        line("MPU-6050", "J5", v[5], "--");
    }
    line("INT MPU", "GPIO 17", v[6], "%3.0f pulsos/s", s->int_rate_hz < 0 ? 0.0f : s->int_rate_hz);
    line("CAN", "J1", v[7], "enviados %" PRIu32 " (recusados %" PRIu32 ")  eco %" PRIu32 "  de outros nos %" PRIu32 "%s",
         st->can_sent, st->can_refused, st->can_echo, st->can_other,
         st->can_burst ? "  [CAN rapido: 200 quadros/s]" : "");

    if (st->i2c_scan[0] != '\0') {
        out("\n%s\n", st->i2c_scan);
    }

    out("\nUltimos avisos:\n");
    int shown = 0;
    char lines[LOG_LINES][LOG_LINE_LEN];
    taskENTER_CRITICAL(&s_log_mux);
    int next = s_log_next;
    memcpy(lines, s_log_ring, sizeof(lines));
    taskEXIT_CRITICAL(&s_log_mux);
    for (int i = 0; i < LOG_LINES; i++) {
        const char *l = lines[(next + i) % LOG_LINES];
        if (l[0] != '\0') {
            out("  %s\n", l);
            shown++;
        }
    }
    if (shown == 0) {
        out("  (nenhum)\n");
    }

    out("\nTeclas: c = CAN rapido (%s)  i = varrer o I2C  r = recomecar  p = %s  h = ajuda\n",
        st->can_burst ? "ligado" : "desligado", panel ? "rolar em vez de redesenhar" : "redesenhar");
    if (help) {
        out("\nAjuda:\n"
            "  - ligue os DOIS cabos: a USB-C da PCB (5 V do LM35, LM358 e PIR) e a USB \"UART\" da DevKit;\n"
            "  - [ OK  ] conferido, [ ... ] esperando uma acao ou uma medida, [FALHA] veja a linha \"->\";\n"
            "  - c: CAN a 200 quadros/s; compare o ruido do LM35 com o CAN ligado e desligado;\n"
            "  - i: lista os enderecos do I2C (o MPU-6050 e 0x68; 0x69 com o AD0 alto);\n"
            "  - r: recomeca a conferencia (bordas, eco do CAN, leituras do LM35);\n"
            "  - detalhes em firmware/hwtest/README.md.\n");
    }
    fwrite(s_panel, 1, s_panel_len, stdout);
    fflush(stdout);
}

static void ui_task(void *arg){
    (void) arg;
    bool panel = true;
    uint32_t help_until = HELP_SHOWN_MS;
    uint32_t prev_t = now_ms();
    uint32_t prev_samples = 0;
    uint32_t prev_pulses = (uint32_t) atomic_load(&s_int_pulses);
    bool prev_found = false;
    uint32_t next_frame = prev_t + UI_PERIOD_MS;

    while (1) {
        int key = key_read(pdMS_TO_TICKS(KEY_POLL_MS));
        switch (key) {
            case 'c': case 'C':
                lock();
                s_st.can_burst = !s_st.can_burst;
                unlock();
                break;
            case 'i': case 'I':
                i2c_scan();
                break;
            case 'r': case 'R':
                restart_checks();
                break;
            case 'p': case 'P':
                panel = !panel;
                break;
            case 'h': case 'H': case '?':
                help_until = now_ms() + HELP_SHOWN_MS;
                break;
            default:
                break;
        }
        if (key < 0 && now_ms() < next_frame) {
            continue;
        }

        snapshot_t s;
        s.now = now_ms();
        lock();
        s.st = s_st;
        s.lm35 = window_summary(&s_st.lm35);
        unlock();
        s.st.lm35 = (window_t) { 0 };   /* the copy shares the buffer: use the summary only */

        /* Rates over the last interval, valid only when the MPU answered through all of it. */
        uint32_t pulses = (uint32_t) atomic_load(&s_int_pulses);
        float dt = (float) (s.now - prev_t) / 1000.0f;
        bool valid = prev_found && s.st.mpu_found && dt >= 0.5f;
        s.mpu_rate_hz = valid ? (float) (s.st.mpu_samples - prev_samples) / dt : -1.0f;
        s.int_rate_hz = valid ? (float) (pulses - prev_pulses) / dt : -1.0f;
        if (dt >= 0.5f) {
            prev_t = s.now;
            prev_samples = s.st.mpu_samples;
            prev_pulses = pulses;
            prev_found = s.st.mpu_found;
        }
        render(&s, panel, s.now < help_until);
        if (s.now >= next_frame) {
            next_frame = s.now + UI_PERIOD_MS;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Boot
 * ------------------------------------------------------------------------- */

void app_main(void){
    /* The panel shows the warnings; the drivers' info lines would only scroll it away. */
    esp_log_set_vprintf(log_capture);
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("i2c.master", ESP_LOG_NONE);   /* every probe of an empty address is an "error" */
    printf("\nTeste de hardware da PCB v1: iniciando...\n");

    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(sys_events_init());

    indicators_config_t indicators = {
        .led_gpio = PCB_PIN_STATUS_LED,
        .buzzer_gpio = GPIO_NUM_NC,
    };
    if (indicators_init(&indicators) == ESP_OK) {
        indicators_set_status(IND_STATUS_BOOT);
    }

    uint32_t now = now_ms();
    input_track_init(&s_st.pir, HCSR501_SETTLE_MS, now);
    input_track_init(&s_st.door1, REEDSWITCH_SETTLE_MS, now);
    input_track_init(&s_st.door2, REEDSWITCH_SETTLE_MS, now);
    input_track_init(&s_st.button, BUTTON_SETTLE_MS, now);
    window_init(&s_st.lm35, s_lm35_storage, LM35_WINDOW);

    // ========== INPUTS ==========
    config_input(PCB_PIN_PIR, HCSR501_GPIO_PULL_UP, HCSR501_GPIO_PULL_DOWN, GPIO_INTR_DISABLE);
    config_input(PCB_PIN_DOOR1, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE);
    config_input(PCB_PIN_DOOR2, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE);
    config_input(PCB_PIN_BUTTON, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE);
    /* INT is push-pull from the MPU; the pull-down keeps it quiet when the GY-521 is out. */
    config_input(PCB_PIN_MPU_INT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_ENABLE, GPIO_INTR_POSEDGE);
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK) {
        err = gpio_isr_handler_add(PCB_PIN_MPU_INT, mpu_int_isr, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MPU INT not counted: %s", esp_err_to_name(err));
    }

    // ========== LM35 ==========
    err = adc_init();
    s_st.adc_ok = err == ESP_OK;
    if (!s_st.adc_ok) {
        ESP_LOGW(TAG, "ADC on GPIO%d not started: %s", PCB_PIN_LM35, esp_err_to_name(err));
    }

    // ========== I2C (MPU-6050) ==========
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PCB_PIN_I2C_SDA,
        .scl_io_num = PCB_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus not started: %s", esp_err_to_name(err));
    }

    // ========== CAN ==========
    /* Before can_init(), which starts the task that reads the registry. */
    err = can_rx_handler_register(CAN_GROUP_ANY, CAN_KIND_ANY, on_can_frame, NULL);
    if (err == ESP_OK) {
        can_config_t can_config = {
            .tx_gpio = PCB_PIN_CAN_TX,
            .rx_gpio = PCB_PIN_CAN_RX,
            .group = CAN_GROUP_PROJECT,
        };
        err = can_init(&can_config);
    }
    s_st.can_ok = err == ESP_OK;
    if (!s_st.can_ok) {
        ESP_LOGW(TAG, "CAN not started: %s", esp_err_to_name(err));
    }

    keys_init();

    // ========== TASKS ==========
    /* Core 1 reads the board; core 0 has the CAN transport and the panel. */
    ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(inputs_task, "inputs", STACK_SMALL, NULL,
                                                                PRIO_MEDIA, NULL, CORE_REALTIME)));
    if (s_st.adc_ok) {
        ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(analog_task, "analog", STACK_SMALL, NULL,
                                                                    PRIO_BAIXA, NULL, CORE_REALTIME)));
    }
    if (s_bus != NULL) {
        ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(mpu_task, "mpu", STACK_MEDIUM, NULL,
                                                                    PRIO_MEDIA, NULL, CORE_REALTIME)));
    }
    if (s_st.can_ok) {
        ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(can_task, "can_test", STACK_SMALL, NULL,
                                                                    PRIO_BAIXA, NULL, CORE_PROTOCOL)));
    }
    ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(ui_task, "panel", STACK_LARGE, NULL,
                                                                PRIO_MINIMA, NULL, CORE_PROTOCOL)));
}
