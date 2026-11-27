#!/usr/bin/env sh
# Builds and runs the hwtest_checks tests with the host compiler (no ESP-IDF needed).
# Usage, from anywhere: sh firmware/hwtest/components/hwtest_checks/test_host/run_tests.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
component="$here/.."
out="${TMPDIR:-/tmp}/test_hwtest_checks"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$component/include" \
    "$component/hwtest_checks.c" "$here/test_hwtest_checks.c" -lm -o "$out"
"$out"
