#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-c++}" -std=c++98 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Wno-unknown-pragmas tests/reader_link.cpp -o build/tests/reader-link
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS=halt_on_error=1 build/tests/reader-link

mkdir -p build/tests/reader-include
printf '#include <unistd.h>\n#include <limits.h>\n#include <errno.h>\n' > build/tests/reader-include/direct.h
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -fno-sanitize=vptr -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Ibuild/tests/reader-include -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp -include strings.h \
    tests/reader_page.cpp src/App.cpp src/Memory/LinAlloc.cpp \
    -Wl,--gc-sections -o build/tests/reader-page
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS=halt_on_error=1 build/tests/reader-page

"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -fno-sanitize=vptr -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp -include strings.h \
    tests/reader_http.cpp src/URL.cpp src/HTTP.cpp \
    -Wl,--gc-sections -o build/tests/reader-http
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS=halt_on_error=1 python3 tests/run-reader-http.py
