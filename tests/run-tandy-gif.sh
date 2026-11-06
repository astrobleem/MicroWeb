#!/bin/sh
# Exercise the production GIF decoder on the host without DOS hardware.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer tests/tandy_gif.cpp src/Image/Gif.cpp src/Colour.cpp \
    -o build/tests/tandy-gif
# LeakSanitizer cannot run in ptrace-based hosts; every test frees its allocator.
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" build/tests/tandy-gif
