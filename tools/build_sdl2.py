#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build SDL 2 from its pinned release.

The Linux package carries SDL along (tools/linux_app.py), and the hosts use
calls that came after SDL 2.0.20, the SDL of Ubuntu 22.04. The release is
built on that system so that it starts there too: this builds the SDL the
package had before (2.30) and installs it in build/toolchain/usr, whose
sdl2-config the build finds when build/toolchain/usr/bin is first on PATH.

SDL loads X11, Wayland, the sound servers and the other system libraries by
name when it starts, so the library does not depend on the versions of the
build machine. Their development packages have to be installed for a backend
to be built at all: the build stops when one of those the package needs is
missing.

    python3 tools/build_sdl2.py
"""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PIN = {
    'version': '2.30.12',
    'url': 'https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz',
    'sha256': 'ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea',
}
PREFIX = ROOT / 'build/toolchain/usr'
STAMP = PREFIX / 'lib/libSDL2.pin'
# What a build without the development package would silently leave out.
REQUIRED = ('SDL_X11', 'SDL_WAYLAND', 'SDL_PULSEAUDIO', 'SDL_ALSA', 'SDL_PIPEWIRE', 'SDL_KMSDRM',
            'SDL_OPENGL', 'SDL_OPENGLES', 'SDL_VULKAN', 'SDL_DBUS', 'SDL_IBUS', 'SDL_LIBUDEV',
            'SDL_WAYLAND_LIBDECOR')


def fetch():
    cache = ROOT / 'build/release-downloads' / (PIN['sha256'] + '.tar.gz')
    cache.parent.mkdir(parents=True, exist_ok=True)
    if not cache.exists():
        with urllib.request.urlopen(PIN['url'], timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != PIN['sha256']:
            raise RuntimeError('SDL source archive checksum mismatch')
        cache.write_bytes(data)
    if hashlib.sha256(cache.read_bytes()).hexdigest() != PIN['sha256']:
        raise RuntimeError('Cached SDL source archive checksum mismatch')
    return cache


def build():
    if STAMP.is_file() and STAMP.read_text().strip() == PIN['sha256'] and (PREFIX / 'bin/sdl2-config').is_file():
        print(f'SDL {PIN["version"]}: {PREFIX} is current')
        return PREFIX
    source = ROOT / 'third_party' / ('SDL2-' + PIN['version'])
    if not (source / 'CMakeLists.txt').is_file():
        source.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(fetch()) as archive:
            # Pinned by its checksum; 'tar' keeps every member inside the folder.
            archive.extractall(source.parent, filter='tar')
    work = ROOT / 'build/sdl2'
    shutil.rmtree(work, ignore_errors=True)
    subprocess.run(['cmake', '-S', str(source), '-B', str(work), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                    '-DCMAKE_INSTALL_PREFIX=' + str(PREFIX), '-DCMAKE_INSTALL_LIBDIR=lib',
                    '-DSDL_SHARED=ON', '-DSDL_STATIC=OFF', '-DSDL_TEST=OFF',
                    # The folders of the build machine stay out of the library:
                    # SDL's assertions name their source file.
                    f'-DCMAKE_C_FLAGS=-ffile-prefix-map={source}=SDL2 -ffile-prefix-map={work}=build',
                    *('-D' + name + '=ON' for name in REQUIRED)], check=True)
    # SDL's own record of what it found on this machine.
    defined = next(work.rglob('SDL_config.h')).read_text()
    wanted = {'SDL_VIDEO_DRIVER_X11': 'X11', 'SDL_VIDEO_DRIVER_WAYLAND': 'Wayland',
              'SDL_VIDEO_DRIVER_KMSDRM': 'KMS/DRM', 'SDL_AUDIO_DRIVER_PULSEAUDIO': 'PulseAudio',
              'SDL_AUDIO_DRIVER_ALSA': 'ALSA', 'SDL_AUDIO_DRIVER_PIPEWIRE': 'PipeWire',
              'SDL_VIDEO_VULKAN': 'Vulkan', 'HAVE_DBUS_DBUS_H': 'D-Bus', 'HAVE_LIBUDEV_H': 'udev',
              'HAVE_LIBDECOR_H': 'libdecor'}
    missing = [name for symbol, name in wanted.items() if f'#define {symbol} 1' not in defined]
    if missing:
        raise RuntimeError('SDL would be built without ' + ', '.join(missing) +
                           ': install their development packages (docs/BUILDING.md)')
    subprocess.run(['ninja', '-C', str(work), 'install/strip'], check=True)
    STAMP.write_text(PIN['sha256'] + '\n')
    print(f'SDL {PIN["version"]}: {PREFIX}')
    return PREFIX


if __name__ == '__main__':
    build()
