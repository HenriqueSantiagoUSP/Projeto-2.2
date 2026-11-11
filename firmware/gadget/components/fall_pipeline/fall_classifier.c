#include <math.h>

#include "fall_pipeline.h"

/* Logistic regression: standardize, weigh, squash. */
static float lr_prob(const float *x){
    float z = fall_lr_intercept;
    for (int i = 0; i < FALL_MODEL_N_FEATURES; i++) {
        z += fall_lr_coef[i] * (x[i] - fall_lr_mean[i]) / fall_lr_scale[i];
    }
    return 1.0f / (1.0f + expf(-z));
}

/*
 * Random forest: each tree is walked from its root to a leaf (at most its depth in
 * comparisons) and the leaves' P(fall) are averaged, as scikit-learn's predict_proba.
 * The thresholds were rounded down to float when exported, so x <= threshold decides
 * exactly as scikit-learn's float32 comparison does.
 */
static float rf_prob(const float *x){
    float sum = 0.0f;
    for (int t = 0; t < FALL_RF_N_TREES; t++) {
        const fall_rf_node_t *node = &fall_rf_nodes[fall_rf_roots[t]];
        while (node->feature >= 0) {
            node = &fall_rf_nodes[x[node->feature] <= node->value ? node->left : node->right];
        }
        sum += node->value;
    }
    return sum / (float) FALL_RF_N_TREES;
}

float fp_classifier_prob(fp_classifier_t classifier, const float features[FALL_MODEL_N_FEATURES]){
    return classifier == FP_CLASSIFIER_LR ? lr_prob(features) : rf_prob(features);
}

float fp_classifier_threshold(fp_classifier_t classifier){
    return classifier == FP_CLASSIFIER_LR ? fall_lr_threshold : fall_rf_threshold;
}

const char *fp_classifier_name(fp_classifier_t classifier){
    return classifier == FP_CLASSIFIER_LR ? "logistic regression" : "random forest";
}
