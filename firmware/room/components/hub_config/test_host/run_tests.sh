#!/usr/bin/env sh
# Builds and runs the configuration JSON tests with the host compiler.
# Needs firmware/room/managed_components/espressif__cjson, which the first
# "idf.py build" of firmware/room downloads.
# Usage, from anywhere: sh firmware/room/components/hub_config/test_host/run_tests.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
components="$here/../.."
cjson="$components/../managed_components/espressif__cjson/cJSON"
common="$components/../../common/protocol/include"
out="${TMPDIR:-/tmp}/test_hub_config"

if [ ! -f "$cjson/cJSON.c" ]; then
    echo "cJSON not found at $cjson: run idf.py build in firmware/room once" >&2
    exit 1
fi

gcc -std=c11 -Wall -Wextra -Werror -O1 \
    -I "$here/stubs" -I "$components/hub_config/include" -I "$components/rules/include" \
    -I "$common" -I "$cjson" \
    "$components/hub_config/hub_config_json.c" "$components/rules/rules.c" "$cjson/cJSON.c" \
    "$here/test_hub_config.c" -lm -o "$out"
"$out"
