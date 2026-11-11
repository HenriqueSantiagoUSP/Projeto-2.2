# Plain C over esp_dsp.h: test_host/ builds the same file with a stub. The cycle count of
# the fall pipeline (features + classifier) lives in fall_pipeline_timed.c.
idf_component_register(
    SRCS "signal_proc.c"
    INCLUDE_DIRS "include"
)
