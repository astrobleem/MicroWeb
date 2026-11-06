#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests/png-include
# Decoder.cpp includes the DOS-only conio header but the tested methods do not
# use it. Dead-section elimination excludes the unrelated decoder factories.
printf '\n' > build/tests/png-include/conio.h
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Ibuild/tests/png-include -Dstricmp=strcasecmp -include strings.h \
    tests/png_decode.cpp src/Image/Png.cpp src/Image/Decoder.cpp \
    -Wl,--gc-sections -o build/tests/png-decode
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" python3 tests/run-png.py
# Repeat real samples with the production page allocators, not just isolated
# malloc-backed rows. This exercises handles sharing the same 8K chunks.
"${CXX:-c++}" -std=c++11 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -ffunction-sections -fdata-sections -Wno-unknown-pragmas \
    -Ibuild/tests/png-include -Dstricmp=strcasecmp -DPNG_REAL_ALLOCATOR -include strings.h \
    tests/png_decode.cpp src/Image/Png.cpp src/Image/Decoder.cpp \
    src/Memory/Memory.cpp src/Memory/MemBlock.cpp src/Memory/LinAlloc.cpp \
    -Wl,--gc-sections -o build/tests/png-decode-real
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" python3 - <<'PY'
import subprocess
for name in ('rgba.png', 'pal.png'):
    for fragment in (1, 7, 256, 4096):
        outputs = []
        for exe in ('png-decode', 'png-decode-real'):
            result = subprocess.run(['build/tests/' + exe, 'examples/' + name,
                                     str(fragment), '8'], capture_output=True, check=True)
            header, pixels = result.stdout.split(b'\n', 1)
            assert header.startswith(b'2 '), (exe, header)
            outputs.append(pixels)
        assert outputs[0] == outputs[1], (name, fragment)
print('PASS: eight exact PNG comparisons with production page allocators under ASan/UBSan')
PY
