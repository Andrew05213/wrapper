#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
check_dir="$(mktemp -d)"
trap 'rm -rf "$check_dir"' EXIT
host_cxx="${CXX:-c++}"
flags=(-std=c++11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
       -ffunction-sections -fdata-sections -I. -Ibuild/_deps/cjson-src
       -Ibuild/_deps/dobby-src/include -pthread)
for check in native_streams native_context_ownership playback_ownership token_data two_factor; do
    extra=()
    if [[ "$check" == token_data ]]; then extra+=(build/_deps/cjson-src/cJSON.c); fi
    "$host_cxx" "${flags[@]}" "tests/$check.cpp" "${extra[@]}" \
        -Wl,--gc-sections -ldl -o "$check_dir/$check"
    "$check_dir/$check"
done
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
