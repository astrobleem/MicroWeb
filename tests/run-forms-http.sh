#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-sanitize=vptr \
    -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp -include strings.h \
    tests/forms_http.cpp src/Nodes/Form.cpp src/URL.cpp src/HTTP.cpp \
    -Wl,--gc-sections -o build/tests/forms-http
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS=halt_on_error=1 python3 tests/run-forms-http.py
