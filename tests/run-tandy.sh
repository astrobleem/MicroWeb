#!/bin/sh
# Run Tandy regressions on the host (no DOS hardware required).
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer tests/tandy_surface.cpp src/Draw/Surf4pak.cpp \
    -o build/tests/tandy-surface
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" build/tests/tandy-surface
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined \
    tests/tandy_modes.cpp src/VidModes.cpp -o build/tests/tandy-modes
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" build/tests/tandy-modes

# DOS MCB word fields are intentionally unaligned on the 8086. Disable only
# the host alignment check; address and other undefined-behavior checks stay on.
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined \
    -fno-sanitize=alignment -fno-omit-frame-pointer -Itests/dos-stub \
    tests/tandy_memory.cpp src/DOS/TndyMem.cpp -o build/tests/tandy-memory
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" build/tests/tandy-memory

# Native RGBI GIF palette fast path and unchanged fallback modes.
tests/run-tandy-gif.sh
