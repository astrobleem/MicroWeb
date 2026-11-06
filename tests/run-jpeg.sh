#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests/jpeg-include
printf '\n' > build/tests/jpeg-include/conio.h
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Ibuild/tests/jpeg-include -Dstricmp=strcasecmp -include strings.h \
    tests/jpeg_decode.cpp src/Image/Jpeg.cpp src/Image/Decoder.cpp src/Colour.cpp \
    -Wl,--gc-sections -o build/tests/jpeg-decode
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" python3 tests/run-jpeg.py
