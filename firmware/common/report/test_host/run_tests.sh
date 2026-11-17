#!/usr/bin/env sh
# Builds and runs the report tests with the host compiler (no ESP-IDF needed).
# Usage, from anywhere: sh firmware/common/report/test_host/run_tests.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
component="$here/.."
out="${TMPDIR:-/tmp}/test_report"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$component/include" \
    "$component/report.c" "$here/test_report.c" -lm -o "$out"
"$out"
