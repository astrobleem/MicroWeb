#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Dclock=TestHTTPClock -Wno-unknown-pragmas -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp -include strings.h \
    tests/http_network.cpp src/HTTP.cpp -o build/tests/http-network
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" build/tests/http-network
