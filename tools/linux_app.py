#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a relocatable Linux AppDir, its tar archive and its AppImage without original Windows files."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from native_app import ROOT, build_host, build_fast, build_kms_host, native_x64, with_host_media
from build_dosbox_pure import core_dylib

SETUP_FILES = ('linux_setup.py', 'setup_win98.py', 'win98_c.py', 'win98_folder.py', 'win98_time_zone.py',
               'win98_glide_driver.py', 'win98_first_boot.py', 'win98_cabs.py', 'win98_iso.py', 'win98_source.py', 'win98_boot.py',
               'import_registry.py', 'provenance.py', 'win98_isos.json')
LAUNCHER = r'''#!/bin/sh
set -eu
neo98_bundle=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export NEO98_APP_MODE=1
export NEO98_DOSBOX_CORE="$neo98_bundle/usr/lib/neo98/libdosbox-pure.so"
export NEO98_SETUP_SCRIPT="$neo98_bundle/usr/share/neo98/setup/tools/linux_setup.py"
# The setup toolkit (tools/setup_toolkit.py): QEMU, mtools, cabextract,
# tesseract and Python, so installing from an ISO needs nothing installed.
if [ -d "$neo98_bundle/usr/lib/neo98/toolkit/bin" ]; then
    export NEO98_TOOLKIT="$neo98_bundle/usr/lib/neo98/toolkit"
    export PATH="$NEO98_TOOLKIT/bin:$NEO98_TOOLKIT/python/bin:$PATH"
    export PYTHONDONTWRITEBYTECODE=1
fi
if [ "${1-}" = --install-launcher ]; then
    exec python3 "$neo98_bundle/usr/share/neo98/linux_desktop_install.py" "$neo98_bundle"
fi
if [ -f "$neo98_bundle/usr/lib/neo98/libshaderc.so.1" ]; then
    export NEO98_SHADERC_LIBRARY="$neo98_bundle/usr/lib/neo98/libshaderc.so.1"
fi
if [ "${1-}" = --open ]; then
    shift
    if [ "$#" -gt 0 ]; then
        exec "$neo98_bundle/usr/bin/neo98-host" --open-host "$1"
    fi
fi
if [ "${1-}" = --kms ]; then
    # Without X11 or Wayland: start from a text console (Ctrl+Alt+F3).
    shift
    exec "$neo98_bundle/usr/bin/neo98-kms" "$@"
fi
if [ "${1-}" = --cli ]; then
    shift
    neo98_config=${NEO98_SETTINGS_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/neo98}
    if [ -z "${NEO98_WIN98_ROOT-}" ] && [ -r "$neo98_config/machine-path" ]; then
        IFS= read -r NEO98_WIN98_ROOT < "$neo98_config/machine-path" || true
        export NEO98_WIN98_ROOT
    fi
    exec "$neo98_bundle/usr/bin/neo98-cli" "$@"
fi
exec "$neo98_bundle/usr/bin/neo98-host" "$@"
'''
ICON = '''<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
<rect x="12" y="12" width="232" height="232" rx="32" fill="#008888"/>
<path d="M48 64h160v128H48z" fill="#c0c0c0" stroke="#222" stroke-width="5"/>
<path d="M56 72h144v26H56z" fill="#000080"/>
<path d="M185 77h10v16h-10z" fill="#c0c0c0"/>
<text x="128" y="168" text-anchor="middle" font-family="sans-serif" font-size="66" font-weight="bold" fill="#222">98</text>
</svg>
'''


# Libraries every desktop Linux provides. Bundling them would tie the package to
# the build machine's graphics stack (libstdc++ and the Mesa drivers, for one).
DESKTOP_LIBRARIES = (
    'linux-vdso', 'ld-linux', 'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so', 'libresolv.so',
    'libutil.so', 'libstdc++', 'libgcc_s', 'libatomic', 'libgtk-3', 'libgdk', 'libatk', 'libatspi', 'libcairo',
    'libpango', 'libharfbuzz', 'libfontconfig', 'libfreetype', 'libglib-2', 'libgobject', 'libgio', 'libgmodule',
    'libX', 'libxcb', 'libxkbcommon', 'libwayland', 'libdbus', 'libsystemd', 'libGL', 'libEGL', 'libOpenGL',
    'libvulkan', 'libdrm', 'libgbm', 'libasound', 'libpulse', 'libepoxy', 'libfribidi', 'libthai', 'libdatrie',
    'libgraphite2', 'libpng16', 'libz.so', 'libexpat', 'libffi', 'libpcre2', 'libmount', 'libblkid',
    'libselinux', 'libbrotli', 'libbz2', 'liblcms2', 'libpixman', 'libglycin', 'libseccomp', 'libuuid',
    'libudev', 'libinput', 'libgudev', 'libevdev', 'libmtdev', 'libwacom', 'libmvec')


