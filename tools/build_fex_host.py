#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the pinned FEXCore for the macOS arm64 host, with the NEO98 patches.

FEXCore is the second x86 engine behind X86::Cpu. This script downloads the revisions pinned in
fex/dependencies.json into build/fex-host/downloads, checks their sha256,
unpacks them into build/fex-host/source, applies fex/patches/*.patch in order
and builds only the FEXCore static libraries into build/fex-host/build.
build/fex-host belongs to the worktree it lies in; it is never shared.

The build is skipped while the stamp matches: same pins, same patches, same
compatibility headers, same compiler and CMake arguments. Only the macOS host
uses it.

    build/analysis-venv/bin/python tools/build_fex_host.py [--force] [--jobs N]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
PINS = ROOT / 'fex/dependencies.json'
PATCHES = ROOT / 'fex/patches'
COMPAT = ROOT / 'fex/darwin-compat'
OUT = ROOT / 'build/fex-host'
SOURCE = OUT / 'source'
BUILD = OUT / 'build'
# The libraries a runner links, in link order.
LIBRARIES = ['FEXCore/Source/libFEXCore.a', 'FEXCore/Source/libFEXCore_Base.a', 'FEXCore/Source/libJemallocDummy.a',
             'External/fmt/libfmt.a', 'External/xxhash/cmake_unofficial/libxxhash.a',
             'External/SoftFloat-3e/libsoftfloat_3e.a', 'External/cephes/libcephes_128bit.a',
             'External/tiny-json/libtiny-json.a']
TARGETS = [path for path in LIBRARIES]


def cmake_arguments():
    flags = f'-idirafter {COMPAT} -include {COMPAT / "fex_darwin_prelude.h"}'
    return ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DBUILD_THUNKS=OFF',
            '-DBUILD_FEXCONFIG=OFF', '-DBUILD_FEX_LINUX_TESTS=OFF', '-DBUILD_STEAM_SUPPORT=OFF',
            '-DENABLE_FEX_ALLOCATOR=OFF', '-DENABLE_JEMALLOC_GLIBC_ALLOC=OFF', '-DENABLE_ASSERTIONS=OFF',
            '-DENABLE_VIXL_DISASSEMBLER=OFF', '-DENABLE_VIXL_SIMULATOR=OFF', '-DENABLE_CCACHE=OFF',
            # Tuned for every Apple silicon Mac, not only the one that builds.
            '-DTUNE_CPU=apple-m1',
            # Plain objects, not LTO bitcode: every runner links FEXCore, and
            # bitcode ties the link to the exact LLVM that compiled it.
            '-DENABLE_LTO=OFF',
            f'-DCMAKE_CXX_FLAGS={flags}']


def include_directories():
    """What a translation unit that includes FEXCore headers needs."""
    return [SOURCE / 'FEXCore/include', BUILD / 'include', BUILD / 'FEXCore/include', SOURCE / 'External/fmt/include',
            SOURCE / 'FEXHeaderUtils', SOURCE / 'External/unordered_dense/include']


def compile_flags():
    return ['-std=gnu++20', '-DARCHITECTURE_arm64=1', '-DFEX_DISABLE_TELEMETRY=1', '-idirafter', str(COMPAT),
            '-include', str(COMPAT / 'fex_darwin_prelude.h'), *[f'-I{d}' for d in include_directories()]]


def compiler_version(env):
    return subprocess.run(['clang++', '--version'], env=env, capture_output=True, text=True).stdout.splitlines()[0]


def identity(env):
    digest = hashlib.sha256()
    digest.update(PINS.read_bytes())
    for path in sorted(PATCHES.glob('*.patch')) + sorted(p for p in COMPAT.rglob('*') if p.is_file()):
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    digest.update(json.dumps(cmake_arguments()).encode())
    digest.update(compiler_version(env).encode())
    return digest.hexdigest()


def download(name, pin):
    folder = OUT / 'downloads'
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f'{name}-{pin["revision"]}.tar.gz'
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != pin['sha256']:
        print(f'Downloading {pin["version"]}', flush=True)
        partial = path.with_suffix('.part')
        with urllib.request.urlopen(pin['url'], timeout=120) as response, open(partial, 'wb') as out:
            shutil.copyfileobj(response, out)
        partial.replace(path)
    got = hashlib.sha256(path.read_bytes()).hexdigest()
    if got != pin['sha256']:
        raise RuntimeError(f'{name}: sha256 {got} does not match the pin {pin["sha256"]}')
    return path


def unpack(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            parts = PurePosixPath(member.name).parts
            if len(parts) < 2 or member.name.startswith('/') or '..' in parts:
                continue
            relative = Path(*parts[1:])
            target = destination / relative
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as source, open(target, 'wb') as out:
                    shutil.copyfileobj(source, out)
                os.chmod(target, member.mode & 0o777 | 0o600)
            elif member.issym():
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists() or target.is_symlink():
                    target.unlink()
                os.symlink(member.linkname, target)


def prepare_source():
    pins = json.loads(PINS.read_text())
    if SOURCE.exists():
        shutil.rmtree(SOURCE)
    for name, pin in pins.items():
        unpack(download(name, pin), SOURCE / pin['path'])
    for patch in sorted(PATCHES.glob('*.patch')):
        subprocess.run(['patch', '-p1', '--forward', '--silent', '-i', str(patch)], cwd=SOURCE, check=True)


def libraries(force=False, jobs=None):
    """The static libraries of the patched FEXCore, rebuilt when the stamp changes."""
    import neo98
    env = neo98.tool_environment()
    env['CC'], env['CXX'] = 'clang', 'clang++'
    want = identity(env)
    stamp = OUT / 'stamp.json'
    result = [BUILD / path for path in LIBRARIES]
    if not force and stamp.exists() and all(p.exists() for p in result):
        if json.loads(stamp.read_text()).get('identity') == want:
            return result
    prepare_source()
    if BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir(parents=True)
    subprocess.run(['cmake', '-S', str(SOURCE), '-B', str(BUILD), *cmake_arguments()], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    # The machine is shared with other builds; four jobs unless told otherwise.
    jobs = jobs or int(os.environ.get('NEO98_FEX_JOBS', '4'))
    subprocess.run(['ninja', '-C', str(BUILD), f'-j{jobs}', *TARGETS], env=env, check=True)
    stamp.write_text(json.dumps({'identity': want, 'revision': json.loads(PINS.read_text())['fex']['revision']},
                                indent=1) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--force', action='store_true', help='rebuild even when the stamp matches')
    parser.add_argument('--jobs', type=int, help='parallel compile jobs (default 4, or NEO98_FEX_JOBS)')
    args = parser.parse_args()
    for path in libraries(args.force, args.jobs):
        print(path)


if __name__ == '__main__':
    main()
