#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests/https-include
printf '#include <unistd.h>\n#include <limits.h>\n#include <errno.h>\n' > build/tests/https-include/direct.h
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-sanitize=vptr \
    -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Ibuild/tests/https-include -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp -include strings.h \
    tests/https_proxy.cpp src/App.cpp src/URL.cpp src/HTTP.cpp \
    -Wl,--gc-sections -o build/tests/https-proxy
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS=halt_on_error=1 python3 tests/run-https-proxy.py
