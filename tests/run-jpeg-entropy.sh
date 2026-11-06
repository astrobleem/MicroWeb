#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 BASELINE_SOURCE_ROOT [--image JPEG ...]" >&2
    exit 2
fi
baseline=$(cd "$1" && pwd)
shift
mkdir -p build/tests/jpeg-include
printf '\n' > build/tests/jpeg-include/conio.h
build_decoder() {
    source_root=$1
    output=$2
    "${CXX:-c++}" -std=c++11 -g -O1 -fsanitize=address,undefined \
        -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
        -Wno-unknown-pragmas -Ibuild/tests/jpeg-include -I"$source_root/src" \
        -Dstricmp=strcasecmp -include strings.h tests/jpeg_entropy.cpp \
        "$source_root/src/Image/Jpeg.cpp" "$source_root/src/Image/Decoder.cpp" \
        "$source_root/src/Colour.cpp" -Wl,--gc-sections -o "$output"
}
build_decoder "$baseline" build/tests/jpeg-entropy-baseline
build_decoder "$PWD" build/tests/jpeg-entropy-current
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" \
UBSAN_OPTIONS="halt_on_error=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}" \
python3 tests/run-jpeg-entropy.py \
    --baseline build/tests/jpeg-entropy-baseline \
    --candidate build/tests/jpeg-entropy-current "$@"
