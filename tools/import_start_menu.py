#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import the installed Win98 Start Menu folder tree onto the RAM volume.

Windows 98 builds its Start menu out of a real directory: the tray asks SHELL32
for the Start Menu shell folder, SHELL32 resolves it through
`HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders`,
and every submenu is a subdirectory, every item a.LNK or.PIF file. Without
that directory the menu is empty no matter how much of SHELL32 runs.

This importer reads `WINDOWS\\Start Menu` out of the Win98 folder of setup
(tools/win98_c.py), keeps every folder and shortcut byte for byte, and emits

  * `build/start-menu/originals/**` the extracted files, not committed,
  * `build/start-menu/seed-manifest.txt` guest path per line for the host probe,
  * `build/generated/start_menu_tree.h` the same tree embedded for the kernel,
  * `build/generated/start-menu-provenance.json` the hashes `neo98.py build` checks,
  * `build/start-menu/start-menu-import.json` the full per-entry report.

Nothing is invented: the names, the nesting and the shortcut payloads are the
bytes of the installation. No original file is committed to the repository.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import sys

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'neo98.py').is_file())
sys.path.insert(0, str(ROOT / 'tools'))
import win98_c    # noqa: E402
# Files::Store keeps 256 nodes. C:\WINDOWS and C:\WINDOWS\SYSTEM plus the ten
# original Explorer files already live there, so the tree gets a budget rather
# than silently filling the volume.
NODE_BUDGET = 160


def listing(folder, root):
    """Every folder and file under `root`, parents first."""
    base = win98_c.path(root, folder)
    if base is None or not base.is_dir():
        return []
    entries = []
    for directory, subdirectories, names in os.walk(base):
        subdirectories.sort()
        here = Path(directory).relative_to(base)
        for name in subdirectories:
            entries.append(((here / name).as_posix(), True))
        for name in sorted(names):
            if not name.startswith('.neo98-'):
                entries.append(((here / name).as_posix(), False))
    entries.sort(key=lambda entry: (entry[0].count('/'), entry[0]))
    return entries


