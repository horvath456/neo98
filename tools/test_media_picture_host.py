#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the Linux catalogue decoder's PNG/JPEG pixels and malformed inputs."""
import os
from pathlib import Path
import subprocess
import sys

if not sys.platform.startswith('linux'):
    print('SKIP: the Linux media picture decoder uses GTK libraries')
    raise SystemExit(77)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
import native_app
from unicorn_dependencies import ensure
from PIL import Image


def main():
    ensure('stb')
    out = neo98.output_directory('media-picture-host')
    out.mkdir(parents=True, exist_ok=True)
    Image.new('RGBA', (3, 2), (200, 100, 50, 128)).save(out / 'alpha.png')
    Image.new('RGB', (3, 2), (200, 100, 50)).save(out / 'solid.jpg', quality=100, subsampling=0)
    source = out / 'fixture.cpp'
    source.write_text(r'''
#include "tools/host_media/picture.h"
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <cstdio>
static void check(bool okay, int code) { if (!okay) std::exit(code); }
static std::vector<unsigned char> read(const char *path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int main(int argc, char **argv) {
    check(argc == 3, 1);
    unsigned width = 0, height = 0;
    std::vector<unsigned> pixels;
    auto png = read(argv[1]), jpeg = read(argv[2]);
    check(HostMediaPicture::Decode(png.data(), png.size(), pixels, width, height), 2);
    check(width == 3 && height == 2 && pixels.size() == 6, 3);
    for (unsigned pixel : pixels) check(pixel == 0x80643219u, 4);
    check(HostMediaPicture::Decode(jpeg.data(), jpeg.size(), pixels, width, height), 5);
    check(width == 3 && height == 2 && pixels.size() == 6, 6);
    for (unsigned pixel : pixels) {
        check(pixel >> 24 == 255, 7);
        check(std::abs(int(pixel >> 16 & 255) - 200) <= 3 &&
              std::abs(int(pixel >> 8 & 255) - 100) <= 3 &&
              std::abs(int(pixel & 255) - 50) <= 3, 8);
    }
    check(!HostMediaPicture::Decode(png.data(), 8, pixels, width, height), 9);
    check(!HostMediaPicture::Decode(jpeg.data(), 2, pixels, width, height), 10);
    std::puts("MEDIA PICTURE PASS: PNG alpha, JPEG pixels, dimensions and truncated inputs");
}
''')
    flags, libraries = native_app.linux_gtk_flags()
    binary = out / 'fixture'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-I' + str(ROOT), *flags,
                    str(source), str(ROOT / 'tools/host_media/picture_linux.cpp'),
                    *libraries, '-o', str(binary)], env=neo98.tool_environment(), check=True)
    subprocess.run([str(binary), str(out / 'alpha.png'), str(out / 'solid.jpg')], check=True)


if __name__ == '__main__':
    main()
