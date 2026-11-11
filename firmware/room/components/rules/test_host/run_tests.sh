#!/usr/bin/env sh
# Builds and runs the alert rule tests with the host compiler (no ESP-IDF needed).
# Usage, from anywhere: sh firmware/room/components/rules/test_host/run_tests.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
component="$here/.."
common="$here/../../../../common/protocol/include"
out="${TMPDIR:-/tmp}/test_rules"

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$component/include" -I "$common" \
    "$component/rules.c" "$here/test_rules.c" -o "$out"
"$out"
