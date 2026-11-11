/*
 * Host tests of fall_pipeline (run_tests.sh builds them and runs them from vetores/):
 *  - the trigger on synthetic |a| (the same cases as modelo/tests/test_blocos.py);
 *  - the filter start and the window right after boot;
 *  - parity with Python, from what 09_exportar_vestivel.py exported with the model:
 *    the classifiers on Python's own feature vectors, then the whole pipeline (filter,
 *    trigger, window, features, classifier) on SisFall excerpts.
 * ESP-DSP is replaced by stubs/esp_dsp.h.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fall_pipeline.h"

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                        \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

#define CHECKF(cond, ...) do {                                                  \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                       \
            printf(__VA_ARGS__);                                                \
            printf("\n");                                                       \
        }                                                                       \
    } while (0)

#define N       FALL_MODEL_WINDOW_LEN
#define NF      FALL_MODEL_N_FEATURES

static fall_pipeline_t s_fp;   /* ~20 KB: not on the stack */

/* ---------------------------------------------------------------------------
 * Trigger
 * ------------------------------------------------------------------------- */

static int trigger_run(const float *a, int n, float l_on, uint32_t first_index, uint32_t *peaks, int max){
    fp_trigger_t trigger;
    fp_trigger_init(&trigger, l_on);
    int count = 0;
    for (int i = 0; i < n; i++) {
        uint32_t index;
        float value;
        if (fp_trigger_feed(&trigger, a[i], first_index + (uint32_t) i, &index, &value) && count < max) {
            peaks[count++] = index;
        }
    }
    return count;
}

static void fill(float *a, int n, float value){
    for (int i = 0; i < n; i++) {
        a[i] = value;
    }
}

static void test_trigger_impact_swallows_the_step_before_it(void){
    static float a[1000];
    uint32_t peaks[8];
    fill(a, 1000, 1.0f);
    a[100] = a[200] = a[300] = a[400] = 2.2f;   /* steps */
    a[460] = 6.0f;                              /* impact 0.3 s after the last one */
    int n = trigger_run(a, 1000, 2.0f, 0, peaks, 8);
    CHECK(n == 4);
    CHECK(peaks[0] == 100 && peaks[1] == 200 && peaks[2] == 300 && peaks[3] == 460);
    CHECK(trigger_run(a, 1000, 7.0f, 0, peaks, 8) == 0);
}

static void test_trigger_keeps_only_the_highest_of_a_rising_run(void){
    static float a[600];
    uint32_t peaks[8];
    fill(a, 600, 1.0f);
    a[50] = 3.0f;
    a[140] = 4.0f;
    a[230] = 5.0f;
    int n = trigger_run(a, 600, 2.0f, 0, peaks, 8);
    CHECK(n == 1 && peaks[0] == 230);
}

static void test_trigger_never_starts_on_the_way_down(void){
    static float a[300];
    uint32_t peaks[8];
    for (int i = 0; i < 300; i++) {
        a[i] = 4.0f - 3.0f * (float) i / 299.0f;   /* starts above L_on and only falls */
    }
    CHECK(trigger_run(a, 300, 2.0f, 0, peaks, 8) == 0);

    /* A peak is not final until the gap has passed. */
    fill(a, 150, 1.0f);
    a[100] = 3.0f;
    CHECK(trigger_run(a, 150, 2.0f, 0, peaks, 8) == 0);
}

static void test_trigger_across_the_wrap_of_the_sample_count(void){
    static float a[400];
    uint32_t peaks[8];
    fill(a, 400, 1.0f);
    a[60] = 3.0f;
    uint32_t first = UINT32_MAX - 99;              /* the count wraps at a[100] */
    int n = trigger_run(a, 400, 2.0f, first, peaks, 8);
    CHECK(n == 1 && peaks[0] == first + 60);
}

/* ---------------------------------------------------------------------------
 * Filter and window
 * ------------------------------------------------------------------------- */

static void test_filter_starts_settled(void){
    fall_pipeline_init(&s_fp, 2.0f, FP_CLASSIFIER_RF);
    const float still[FP_CHANNELS] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };
    bool any = false;
    for (int i = 0; i < 1000; i++) {
        any |= fall_pipeline_feed(&s_fp, still);
    }
    float newest;
    sp_ring_copy_last(&s_fp.ring[2], 1, &newest);
    CHECK(!any);                                 /* 1 g at rest: nothing to trigger */
    CHECK(fabsf(newest - 1.0f) < 1e-5f);         /* no ringing from a zero start */
}

