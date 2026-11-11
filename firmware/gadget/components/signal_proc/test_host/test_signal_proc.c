/*
 * Host tests of signal_proc (ring buffer, window features, activity): build and run
 * with run_tests.sh. ESP-DSP is replaced by stubs/esp_dsp.h.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "signal_proc.h"

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                        \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

#define NEAR(a, b)  (fabsf((a) - (b)) < 1e-4f)

static void test_ring_partial(void){
    float storage[8];
    float out[8];
    sp_ring_t ring;
    sp_ring_init(&ring, storage, 8);
    CHECK(sp_ring_copy_last(&ring, 4, out) == 0);   /* empty */

    for (int i = 1; i <= 3; i++) {
        sp_ring_push(&ring, (float) i);
    }
    /* Asking for more than it holds gives what there is, oldest first. */
    CHECK(sp_ring_copy_last(&ring, 5, out) == 3);
    CHECK(out[0] == 1.0f && out[1] == 2.0f && out[2] == 3.0f);
}

static void test_ring_wraps(void){
    float storage[8];
    float out[8];
    sp_ring_t ring;
    sp_ring_init(&ring, storage, 8);
    for (int i = 1; i <= 13; i++) {        /* 13 into 8: 1..5 are overwritten */
        sp_ring_push(&ring, (float) i);
    }
    CHECK(ring.count == 8);

    CHECK(sp_ring_copy_last(&ring, 8, out) == 8);
    int ordered = 1;
    for (int i = 0; i < 8; i++) {
        ordered &= out[i] == (float) (6 + i);
    }
    CHECK(ordered);

    /* The newest 3 sit across the wrap point. */
    CHECK(sp_ring_copy_last(&ring, 3, out) == 3);
    CHECK(out[0] == 11.0f && out[1] == 12.0f && out[2] == 13.0f);
}

static void test_ring_exactly_at_head(void){
    float storage[4];
    float out[4];
    sp_ring_t ring;
    sp_ring_init(&ring, storage, 4);
    for (int i = 1; i <= 8; i++) {         /* head back at 0 */
        sp_ring_push(&ring, (float) i);
    }
    CHECK(sp_ring_copy_last(&ring, 4, out) == 4);
    CHECK(out[0] == 5.0f && out[3] == 8.0f);
}

static void test_features_constant(void){
    float x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = 1.0f;                       /* at rest: |a| = 1 g */
    }
    sp_features_t f;
    sp_features_compute(x, 16, &f);
    CHECK(f.n == 16);
    CHECK(NEAR(f.mean, 1.0f) && NEAR(f.rms, 1.0f) && NEAR(f.std, 0.0f));
    CHECK(NEAR(f.min, 1.0f) && NEAR(f.max, 1.0f) && NEAR(f.crest, 1.0f));
}

static void test_features_known_values(void){
    /* 1, 3, 1, 3: mean 2, rms sqrt(5), std 1, crest 3/sqrt(5). */
    float x[4] = { 1.0f, 3.0f, 1.0f, 3.0f };
    sp_features_t f;
    sp_features_compute(x, 4, &f);
    CHECK(NEAR(f.mean, 2.0f));
    CHECK(NEAR(f.rms, sqrtf(5.0f)));
    CHECK(NEAR(f.std, 1.0f));
    CHECK(NEAR(f.min, 1.0f) && NEAR(f.max, 3.0f));
    CHECK(NEAR(f.crest, 3.0f / sqrtf(5.0f)));
}

static void test_features_fall_shape(void){
    /* Rest, free fall, impact, rest: the minimum and the crest stand out. */
    float x[40];
    for (int i = 0; i < 40; i++) {
        x[i] = 1.0f;
    }
    for (int i = 10; i < 20; i++) {
        x[i] = 0.1f;
    }
    x[20] = 4.0f;
    sp_features_t f;
    sp_features_compute(x, 40, &f);
    CHECK(NEAR(f.min, 0.1f) && NEAR(f.max, 4.0f));
    CHECK(f.crest > 3.0f);
}

static void test_features_empty(void){
    float x[1] = { 5.0f };
    sp_features_t f;
    sp_features_compute(x, 0, &f);
    CHECK(f.n == 0 && f.rms == 0.0f && f.crest == 0.0f);
}

static void test_activity(void){
    sp_activity_t a = { 0 };
    CHECK(sp_activity_take_mg(&a) == 0);   /* nothing yet */
    for (int i = 0; i < 100; i++) {
        sp_activity_add(&a, 1.0f);
    }
    CHECK(sp_activity_take_mg(&a) == 0);

    /* Two batches merged as the sampling task does: 1.5 g throughout -> 500 mg. */
    sp_activity_t batch1 = { 0 }, batch2 = { 0 };
    for (int i = 0; i < 50; i++) {
        sp_activity_add(&batch1, 1.5f);
        sp_activity_add(&batch2, 0.5f);    /* |a| - 1 g = -0.5: same square */
    }
    sp_activity_merge(&a, &batch1);
    sp_activity_merge(&a, &batch2);
    CHECK(sp_activity_take_mg(&a) == 500);
    CHECK(sp_activity_take_mg(&a) == 0);   /* restarted by the take */
}

int main(void){
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "ring: partial fill",             test_ring_partial },
        { "ring: wraps, oldest first",      test_ring_wraps },
        { "ring: head back at zero",        test_ring_exactly_at_head },
        { "features: constant signal",      test_features_constant },
        { "features: known values",         test_features_known_values },
        { "features: shape of a fall",      test_features_fall_shape },
        { "features: empty window",         test_features_empty },
        { "activity: RMS and merge",        test_activity },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = s_failures;
        tests[i].fn();
        printf("%s %s\n", s_failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
