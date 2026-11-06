#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests/loadtask-include
printf '#include <unistd.h>\n#include <limits.h>\n#include <errno.h>\n' > build/tests/loadtask-include/direct.h
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -fno-sanitize=vptr -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Ibuild/tests/loadtask-include -Dstricmp=strcasecmp -Dstrnicmp=strncasecmp -include strings.h \
    tests/loadtask_eof.cpp src/App.cpp -Wl,--gc-sections -o build/tests/loadtask-eof
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" build/tests/loadtask-eof
