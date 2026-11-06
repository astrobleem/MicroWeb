#!/bin/sh
# Exercise production DOS input and UI wheel arithmetic with simulated INT 33h.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-sanitize=vptr \
    -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
    -Wno-unknown-pragmas -Itests/input-stub -Dstricmp=strcasecmp -include strings.h \
    tests/wheel_input.cpp -Wl,--gc-sections -o build/tests/wheel-input
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1" build/tests/wheel-input
