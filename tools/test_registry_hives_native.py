#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The registry read from the hives at run time against the Python import.

The host starts from C:\\WINDOWS\\SYSTEM.DAT and USER.DAT of its drive
(src/registry_hives.h) instead of an image compiled into it. This test takes
the hives of the pristine Win98 folder and checks that the C++ import is the
Python import:

1. tools/import_registry.py generates its image from those hives into the
   test's own folder; a store decoded from that image and a store decoded
   from the hives by the C++ import hold the same keys and values in the same
   order, with the keys of the LAN card and the display adapter
   (tools/network_setup.py, tools/display_setup.py).
2. Over the whole of both hives, not only the selection: every key and value
   the C++ decoder reads is the one the Python decoder reads.
3. A file that is no hive is refused (ERROR_BADDB); without the INF files the
   hives load without the LAN card.
4. HKCR resolves the original Explorer desktop COM class to BROWSEUI.DLL
   without a replay journal. An empty alias made SHCreateDesktop fail before
   SHLWAPI tried to copy a resource string to address 0x2e8.

    build/analysis-venv/bin/python tools/test_registry_hives_native.py

Results in build/registry-hives/results.json.
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import import_registry  # noqa: E402
import win98_c  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def python_dump(files, target):
    """Every key and value of both hives as tools/import_registry.py decodes them."""
    counts = {}
    with target.open('w') as out:
        for label in ('SYSTEM.DAT', 'USER.DAT'):
            tree = import_registry.parse(files[label])['tree']
            values = sum(len(entry[3]) for entry in tree.values())
            counts[label] = (len(tree), values)
            out.write(f'H {label} {len(tree)} {values}\n')
            for full, _name, _parent, entries in tree.values():
                out.write(f"K {full.encode('cp1252').hex()}\n")
                for name, kind, payload in entries:
                    out.write(f"V {name.encode('cp1252').hex()} {kind} {payload.hex()}\n")
    return counts