static void test_window_right_after_boot_repeats_the_oldest_sample(void){
    fall_pipeline_init(&s_fp, 2.0f, FP_CLASSIFIER_RF);
    int completed_at = -1;
    for (int i = 0; i < 400 && completed_at < 0; i++) {
        float d = (float) (i - 40) / 4.0f;
        float sample[FP_CHANNELS] = { 0.0f, 0.0f, 1.0f + 4.0f * expf(-d * d), 0.0f, 0.0f, 0.0f };
        if (fall_pipeline_feed(&s_fp, sample)) {
            completed_at = i;
        }
    }
    /* The filtered bump peaks a little after sample 40; the window closes POST - 1 later. */
    CHECK(completed_at > 0);
    CHECK((uint32_t) completed_at == s_fp.window_peak_index + FALL_MODEL_POST - 1);
    CHECK(s_fp.window_a_peak > 2.0f);
    /* Fewer than N samples so far: the start of the window repeats the first one (1 g). */
    CHECK(fabsf(s_fp.window[2][0] - 1.0f) < 1e-5f && s_fp.window[2][0] == s_fp.window[2][1]);

    fp_result_t r;
    fall_pipeline_classify(&s_fp, &r);
    CHECK(r.peak_index == s_fp.window_peak_index);
    CHECK(r.prob >= 0.0f && r.prob <= 1.0f);
    CHECK(r.fall == (r.prob >= fp_classifier_threshold(FP_CLASSIFIER_RF)));
}

/* ---------------------------------------------------------------------------
 * Parity with Python
 * ------------------------------------------------------------------------- */

typedef struct {
    char     file[64];
    uint32_t peak;
    float    a_peak;
    float    f[NF];
    float    lr;
    float    rf;
} expected_t;

#define MAX_EXPECTED 256
static expected_t s_expected[MAX_EXPECTED];
static int s_n_expected = 0;

static void test_feature_names_match_the_model(void){
    for (int i = 0; i < NF; i++) {
        CHECKF(strcmp(fall_feature_names[i], fall_model_feature_names[i]) == 0,
               "feature %d: C \"%s\", model \"%s\"", i, fall_feature_names[i], fall_model_feature_names[i]);
    }
}

static bool load_expected(void){
    FILE *f = fopen("esperado.csv", "r");
    if (f == NULL) {
        printf("  esperado.csv not found: run from test_host/vetores (run_tests.sh does)\n");
        return false;
    }
    static char line[4096];
    if (fgets(line, sizeof(line), f) == NULL) {
        fclose(f);
        return false;
    }
    /* Header: arquivo,pico,a_pico,<features in model order>,lr_prob,rf_prob */
    char *tok = strtok(line, ",\r\n");
    for (int col = 0; tok != NULL; col++, tok = strtok(NULL, ",\r\n")) {
        if (col >= 3 && col < 3 + NF) {
            CHECKF(strcmp(tok, fall_model_feature_names[col - 3]) == 0, "column %d is \"%s\"", col, tok);
        }
    }
    while (s_n_expected < MAX_EXPECTED && fgets(line, sizeof(line), f) != NULL) {
        expected_t *e = &s_expected[s_n_expected];
        tok = strtok(line, ",\r\n");
        if (tok == NULL) {
            continue;
        }
        snprintf(e->file, sizeof(e->file), "%s", tok);
        e->peak = (uint32_t) strtoul(strtok(NULL, ","), NULL, 10);
        e->a_peak = strtof(strtok(NULL, ","), NULL);
        for (int i = 0; i < NF; i++) {
            e->f[i] = strtof(strtok(NULL, ","), NULL);
        }
        e->lr = strtof(strtok(NULL, ","), NULL);
        e->rf = strtof(strtok(NULL, ",\r\n"), NULL);
        s_n_expected++;
    }
    fclose(f);
    return s_n_expected > 0;
}

/* The classifiers alone, on Python's feature vectors: differences can only come from the model export. */
static void test_classifiers_on_python_features(void){
    float worst_lr = 0.0f, worst_rf = 0.0f;
    for (int k = 0; k < s_n_expected; k++) {
        const expected_t *e = &s_expected[k];
        float lr = fp_classifier_prob(FP_CLASSIFIER_LR, e->f);
        float rf = fp_classifier_prob(FP_CLASSIFIER_RF, e->f);
        worst_lr = fmaxf(worst_lr, fabsf(lr - e->lr));
        worst_rf = fmaxf(worst_rf, fabsf(rf - e->rf));
        CHECKF(fabsf(lr - e->lr) < 1e-4f, "%s @%u: LR %.6f, Python %.6f", e->file, e->peak, lr, e->lr);
        /* One tree may land differently when a feature sits on a split (float vs double parsing). */
        CHECKF(fabsf(rf - e->rf) <= 1.0f / FALL_RF_N_TREES + 1e-5f, "%s @%u: RF %.6f, Python %.6f",
               e->file, e->peak, rf, e->rf);
    }
    printf("  classifiers on Python's features (%d windows): max |dP| LR %.2g, RF %.2g\n",
           s_n_expected, worst_lr, worst_rf);
}

static float feature_tolerance(int i, float expected){
    if (i == FF_A_ZCR) {
        return 4.0f / (N - 1);   /* |a| - mean right at zero can flip a few crossings */
    }
    return 2e-3f * fmaxf(1.0f, fabsf(expected));
}

static bool decided_alike(float c, float p, float threshold){
    return (c >= threshold) == (p >= threshold) || fabsf(p - threshold) < 0.05f;
}

