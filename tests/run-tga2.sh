#!/bin/sh
# The host models I/O and aliasing. This is not a real TGA-II hardware test.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-sanitize=alignment \
    -fno-omit-frame-pointer -Itests/tga2-stub \
    tests/tga2_video.cpp src/DOS/Tga2Vid.cpp -o build/tests/tga2-video
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1" build/tests/tga2-video
