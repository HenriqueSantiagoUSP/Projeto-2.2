/*
 * GERADO por modelo/experimentos/09_exportar_pulseira.py em 2026-11-17: não edite à mão.
 * Treino: SisFall, cenário A (±8 g / ±500 °/s (pulseira atual)), gatilho causal com L_on = 2.0 g,
 * 12723 janelas (1784 de impacto), 38 pessoas.
 * Validação por pessoa (5 dobras), limiar com 95 % das janelas de impacto acima:
 *   regressão logística: 0.62 alarme falso/h, 94.9 % das quedas com <= 1 alarme/h
 *   Random Forest: 0.55 alarme falso/h, 95.1 % das quedas com <= 1 alarme/h
 */

#pragma once

#include <stdint.h>

/* Pipeline (modelo/queda/pipeline.py and atributos.py): the C side must match these. */
#define FALL_MODEL_FS_HZ            200
#define FALL_MODEL_TRIGGER_G        2.0f   /* L_on the models were trained with */
#define FALL_MODEL_TRIGGER_GAP      100      /* samples without a higher |a| before a peak is final */
#define FALL_MODEL_PRE              200      /* window samples before the peak */
#define FALL_MODEL_POST             160      /* window samples from the peak on */
#define FALL_MODEL_WINDOW_LEN       360
#define FALL_MODEL_N_FEATURES       23
#define FALL_MODEL_RANGE_G          8.0f   /* accelerometer range of the training data */

/* 2nd order Butterworth low-pass, ESP-DSP order {b0, b1, b2, a1, a2} (a0 = 1). */
extern const float fall_model_biquad[5];

/* Feature names in model order, as modelo/queda/atributos.py calls them. */
extern const char *const fall_model_feature_names[FALL_MODEL_N_FEATURES];

/* Logistic regression: p = 1 / (1 + exp(-(intercept + sum coef_i * (x_i - mean_i) / scale_i))). */
extern const float fall_lr_mean[FALL_MODEL_N_FEATURES];
extern const float fall_lr_scale[FALL_MODEL_N_FEATURES];
extern const float fall_lr_coef[FALL_MODEL_N_FEATURES];
extern const float fall_lr_intercept;
extern const float fall_lr_threshold;   /* fall when p >= this */

/* Random forest: p = mean over the trees of P(fall) at the leaf each one reaches. */
#define FALL_RF_N_TREES             60
#define FALL_RF_N_NODES             8854

typedef struct {
    int8_t   feature;   /* -1 = leaf */
    uint16_t left;      /* internal node: next node when x[feature] <= value */
    uint16_t right;     /* internal node: next node otherwise */
    float    value;     /* internal node: threshold; leaf: P(fall) */
} fall_rf_node_t;

extern const uint16_t fall_rf_roots[FALL_RF_N_TREES];
extern const fall_rf_node_t fall_rf_nodes[FALL_RF_N_NODES];
extern const float fall_rf_threshold;   /* fall when p >= this */
