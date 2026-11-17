#!/usr/bin/env sh
# Builds and runs the button_gesture tests with the host compiler (no ESP-IDF needed).
# Usage, from anywhere: sh firmware/gadget/components/button_gesture/test_host/run_tests.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
component="$here/.."
out="${TMPDIR:-/tmp}/test_button_gesture"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$component/include" \
    "$component/button_gesture.c" "$here/test_button_gesture.c" -o "$out"
"$out"
