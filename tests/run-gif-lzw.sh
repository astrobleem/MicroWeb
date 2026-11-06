#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -O2 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer tests/gif_lzw.cpp src/Image/Gif.cpp src/Colour.cpp \
    -o build/tests/gif-lzw
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" \
    python3 tests/run-gif-lzw.py build/tests/gif-lzw
