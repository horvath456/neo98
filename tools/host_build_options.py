#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build target and renderer selection for the public host commands."""
import os
import platform
import sys

TARGETS = ('auto', 'macos-arm64', 'linux-x64')
GPUS = ('auto', 'metal', 'vulkan', 'none')


def add_arguments(parser):
    parser.add_argument('--target', choices=TARGETS, default='auto',
                        help='build/run: host target; auto uses this machine; cross compilation is unavailable')
    parser.add_argument('--gpu', choices=GPUS, default='auto',
                        help='build/run: Metal on macOS, Vulkan on Linux; none disables hardware 3D; CLI uses none')
    parser.add_argument('--host-media', action='store_true',
                        help='build/run/app/dmg: build Kodi on the host in (Media Center 98 and its add-on in Kodi; '
                             '), the gimmick of the phone and the tablet; '
                             'same as NEO98_WITH_HOST_MEDIA=1')


def resolve(target='auto', gpu='auto', *, cli=False):
    machine = platform.machine().lower()
    local = ('linux-x64' if sys.platform == 'linux' and machine in ('x86_64', 'amd64') else
             'macos-arm64' if sys.platform == 'darwin' and machine in ('arm64', 'aarch64') else None)
    if target not in TARGETS or gpu not in GPUS:
        raise RuntimeError('Unknown host target or GPU backend')
    if local is None:
        raise RuntimeError('The host requires macOS ARM64 or Linux x86_64')
    if target != 'auto' and target != local:
        raise RuntimeError(f'--target {target} cannot build on {sys.platform} {machine}; cross compilation is unavailable')
    if cli:
        if local != 'linux-x64':
            raise RuntimeError('cli and build-cli currently require Linux x86_64')
        if gpu not in ('auto', 'none'):
            raise RuntimeError('The CLI build has no GPU backend; use --gpu none or omit --gpu')
        return local, 'none'
    selected = gpu if gpu != 'auto' else os.environ.get('NEO98_GPU') or (
        'vulkan' if local == 'linux-x64' else 'metal')
    allowed = ('vulkan', 'none') if local == 'linux-x64' else ('metal', 'none')
    if selected not in allowed:
        raise RuntimeError(f'GPU backend {selected!r} is unavailable for {local}; choose {" or ".join(allowed)}')
    return local, selected


def configure(target='auto', gpu='auto', *, cli=False):
    target, gpu = resolve(target, gpu, cli=cli)
    os.environ['NEO98_GPU'] = gpu
    if target == 'linux-x64':
        os.environ['NEO98_WITH_VULKAN'] = '1' if gpu == 'vulkan' else '0'
    return target, gpu
