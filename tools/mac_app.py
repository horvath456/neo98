#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build build/Neo98.app and build/Neo98.dmg (docs/BUILDING.md).

    python3 neo98.py app # the bundle
    python3 neo98.py dmg # the bundle in a disk image with a link to /Applications

The bundle carries everything the host reads at run time, so it starts on a
Mac without this checkout, without Homebrew and without Python:

    Contents/MacOS/Neo98 build/native-app/neo98-host, symbols stripped
    Contents/Resources/Neo98.layout the fields of a saved state of this very program (tools/state_layout.py)
    Contents/MacOS/neo98-dock-helper the Dock icon of a guest program (tools/host_dock.h)
    Contents/Frameworks/ every library `otool -L` names outside the
                                       system (SDL2, the SDL3 it opens, Unicorn)
                                       and the DOS machine, install names set to
                                       @rpath, the program's only rpath
                                       @executable_path/../Frameworks
    (no file of Win98: registry and system font come from the machine folder
     at start, tools/host_registry.h)
    Contents/Resources/setup/tools/ the Python files of the setup assistant
                                       (tools/setup_win98.py and what it imports;
                                       standard library only, run by the toolkit's Python)
    Contents/Resources/toolkit/ QEMU, mtools, cabextract, tesseract and Python for setup
                                       (tools/setup_toolkit.py)
    Contents/Resources/Neo98.icns the icon, drawn by this file
    Contents/Info.plist names the app as opener of.exe,.com,.bat,.pif,.lnk,.scr and viewer of.iso and.cue,
                                       and the service "Open as game machine" for.exe and.com in the Finder

The signature is ad hoc; nothing is notarised.
"""
from __future__ import annotations

import argparse
import os
import plistlib
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))

BUILD = ROOT / 'build'
APP = BUILD / 'Neo98.app'
DMG = BUILD / 'Neo98.dmg'
IDENTIFIER = 'at.andreas-horvath.neo98'
# The Python files of the assistant, relative to tools/. setup_win98.py with
# --user imports nothing else (tools/test_mac_app_host.py runs it isolated).
SETUP_FILES = ('setup_win98.py', 'win98_c.py', 'win98_folder.py', 'win98_time_zone.py',
               'win98_glide_driver.py', 'win98_first_boot.py', 'win98_cabs.py', 'win98_iso.py', 'win98_source.py', 'win98_boot.py',
               'import_registry.py', 'provenance.py', 'win98_isos.json')
SETUP_DIRECTORIES = ('freedos_boot',)
SYSTEM_PREFIXES = ('/usr/lib/', '/System/')


# --- The icon -------------------------------------------------------------
# A Win98-grey window with a blue title bar on the teal desktop, "98" in block
# digits in its client area. Drawn here with rectangles; no artwork of anyone
# else. Rendered once at 2048 pixels, the sizes of the iconset come from sips.

class Canvas:
    def __init__(self, size):
        self.size = size
        self.rows = [bytearray(size * 4) for _ in range(size)]

    def span(self, y, x0, x1, colour):
        if 0 <= y < self.size:
            x0, x1 = max(0, int(x0)), min(self.size, int(x1))
            if x1 > x0:
                self.rows[y][x0 * 4:x1 * 4] = bytes(colour) * (x1 - x0)

    def rect(self, x0, y0, x1, y1, colour):
        for y in range(int(y0), int(y1)):
            self.span(y, x0, x1, colour)

    def rounded(self, x0, y0, x1, y1, radius, top, bottom):
        """A rounded rectangle with a vertical gradient from `top` to `bottom`."""
        height = y1 - y0
        for y in range(int(y0), int(y1)):
            t = (y - y0) / max(1, height - 1)
            colour = tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,)
            inset = 0
            if y < y0 + radius:
                d = y0 + radius - y - 0.5
                inset = radius - (radius * radius - d * d) ** 0.5
            elif y >= y1 - radius:
                d = y - (y1 - radius) + 0.5
                inset = radius - (radius * radius - d * d) ** 0.5
            self.span(y, x0 + inset, x1 - inset, colour)

    def gradient_x(self, x0, y0, x1, y1, left, right):
        row = bytearray()
        for x in range(int(x0), int(x1)):
            t = (x - x0) / max(1, x1 - x0 - 1)
            row += bytes(int(a + (b - a) * t) for a, b in zip(left, right)) + b'\xff'
        for y in range(int(y0), int(y1)):
            self.rows[y][int(x0) * 4:int(x1) * 4] = row

    def png(self, path):
        raw = b''.join(b'\0' + bytes(row) for row in self.rows)

        def chunk(kind, data):
            body = kind + data
            return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))
        Path(path).write_bytes(b'\x89PNG\r\n\x1a\n'
                               + chunk(b'IHDR', struct.pack('>IIBBBBB', self.size, self.size, 8, 6, 0, 0, 0))
                               + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


# Seven-segment-like block digits on a 5x7 grid.
DIGITS = {'9': ('#####', '#...#', '#...#', '#####', '....#', '....#', '#####'),
          '8': ('#####', '#...#', '#...#', '#####', '#...#', '#...#', '#####')}


def draw_icon(path, size=2048):
    c = Canvas(size)
    u = size / 1024
    # The macOS icon shape: 824 of 1024 with a radius of 185.
    c.rounded(100 * u, 100 * u, 924 * u, 924 * u, 185 * u, (0, 150, 150), (0, 100, 104))
    grey, white, dark, black = (192, 192, 192, 255), (255, 255, 255, 255), (128, 128, 128, 255), (0, 0, 0, 255)
    x0, y0, x1, y1 = 212 * u, 262 * u, 812 * u, 762 * u
    b = 10 * u
    # The 3D frame of a Win98 window: light top and left, dark bottom and right.
    c.rect(x0, y0, x1, y1, black)
    c.rect(x0, y0, x1 - b, y1 - b, white)
    c.rect(x0 + b, y0 + b, x1 - b, y1 - b, dark)
    c.rect(x0 + b, y0 + b, x1 - 2 * b, y1 - 2 * b, grey)
    # Title bar with the Win98 gradient and three buttons.
    tx0, ty0, tx1, ty1 = x0 + 3 * b, y0 + 3 * b, x1 - 4 * b, y0 + 3 * b + 72 * u
    c.gradient_x(tx0, ty0, tx1, ty1, (0, 0, 128), (16, 132, 208))
    side = 52 * u
    for index in range(3):
        bx1 = tx1 - 10 * u - index * (side + 8 * u)
        bx0, by0 = bx1 - side, ty0 + 10 * u
        c.rect(bx0, by0, bx1, by0 + side, black)
        c.rect(bx0, by0, bx1 - 5 * u, by0 + side - 5 * u, white)
        c.rect(bx0 + 5 * u, by0 + 5 * u, bx1 - 5 * u, by0 + side - 5 * u, grey)
    # Client area, sunken, white.
    cx0, cy0, cx1, cy1 = tx0, ty1 + 2 * b, tx1, y1 - 4 * b
    c.rect(cx0, cy0, cx1, cy1, white)
    c.rect(cx0, cy0, cx1 - b / 2, cy1 - b / 2, dark)
    c.rect(cx0 + b / 2, cy0 + b / 2, cx1 - b / 2, cy1 - b / 2, white)
    # "98".
    cell = 40 * u
    width = (5 + 1 + 5) * cell
    left = (cx0 + cx1 - width) / 2
    top = (cy0 + cy1 - 7 * cell) / 2
    for position, digit in enumerate('98'):
        for row, line in enumerate(DIGITS[digit]):
            for column, mark in enumerate(line):
                if mark == '#':
                    gx = left + (position * 6 + column) * cell
                    gy = top + row * cell
                    c.rect(gx, gy, gx + cell + 1, gy + cell + 1, (0, 0, 128, 255))
    c.png(path)


def build_icon(target, work=None):
    # Two builds at once (two tests) each need their own work folder.
    work = Path(work) if work else BUILD / 'app-icon'
    shutil.rmtree(work, ignore_errors=True)
    iconset = work / 'Neo98.iconset'
    iconset.mkdir(parents=True)
    master = work / 'icon-2048.png'
    draw_icon(master)
    for points in (16, 32, 128, 256, 512):
        for factor in (1, 2):
            pixels = points * factor
            name = f'icon_{points}x{points}' + ('@2x' if factor == 2 else '') + '.png'
            subprocess.run(['/usr/bin/sips', '-z', str(pixels), str(pixels), str(master), '--out', str(iconset / name)],
                           check=True, capture_output=True)
    subprocess.run(['/usr/bin/iconutil', '-c', 'icns', str(iconset), '-o', str(target)], check=True)
    shutil.rmtree(work, ignore_errors=True)
    return target


# --- Libraries -------------------------------------------------------------

def tool(*command):
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def dependencies(binary):
    """The libraries `otool -L` names, without the binary's own id."""
    lines = tool('otool', '-L', str(binary)).splitlines()[1:]
    names = [line.strip().split(' (')[0] for line in lines if line.strip()]
    own = tool('otool', '-D', str(binary)).splitlines()[1:]
    return [name for name in names if name not in own]


def rpaths(binary):
    out, lines = [], tool('otool', '-l', str(binary)).splitlines()
    for index, line in enumerate(lines):
        if 'LC_RPATH' in line:
            out.append(lines[index + 2].strip().split(' ', 1)[1].rsplit(' (offset', 1)[0])
    return out


def resolve(name, binary, executable):
    """The file behind an install name of `binary`."""
    if name.startswith('@rpath/'):
        for base in rpaths(binary) + rpaths(executable):
            base = base.replace('@loader_path', str(Path(binary).parent)).replace('@executable_path',
                                                                                 str(Path(executable).parent))
            candidate = Path(base) / name[len('@rpath/'):]
            if candidate.exists():
                return candidate.resolve()
        raise RuntimeError(f'{binary}: cannot find {name} in its rpaths')
    if name.startswith('@loader_path/'):
        return (Path(binary).parent / name[len('@loader_path/'):]).resolve()
    if name.startswith('@executable_path/'):
        return (Path(executable).parent / name[len('@executable_path/'):]).resolve()
    return Path(name).resolve()


def sdl3_library(sdl2):
    """sdl2-compat opens SDL3 at run time, first as @loader_path/libSDL3.dylib."""
    if b'@loader_path/libSDL3.dylib' not in Path(sdl2).read_bytes():
        return None
    for base in [*(p.replace('@loader_path', str(Path(sdl2).parent)) for p in rpaths(sdl2)),
                 '/opt/homebrew/opt/sdl3/lib', '/usr/local/opt/sdl3/lib', '/opt/homebrew/lib', '/usr/local/lib']:
        candidate = Path(base) / 'libSDL3.dylib'
        if candidate.exists():
            return candidate.resolve()
    raise RuntimeError('sdl2-compat needs libSDL3.dylib and it was not found; brew install sdl3')


def collect_libraries(executable, frameworks, extra=()):
    """Copy every non-system library the program and its libraries name into
    `frameworks`. Returns {original install name or path: bundled file name}."""
    bundled = {}       # resolved source -> name in Frameworks
    renames = {}       # consumer (bundled path) -> [(old name, new name)]
    queue = [(Path(executable), Path(executable))]
    seen = set()

    def add(source, name=None):
        source = Path(source).resolve()
        if source not in bundled:
            bundled[source] = name or source.name
            target = frameworks / bundled[source]
            shutil.copyfile(source, target)
            target.chmod(0o755)
            queue.append((source, target))
        return bundled[source]

    for source, name in extra:
        add(source, name)
    while queue:
        source, target = queue.pop(0)
        if target in seen:
            continue
        seen.add(target)
        for name in dependencies(source):
            if name.startswith(SYSTEM_PREFIXES):
                continue
            library = resolve(name, source, executable)
            renames.setdefault(target, []).append((name, add(library)))
            if library.name.startswith('libSDL2'):
                sdl3 = sdl3_library(library)
                if sdl3:
                    add(sdl3, 'libSDL3.dylib')
    return bundled, renames


def fix_install_names(program, frameworks, renames):
    for library in sorted(frameworks.iterdir()):
        subprocess.run(['install_name_tool', '-id', f'@rpath/{library.name}', str(library)],
                       check=True, capture_output=True)
        for path in rpaths(library):
            subprocess.run(['install_name_tool', '-delete_rpath', path, str(library)], check=True, capture_output=True)
        subprocess.run(['install_name_tool', '-add_rpath', '@loader_path', str(library)], check=True, capture_output=True)
    for consumer, changes in renames.items():
        consumer = program if consumer.name == Path(program).name and consumer.parent != frameworks else consumer
        for old, new in changes:
            subprocess.run(['install_name_tool', '-change', old, f'@rpath/{new}', str(consumer)],
                           check=True, capture_output=True)
    for path in rpaths(program):
        subprocess.run(['install_name_tool', '-delete_rpath', path, str(program)], check=True, capture_output=True)
    subprocess.run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', str(program)],
                   check=True, capture_output=True)


