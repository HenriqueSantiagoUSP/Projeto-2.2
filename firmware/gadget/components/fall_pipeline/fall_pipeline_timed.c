#include "freertos/FreeRTOS.h"

#include "esp_cpu.h"
#include "fall_pipeline.h"

/* Device-only half of fall_pipeline: everything that needs the CPU or FreeRTOS. */

static portMUX_TYPE s_bench_lock = portMUX_INITIALIZER_UNLOCKED;

uint32_t fall_pipeline_classify_timed(fall_pipeline_t *fp, fp_result_t *out){
    /*
     * A spinlock, not just interrupts off: on the dual-core ESP32-S3 that is what keeps
     * both ISRs and the other core out of the count (slide "Segmentação e atributos").
     * It runs once per trigger (a few times a minute) and well under a millisecond.
     */
    taskENTER_CRITICAL(&s_bench_lock);
    uint32_t start = esp_cpu_get_cycle_count();
    fall_pipeline_classify(fp, out);
    uint32_t end = esp_cpu_get_cycle_count();
    taskEXIT_CRITICAL(&s_bench_lock);
    return end - start;
}
