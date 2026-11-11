#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install the launcher of the moved AppDir, or of the AppImage, in the user's application menu."""
import os
import subprocess
import shutil
from pathlib import Path
import sys

MIME_TYPES = ('application/x-ms-dos-executable', 'application/x-msdownload', 'application/x-dosexec',
              'application/vnd.microsoft.portable-executable', 'application/x-ms-ne-executable',
              'application/x-ms-shortcut', 'application/x-neo98-pif', 'application/x-bat',
              'text/x-msdos-batch', 'application/x-cd-image', 'application/x-cue')


def quote(value):
    # Desktop Entry Exec has its own escaping before command-line quoting.
    value = str(value).replace('%', '%%')
    for old, new in (('\\', '\\\\'), ('"', '\\"'), ('`', '\\`'), ('$', '\\$')):
        value = value.replace(old, new)
    value = value.replace('\\', '\\\\')
    return '"' + value + '"'


def main():
    bundle = Path(sys.argv[1]).resolve()
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    target = data / 'applications/neo98.desktop'
    target.parent.mkdir(parents=True, exist_ok=True)
    program, icon = bundle / 'AppRun', bundle / 'neo98.svg'
    # Started from an AppImage, the bundle is a mount that ends with this
    # process: the launcher starts the AppImage file, and the icon gets a
    # copy that stays.
    image = os.environ.get('APPIMAGE')
    if image and os.environ.get('APPDIR') and Path(os.environ['APPDIR']).resolve() == bundle and Path(image).is_file():
        program = Path(image).resolve()
        icon = data / 'icons/hicolor/scalable/apps/neo98.svg'
        icon.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(bundle / 'neo98.svg', icon)
    text = '[Desktop Entry]\nType=Application\nName=NEO98\nComment=Windows-98-Programme auf Linux\n'
    text += 'Exec=/usr/bin/env -- ' + quote(program) + ' --open %f\nIcon=' + str(icon).replace('\\', '\\\\') + '\n'
    text += 'Terminal=false\nCategories=Game;Emulator;\n'
    text += 'MimeType=' + ';'.join(MIME_TYPES) + ';\n'
    text += 'Actions=Setup;\n\n[Desktop Action Setup]\nName=Set up Windows 98\nExec=/usr/bin/env -- ' + quote(program) + ' --setup-linux\n'
    temporary = target.with_name(target.name + '.' + str(os.getpid()))
    temporary.write_text(text); temporary.replace(target)
    mime = target.parent.parent / 'mime'
    (mime / 'packages').mkdir(parents=True, exist_ok=True)
    (mime / 'packages/neo98.xml').write_text('''<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-neo98-pif"><comment>Windows program information</comment><glob pattern="*.pif"/></mime-type>
</mime-info>
''')
    for command in (['update-mime-database', str(mime)], ['update-desktop-database', str(target.parent)]):
        if shutil.which(command[0]): subprocess.run(command, check=True)
    print(f'Launcher installed: {target}')
    return 0

if __name__ == '__main__': raise SystemExit(main())