# --- Info.plist ------------------------------------------------------------

def document_type(name, extensions, role, types, rank='Alternate'):
    return {'CFBundleTypeName': name, 'CFBundleTypeExtensions': list(extensions), 'CFBundleTypeRole': role,
            'LSItemContentTypes': list(types), 'LSHandlerRank': rank, 'CFBundleTypeIconFile': 'Neo98'}


def imported_type(identifier, description, extensions, conforms=('public.data',)):
    return {'UTTypeIdentifier': identifier, 'UTTypeDescription': description,
            'UTTypeConformsTo': list(conforms),
            'UTTypeTagSpecification': {'public.filename-extension': list(extensions)}}


# The entry "Open as game machine" in the context menu of the Finder
# (Rechtsklick > Dienste): a service of the app. macOS sends the chosen files
# to the provider of tools/host_game_menu.mm (openAsGameMachine:userData:error:)
# and starts the app for it when it does not run. The types are the program
# files a game machine can be made for. NSRequiredContext makes the service
# one that is on without a visit to the system settings, and keeps it to the
# Finder.
GAME_SERVICE_TYPES = ('com.microsoft.windows-executable', 'at.andreas-horvath.neo98.com')
GAME_SERVICE = {
    'NSMenuItem': {'default': 'Open as game machine'},
    'NSMessage': 'openAsGameMachine',
    'NSPortName': 'Neo98',
    'NSSendFileTypes': list(GAME_SERVICE_TYPES),
    'NSRequiredContext': {'NSApplicationIdentifier': 'com.apple.finder'},
    # The app may have to start first; the provider only queues the files.
    'NSTimeout': '30000',
}