def main():
    out = neo98.output_directory('registry-hives')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    pristine = win98_c.use_pristine()
    environment = neo98.tool_environment()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    # The Python import of exactly these hives.
    subprocess.run([sys.executable, str(ROOT / 'tools/import_registry.py'), '--folder', str(pristine),
                    '--whole-hives', '--output', str(out / 'import'), '--generated', str(out / 'generated')],
                   check=True, env=environment, stdout=subprocess.DEVNULL)
    header = out / 'generated/win98_registry_data.h'
    files = {name: out / 'import' / name for name in ('SYSTEM.DAT', 'USER.DAT')}
    inf = {name: win98_c.path(f'C:\\WINDOWS\\INF\\{name}', pristine) for name in ('NETAMD.INF', 'NETCLI.INF')}
    check('hives and INF files are in the folder', all(p.is_file() for p in files.values())
          and all(p and p.is_file() for p in inf.values()))

    binary = out / 'test-registry-hives'
    build = subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                            f'-DNEO98_REGISTRY_IMAGE_HEADER="{header}"',
                            str(ROOT / 'tools/test_registry_hives_native.cpp'), str(ROOT / 'src/registry.cpp'),
                            str(ROOT / 'src/registry_persist.cpp'), str(ROOT / 'src/config_manager.cpp'),
                            '-o', str(binary)], cwd=ROOT, env=environment, capture_output=True, text=True)
    (out / 'build.log').write_text(build.stdout + build.stderr)
    check('runner builds', build.returncode == 0)
    text = ''
    if build.returncode == 0:
        run = subprocess.run([str(binary), str(files['SYSTEM.DAT']), str(files['USER.DAT']),
                              str(inf['NETAMD.INF']), str(inf['NETCLI.INF']), str(out / 'cpp.dump')],
                             cwd=ROOT, env=environment, capture_output=True, text=True, errors='replace')
        text = run.stdout + run.stderr
        (out / 'run.log').write_text(text)
        check('runner passes', run.returncode == 0 and 'Registry hives PASS' in text)
        check('original FilterGraph class is reachable through predefined aliases',
              'registry aliases image: PASS' in text and 'registry aliases hives: PASS' in text)
        check('Explorer desktop COM class resolves to BROWSEUI through HKCR without journal repairs',
              'Explorer desktop class image: PASS' in text and 'Explorer desktop class hives: PASS' in text)
    import re
    compared = re.search(r'compared (\d+) keys, (\d+) values, (\d+) differences', text)
    check('store from the hives equals the store from the Python image',
          compared and int(compared.group(1)) > 15000 and int(compared.group(2)) > 20000 and compared.group(3) == '0')
    check('LAN card and display adapter added', 'network=1' in text)
    # The devices of the installation machine are the port's own (
    # tools/hardware_setup.py): the selection, which the C++ import equals,
    # names the display adapter and the CD-ROM drive of src/neo98_hardware.h
    # and nothing of QEMU's PC under Enum and Services\Class.
    import hardware_setup
    hardware = hardware_setup.names()
    hives = {name: import_registry.parse(files[name]) for name in ('SYSTEM.DAT', 'USER.DAT')}
    selected, _ = import_registry.select(hives, whole_hives=True)
    paths = []
    for key in selected:
        paths.append(key['anchor'][1] if 'anchor' in key else paths[key['parent']] + '\\' + key['name'])
    devices = {path.lower(): dict((n.lower(), v.rstrip(b'\0').decode('cp1252')) for n, k, v in key['values'] if k == 1)
               for path, key in zip(paths, selected)
               if path.lower().startswith(('enum\\', 'system\\currentcontrolset\\services\\class\\'))}
    display = devices.get(hardware['DisplayNode'].lower(), {})
    drive = [v for path, v in devices.items() if path.startswith(hardware['CdDevice'].lower() + '\\')]
    check('the display adapter is the port\'s', display.get('devicedesc') == hardware['DisplayName']
          and display.get('mfg') == hardware['Maker'] and display.get('driver', '').lower() == 'display\\0000'
          and devices['system\\currentcontrolset\\services\\class\\display\\0000'].get('driverdesc') == hardware['DisplayName'])
    check('the CD-ROM drive is the port\'s', len(drive) == 1 and drive[0].get('devicedesc') == hardware['CdName']
          and drive[0].get('driver', '').lower() == 'cdrom\\0000' and drive[0].get('currentdriveletterassignment') == 'D')
    sound = devices.get(hardware['SoundNode'].lower(), {})
    monitor = devices.get(hardware['MonitorNode'].lower(), {})
    card = devices.get('enum\\root\\net\\0001', {})
    check('the sound card, the monitor and the network adapter are the port\'s',
          sound.get('devicedesc') == hardware['SoundName'] and sound.get('class') == 'MEDIA'
          and monitor.get('devicedesc') == hardware['MonitorName'] and monitor.get('driver') == hardware['MonitorDriver']
          and card.get('devicedesc') == hardware['NetName'] and card.get('compatibleids') == hardware['NetCompatibleId']
          and devices['system\\currentcontrolset\\services\\class\\net\\0001'].get('driverdesc') == hardware['NetName'])
    check('no floppy disk controller and no floppy disk drive',
          not [path for path in devices if path.startswith(('enum\\flop\\', 'enum\\root\\*pnp0700'))
               or path.endswith('class\\fdc\\0000')])
    stale = re.compile('qemu|cirrus|red hat|bochs|virtio|pnp0905|pcnet family', re.I)
    left = [path for path, values in devices.items() if stale.search(path) or any(stale.search(v) for v in values.values())]
    check('nothing of the installation machine is left under Enum and Services\\Class', not left)
    if left:
        print('    left:', left[:6])
    check('a broken hive is ERROR_BADDB', 'broken hive: error=1009' in text)
    check('without INF files no LAN card', re.search(r'without INF: error=0 network=0', text))

    counts = python_dump(files, out / 'python.dump')
    same = (out / 'cpp.dump').is_file() and \
        hashlib.sha256((out / 'cpp.dump').read_bytes()).digest() == hashlib.sha256((out / 'python.dump').read_bytes()).digest()
    check('C++ decoder equals the Python decoder over both whole hives', same)
    check('the whole hives are large', counts['SYSTEM.DAT'][0] > 15000 and counts['USER.DAT'][0] > 300)

    for name in (*([] if "--keep" in sys.argv else ["cpp.dump", "python.dump"]), "import", 'generated', 'test-registry-hives', 'test-registry-hives.dSYM'):
        target = out / name
        if target.is_dir():
            shutil.rmtree(target, ignore_errors=True)
        else:
            target.unlink(missing_ok=True)
    report = {'checks': checks, 'hives': {k: {'keys': v[0], 'values': v[1]} for k, v in counts.items()},
              'compared': compared.group(0) if compared else None}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    failed = [name for name, okay in checks.items() if not okay]
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print(f'{len(failed)} of {len(checks)} checks failed; logs under {out}')
        return 1
    print(f"registry hives: {len(checks)} checks passed ({report['compared']}; "
          f"whole hives {counts['SYSTEM.DAT'][0]} + {counts['USER.DAT'][0]} keys)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
