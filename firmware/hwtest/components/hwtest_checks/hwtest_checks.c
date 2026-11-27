#include <math.h>

#include "hwtest_checks.h"

/* ---------------------------------------------------------------------------
 * Digital input
 * ------------------------------------------------------------------------- */

void input_track_init(input_track_t *t, uint32_t settle_ms, uint32_t now_ms){
    *t = (input_track_t) {
        .settle_ms = settle_ms,
        .stable = -1,
        .raw = -1,
        .raw_since_ms = now_ms,
    };
}

bool input_track_feed(input_track_t *t, int level, uint32_t now_ms){
    level = level ? 1 : 0;
    if (level != t->raw) {
        t->raw = level;
        t->raw_since_ms = now_ms;
    }
    if (t->raw == t->stable || now_ms - t->raw_since_ms < t->settle_ms) {
        return false;   /* unchanged, or still bouncing */
    }
    int previous = t->stable;
    t->stable = t->raw;
    if (t->stable) {
        t->seen_high = true;
    } else {
        t->seen_low = true;
    }
    if (previous == 0) {
        t->rises++;
    } else if (previous == 1) {
        t->falls++;
    }
    return true;
}

void input_track_restart(input_track_t *t){
    t->rises = 0;
    t->falls = 0;
    t->seen_low = t->stable == 0;
    t->seen_high = t->stable == 1;
}

/* ---------------------------------------------------------------------------
 * Window statistics
 * ------------------------------------------------------------------------- */

void window_init(window_t *w, float *storage, size_t cap){
    *w = (window_t) { .buf = storage, .cap = cap };
}

void window_push(window_t *w, float value){
    if (w->cap == 0) {
        return;
    }
    w->buf[w->head] = value;
    w->head = (w->head + 1) % w->cap;
    if (w->len < w->cap) {
        w->len++;
    }
}

void window_clear(window_t *w){
    w->len = 0;
    w->head = 0;
}

window_summary_t window_summary(const window_t *w){
    window_summary_t s = { .n = w->len };
    if (w->len == 0) {
        return s;
    }
    /* The order does not matter for these: the first len slots hold the window either way. */
    double sum = 0.0;
    s.min = s.max = w->buf[0];
    for (size_t i = 0; i < w->len; i++) {
        float v = w->buf[i];
        sum += v;
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
    }
    double mean = sum / (double) w->len;
    double sq = 0.0;
    for (size_t i = 0; i < w->len; i++) {
        double d = w->buf[i] - mean;
        sq += d * d;
    }
    s.mean = (float) mean;
    s.std = (float) sqrt(sq / (double) w->len);
    return s;
}

/* ---------------------------------------------------------------------------
 * Verdicts
 * ------------------------------------------------------------------------- */

static check_verdict_t verdict(check_state_t state, const char *why){
    return (check_verdict_t) { .state = state, .why = why };
}

float lm35_celsius(const lm35_chain_t *chain, float mv){
    return (mv - chain->offset_mv) / chain->slope_mv_per_c;
}

bool lm35_noisy(const lm35_chain_t *chain, const window_summary_t *mv){
    /* The spread in mV, through the same slope as the temperature. */
    return mv->n >= 10 && mv->std / chain->slope_mv_per_c > CHECK_LM35_NOISE_MAX_C;
}

check_verdict_t check_lm35(const lm35_chain_t *chain, const window_summary_t *mv){
    if (mv->n == 0) {
        return verdict(CHECK_WAITING, "aguardando as leituras");
    }
    if (mv->mean < CHECK_LM35_NO_SIGNAL_MV) {
        return verdict(CHECK_FAIL, "sem sinal: a USB-C da PCB esta ligada? (o LM35, o LM358 e o PIR usam o 5 V dela)");
    }
    float celsius = lm35_celsius(chain, mv->mean);
    if (celsius < chain->min_c || celsius > chain->max_c) {
        return verdict(CHECK_FAIL, "fora da faixa de 0 a 57 C: confira o LM35 (U2), o LM358 (U3) e o ganho (R1, R2)");
    }
    if (lm35_noisy(chain, mv)) {
        return verdict(CHECK_OK, "ruido acima de 0.2 C: tecle c para ver se vem do CAN");
    }
    return verdict(CHECK_OK, NULL);
}

