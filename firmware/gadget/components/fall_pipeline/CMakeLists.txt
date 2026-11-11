# fall_pipeline.c, fall_features.c, fall_classifier.c and the generated fall_model.c are
# plain C over esp_dsp.h and signal_proc (test_host/ builds them with a stub); the timed
# wrapper, which needs the CPU cycle counter and a spinlock, lives apart.
idf_component_register(
    SRCS "fall_pipeline.c" "fall_features.c" "fall_classifier.c" "fall_model.c" "fall_pipeline_timed.c"
    INCLUDE_DIRS "include"
    REQUIRES "signal_proc"
    PRIV_REQUIRES "esp_hw_support"
)
