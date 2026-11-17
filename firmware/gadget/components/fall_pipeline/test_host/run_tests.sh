#!/usr/bin/env sh
# Builds and runs the fall_pipeline tests with the host compiler (no ESP-IDF needed:
# stubs/esp_dsp.h stands in for ESP-DSP). The parity tests read vetores/, exported by
# modelo/experimentos/09_exportar_pulseira.py together with the model.
# Usage, from anywhere: sh firmware/gadget/components/fall_pipeline/test_host/run_tests.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
component="$here/.."
signal_proc="$component/../signal_proc"
out="${TMPDIR:-/tmp}/test_fall_pipeline"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$component/include" -I "$signal_proc/include" -I "$here/stubs" \
    "$component/fall_pipeline.c" "$component/fall_features.c" "$component/fall_classifier.c" \
    "$component/fall_model.c" "$signal_proc/signal_proc.c" "$here/test_fall_pipeline.c" -lm -o "$out"
cd "$here/vetores"
"$out"