check_verdict_t check_both_levels(const input_track_t *t, const char *wait_hint){
    if (t->seen_low && t->seen_high) {
        return verdict(CHECK_OK, NULL);
    }
    return verdict(CHECK_WAITING, wait_hint);
}

check_verdict_t check_pir(const input_track_t *t, uint32_t now_ms, uint32_t warmup_ms){
    if (now_ms < warmup_ms) {
        return verdict(CHECK_WAITING, "aquecendo (o PIR precisa de 1 min depois de ligado)");
    }
    if (t->seen_low && t->seen_high) {
        return verdict(CHECK_OK, NULL);
    }
    if (!t->seen_high) {
        return verdict(CHECK_WAITING, "passe a mao na frente do PIR");
    }
    return verdict(CHECK_WAITING, "saia da frente do PIR e espere ele voltar a livre");
}

bool mpu_at_rest(float amag_g, float gyro_dps){
    return amag_g >= CHECK_MPU_REST_MIN_G && amag_g <= CHECK_MPU_REST_MAX_G && gyro_dps < 10.0f;
}

check_verdict_t check_mpu(bool found, float rate_hz, float expected_hz, bool seen_rest){
    if (!found) {
        return verdict(CHECK_FAIL, "nao responde no I2C (0x68/0x69): confira o GY-521 no J5 (3V3, GND, SDA 1, SCL 2)");
    }
    if (rate_hz < 0.0f) {
        return verdict(CHECK_WAITING, "medindo a taxa de amostragem");
    }
    if (fabsf(rate_hz - expected_hz) > expected_hz * CHECK_MPU_RATE_TOL) {
        return verdict(CHECK_FAIL, rate_hz == 0.0f ? "responde, mas nao entrega amostras (FIFO parado)"
                                                   : "taxa de amostragem fora dos 200 amostras/s");
    }
    if (!seen_rest) {
        return verdict(CHECK_WAITING, "deixe a placa parada um instante: |a| deve dar ~1 g");
    }
    return verdict(CHECK_OK, NULL);
}

check_verdict_t check_mpu_int(bool mpu_found, float pulses_hz, float expected_hz){
    if (!mpu_found) {
        return verdict(CHECK_WAITING, "depende do MPU-6050");
    }
    if (pulses_hz < 0.0f) {
        return verdict(CHECK_WAITING, "medindo os pulsos");
    }
    if (pulses_hz == 0.0f) {
        return verdict(CHECK_FAIL, "sem pulsos: confira o INT do GY-521 (pino 8 do J5) ate o GPIO 17");
    }
    if (fabsf(pulses_hz - expected_hz) > expected_hz * CHECK_MPU_RATE_TOL) {
        return verdict(CHECK_FAIL, "pulsos fora da taxa de amostragem: mau contato no INT?");
    }
    return verdict(CHECK_OK, NULL);
}

check_verdict_t check_can(bool init_ok, uint32_t sent, uint32_t echoed){
    if (!init_ok) {
        return verdict(CHECK_FAIL, "o CAN nao iniciou: veja os ultimos avisos");
    }
    if (echoed > 0) {
        return verdict(CHECK_OK, NULL);
    }
    if (sent < 3) {
        return verdict(CHECK_WAITING, "enviando os primeiros quadros");
    }
    return verdict(CHECK_FAIL, "sem eco: o transceptor esta no J1? TX/RX (GPIO 13/14) trocados? 3V3 no J1?");
}

/* ---------------------------------------------------------------------------
 * Summary
 * ------------------------------------------------------------------------- */

summary_t check_summary(const check_verdict_t *verdicts, size_t count, size_t *ok_count){
    size_t ok = 0;
    bool failed = false;
    for (size_t i = 0; i < count; i++) {
        ok += verdicts[i].state == CHECK_OK;
        failed |= verdicts[i].state == CHECK_FAIL;
    }
    if (ok_count != NULL) {
        *ok_count = ok;
    }
    if (failed) {
        return SUMMARY_FAILED;
    }
    return ok == count ? SUMMARY_ALL_OK : SUMMARY_WAITING;
}
