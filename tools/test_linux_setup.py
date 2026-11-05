#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check imports, ZIP containment and packaged first-launch persistence."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
from native_app import ROOT
from linux_setup import import_machine, validate
import win98_c


def main():
    out = ROOT / 'build/native-linux/setup-test'; out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as temporary:
        work = Path(temporary)
        original = work / 'original'; original.mkdir()
        for name in ('WINDOWS/EXPLORER.EXE', 'WINDOWS/SYSTEM.DAT', 'WINDOWS/USER.DAT', 'WINDOWS/FONTS/VGAFIX.FON'):
            path = original / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'test')
        (original / win98_c.MARKER).write_text('{}')
        win98_c.journal(original).write_bytes(b'journal')
        import_machine(original, work / 'copy')
        assert win98_c.journal(work / 'copy').read_bytes() == b'journal'
        archive = work / 'win98.zip'
        with zipfile.ZipFile(archive, 'w') as stream:
            for file in original.rglob('*'):
                if file.is_file(): stream.write(file, 'machine/C/' + str(file.relative_to(original)))
            stream.write(win98_c.journal(original), 'machine/C.registry')
        import_machine(archive, work / 'zip-copy')
        assert win98_c.journal(work / 'zip-copy').read_bytes() == b'journal'
        validate(work / 'zip-copy')
        for bad in ('../escape', '/escape', 'C:\\escape'):
            archive = work / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as stream: stream.writestr(bad, 'outside')
            try: import_machine(archive, work / 'bad-target')
            except RuntimeError: pass
            else: raise AssertionError('Unsafe archive accepted')
            assert not (work / 'bad-target').exists()
        for protected in (original, work / 'zip-copy'):
            try: import_machine(original, protected)
            except RuntimeError: pass
            else: raise AssertionError('Existing destination overwritten')
    binary = ROOT / 'build/Neo98.AppDir/AppRun'
    root = win98_c.use_pristine()
    config = out / ('settings-' + str(os.getpid())); config.mkdir()
    env = dict(os.environ, NEO98_SETTINGS_DIR=str(config), NEO98_LINUX_SETUP_SOURCE=str(root),
        SDL_AUDIODRIVER='dummy', NEO98_CLIPBOARD='private', NEO98_HOST_INPUT='2:menu=quit')
    for key in ('NEO98_WIN98_ROOT', 'NEO98_SETTINGS_FILE', 'NEO98_LOG_TO_FILE'): env.pop(key, None)
    with (out / 'existing.log').open('w') as stream:
        result = subprocess.run([str(binary), '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--ram-drive', '--no-registry-file', '--time-limit', '15'],
            cwd='/tmp', env=env, stdout=stream, stderr=stream, timeout=30)
    assert result.returncode == 0, (out / 'existing.log').read_text()[-3000:]
    assert (config / 'machine-path').read_text().strip() == str(root)
    env.pop('NEO98_LINUX_SETUP_SOURCE')
    with (out / 'restored.log').open('w') as stream:
        result = subprocess.run([str(binary), '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--ram-drive', '--no-registry-file', '--time-limit', '15'],
            cwd='/tmp', env=env, stdout=stream, stderr=stream, timeout=30)
    assert result.returncode == 0, (out / 'restored.log').read_text()[-3000:]
    assert not win98_c.pristine_problems(root)
    (out / 'results.json').write_text(json.dumps({'folder_import': True, 'zip_import': True, 'journal_preserved': True,
        'unsafe_zip_refused': True, 'existing_destination_preserved': True, 'packaged_first_launch': True,
        'machine_choice_persisted': True, 'source_unchanged': True}, indent=2) + '\n')
    print('LINUX SETUP PASS: imports, journal, ZIP containment, first-launch dialog, persisted machine')

if __name__ == '__main__': main()
