#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Wno-unknown-pragmas tests/url.cpp src/URL.cpp -o build/tests/url
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" UBSAN_OPTIONS=halt_on_error=1 build/tests/url
