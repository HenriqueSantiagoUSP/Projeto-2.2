#!/usr/bin/env sh
# Builds and runs the signal_proc tests with the host compiler (no ESP-IDF needed:
# stubs/esp_dsp.h stands in for ESP-DSP).
# Usage, from anywhere: sh firmware/gadget/components/signal_proc/test_host/run_tests.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
component="$here/.."
out="${TMPDIR:-/tmp}/test_signal_proc"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$component/include" -I "$here/stubs" \
    "$component/signal_proc.c" "$here/test_signal_proc.c" -lm -o "$out"
"$out"
