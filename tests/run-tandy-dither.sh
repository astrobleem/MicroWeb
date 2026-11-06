#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -O2 -g -fsanitize=undefined \
    tests/tandy_dither.cpp src/Colour.cpp -o build/tests/tandy-dither
UBSAN_OPTIONS=halt_on_error=1 build/tests/tandy-dither
tests/run-tandy-gif.sh
tests/run-png.sh
tests/run-jpeg.sh
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" \
python3 tests/run-tandy-dither.py
