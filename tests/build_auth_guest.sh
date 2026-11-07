#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${ANDROID_NDK_PATH:?Set ANDROID_NDK_PATH to the NDK used for the lite build}"
compiler="$ANDROID_NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android22-clang++"
"$compiler" -std=c++11 -O2 -ffunction-sections -fdata-sections \
    -I. -Ibuild/_deps/cjson-src -Ibuild/_deps/dobby-src/include \
    tests/auth_library_abi.cpp lite/auth_interaction.cpp lite/auth_sdk_interaction.cpp build/_deps/dobby-build/libdobby.a \
    -Wl,--gc-sections -Lrootfs/system/lib64 \
    -landroidappmusic -lstoreservicescore -lmediaplatform -lCoreFoundation \
    -lc++_shared -llog -lcurl -ldl -o "${1:-/tmp/wrapper-auth-library-abi}"
