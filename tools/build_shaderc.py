#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build libshaderc.so.1 from its pinned source.

The Vulkan renderer loads shaderc by name while a game runs
(tools/vulkan_program.cpp), and the Linux package carries the library along
(tools/linux_app.py). Ubuntu 24.04 has a package of it. Ubuntu 22.04, on
which the release is built so that it starts there too, has none: this
builds the same version from source and puts it where the package and a
development build look first, build/toolchain/usr/lib/x86_64-linux-gnu.

The dependencies (glslang, SPIRV-Tools, SPIRV-Headers) are the revisions the
DEPS file of the pinned commit names.

    python3 tools/build_shaderc.py
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PIN = {
    'url': 'https://github.com/google/shaderc',
    'tag': 'v2023.8',
    'commit': 'e6edd6d48fa5bdd9d176794c6810fae7f8e938e1',
}
OUT = ROOT / 'build/toolchain/usr/lib/x86_64-linux-gnu/libshaderc.so.1'
STAMP = OUT.with_name('libshaderc.so.1.pin')


def build():
    if OUT.is_file() and STAMP.is_file() and STAMP.read_text().strip() == PIN['commit']:
        print(f'shaderc {PIN["tag"]}: {OUT} is current')
        return OUT
    source = ROOT / 'third_party/shaderc'
    if not (source / '.git').is_dir():
        source.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--depth', '1', '--branch', PIN['tag'], PIN['url'], str(source)], check=True)
    head = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=source, check=True, capture_output=True, text=True).stdout.strip()
    if head != PIN['commit']:
        raise RuntimeError(f'third_party/shaderc is at {head}, not at the pinned {PIN["commit"]}')
    subprocess.run([sys.executable, 'utils/git-sync-deps'], cwd=source, check=True)
    work = ROOT / 'build/shaderc'
    subprocess.run(['cmake', '-S', str(source), '-B', str(work), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                    '-DSHADERC_SKIP_TESTS=ON', '-DSHADERC_SKIP_EXAMPLES=ON', '-DSHADERC_SKIP_COPYRIGHT_CHECK=ON',
                    '-DSHADERC_SKIP_INSTALL=ON', '-DSPIRV_SKIP_EXECUTABLES=ON', '-DENABLE_GLSLANG_BINARIES=OFF'],
                   check=True)
    subprocess.run(['ninja', '-C', str(work), 'shaderc_shared'], check=True)
    built = work / 'libshaderc/libshaderc_shared.so.1'
    if not built.is_file():
        raise RuntimeError(f'{built} was not built')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    temporary = OUT.with_name(OUT.name + f'.{os.getpid()}')
    shutil.copyfile(built.resolve(), temporary)
    subprocess.run(['strip', '--strip-unneeded', str(temporary)], check=True)
    temporary.chmod(0o755)
    temporary.replace(OUT)
    STAMP.write_text(PIN['commit'] + '\n')
    print(f'shaderc {PIN["tag"]}: {OUT}')
    return OUT


if __name__ == '__main__':
    build()
