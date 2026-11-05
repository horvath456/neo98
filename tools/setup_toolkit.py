#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the setup toolkit that the Mac app and the Linux package carry.

    python3 tools/setup_toolkit.py              build build/setup-toolkit
    python3 tools/setup_toolkit.py --check      only run the checks on an existing toolkit

With it the release needs nothing installed to turn the user's Windows 98 ISO
into drive C: (tools/setup_win98.py):

    bin/qemu-system-i386   QEMU, only the i386 target, no optional features
    bin/mtools + mformat, mcopy, mdel, mmd, mdir (links to mtools)
    bin/cabextract
    bin/tesseract          from the build machine (Homebrew or the distribution)
    share/qemu/            the BIOS images Setup boots with
    share/tessdata/        the English model of tesseract
    python/                a relocatable CPython for the assistant
    lib/ (Linux) or Frameworks/ (macOS)  every library the tools need
                           beyond the system, found through relative paths
    licenses/, manifest.json

QEMU, mtools and cabextract are built from the sources pinned in
tools/setup_toolkit.json; make_release_assets.py attaches those archives to a
release. Build dependencies: a C compiler, make, ninja, pkg-config, glib and
pixman headers and tesseract (Linux: the -dev packages and tesseract-ocr;
macOS: brew install ninja pkg-config glib pixman tesseract).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PINS = json.loads((ROOT / 'tools/setup_toolkit.json').read_text())
DOWNLOADS = ROOT / 'build/toolkit-downloads'
WORK = ROOT / 'build/toolkit-work'
OUT = ROOT / 'build/setup-toolkit'
MTOOLS_COMMANDS = ('mformat', 'mcopy', 'mdel', 'mmd', 'mdir')
# What Setup boots: SeaBIOS, the Cirrus VGA BIOS and the option ROMs QEMU's pc
# machine loads by default.
QEMU_FIRMWARE = ('bios-256k.bin', 'vgabios-cirrus.bin', 'kvmvapic.bin', 'linuxboot_dma.bin')
# Libraries every Linux desktop has; bundling them would break the package.
LINUX_SYSTEM = ('linux-vdso', 'ld-linux', 'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so',
                'libresolv.so', 'libutil.so')
MACOS = sys.platform == 'darwin'


def jobs():
    return str(os.cpu_count() or 2)