def bundle_libraries(binaries, libraries):
    """Copy what the programs link beyond the desktop (SDL 2, for one) into
    usr/lib/neo98 and let them find it there."""
    pending, done = list(binaries), set()
    while pending:
        binary = pending.pop()
        if binary in done: continue
        done.add(binary)
        listing = subprocess.run(['ldd', str(binary)], capture_output=True, text=True).stdout
        located = {}
        for line in listing.splitlines():
            name, arrow, location = line.strip().partition(' => ')
            if arrow: located[name] = location.split(' (')[0].strip()
        # Only what this file names itself: what a desktop library needs comes
        # with the desktop.
        needed = subprocess.run(['patchelf', '--print-needed', str(binary)], capture_output=True, text=True,
                                check=True).stdout.split()
        for name in needed:
            if name.startswith(DESKTOP_LIBRARIES): continue
            location = located.get(name, 'not found')
            if location == 'not found': raise RuntimeError(f'{binary.name}: {name} not found')
            target = libraries / name
            if not target.exists():
                shutil.copy2(Path(location).resolve(), target); target.chmod(0o755)
                pending.append(target)
    for binary in done:
        rpath = '$ORIGIN' if binary.parent == libraries else '$ORIGIN/../lib/neo98'
        subprocess.run(['patchelf', '--set-rpath', rpath, str(binary)], check=True)


# The program at the head of an AppImage: it mounts the package that follows
# it in the file and starts AppRun (https://github.com/AppImage/type2-runtime,
# MIT). It is linked statically and brings its own FUSE and squashfs code, so
# the file starts on a system that has only fusermount.
APPIMAGE_RUNTIME = {
    'url': 'https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64',
    'sha256': '2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d',
    'revision': 'dd6cebedcbddde9c82f89b011e8e1d40b6e43868',
}


def appimage(appdir):
    """The AppDir as one file to download and start: the runtime, then the
    folder as a squashfs image. The same input gives the same bytes."""
    import hashlib, urllib.request
    if not shutil.which('mksquashfs'): raise RuntimeError('The AppImage needs mksquashfs (squashfs-tools).')
    cache = ROOT / 'build/release-downloads' / (APPIMAGE_RUNTIME['sha256'] + '.appimage-runtime')
    cache.parent.mkdir(parents=True, exist_ok=True)
    if not cache.exists():
        with urllib.request.urlopen(APPIMAGE_RUNTIME['url'], timeout=120) as response: data = response.read()
        if hashlib.sha256(data).hexdigest() != APPIMAGE_RUNTIME['sha256']:
            raise RuntimeError('AppImage runtime checksum mismatch')
        temporary = cache.with_name(cache.name + f'.{os.getpid()}')
        temporary.write_bytes(data); temporary.replace(cache)
    runtime = cache.read_bytes()
    if hashlib.sha256(runtime).hexdigest() != APPIMAGE_RUNTIME['sha256']:
        raise RuntimeError('Cached AppImage runtime checksum mismatch')
    image = ROOT / f'build/.Neo98-{os.getpid()}.squashfs'
    out = ROOT / 'build/Neo98-x86_64.AppImage'
    try:
        subprocess.run(['mksquashfs', str(appdir), str(image), '-root-owned', '-noappend', '-no-xattrs',
                        '-comp', 'zstd', '-Xcompression-level', '19', '-b', '1M',
                        '-mkfs-time', '0', '-all-time', '0', '-quiet', '-no-progress'], check=True)
        temporary = out.with_name(out.name + f'.{os.getpid()}')
        temporary.write_bytes(runtime + image.read_bytes()); temporary.chmod(0o755); temporary.replace(out)
    finally:
        image.unlink(missing_ok=True)
    print(f'AppImage: {out}')
    return out


