#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash tests/verify_library_hashes.sh
check_dir="$(mktemp -d)"
trap 'rm -rf "$check_dir"' EXIT
host_cxx="${CXX:-c++}"
flags=(-std=c++11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
       -ffunction-sections -fdata-sections -I. -Ibuild/_deps/cjson-src
       -Ibuild/_deps/dobby-src/include -pthread)
for check in native_streams native_processing native_batch_http native_context_ownership playback_ownership m3u8_route token_data two_factor auth_input authentication token_cache auth_interaction auth_sdk_interaction network_config; do
    extra=()
    if [[ "$check" == native_batch_http ]]; then extra+=(lite/native_batch_http.cpp); fi
    if [[ "$check" == token_data || "$check" == auth_input || "$check" == token_cache || "$check" == m3u8_route ]]; then extra+=(build/_deps/cjson-src/cJSON.c); fi
    if [[ "$check" == two_factor || "$check" == authentication || "$check" == auth_interaction || "$check" == auth_sdk_interaction ]]; then extra+=(lite/auth_interaction.cpp); fi
    "$host_cxx" "${flags[@]}" "tests/$check.cpp" "${extra[@]}" \
        -Wl,--gc-sections -ldl -o "$check_dir/$check"
    timeout 120s "$check_dir/$check"
done
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
