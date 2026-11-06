#!/usr/bin/env python3
"""Independent Pillow encoder/oracle, code growth, KwKwK, clears and chunks."""
from pathlib import Path
import random, subprocess, sys
from PIL import Image
root = Path(__file__).resolve().parent.parent
out = root / 'build/tests/gif-lzw-data'
out.mkdir(parents=True, exist_ok=True)
exe = Path(sys.argv[1]).resolve()
rgbi = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),(85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
rng = random.Random(19)
for name, pixels in [('noise',bytes(rng.randrange(16) for _ in range(64000))), ('runs',bytes((x//101)%16 for x in range(64000))), ('solid',bytes([3])*64000), ('pattern',bytes((x*7+x//320)%16 for x in range(64000)))]:
    image = Image.frombytes('P',(320,200),pixels)
    image.putpalette(sum((list(c) for c in rgbi), []))
    path = out / (name+'.gif')
    image.save(path,optimize=False,interlace=False)
    for chunk in [1,2,3,7,255,4096,65536]:
        raw = out / (name+'.raw')
        subprocess.run([str(exe),str(path),str(raw),str(chunk),'1'],check=True)
        assert raw.read_bytes()==pixels, (name,chunk)
print('PASS: 28 compressed GIF decodes pixel-exact to Pillow input')
# Keep a full 4096-entry dictionary frozen without a clear, as GIF permits.
# Minimum code size 2 also covers multiple codes in a single byte.
import struct
pixels = bytes(i % 4 for i in range(64000))
packed = bytearray(); reservoir = count = 0
width = 3; next_entry = 6; previous = False
codes = [4] + list(pixels) + [5]
for value in codes:
    reservoir |= value << count; count += width
    while count >= 8:
        packed.append(reservoir & 255); reservoir >>= 8; count -= 8
    if value == 4:
        width = 3; next_entry = 6; previous = False
    elif value != 5:
        if previous and next_entry < 4096:
            next_entry += 1
            if next_entry == 1 << width and width < 12: width += 1
        previous = True
if count: packed.append(reservoir)
# EOI must ignore arbitrary trailing payload bytes, not interpret them as codes.
packed += b'\xff\xff\xff'
header = b'GIF89a'+struct.pack('<HHBBB',320,200,0x81,0,0)+bytes(sum((list(c) for c in rgbi[:4]),[]))
header += b','+struct.pack('<HHHHB',0,0,320,200,0)+b'\x02'
for block in [1,7,255]:
    payload = b''.join(bytes([len(packed[i:i+block])])+packed[i:i+block] for i in range(0,len(packed),block))
    path = out/'frozen.gif'; path.write_bytes(header+payload+b'\x00;')
    for chunk in [1,3,4096]:
        raw = out/'frozen.raw'
        subprocess.run([str(exe),str(path),str(raw),str(chunk),'1'],check=True)
        assert raw.read_bytes()==pixels
print('PASS: 9 frozen-full-dictionary/minimum-code-size-2/padded-EOI decodes')
