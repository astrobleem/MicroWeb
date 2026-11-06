#!/bin/sh
# Exercise the production surface using TGA-II's complete linear aperture.
# This is a host rendering regression, not a DOS BIOS/hardware emulator.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer tests/tga2_surface.cpp src/Draw/Surf4pak.cpp \
    -o build/tests/tga2-surface
# LeakSanitizer is unavailable on ptrace-based hosts; test allocations are freed.
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1" build/tests/tga2-surface