def release_version():
    """The newest release tag reachable from HEAD (v0.2.0 gives 0.2.0); 0.0
    for a checkout without tags."""
    try:
        tag = subprocess.check_output(['git', 'describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*'],
                                      cwd=ROOT, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return '0.0'
    return tag[1:]


def info_plist(version, revision):
    return {
        'CFBundleName': 'Neo98', 'CFBundleDisplayName': 'Neo98', 'CFBundleIdentifier': IDENTIFIER,
        'CFBundleExecutable': 'Neo98', 'CFBundlePackageType': 'APPL', 'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundleShortVersionString': version, 'CFBundleVersion': revision, 'CFBundleIconFile': 'Neo98',
        'CFBundleDevelopmentRegion': 'en', 'LSMinimumSystemVersion': '13.0',
        'LSApplicationCategoryType': 'public.app-category.utilities', 'NSHighResolutionCapable': True,
        'NSCameraUsageDescription': 'Neo98 shares the camera with Windows 98 when requested.',
        'NSMicrophoneUsageDescription': 'Neo98 shares the microphone with Windows 98 when a program records audio.',
        'NSLocationWhenInUseUsageDescription': 'Neo98 needs location permission to display Wi-Fi network names.',
        'NSLocationUsageDescription': 'Neo98 needs location permission to display Wi-Fi network names.',
        'NSPrincipalClass': 'NSApplication', 'NSSupportsAutomaticGraphicsSwitching': True,
        'NSHumanReadableCopyright': 'Neo98, GPL-2.0-or-later. Windows 98 is not included.',
        'NSServices': [GAME_SERVICE],
        'CFBundleDocumentTypes': [
            document_type('Windows program', ('exe', 'com', 'scr'), 'Shell',
                          ('com.microsoft.windows-executable', 'at.andreas-horvath.neo98.com',
                           'at.andreas-horvath.neo98.scr'), 'Default'),
            document_type('Windows batch file', ('bat',), 'Shell',
                          ('com.microsoft.batch-file', 'at.andreas-horvath.neo98.bat')),
            document_type('Windows shortcut', ('lnk', 'pif'), 'Shell',
                          ('at.andreas-horvath.neo98.lnk', 'at.andreas-horvath.neo98.pif'), 'Default'),
            document_type('CD image', ('iso', 'cue'), 'Viewer',
                          ('public.iso-image', 'at.andreas-horvath.neo98.cue')),
        ],
        'UTImportedTypeDeclarations': [
            imported_type('com.microsoft.windows-executable', 'Windows program', ('exe',),
                          ('public.data', 'public.executable')),
            imported_type('at.andreas-horvath.neo98.com', 'DOS program', ('com',)),
            imported_type('at.andreas-horvath.neo98.scr', 'Windows screensaver', ('scr',)),
            imported_type('at.andreas-horvath.neo98.bat', 'Windows batch file', ('bat',), ('public.text',)),
            imported_type('at.andreas-horvath.neo98.lnk', 'Windows shortcut', ('lnk',)),
            imported_type('at.andreas-horvath.neo98.pif', 'DOS program shortcut', ('pif',)),
            imported_type('at.andreas-horvath.neo98.cue', 'CUE sheet', ('cue',), ('public.text',)),
        ],
    }


# --- The bundle ------------------------------------------------------------

def revision():
    try:
        return subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'


def signing_identity():
    """Use ad hoc signing unless the builder explicitly selects an identity.

    Do not include a developer's personal certificate merely because it is
    available in their login keychain.
    """
    return os.environ.get('NEO98_SIGN_IDENTITY') or '-'


def build_app(build=True, target=APP):
    from native_app import build_host, with_host_media
    from build_dosbox_pure import core_dylib
    binary = build_host() if build else BUILD / 'native-app/neo98-host'
    if not binary.is_file():
        raise RuntimeError(f'{binary} is missing; run python3 neo98.py build first')
    target = Path(target)
    staging = target.with_name(target.name + '.neu')
    shutil.rmtree(staging, ignore_errors=True)
    contents = staging / 'Contents'
    macos, frameworks, resources = contents / 'MacOS', contents / 'Frameworks', contents / 'Resources'
    for directory in (macos, frameworks, resources / 'setup/tools'):
        directory.mkdir(parents=True)
    program = macos / 'Neo98'
    shutil.copyfile(binary, program)
    program.chmod(0o755)
    # The DOS machine is opened with dlopen, so otool does not name it.
    bundled, renames = collect_libraries(binary, frameworks, extra=[(core_dylib(), 'libdosbox-pure.dylib')])
    renames = {(program if consumer == Path(binary) else consumer): changes for consumer, changes in renames.items()}
    fix_install_names(program, frameworks, renames)
    subprocess.run(['strip', '-S', '-x', str(program)], check=True, capture_output=True)
    # The table of the fields of a saved state (tools/state_layout.py). It was made from the debug information of
    # this very link and names the program by its UUID, which strip and the
    # new install names leave alone; without it the app keeps no states of
    # Windows games and says so (tools/native_app.py, state_layout).
    layout = Path(str(binary) + '.layout')
    if layout.is_file():
        shutil.copyfile(layout, resources / 'Neo98.layout')
    else:
        (resources / 'Neo98.layout').unlink(missing_ok=True)
        print(f'warning: {layout} is missing; this app saves no states of Windows programs', file=sys.stderr)
    # The Dock icon of a guest program (tools/host_dock.h): the host copies it
    # into a bundle per program at run time.
    helper = macos / 'neo98-dock-helper'
    shutil.copyfile(Path(binary).with_name('neo98-dock-helper'), helper)
    helper.chmod(0o755)
    subprocess.run(['strip', '-S', '-x', str(helper)], check=True, capture_output=True)
    identity = signing_identity()
    signing_options = [] if identity == '-' else ['--options', 'runtime', '--timestamp']
    subprocess.run(['codesign', '--force', '--sign', identity, *signing_options, str(helper)], check=True, capture_output=True)
    for name in SETUP_FILES:
        shutil.copyfile(ROOT / 'tools' / name, resources / 'setup/tools' / name)
    for name in SETUP_DIRECTORIES:
        shutil.copytree(ROOT / 'tools' / name, resources / 'setup/tools' / name)
    # The setup toolkit (tools/setup_toolkit.py): what installing Windows from
    # the user's ISO needs, so the app needs no Homebrew and no Xcode tools.
    toolkit = ROOT / 'build/setup-toolkit'
    if (toolkit / 'manifest.json').is_file():
        shutil.copytree(toolkit, resources / 'toolkit', symlinks=True)
    else:
        print('warning: no setup toolkit (tools/setup_toolkit.py); ISO setup needs Homebrew tools', file=sys.stderr)
    shutil.copyfile(ROOT / 'COPYING', resources / 'COPYING')
    shutil.copyfile(ROOT / 'THIRD_PARTY.md', resources / 'THIRD_PARTY.md')
    shutil.copytree(ROOT / 'LICENSES', resources / 'LICENSES')
    shutil.copyfile(ROOT / 'docs/FREEDOS-BOOT.md', resources / 'FREEDOS-BOOT.md')
    build_icon(resources / 'Neo98.icns', target.with_name(target.name + '.icon'))
    commit = revision()
    for name in ('NEO98HW.EXE', 'NEO98CAP.DRV', 'NEO98FF.DLL', 'CAMERA.EXE', 'CAMERA.LNK'):
        shutil.copy2(ROOT / 'build/host-devices' / name, resources / name)
    for name in (('MEDIACTR.EXE', 'MEDIACTR.LNK', 'service.neo98')
                 if with_host_media() else ()):
        (shutil.copytree if name == 'service.neo98' else shutil.copy2)(ROOT / 'build/host-media' / name, resources / name)
    with (contents / 'Info.plist').open('wb') as output:
        plistlib.dump(info_plist(release_version(), commit), output)
    (contents / 'PkgInfo').write_bytes(b'APPL????')
    for library in sorted(frameworks.iterdir()):
        subprocess.run(['codesign', '--force', '--sign', identity, *signing_options, str(library)], check=True, capture_output=True)
    # Every Mach-O file of the toolkit needs the app's signature for notarization.
    for path in sorted((resources / 'toolkit').rglob('*')) if (resources / 'toolkit').is_dir() else ():
        if path.is_file() and not path.is_symlink() and path.read_bytes()[:4] in (
                b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'):
            subprocess.run(['codesign', '--force', '--sign', identity, *signing_options, str(path)],
                           check=True, capture_output=True)
    # The program carries the entitlement of Hypervisor.framework: ARM64
    # programs run in a machine of that framework (docs/ARM64-APPS.md).
    subprocess.run(['codesign', '--force', '--sign', identity, *signing_options, '--entitlements',
                    str(ROOT / 'tools/arm64_hypervisor.entitlements'), str(staging)], check=True, capture_output=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(staging)], check=True, capture_output=True)
    shutil.rmtree(target, ignore_errors=True)
    staging.rename(target)
    leftover = [name for name in dependencies(target / 'Contents/MacOS/Neo98')
                if not name.startswith(SYSTEM_PREFIXES) and not name.startswith('@rpath/')]
    for library in (target / 'Contents/Frameworks').iterdir():
        leftover += [f'{library.name}: {name}' for name in dependencies(library)
                     if not name.startswith(SYSTEM_PREFIXES) and not name.startswith('@rpath/')]
    if leftover:
        raise RuntimeError('Libraries outside the bundle: ' + ', '.join(leftover))
    size = sum(f.stat().st_size for f in target.rglob('*') if f.is_file()) / (1024 * 1024)
    print(f'{target}: {size:.0f} MB, revision {commit}, libraries: '
          + ', '.join(sorted(p.name for p in (target / "Contents/Frameworks").iterdir())))
    return target


def build_dmg(build=True, app=None, target=DMG):
    app = Path(app) if app else build_app(build)
    target = Path(target)
    stage = target.with_name(target.name + '.stage')
    shutil.rmtree(stage, ignore_errors=True)
    stage.mkdir(parents=True)
    try:
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(app), str(stage / 'Neo98.app')], check=True)
        os.symlink('/Applications', stage / 'Applications')
        target.unlink(missing_ok=True)
        subprocess.run(['hdiutil', 'create', '-quiet', '-volname', 'Neo98', '-srcfolder', str(stage),
                        '-fs', 'HFS+', '-format', 'UDZO', '-ov', str(target)], check=True)
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    print(f'{target}: {target.stat().st_size / (1024 * 1024):.0f} MB')
    return target


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('app', 'dmg', 'icon'))
    parser.add_argument('--no-build', action='store_true', help='use the existing build/native-app/neo98-host')
    parser.add_argument('--output', type=Path, help='another place for the bundle than build/Neo98.app')
    args = parser.parse_args(argv)
    if args.command == 'icon':
        print(build_icon(BUILD / 'Neo98.icns'))
    elif args.command == 'app':
        build_app(not args.no_build, args.output or APP)
    else:
        build_dmg(not args.no_build)
    return 0


if __name__ == '__main__':
    sys.exit(main())
