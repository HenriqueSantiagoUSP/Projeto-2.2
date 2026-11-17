#include "report.h"

#include <math.h>
#include <string.h>

bool report_due(const report_t *r, float value, float delta, uint64_t max_interval_ms, uint64_t now_ms){
    if (!r->sent) {
        return true;
    }
    if (delta > 0.0f && fabsf(value - r->value) >= delta) {
        return true;
    }
    return now_ms - r->sent_ms >= max_interval_ms;
}

void report_mark(report_t *r, float value, uint64_t now_ms){
    r->sent = true;
    r->value = value;
    r->sent_ms = now_ms;
}

void report_reset(report_t *r){
    memset(r, 0, sizeof(*r));
}

void report_rms_add(report_rms_t *acc, uint16_t value){
    acc->sum_sq += (uint64_t) value * value;
    acc->count++;
}

uint16_t report_rms_take(report_rms_t *acc){
    uint16_t rms = 0;
    if (acc->count > 0) {
        double mean_sq = (double) acc->sum_sq / acc->count;
        rms = (uint16_t) lround(sqrt(mean_sq));   /* never above the largest input, so it fits */
    }
    acc->sum_sq = 0;
    acc->count = 0;
    return rms;
}