def package(no_build=False, *, release=False, image=False):
    if release:
        os.environ['NEO98_WITH_HOST_MEDIA'] = '0'
    if not native_x64(): raise RuntimeError('The Linux package requires Linux x86_64.')
    if not no_build:
        build_host(vulkan=os.environ.get('NEO98_WITH_VULKAN', '1') == '1')
        build_fast('neo98_cli.cpp', 'neo98-cli')
        build_kms_host(vulkan=os.environ.get('NEO98_WITH_VULKAN', '1') == '1')
    from check_release_embedded import check_dependencies
    for name in ('neo98-host', 'neo98-cli', 'neo98-kms'):
        binary = ROOT / 'build/native-app' / name
        if b'uc_open' in binary.read_bytes():
            # The package may carry Apache-2.0 shaderc only because nothing in
            # it is GPLv2-only; Unicorn is, so it must never be in here.
            raise RuntimeError(f'{name} contains Unicorn; the Linux package must not')
        import json
        dependencies = Path(str(binary) + '.dependencies.json')
        problems = check_dependencies(json.loads(dependencies.read_text()))
        if problems: raise RuntimeError('\n'.join(problems))
    out = ROOT / 'build/Neo98.AppDir'
    staging = ROOT / f'build/.NEO98.AppDir-{os.getpid()}'
    if staging.exists(): shutil.rmtree(staging)
    binaries = staging / 'usr/bin'; libraries = staging / 'usr/lib/neo98'
    scripts = staging / 'usr/share/neo98/setup/tools'
    for folder in (binaries, libraries, scripts): folder.mkdir(parents=True)
    for name in ('neo98-host', 'neo98-cli', 'neo98-kms'):
        shutil.copy2(ROOT / 'build/native-app' / name, binaries / name)
        subprocess.run(['strip', '--strip-unneeded', str(binaries / name)], check=True)
    # Without the table of saved states the app keeps no Windows states (tools/native_app.py, state_problems).
    layout = ROOT / 'build/native-app/neo98-host.layout'
    if layout.is_file():
        shutil.copy2(layout, binaries / 'neo98-host.layout')
    else:
        print(f'warning: {layout} is missing; this app saves no states of Windows programs', file=sys.stderr)
    shutil.copy2(core_dylib(), libraries / 'libdosbox-pure.so')
    bundle_libraries([binaries / 'neo98-host', binaries / 'neo98-cli', binaries / 'neo98-kms',
                      libraries / 'libdosbox-pure.so'], libraries)
    toolkit = ROOT / 'build/setup-toolkit'
    if not (toolkit / 'manifest.json').is_file():
        if release: raise RuntimeError('build/setup-toolkit is missing; run python3 tools/setup_toolkit.py')
        print('warning: no setup toolkit (tools/setup_toolkit.py); ISO setup needs the tools installed', file=sys.stderr)
    else:
        shutil.copytree(toolkit, libraries / 'toolkit', symlinks=True)
    # The Vulkan renderer loads shaderc by name while a game runs. The package
    # carries it when the build machine has it. Nothing in the package is
    # GPLv2-only (there is no Unicorn on Linux), so the package as a whole is
    # distributed under the GPL version 3, which accepts Apache-2.0 shaderc.
    candidates = [ROOT / 'build/toolchain/usr/lib/x86_64-linux-gnu/libshaderc.so.1',
                  Path('/usr/lib/x86_64-linux-gnu/libshaderc.so.1')]
    if os.environ.get('NEO98_SYSROOT'):
        candidates.insert(0, Path(os.environ['NEO98_SYSROOT']) / 'usr/lib/x86_64-linux-gnu/libshaderc.so.1')
    shaderc = next((p for p in candidates if p.is_file()), None)
    if shaderc: shutil.copy2(shaderc, libraries / 'libshaderc.so.1')

    for name in SETUP_FILES: shutil.copy2(ROOT / 'tools' / name, scripts / name)
    shutil.copytree(ROOT / 'tools/freedos_boot', scripts / 'freedos_boot')
    for name in ('NEO98HW.EXE', 'NEO98CAP.DRV', 'NEO98FF.DLL', 'CAMERA.EXE', 'CAMERA.LNK'):
        shutil.copy2(ROOT / 'build/host-devices' / name, staging / 'usr/share/neo98' / name)
    for name in (('MEDIACTR.EXE', 'MEDIACTR.LNK', 'service.neo98')
                 if with_host_media() else ()):
        (shutil.copytree if name == 'service.neo98' else shutil.copy2)(ROOT / 'build/host-media' / name, staging / 'usr/share/neo98' / name)
    shutil.copy2(ROOT / 'tools/linux_desktop_install.py', staging / 'usr/share/neo98/linux_desktop_install.py')
    launcher = staging / 'AppRun'; launcher.write_text(LAUNCHER); launcher.chmod(0o755)
    (staging / 'neo98.svg').write_text(ICON)
    # The icon a file manager shows for the AppImage.
    (staging / '.DirIcon').symlink_to('neo98.svg')
    (staging / 'neo98.desktop').write_text('[Desktop Entry]\nType=Application\nName=NEO98\n'
        'Comment=Windows 98 applications on Linux\nExec=AppRun\nIcon=neo98\nTerminal=false\nCategories=Game;Emulator;\n')
    (staging / 'README.txt').write_text('Neo98 for Linux x86_64\n\n'
        'Run ./AppRun and select your Windows 98 folder, ZIP archive or ISO.\n'
        'Started as Neo98-x86_64.AppImage, give the options below to that file instead of ./AppRun.\n'
        'Use ./AppRun --setup-linux to reopen setup, or ./AppRun --cli for the terminal host.\n'
        'Without a desktop, run ./AppRun --kms from a text console (Ctrl+Alt+F3): Windows then\n'
        'fills the screen through DRM/KMS, with keyboard and mouse through libinput;\n'
        'Ctrl+Alt+Backspace ends it. It needs Mesa (libgbm, libEGL) and libinput, and your\n'
        'user in the groups video and input.\n'
        'Use ./AppRun --install-launcher to install a launcher and register Neo98 for Open With.\n'
        'For double-click launching, select Neo98 as the default application in your file manager.\n'
        'This package contains no Windows files.\n'
        'This package is distributed under the GNU GPL version 3 (LICENSES/GPL-3.0.txt),\n'
        'because it includes shaderc (Apache 2.0). Neo98 itself is licensed under the\n'
        'GPL version 2 or later (COPYING). THIRD_PARTY.md lists all components.\n\n'
        'The graphical host needs a desktop with GTK 3 and X11 or XWayland, and a Vulkan driver for 3D.\n'
        'Wayland sessions use XWayland by default.\n'
        'The legacy native Wayland output requires SDL_VIDEODRIVER=wayland.\n'
        'Everything else is in this package: SDL 2, and for installing Windows from your ISO\n'
        'QEMU, mtools, cabextract, tesseract and Python (usr/lib/neo98/toolkit).\n'
        'Binaries require the glibc version used by the build machine.\n')
    shutil.copy2(ROOT / 'COPYING', staging / 'COPYING')
    shutil.copy2(ROOT / 'THIRD_PARTY.md', staging / 'THIRD_PARTY.md')
    shutil.copytree(ROOT / 'LICENSES', staging / 'LICENSES')
    shutil.copy2(ROOT / 'docs/FREEDOS-BOOT.md', staging / 'FREEDOS-BOOT.md')
    shutil.copy2(ROOT / 'third_party/dosbox-pure/LICENSE', staging / 'DOSBox-Pure-LICENSE')
    subprocess.run(['desktop-file-validate', str(staging / 'neo98.desktop')], check=True)
    if release:
        from check_release_bundle import check_host_media
        problems = check_host_media(staging)
        if problems: raise RuntimeError('Release bundle check failed: ' + ', '.join(problems))
    if out.exists(): shutil.rmtree(out)
    staging.rename(out)
    archive = ROOT / 'build/Neo98-linux-x64.tar.gz'
    with tarfile.open(archive, 'w:gz') as stream: stream.add(out, arcname='NEO98.AppDir')
    print(f'Linux app: {out}\nPackage: {archive}')
    if release or image: appimage(out)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--release', action='store_true',
                        help='exclude Media Center regardless of NEO98_WITH_HOST_MEDIA and check the package')
    parser.add_argument('--appimage', action='store_true',
                        help='also write build/Neo98-x86_64.AppImage (a release always does)')
    args = parser.parse_args()
    package(args.no_build, release=args.release, image=args.appimage)
    return 0

if __name__ == '__main__': raise SystemExit(main())
