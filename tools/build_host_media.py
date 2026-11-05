#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build Media Center 98 (MEDIACTR.EXE) and its Start menu shortcut from
tools/host_media/mediacenter.c, and put NEO98's Kodi add-on service.neo98
beside them. The host seeds the program into C: (tools/host_media/seed.h)
and the add-on into Kodi (tools/host_media/service.h). """
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path
sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parents[1])]
import neo98  # noqa: E402
from build_host_devices import import_libraries, shortcut, write_icon  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
# What the build makes and the host seeds into C:.
FILES = ('MEDIACTR.EXE', 'MEDIACTR.LNK')
SOURCES = (ROOT / 'tools/host_media/mediacenter.c', ROOT / 'tools/host_devices/guest_win32.h', ROOT / 'src/host_media_abi.h')
# The add-on for Kodi, as files of its folder.
ADDON = ROOT / 'tools/host_media/kodi_addon/service.neo98'
ADDON_FILES = ('addon.xml', 'service.py')
FOLDER = 'C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Entertainment'


def television(size):
    """The Media Center icon: a television with a play triangle, rows of VGA indices, None transparent."""
    s = size / 32
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            px, py = (x + 0.5) / s, (y + 0.5) / s
            colour = None
            if 2 <= px <= 30 and 4 <= py <= 25:
                colour = 0 if px < 3 or px > 29 or py < 5 or py > 24 else 8 if px > 27 or py > 22 else 7
                if 5 <= px <= 25 and 7 <= py <= 21:
                    colour = 1 if px < 6 or py < 8 else 4
                    # The triangle of Play.
                    if 11 <= px <= 20 and abs(py - 14) <= (20 - px) * 0.6:
                        colour = 15
                if 26.5 <= px <= 28 and py in range(8, 20, 3):
                    colour = 0
            if 12 <= px <= 20 and 25 <= py <= 28:
                colour = 8 if py < 27 else 0
            row.append(colour)
        rows.append(row)
    return rows


RC = """1 ICON "{icon}"
1 VERSIONINFO
FILEVERSION 1,0,0,0
PRODUCTVERSION 1,0,0,0
FILEOS 0x4
FILETYPE 0x1
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904E4"
    BEGIN
      VALUE "CompanyName", "NEO98"
      VALUE "FileDescription", "{description}"
      VALUE "FileVersion", "1.0"
      VALUE "InternalName", "{name}"
      VALUE "OriginalFilename", "{name}.EXE"
      VALUE "ProductName", "NEO98"
      VALUE "ProductVersion", "1.0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1252
  END
END
"""


def program(out, env, libraries, name, description, picture, defines, source=SOURCES[0]):
    icon = out / (name.lower() + '.ico')
    write_icon(icon, [picture(32), picture(16)])
    rc = out / (name.lower() + '.rc')
    rc.write_text(RC.format(icon=icon.name, description=description, name=name))
    subprocess.run(['llvm-rc', '/fo', rc.with_suffix('.res').name, rc.name], env=env, check=True, cwd=out)
    obj, exe = out / (name.lower() + '.obj'), out / (name + '.EXE')
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                    '-Wall', '-Wextra', '-Wno-unused-parameter', *defines, '-c', str(source), '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows,4.0', '/osversion:4.0', '/nodefaultlib', '/machine:x86',
                    '/timestamp:0', '/out:' + str(exe), str(obj), str(rc.with_suffix('.res')), *libraries], env=env, check=True)
    return exe


def build():
    env = neo98.tool_environment()
    compiler = subprocess.check_output(['clang', '--version'], env=env)
    identity = hashlib.sha256(b''.join(p.read_bytes() for p in (*SOURCES, *(ADDON / f for f in ADDON_FILES))) + compiler + Path(__file__).read_bytes() +
                              (ROOT / 'tools/build_host_devices.py').read_bytes()).hexdigest()
    out = ROOT / 'build/host-media'
    out.mkdir(parents=True, exist_ok=True)
    stamp = out / 'stamp'
    if all((out / name).exists() for name in FILES) and stamp.exists() and stamp.read_text() == identity:
        return out
    for name in ('NETFLIX.EXE', 'NETFLIX.LNK', 'KODI.EXE', 'KODI.LNK'):
        (out / name).unlink(missing_ok=True)
    libraries = import_libraries(out / 'imports', env, [SOURCES[0], SOURCES[1]])
    program(out, env, libraries, 'MEDIACTR', 'Media Center 98', television, [])
    (out / 'MEDIACTR.LNK').write_bytes(shortcut('C:\\WINDOWS\\MEDIACTR.EXE', 'C:\\WINDOWS', "Netflix, YouTube and the videos of Kodi on this computer."))
    shutil.rmtree(out / ADDON.name, ignore_errors=True)
    (out / ADDON.name).mkdir()
    for name in ADDON_FILES:
        shutil.copy2(ADDON / name, out / ADDON.name / name)
    stamp.write_text(identity)
    return out


if __name__ == '__main__':
    print(build())
