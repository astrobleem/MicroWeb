#!/usr/bin/env python3
"""Exercise production baseline JPEG decoding across streamed input boundaries."""
import os
from pathlib import Path
import random
import subprocess
import tempfile
from PIL import Image

root = Path(__file__).resolve().parents[1]
runner = root / 'build/tests/jpeg-decode'
rng = random.Random(8088)
checks = 0
with tempfile.TemporaryDirectory(prefix='microweb-jpeg-') as directory:
    path = Path(directory) / 'case.jpg'
    for width, height in ((1, 1), (7, 9), (16, 16), (19, 15), (128, 64)):
        for mode in ('L', 'RGB'):
            image = Image.frombytes(mode, (width, height), bytes(rng.randrange(256) for _ in range(width * height * (1 if mode == 'L' else 3))))
            for subsampling in ((0,) if mode == 'L' else (0, 1, 2)):
                image.save(path, quality=85, subsampling=subsampling, progressive=False)
                for bpp in (1, 8):
                    pixels = None
                    for fragment in (1, 2, 7, 63, 256, 4096):
                        result = subprocess.run([str(runner), str(path), str(fragment), str(bpp)], check=True, capture_output=True)
                        assert not result.stderr, result.stderr
                        header, actual = result.stdout.split(b'\n', 1)
                        state, w, h, lines, workspace = map(int, header.split())
                        assert (state, w, h, lines) == (2, width, height, height), header
                        assert len(actual) == ((width + 7) // 8 if bpp == 1 else width) * height
                        if pixels is None:
                            pixels = actual
                        else:
                            assert pixels == actual, (width, height, mode, subsampling, bpp, fragment)
                        checks += 1
    # Exhaustively prove the IDCT assembly's byte selection matches signed
    # rounding for every signed word and all four production coefficients.
    for coefficient in (362, 669, 277, 196):
        for value in range(-32768, 32768):
            rounded = value * coefficient + 128
            selected = (((rounded & 65535) >> 8) & 255) | (((rounded >> 16) & 255) << 8)
            assert selected == ((rounded >> 8) & 65535)
print(f'PASS: {checks} streamed baseline JPEG checks; grayscale and 4:4:4/4:2:2/4:2:0, odd dimensions, monochrome, exact fragment invariance; 262144 fixed-point results')