def run(*command, cwd=None, env=None):
    print('+', ' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)


def fetch(name):
    """The pinned archive, downloaded once and checked every time."""
    pin = PINS[name]
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    path = DOWNLOADS / pin['url'].rsplit('/', 1)[1].replace('%2B', '+')
    if not path.exists():
        print(f'Downloading {name} {pin["version"]}', flush=True)
        partial = path.with_name(path.name + '.part')
        with urllib.request.urlopen(pin['url'], timeout=120) as response, partial.open('wb') as out:
            shutil.copyfileobj(response, out)
        partial.rename(path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != pin['sha256']:
        path.unlink()
        raise RuntimeError(f'{path.name}: SHA-256 {digest} does not match the pin')
    return path


def unpack(name):
    archive = fetch(name)
    target = WORK / name
    marker = target / '.neo98-unpacked'
    if marker.exists() and marker.read_text() == PINS[name]['sha256']:
        return next(p for p in target.iterdir() if p.is_dir())
    shutil.rmtree(target, ignore_errors=True)
    target.mkdir(parents=True)
    def keep(member, path):
        # QEMU's roms/ holds firmware sources (with an absolute link); the
        # prebuilt images in pc-bios/ are what the toolkit carries.
        if '/roms/' in member.name:
            return None
        try:
            return tarfile.data_filter(member, path)
        except tarfile.FilterError:
            return None
    with tarfile.open(archive) as stream:
        stream.extractall(target, filter=keep)
    marker.write_text(PINS[name]['sha256'])
    return next(p for p in target.iterdir() if p.is_dir())


def build_autotools(name, binary, configure=()):
    source = unpack(name)
    if not (source / binary).is_file():
        run('./configure', *configure, cwd=source)
        run('make', '-j', jobs(), binary, cwd=source)
    return source / binary


def python_pin():
    return 'python-macos-arm64' if MACOS else 'python-linux-x64'


def build_qemu():
    source = unpack('qemu')
    build = source / 'build-neo98'
    program = build / 'qemu-system-i386'
    if not program.is_file():
        build.mkdir(exist_ok=True)
        # QEMU's bundled meson needs a Python that can create a venv; the
        # pinned relocatable one can, on every build machine.
        python = unpack(python_pin()) / 'bin/python3'
        run(source / 'configure', '--target-list=i386-softmmu', '--without-default-features',
            '--enable-tcg', '--enable-pixman', '--disable-docs', '--disable-werror',
            '--disable-user', '--disable-tools', f'--python={python}',
            # No build machine path in the binary (tools/check_release_bundle.py).
            f'--extra-cflags=-ffile-prefix-map={source}=qemu', cwd=build)
        run('ninja', '-C', build, 'qemu-system-i386')
    return program, source


def tesseract_data():
    candidates = [os.environ.get('TESSDATA_PREFIX', '')]
    if MACOS:
        brew = subprocess.run(['brew', '--prefix'], capture_output=True, text=True).stdout.strip() or '/opt/homebrew'
        candidates += [f'{brew}/share/tessdata', f'{brew}/opt/tesseract/share/tessdata']
    candidates += ['/usr/share/tesseract-ocr/5/tessdata', '/usr/share/tesseract-ocr/4.00/tessdata',
                   '/usr/share/tessdata']
    for candidate in candidates:
        if candidate and (Path(candidate) / 'eng.traineddata').is_file():
            return Path(candidate) / 'eng.traineddata'
    raise RuntimeError('eng.traineddata not found; install tesseract (and tesseract-ocr-eng)')


# --- Linux: copy what ldd names and point the RUNPATH at it -----------------

def linux_bundle(out):
    libraries = out / 'lib'
    libraries.mkdir(exist_ok=True)
    patchelf = shutil.which('patchelf')
    if not patchelf:
        raise RuntimeError('patchelf is missing (apt install patchelf, or pip install patchelf)')
    pending = [p for p in (out / 'bin').iterdir() if p.is_file() and not p.is_symlink() and is_elf(p)]
    done = set()
    while pending:
        binary = pending.pop()
        if binary in done:
            continue
        done.add(binary)
        listing = subprocess.run(['ldd', str(binary)], capture_output=True, text=True).stdout
        for line in listing.splitlines():
            parts = line.split('=>')
            if len(parts) != 2:
                continue
            name, location = parts[0].strip(), parts[1].strip().split(' (')[0].strip()
            if name.startswith(LINUX_SYSTEM):
                continue
            if location == 'not found':
                raise RuntimeError(f'{binary.name}: {name} not found')
            target = libraries / name
            if not target.exists():
                shutil.copy2(Path(location).resolve(), target)
                target.chmod(0o755)
                pending.append(target)
        relative = '$ORIGIN' if binary.parent == libraries else '$ORIGIN/../lib'
        run(patchelf, '--set-rpath', relative, binary)


def is_elf(path):
    with path.open('rb') as stream:
        return stream.read(4) == b'\x7fELF'


# --- macOS: the bundling of tools/mac_app.py, per tool ----------------------

def macos_bundle(out):
    sys.path.insert(0, str(ROOT / 'tools'))
    from mac_app import collect_libraries, dependencies, fix_install_names, SYSTEM_PREFIXES
    frameworks = out / 'Frameworks'
    frameworks.mkdir(exist_ok=True)
    for program in sorted((out / 'bin').iterdir()):
        if program.is_symlink() or not program.is_file():
            continue
        _, renames = collect_libraries(program, frameworks)
        renames = {(program if consumer == program else consumer): changes for consumer, changes in renames.items()}
        fix_install_names(program, frameworks, renames)
    leftover = []
    for binary in [*(out / 'bin').iterdir(), *frameworks.iterdir()]:
        if binary.is_symlink():
            continue
        leftover += [f'{binary.name}: {name}' for name in dependencies(binary)
                     if not name.startswith(SYSTEM_PREFIXES) and not name.startswith('@rpath/')]
    if leftover:
        raise RuntimeError('Libraries outside the toolkit: ' + ', '.join(leftover))
    # install_name_tool invalidates signatures; arm64 runs only signed code.
    for binary in [*frameworks.iterdir(), *(out / 'bin').iterdir()]:
        if not binary.is_symlink():
            run('codesign', '--force', '--sign', '-', binary)


def build(out=OUT):
    staging = out.with_name(out.name + '.new')
    shutil.rmtree(staging, ignore_errors=True)
    for folder in ('bin', 'share/qemu', 'share/tessdata', 'licenses'):
        (staging / folder).mkdir(parents=True)
    qemu, qemu_source = build_qemu()
    shutil.copy2(qemu, staging / 'bin/qemu-system-i386')
    for name in QEMU_FIRMWARE:
        shutil.copy2(qemu_source / 'pc-bios' / name, staging / 'share/qemu' / name)
    shutil.copy2(qemu_source / 'COPYING', staging / 'licenses/qemu-COPYING')
    mtools = build_autotools('mtools', 'mtools', ['--disable-floppyd'])
    shutil.copy2(mtools, staging / 'bin/mtools')
    for name in MTOOLS_COMMANDS:
        (staging / 'bin' / name).symlink_to('mtools')
    shutil.copy2(mtools.parent / 'COPYING', staging / 'licenses/mtools-COPYING')
    cabextract = build_autotools('cabextract', 'cabextract')
    shutil.copy2(cabextract, staging / 'bin/cabextract')
    shutil.copy2(cabextract.parent / 'COPYING', staging / 'licenses/cabextract-COPYING')
    tesseract = shutil.which('tesseract')
    if not tesseract:
        raise RuntimeError('tesseract is missing on the build machine')
    shutil.copy2(Path(tesseract).resolve(), staging / 'bin/tesseract')
    shutil.copy2(tesseract_data(), staging / 'share/tessdata/eng.traineddata')
    (staging / 'licenses/tesseract.txt').write_text(
        'tesseract and leptonica: Apache-2.0 (https://github.com/tesseract-ocr/tesseract, '
        'https://github.com/DanBloomberg/leptonica); the libraries in lib/ or Frameworks/ '
        'keep the licences of their projects.\n')
    for program in (staging / 'bin').iterdir():
        if not program.is_symlink():
            program.chmod(0o755)  # Homebrew installs its programs read-only
            subprocess.run(['strip', *(['-S', '-x'] if MACOS else ['--strip-unneeded']), str(program)], check=True)
    if MACOS:
        macos_bundle(staging)
    else:
        linux_bundle(staging)
    # The assistant's Python, unpacked as distributed.
    with tarfile.open(fetch(python_pin())) as stream:
        stream.extractall(staging, filter='data')
    trim_python(staging / 'python')
    manifest = {name: {k: v for k, v in pin.items() if k in ('version', 'url', 'sha256', 'license')}
                for name, pin in PINS.items() if name in ('qemu', 'mtools', 'cabextract', python_pin())}
    manifest['tesseract'] = {'version': subprocess.run([str(staging / 'bin/tesseract'), '--version'],
                                                       capture_output=True, text=True, env=clean_env(staging)
                                                       ).stdout.splitlines()[0]}
    (staging / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    check(staging)
    shutil.rmtree(out, ignore_errors=True)
    staging.rename(out)
    size = sum(p.stat().st_size for p in out.rglob('*') if p.is_file() and not p.is_symlink()) / 2**20
    print(f'Setup toolkit: {out} ({size:.0f} MB)')
    return out


def trim_python(python):
    """Keep what the assistant runs: the interpreter and the standard library
    without Tk, IDLE, pip, tests and headers."""
    stdlib = next((python / 'lib').glob('python3.*'))
    remove = [python / 'include', python / 'share', python / 'lib/pkgconfig', stdlib / 'site-packages']
    remove += [stdlib / name for name in ('idlelib', 'tkinter', 'turtledemo', 'ensurepip', 'test', 'lib2to3',
                                          'pydoc_data', 'turtle.py')]
    remove += list(stdlib.glob('config-3.*'))
    remove += [p for p in (python / 'lib').iterdir() if p.name.startswith(('libtcl', 'libtk', 'tcl', 'tk', 'itcl', 'thread'))]
    remove += [p for p in (stdlib / 'lib-dynload').glob('_tkinter*')]
    remove += [p for p in (python / 'bin').iterdir()
               if p.name.startswith(('2to3', 'idle', 'pip', 'pydoc')) or p.name.endswith('-config')]
    remove += list(python.rglob('__pycache__'))
    interpreter = (python / 'bin/python3').resolve().read_bytes()
    remove += [p for p in (python / 'lib').glob('libpython*') if b'libpython' not in interpreter]
    for path in remove:
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        elif path.exists() or path.is_symlink():
            path.unlink()


def clean_env(toolkit):
    """What the packaged app gives the tools: no developer paths at all."""
    return {'PATH': f'{toolkit}/bin:{toolkit}/python/bin:/usr/bin:/bin', 'HOME': os.environ.get('HOME', '/tmp'),
            'TESSDATA_PREFIX': str(toolkit / 'share/tessdata'), 'LC_ALL': 'C', 'PYTHONDONTWRITEBYTECODE': '1'}


def check(toolkit):
    """Every tool starts from the toolkit alone and reports its version."""
    env = clean_env(toolkit)
    commands = [[toolkit / 'bin/qemu-system-i386', '-L', toolkit / 'share/qemu', '--version'],
                [toolkit / 'bin/mcopy', '--version'], [toolkit / 'bin/cabextract', '--version'],
                [toolkit / 'bin/tesseract', '--list-langs'], [toolkit / 'python/bin/python3', '-c',
                                                             'import ssl, zlib, json, hashlib, zipfile, tarfile, socket, concurrent.futures; print("python ok")']]
    for command in commands:
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, env=env)
        text = (result.stdout + result.stderr).strip().splitlines()
        if result.returncode != 0 or (command[1] == '--list-langs' and 'eng' not in result.stdout):
            raise RuntimeError(f'{Path(command[0]).name} does not run from the toolkit: {" ".join(text[-3:])}')
        print(f'  ok  {Path(command[0]).name}: {text[0] if text else ""}')
    if not MACOS:
        for binary in [*(toolkit / 'bin').iterdir(), *(toolkit / 'lib').iterdir()]:
            if binary.is_symlink() or not is_elf(binary):
                continue
            listing = subprocess.run(['ldd', str(binary)], capture_output=True, text=True, env=env).stdout
            if 'not found' in listing:
                raise RuntimeError(f'{binary.name}: unresolved libraries\n{listing}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='check build/setup-toolkit and stop')
    parser.add_argument('--out', type=Path, default=OUT)
    args = parser.parse_args()
    if args.check:
        check(args.out.resolve())
    else:
        build(args.out.resolve())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
