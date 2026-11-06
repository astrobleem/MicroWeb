#!/usr/bin/env python3
"""Compare exact decoder output, not JPEG quality, with an unmodified oracle.

Use run-jpeg-entropy.sh BASELINE_SOURCE_ROOT [--image JPEG ...] to compile both
versions with sanitizers. Fixtures and decoded pixels stay in a temp directory.
"""
import argparse
from dataclasses import dataclass
import fnmatch
import io
from pathlib import Path
import random
import struct
import subprocess
import tempfile

from PIL import Image


@dataclass
class Fixture:
    name: str
    data: bytes
    width: int
    height: int
    all_splits: bool = False
    all_truncations: bool = False
    flat: bool = False


def segment(marker, payload):
    return bytes((255, marker)) + struct.pack('>H', len(payload) + 2) + payload


def long_code_jpeg(width, height, dc_length=16, restart=0, ac_length=16):
    """Legal baseline JPEG using long DC-zero and AC-EOB canonical codes.

    AC has one symbol at each length 1..ac_length. At length 16 its final EOB
    code is 0xfffe, forcing byte stuffing. Other AC values are valid but unused.
    At DC length 16 every block emits 00 00 FF 00 FE, so large fixtures also
    place FF and its stuffed zero on opposite sides of the 256-byte ring.
    """
    dc_counts = bytes(int(length == dc_length) for length in range(1, 17))
    ac_values = bytes((list(range(1, 11)) + list(range(0x11, 0x16)))[:ac_length - 1] + [0])
    ac_counts = bytes(int(length <= ac_length) for length in range(1, 17))
    dht = b'\x00' + dc_counts + b'\x00' + b'\x10' + ac_counts + ac_values
    header = (b'\xff\xd8' + segment(0xdb, b'\x00' + b'\x01' * 64)
              + segment(0xc0, struct.pack('>BHHB', 8, height, width, 1)
                        + b'\x01\x11\x00') + segment(0xc4, dht))
    if restart:
        header += segment(0xdd, struct.pack('>H', restart))
    header += segment(0xda, b'\x01\x01\x00\x00\x3f\x00')
    blocks = ((width + 7) // 8) * ((height + 7) // 8)
    entropy = bytearray()
    bits = ''
    restart_number = 0

    def flush():
        nonlocal bits
        bits += '1' * (-len(bits) % 8)
        raw = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
        entropy.extend(raw.replace(b'\xff', b'\xff\x00'))
        bits = ''

    for block in range(blocks):
        bits += '0' * dc_length + '1' * (ac_length - 1) + '0'
        if restart and (block + 1) % restart == 0 and block + 1 < blocks:
            flush()
            entropy.extend((255, 0xd0 + restart_number))
            restart_number = (restart_number + 1) % 8
    flush()
    return header + entropy + b'\xff\xd9'


def entropy_start(data):
    offset = 2
    while offset + 4 <= len(data):
        assert data[offset] == 255, offset
        marker = data[offset + 1]
        size = int.from_bytes(data[offset + 2:offset + 4], 'big')
        assert size >= 2
        offset += size + 2
        if marker == 0xda:
            return offset
    raise AssertionError('Missing start of scan')


def fixtures(extra_images):
    rng = random.Random(8088)
    for width, height in ((1, 1), (7, 9), (16, 16), (19, 15), (129, 65)):
        for mode in ('L', 'RGB'):
            channels = 1 if mode == 'L' else 3
            image = Image.frombytes(mode, (width, height), bytes(
                rng.randrange(256) for _ in range(width * height * channels)))
            for subsampling in ((0,) if mode == 'L' else (0, 1, 2)):
                output = io.BytesIO()
                image.save(output, format='JPEG', quality=91,
                           subsampling=subsampling, progressive=False)
                yield Fixture(f'{mode}-{subsampling}-{width}x{height}',
                              output.getvalue(), width, height,
                              all_splits=(width == 7),
                              all_truncations=(width == 1))

    # Test every legal Huffman length, including both sides of prefix-table
    # cutoffs. Pillow only creates the bytes; it is not the pixel oracle.
    for length in range(1, 17):
        yield Fixture(f'long-code-{length}', long_code_jpeg(9, 9, length),
                      9, 9, all_splits=length in (1, 8, 9, 16),
                      all_truncations=length == 16, flat=True)
    for restart in (0, 1, 7):
        data = long_code_jpeg(257, 65, restart=restart)
        entropy = data[entropy_start(data):-2]
        assert b'\xff\x00' in entropy and len(entropy) > 256
        if restart == 0:
            assert any(entropy[i:i + 2] == b'\xff\x00'
                       for i in range(255, len(entropy) - 1, 256))
        if restart == 1:
            assert any(entropy[i] == 255 and 0xd0 <= entropy[i + 1] <= 0xd7
                       for i in range(255, len(entropy) - 1, 256))
        yield Fixture(f'ring-wrap-restart-{restart}', data, 257, 65, flat=True)
    yield Fixture('restart-padding', long_code_jpeg(17, 9, 9, restart=1),
                  17, 9, all_splits=True, all_truncations=True, flat=True)
    # The final EOB occupies just the last bit of a byte immediately before
    # EOI/RST. An eight-bit prefix peek must fall back without swallowing it.
    yield Fixture('short-final-code', long_code_jpeg(1, 1, 7, ac_length=1),
                  1, 1, all_splits=True, all_truncations=True, flat=True)
    yield Fixture('short-code-before-restart',
                  long_code_jpeg(17, 9, 7, restart=1, ac_length=1),
                  17, 9, all_splits=True, all_truncations=True, flat=True)

    # Recent Pillow supports real-image restart intervals. Detect the emitted
    # DRI/RST markers rather than silently treating ignored options as coverage.
    image = Image.frombytes('RGB', (97, 67), bytes(
        rng.randrange(256) for _ in range(97 * 67 * 3)))
    for subsampling in (0, 1, 2):
        output = io.BytesIO()
        image.save(output, format='JPEG', quality=85, subsampling=subsampling,
                   progressive=False, restart_marker_blocks=3)
        data = output.getvalue()
        if b'\xff\xdd' in data and any(bytes((255, n)) in data
                                            for n in range(0xd0, 0xd8)):
            yield Fixture(f'pillow-restart-{subsampling}', data, 97, 67)
        else:
            print(f'NOTE: Pillow cannot emit restart markers ({subsampling}); '
                  'handcrafted restart coverage remains enabled.', flush=True)

    for path in extra_images:
        data = path.read_bytes()
        with Image.open(io.BytesIO(data)) as image:
            width, height = image.size
        yield Fixture(f'external-{path.name}', data, width, height)


def requests(fixture):
    size = len(fixture.data)
    result = []
    for bpp in (1, 8):
        for fragment in (1, 2, 3, 7, 15, 31, 63, 127, 254, 255, 256, 257,
                         511, 4096, 65535):
            result.append((size, fragment, -1, bpp, 0, 0))
        if fixture.all_splits:
            result.extend((size, 65535, split, bpp, 0, 0)
                          for split in range(size + 1))
        if fixture.width > 1 and fixture.height > 1:
            for fragment in (1, 255, 256, 257, 65535):
                result.append((size, fragment, -1, bpp,
                               max(1, fixture.width // 2),
                               max(1, fixture.height // 2)))
    start = entropy_start(fixture.data)
    cuts = set(range(size)) if fixture.all_truncations else {
        0, 1, 2, 3, start - 1, start, start + 1,
        size // 2, size - 3, size - 2, size - 1}
    # Split/truncate between stuffed FF/00 and between each RST marker byte.
    for offset in range(start, size - 1):
        if fixture.data[offset] == 255:
            for split in (offset, offset + 1, offset + 2):
                cuts.add(split)
                if split <= size:
                    result.append((size, 65535, split, 8, 0, 0))
    for cut in sorted(cuts):
        if 0 <= cut < size:
            for fragment in (1, 257):
                result.append((cut, fragment, -1, 8, 0, 0))
    # Preserve order while avoiding duplicate split boundaries.
    return list(dict.fromkeys(result))


def run_decoder(executable, path, plans, timeout):
    request = ''.join(' '.join(map(str, row)) + '\n' for row in plans).encode()
    process = subprocess.run([str(executable), str(path)], input=request,
                             capture_output=True, timeout=timeout)
    assert process.returncode == 0, (executable, path.name, process.returncode,
                                     process.stderr.decode(errors='replace'))
    assert not process.stderr, (executable, process.stderr.decode(errors='replace'))
    output = io.BytesIO(process.stdout)
    records = []
    for number in range(len(plans)):
        header = output.readline()
        fields = tuple(map(int, header.split()))
        assert len(fields) == 7 and fields[0] == number, (number, header)
        _, state, width, height, lines, workspace, count = fields
        pixels = output.read(count)
        assert len(pixels) == count, (number, count, len(pixels))
        records.append((state, width, height, lines, pixels))
    assert output.read() == b'', 'Unexpected trailing decoder output'
    return records, workspace


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--image', type=Path, action='append', default=[])
    parser.add_argument('--timeout', type=float, default=60,
                        help='wall-clock bound per fixture/binary batch')
    parser.add_argument('--match', default='*', help='fixture-name glob for focused runs')
    args = parser.parse_args()
    count = split_count = truncated_count = fixture_count = 0
    with tempfile.TemporaryDirectory(prefix='microweb-jpeg-entropy-') as directory:
        path = Path(directory) / 'case.jpg'
        for fixture in fixtures(args.image):
            if not fnmatch.fnmatchcase(fixture.name, args.match):
                continue
            if fixture.flat:
                # Validate handcrafted fixture syntax/content, not the tested
                # decoder's image quality. Its output uses a dithered palette.
                with Image.open(io.BytesIO(fixture.data)) as image:
                    assert image.getextrema() == (128, 128), fixture.name
            path.write_bytes(fixture.data)
            plans = requests(fixture)
            old, old_workspace = run_decoder(args.baseline.resolve(), path, plans,
                                             args.timeout)
            new, new_workspace = run_decoder(args.candidate.resolve(), path, plans,
                                             args.timeout)
            anchors = {}
            for plan, expected, actual in zip(plans, old, new):
                assert actual == expected, (fixture.name, plan,
                                           expected[:4], actual[:4],
                                           'pixels match', expected[4] == actual[4])
                limit, _, split, bpp, width, height = plan
                if limit == len(fixture.data):
                    width = width or fixture.width
                    height = height or fixture.height
                    assert actual[:4] == (2, width, height, height), (
                        fixture.name, plan, actual[:4])
                    expected_size = ((width + 7) // 8 if bpp == 1 else width) * height
                    assert len(actual[4]) == expected_size
                    key = (bpp, width, height)
                    anchor = anchors.setdefault(key, actual)
                    assert actual == anchor, (fixture.name, plan, 'fragment variance')
                    split_count += split >= 0
                else:
                    # Truncation may remain Decoding; this API has no EOF call.
                    # Require bounded termination and exact old-state behavior,
                    # rather than incorrectly demanding rejection at every cut.
                    assert actual[0] in (1, 2, 3), (fixture.name, plan, actual[:4])
                    truncated_count += 1
                count += 1
            fixture_count += 1
            print(f'PASS {fixture.name}: {len(plans)} schedules', flush=True)
    assert fixture_count, 'No fixtures selected'
    print(f'PASS: {count} exact oracle comparisons across {fixture_count} fixtures; '
          f'{split_count} split schedules, {truncated_count} truncated streams; '
          f'decoder workspace {old_workspace} -> {new_workspace} host bytes. '
          'These checks prove decoder equivalence, not reference JPEG quality.')


if __name__ == '__main__':
    main()
