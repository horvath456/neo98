#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Microsoft ADPCM decoder of PlaySound (src/ms_adpcm.h).

Two checks. A synthetic mono file with two blocks, a short last block and
corrections of both signs is decoded and compared with an independent
decoder in this script. Every ADPCM wave of the imported tree's MEDIA folder
is decoded and compared sample by sample with macOS afconvert, when both are
present; START.WAV, the Explorer "Navigating" sound, is one of them.
"""
import random, shutil, struct, subprocess, sys
from pathlib import Path
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/ms-adpcm'
COEFFICIENTS = [(256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232)]
ADAPT = [230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230]


def trunc(a, b):
    return int(a / b) if a * b < 0 else a // b


def reference(data, channels, align, per_block):
    out = []
    while len(data) >= 7 * channels:
        block, data = data[:align], data[align:]
        at = channels
        index = list(block[:channels])
        delta = [struct.unpack_from('<h', block, at + 2 * c)[0] for c in range(channels)]; at += 2 * channels
        one = [struct.unpack_from('<h', block, at + 2 * c)[0] for c in range(channels)]; at += 2 * channels
        two = [struct.unpack_from('<h', block, at + 2 * c)[0] for c in range(channels)]; at += 2 * channels
        frames = min(per_block, 2 + (len(block) - 7 * channels) * 2 // channels)
        out += two + one
        nibbles = [n for byte in block[at:] for n in (byte >> 4, byte & 15)]
        for i in range((frames - 2) * channels):
            c, n = i % channels, nibbles[i]
            first, second = COEFFICIENTS[index[c]]
            sample = trunc(one[c] * first + two[c] * second, 256) + (n - 16 if n & 8 else n) * delta[c]
            sample = max(-32768, min(32767, sample))
            out.append(sample)
            two[c], one[c] = one[c], sample
            delta[c] = min(0x7fffffff // 768, max(16, trunc(ADAPT[n] * delta[c], 256)))
    return out


def wave(channels, align, per_block, data):
    fmt = struct.pack('<HHIIHHHHH', 2, channels, 22050, 22050 * align // per_block, align, 4, 32, per_block, 7)
    fmt += b''.join(struct.pack('<hh', *c) for c in COEFFICIENTS)
    body = b'WAVEfmt ' + struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', len(data)) + data
    return b'RIFF' + struct.pack('<I', len(body)) + body


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    tool = OUT / 'decode'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    str(ROOT / 'tools/test_ms_adpcm.cpp'), '-o', str(tool)], check=True)
    random.seed(98)
    checked = 0
    for channels in (1, 2):
        align = 64 * channels
        per_block = 2 + (align - 7 * channels) * 2 // channels
        blocks = []
        for length in (align, align, 7 * channels + 10):
            head = bytes(random.randrange(7) for _ in range(channels))
            head += b''.join(struct.pack('<h', random.randrange(16, 3000)) for _ in range(channels))
            head += b''.join(struct.pack('<h', random.randrange(-20000, 20000)) for _ in range(2 * channels))
            blocks.append(head + bytes(random.randrange(256) for _ in range(length - len(head))))
        data = b''.join(blocks)
        source, result = OUT / f'synthetic{channels}.wav', OUT / f'synthetic{channels}.raw'
        source.write_bytes(wave(channels, align, per_block, data))
        subprocess.run([str(tool), str(source), str(result)], check=True)
        got = list(struct.unpack(f'<{result.stat().st_size // 2}h', result.read_bytes()))
        assert got == reference(data, channels, align, per_block), channels
        checked += len(got)
    compared = []
    media = win98_c.FOLDER / 'WINDOWS/MEDIA'
    if media.is_dir() and shutil.which('afconvert'):
        for file in sorted(media.iterdir()):
            head = file.read_bytes()[:64] if file.suffix.lower() == '.wav' else b''
            at = head.find(b'fmt ')
            if at < 0 or struct.unpack_from('<H', head, at + 8)[0] != 2:
                continue
            mine, theirs = OUT / (file.stem + '.raw'), OUT / (file.stem + '.ref.wav')
            subprocess.run([str(tool), str(file), str(mine)], check=True)
            subprocess.run(['afconvert', '-f', 'WAVE', '-d', 'LEI16', str(file), str(theirs)], check=True)
            ref = theirs.read_bytes()
            start = ref.index(b'data') + 8
            ref = ref[start:start + struct.unpack_from('<I', ref, start - 4)[0]]
            assert mine.read_bytes() == ref, file.name
            compared.append(file.name)
    print(f'PASS MS ADPCM: {checked} synthetic samples equal the reference; '
          f'afconvert agrees on {", ".join(compared) if compared else "no local file (skipped)"}')


if __name__ == '__main__':
    main()