/* Feeds one excerpt sample by sample and checks every window against Python. */
static void check_file(const char *name){
    char path[sizeof(s_expected[0].file) + 8];
    snprintf(path, sizeof(path), "%.*s.csv", (int) sizeof(s_expected[0].file) - 1, name);
    FILE *f = fopen(path, "r");
    CHECKF(f != NULL, "%s not found", path);
    if (f == NULL) {
        return;
    }

    int first = -1, count = 0;
    for (int k = 0; k < s_n_expected; k++) {
        if (strcmp(s_expected[k].file, name) == 0) {
            if (first < 0) {
                first = k;
            }
            count++;
        }
    }

    fall_pipeline_init(&s_fp, FALL_MODEL_TRIGGER_G, FP_CLASSIFIER_RF);
    char line[256];
    fgets(line, sizeof(line), f);   /* header */
    int got = 0;
    float worst_feature = 0.0f;
    int worst_index = 0;
    float worst_rf = 0.0f, worst_lr = 0.0f;
    while (fgets(line, sizeof(line), f) != NULL) {
        float s[FP_CHANNELS];
        if (sscanf(line, "%f,%f,%f,%f,%f,%f", &s[0], &s[1], &s[2], &s[3], &s[4], &s[5]) != FP_CHANNELS) {
            continue;
        }
        if (!fall_pipeline_feed(&s_fp, s)) {
            continue;
        }
        fp_result_t r;
        fall_pipeline_classify(&s_fp, &r);
        if (got >= count) {
            got++;
            continue;
        }
        const expected_t *e = &s_expected[first + got];
        got++;

        CHECKF(r.peak_index == e->peak, "%s: peak at %u, Python %u", name, r.peak_index, e->peak);
        CHECKF(fabsf(r.a_peak - e->a_peak) < 1e-4f * fmaxf(1.0f, e->a_peak), "%s @%u: |a| peak %.6f, Python %.6f",
               name, e->peak, r.a_peak, e->a_peak);
        for (int i = 0; i < NF; i++) {
            float err = fabsf(r.features[i] - e->f[i]);
            float rel = err / feature_tolerance(i, e->f[i]);
            if (rel > worst_feature) {
                worst_feature = rel;
                worst_index = i;
            }
            CHECKF(err <= feature_tolerance(i, e->f[i]), "%s @%u: %s = %.7g, Python %.7g", name, e->peak,
                   fall_feature_names[i], r.features[i], e->f[i]);
        }
        float lr = fp_classifier_prob(FP_CLASSIFIER_LR, r.features);
        worst_rf = fmaxf(worst_rf, fabsf(r.prob - e->rf));
        worst_lr = fmaxf(worst_lr, fabsf(lr - e->lr));
        CHECKF(fabsf(r.prob - e->rf) < 0.05f, "%s @%u: RF %.4f, Python %.4f", name, e->peak, r.prob, e->rf);
        CHECKF(fabsf(lr - e->lr) < 0.02f, "%s @%u: LR %.4f, Python %.4f", name, e->peak, lr, e->lr);
        CHECKF(decided_alike(r.prob, e->rf, fall_rf_threshold), "%s @%u: RF decision differs", name, e->peak);
        CHECKF(decided_alike(lr, e->lr, fall_lr_threshold), "%s @%u: LR decision differs", name, e->peak);
    }
    fclose(f);
    CHECKF(got == count, "%s: %d windows, Python %d", name, got, count);
    printf("  %-14s %2d windows; worst feature %s at %.0f%% of its tolerance; max |dP| RF %.3f, LR %.3f\n",
           name, got, fall_feature_names[worst_index], 100.0f * worst_feature, worst_rf, worst_lr);
}

static void test_pipeline_parity_with_python(void){
    const char *done[32];
    int n_done = 0;
    for (int k = 0; k < s_n_expected; k++) {
        bool seen = false;
        for (int j = 0; j < n_done; j++) {
            seen |= strcmp(done[j], s_expected[k].file) == 0;
        }
        if (!seen && n_done < 32) {
            done[n_done++] = s_expected[k].file;
            check_file(s_expected[k].file);
        }
    }
}

int main(void){
    struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        { "trigger: impact swallows the step before it",     test_trigger_impact_swallows_the_step_before_it },
        { "trigger: only the highest of a rising run",       test_trigger_keeps_only_the_highest_of_a_rising_run },
        { "trigger: never starts on the way down",           test_trigger_never_starts_on_the_way_down },
        { "trigger: across the wrap of the sample count",    test_trigger_across_the_wrap_of_the_sample_count },
        { "filter starts settled",                           test_filter_starts_settled },
        { "window right after boot",                         test_window_right_after_boot_repeats_the_oldest_sample },
        { "feature names match the model",                   test_feature_names_match_the_model },
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        printf("%s\n", tests[i].name);
        tests[i].fn();
    }

    printf("parity with Python (vetores/)\n");
    CHECK(load_expected());
    if (s_n_expected > 0) {
        test_classifiers_on_python_features();
        test_pipeline_parity_with_python();
    }

    printf("\n%d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