def extract(folder, root, entries, out):
    """Copy each file out of the folder; it is only read."""
    records = []
    for relative, directory in entries:
        target = out / relative
        if directory:
            target.mkdir(parents=True, exist_ok=True)
            records.append({'path': relative, 'directory': True})
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            win98_c.copy(f'{root}/{relative}', target, folder)
        data = target.read_bytes()
        records.append({'path': relative, 'directory': False, 'bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest()})
    return records


def guest(guest_root, relative):
    return guest_root + '\\' + relative.replace('/', '\\')


def manifest(records, guest_root, out, originals):
    """One guest path per line; '-' marks a directory, otherwise the host file.

    The host probe seeds the volume from this file, so the runner needs no
    directory walker of its own and the set it seeds is the set this importer
    recorded.
    """
    lines = [f'{guest_root}\t-']
    for record in records:
        source = '-' if record['directory'] else str((originals / record['path']).resolve())
        lines.append(f"{guest(guest_root, record['path'])}\t{source}")
    lines += extra_seeds(out, originals)
    text = '\n'.join(lines) + '\n'
    (out / 'seed-manifest.txt').write_text(text)
    return len(lines)


# Files outside the Start Menu that the shell addresses by their registry
# path: the hive names Internet Explorer's icon as
# "C:\PROGRA~1\INTERN~1\iexplore.exe", so the file sits under that literal
# short-name path (the RAM volume has no 8.3 aliases yet).
EXTRA_SEEDS = [
    ('C:\\PROGRA~1', None),
    ('C:\\PROGRA~1\\INTERN~1', None),
    ('C:\\PROGRA~1\\INTERN~1\\iexplore.exe', 'PROGRA~1/INTERN~1/IEXPLORE.EXE'),
    # The desktop's "Setup MSN Internet Access" shortcut carries this icon.
    ('C:\\Program Files', None),
    ('C:\\Program Files\\online services', None),
    ('C:\\Program Files\\online services\\msn50', None),
    ('C:\\Program Files\\online services\\msn50\\msnlogo.ico', 'PROGRA~1/ONLINE~1/MSN50/MSNLOGO.ICO'),
    # Web Folders (My Computer) names its icon source with mixed short and long components.
    # The taskbar's Quick Launch band lists this folder.
    ('C:\\WINDOWS\\Application Data', None),
    ('C:\\WINDOWS\\Application Data\\Microsoft', None),
    ('C:\\WINDOWS\\Application Data\\Microsoft\\Internet Explorer', None),
    ('C:\\WINDOWS\\Application Data\\Microsoft\\Internet Explorer\\Quick Launch', None),
    ('C:\\WINDOWS\\Application Data\\Microsoft\\Internet Explorer\\Quick Launch\\Show Desktop.scf', 'WINDOWS/APPLIC~1/MICROS~1/INTERN~1/QUICKL~1/SHOWDE~1.SCF'),
    ('C:\\WINDOWS\\Application Data\\Microsoft\\Internet Explorer\\Quick Launch\\Launch Internet Explorer Browser.lnk', 'WINDOWS/APPLIC~1/MICROS~1/INTERN~1/QUICKL~1/LAUNCH~2.LNK'),
    ('C:\\WINDOWS\\Application Data\\Microsoft\\Internet Explorer\\Quick Launch\\Launch Outlook Express.lnk', 'WINDOWS/APPLIC~1/MICROS~1/INTERN~1/QUICKL~1/LAUNCH~1.LNK'),
    ('C:\\WINDOWS\\Application Data\\Microsoft\\Internet Explorer\\Quick Launch\\Dev-C++.lnk', 'WINDOWS/APPLIC~1/MICROS~1/INTERN~1/QUICKL~1/DEV-C_~1.LNK'),
    ('C:\\PROGRA~1\\COMMON~1', None),
    ('C:\\PROGRA~1\\COMMON~1\\MICROS~1', None),
    ('C:\\PROGRA~1\\COMMON~1\\MICROS~1\\Web Folders', None),
    ('C:\\PROGRA~1\\COMMON~1\\MICROS~1\\Web Folders\\MSONSEXT.DLL', 'PROGRA~1/COMMON~1/MICROS~1/WEBFOL~1/MSONSEXT.DLL'),
]


def extra_seeds(out, originals):
    lines = []
    folder = getattr(extra_seeds, 'folder', None)
    for guest_path, image_path in EXTRA_SEEDS:
        if image_path is None:
            lines.append(f'{guest_path}\t-')
            continue
        target = originals / image_path.replace('/', '_')
        if not target.exists() and folder is not None:
            source = win98_c.path(image_path, folder)
            if source is not None and source.is_file():
                shutil.copyfile(source, target)
        if target.exists():
            lines.append(f'{guest_path}\t{target.resolve()}')
    return lines


def header(records, guest_root, originals, generated):
    """Embed the same tree for the kernel, which has no host filesystem."""
    blob, lines = bytearray(), []
    table = [(guest_root, True, 0, 0)]
    for record in records:
        if record['directory']:
            table.append((guest(guest_root, record['path']), True, 0, 0))
            continue
        data = (originals / record['path']).read_bytes()
        table.append((guest(guest_root, record['path']), False, len(blob), len(data)))
        blob += data
    lines.append('// Original local Windows 98 Start Menu folder. Do not redistribute.')
    lines.append('#pragma once')
    lines.append('static const unsigned char StartMenuData[] = {')
    lines += [','.join(f'0x{b:02x}' for b in blob[i:i + 24]) + ',' for i in range(0, len(blob), 24)]
    lines.append('};')
    lines.append('static const struct { const char *path; unsigned directory, offset, bytes; }'
                 ' StartMenuEntries[] = {')
    for path, directory, offset, size in table:
        lines.append('    {"%s", %d, %d, %d},' % (path.replace('\\', '\\\\'), int(directory), offset, size))
    lines.append('};')
    text = '\n'.join(lines) + '\n'
    target = generated / 'start_menu_tree.h'
    if not target.exists() or target.read_text() != text:
        target.write_text(text)
    return target, len(table), len(blob)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--folder', type=Path, default=win98_c.FOLDER,
                        help='the Win98 folder of setup (default build/win98-c)')
    parser.add_argument('--root', default='WINDOWS/Start Menu', help='folder inside the Win98 folder')
    parser.add_argument('--guest-root', default='C:\\WINDOWS\\Start Menu',
                        help='where the RAM volume keeps it; must match Shell Folders')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/start-menu')
    parser.add_argument('--node-budget', type=int, default=NODE_BUDGET)
    args = parser.parse_args()

    folder = win98_c.require(args.folder.resolve())
    out = args.out.resolve()
    originals = out / 'originals'
    previous = out / 'start-menu-import.json'
    source = json.loads(previous.read_text()) if previous.is_file() else {}
    if (source.get('folder'), source.get('image_root')) != (str(folder), args.root):
        shutil.rmtree(originals, ignore_errors=True)
    originals.mkdir(parents=True, exist_ok=True)
    generated = ROOT / 'build/generated'
    generated.mkdir(parents=True, exist_ok=True)

    entries = listing(folder, args.root)
    if not entries:
        raise SystemExit(f'{args.root} is empty in {folder}')
    if len(entries) + 1 > args.node_budget:
        raise SystemExit(f'{len(entries) + 1} nodes exceed the budget of {args.node_budget}')
    records = extract(folder, args.root, entries, originals)
    extra_seeds.folder = folder
    seeded = manifest(records, args.guest_root, out, originals)
    target, nodes, payload = header(records, args.guest_root, originals, generated)

    files = [r for r in records if not r['directory']]
    report = {
        'folder': str(folder),
        'access': 'read-only copy out of the Win98 folder; no original file is committed',
        'image_root': args.root, 'guest_root': args.guest_root,
        'directories': len(records) - len(files), 'files': len(files),
        'nodes': nodes, 'payload_bytes': payload, 'manifest_lines': seeded,
        'node_budget': args.node_budget,
        'registry': 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\'
                    'Shell Folders\\Start Menu of the imported hive already names this path; '
                    'the imported Start Menu remains the source of these entries',
        'entries': records,
    }
    (out / 'start-menu-import.json').write_text(json.dumps(report, indent=2) + '\n')
    (generated / 'start-menu-provenance.json').write_text(json.dumps({
        'source': {'folder': str(folder), 'root': args.root,
                   'access': 'read-only copy out of the Win98 folder'},
        'guest_root': args.guest_root, 'nodes': nodes, 'payload_bytes': payload,
        'generated_sha256': {target.name: hashlib.sha256(target.read_bytes()).hexdigest()},
    }, indent=2) + '\n')
    print(f"Imported {len(files)} shortcuts in {len(records) - len(files)} folders "
          f"({payload} bytes) as {args.guest_root}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
