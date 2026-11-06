#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -fno-sanitize=vptr -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    tests/forms.cpp src/Nodes/Form.cpp src/URL.cpp -Wl,--gc-sections -o build/tests/forms
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" build/tests/forms
