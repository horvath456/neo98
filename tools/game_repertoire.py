#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pinned, local Win9x game corpus. Run host operations through a shared serial test lock.

No downloaded code is executed on macOS. Guest installers run only in a private
APFS clone. A screenshot is evidence, never an automatic gameplay assertion.
NEO98_WIN98_ROOT selects a reference Windows tree and its adjacent journal;
without a reference journal, the host loads the original hives alone.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'tools/game_repertoire.json'
sys.path.insert(0, str(ROOT / 'tools'))
from external_paths import external_path
sys.path.insert(0, str(ROOT))
from neo98 import source_revision
HOME = None
GIB = 1024 ** 3


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def guard(home, reserve=0):
    if shutil.disk_usage(home).free < 8 * GIB + reserve:
        raise RuntimeError('STOP: operation would breach the 8 GiB free-space reserve')
    # Source checkout, archives and extracted payload all count toward the budget.
    # Temporary APFS C: snapshots include existing Windows, not corpus assets.
    # Their actual allocation is still covered by the free-space check.
    roots = [p for p in home.iterdir() if p.name != 'runs']
    roots += [p for phase in (home/'runs').glob('*/*/*') if phase.is_dir()
              for p in phase.iterdir() if p.name not in ('c', 'game')]
    size = sum(int(line.split()[0]) * 1024 for line in
               subprocess.check_output(['du', '-sk', *map(str, roots)], text=True).splitlines()) if roots else 0
    if size + reserve > 6 * GIB:
        raise RuntimeError('STOP: corpus would exceed 6 GiB')


def inside(root, relative):
    if not relative or '\\' in relative or PureWindowsPath(relative).drive:
        raise ValueError(f'Unsafe relative path: {relative!r}')
    path = root / relative
    if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f'Path escapes corpus: {relative!r}')
    return path


def archive_path(home, game):
    return inside(home, f"downloads/{game['id']}/{game['filename']}")


def verify(home, game):
    path = archive_path(home, game)
    if not re.fullmatch(r'[0-9a-f]{64}', game.get('sha256') or ''):
        raise ValueError(f"Missing SHA-256 pin: {game['id']}")
    if digest(path) != game['sha256']:
        raise ValueError(f"SHA-256 mismatch: {game['id']}")
    return path


def fetch(home, game):
    path = archive_path(home, game)
    if path.exists():
        return verify(home, game)
    guard(home, game['size_bytes'])
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(path.name + '.part')
    try:
        with urllib.request.urlopen(game['source_url'], timeout=90) as response, partial.open('wb') as output:
            size = 0
            while data := response.read(1024 * 1024):
                size += len(data)
                if size > game['size_bytes']:
                    raise ValueError('Download exceeds pinned size')
                if shutil.disk_usage(home).free < 8 * GIB + len(data):
                    raise RuntimeError('STOP: 8 GiB reserve')
                output.write(data)
        if size != game['size_bytes'] or digest(partial) != game['sha256']:
            raise ValueError('Downloaded size or SHA-256 does not match manifest')
        partial.rename(path)
    finally:
        partial.unlink(missing_ok=True)
    return path


def extract(source, target, home, *, rooted_cab=False):
    """Reject traversal, links and oversized archives before extraction."""
    if target.exists():
        raise ValueError(f'Refusing to overwrite {target}')
    if zipfile.is_zipfile(source) and not rooted_cab:
        with zipfile.ZipFile(source) as archive:
            entries = archive.infolist()
            for entry in entries:
                inside(target, entry.filename.rstrip('/'))
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('Symlink in ZIP')
            guard(home, sum(e.file_size for e in entries))
            target.mkdir(parents=True)
            archive.extractall(target)
    else:
        listing = subprocess.run(['7zz', 'l', '-slt', str(source)], capture_output=True, text=True)
        if listing.returncode:
            raise ValueError('Not a supported archive; use guest installer')
        if rooted_cab and 'Type = Cab' not in listing.stdout.split('----------\n', 1)[0].splitlines():
            raise ValueError('Rooted member names are supported only for CAB packages')
        entries = listing.stdout.split('----------\n', 1)
        if len(entries) != 2:
            raise ValueError('No archive member list')
        total = 0
        for block in entries[1].split('\n\n'):
            fields = dict(line.split(' = ', 1) for line in block.splitlines() if ' = ' in line)
            if 'Path' not in fields:
                continue
            member = fields['Path'].replace('\\', '/')
            # 7-Zip lists these self-extracting Microsoft CABs with a leading
            # slash, but extracts relative names unless -spf is requested.
            if rooted_cab:
                member = member.lstrip('/')
            inside(target, member)
            if fields.get('Symbolic Link') or fields.get('Hard Link') or 'l' in fields.get('Attributes', '').split('_')[-1]:
                raise ValueError('Link in archive')
            total += int(fields.get('Size', 0))
        guard(home, total)
        target.mkdir(parents=True)
        command = (['unar', '-q', '-D', '-o', str(target), str(source)] if source.suffix.lower() == '.rar'
                   else ['7zz', 'x', '-y', '-bd', '-o' + str(target), str(source)])
        try:
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        except subprocess.CalledProcessError:
            shutil.rmtree(target)
            raise
    for path in target.rglob('*'):
        if path.is_symlink() or not path.resolve().is_relative_to(target.resolve()):
            raise ValueError('Unsafe extracted path')


def extract_inno(source, target, home):
    if target.exists():
        raise ValueError(f'Refusing to overwrite {target}')
    listing = subprocess.run(['innoextract', '--list', '--list-sizes', str(source)],
                             capture_output=True, text=True, check=True)
    names = re.findall(r'^ - "([^"]+)"', listing.stdout, re.M)
    if not names:
        raise ValueError('No Inno Setup file list')
    for name in names:
        inside(target, name)
    sizes = re.findall(r'^ - "[^"]+" \(([0-9.]+) (B|KiB|MiB|GiB)\)', listing.stdout, re.M)
    if len(sizes) != len(names):
        raise ValueError('Cannot bound Inno Setup extraction size')
    units = {'B': 1, 'KiB': 1024, 'MiB': 1024**2, 'GiB': GIB}
    # Listings round sizes; one extra display unit per file bounds that loss.
    guard(home, sum(int((float(size) + 1) * units[unit]) for size, unit in sizes))
    target.mkdir(parents=True)
    try:
        subprocess.run(['innoextract', '--silent', '--extract', '--output-dir',
                        str(target), str(source)], check=True)
    except subprocess.CalledProcessError:
        shutil.rmtree(target)
        raise


def prepare(home, game, work=None):
    source = verify(home, game)
    for asset in game.get('extra_assets', []):
        verify(home, asset)
    recipe = game['install']
    # The corpus cache may contain only the SFX executable from a guest
    # recipe. Keep explicitly unpacked CABs separate and writable locally.
    relative = ('prepared-cab/' if recipe['method'] == 'cab' else 'prepared/') + game['id']
    target = inside(home, relative)
    if target.exists():
        return target
    if work is not None:
        target = inside(work, relative)
        if target.exists():
            return target
    budget = home if work is None else work
    if recipe['method'] == 'cab':
        extract(source, target, budget, rooted_cab=True)
    elif recipe['method'] == 'archive':
        extract(source, target, budget)
        for step in recipe.get('nested', []):
            decoder = extract_inno if step.get('format') == 'inno' else extract
            decoder(inside(target, step['source']), inside(target, step['target']), budget)
    elif recipe['method'] == 'guest':
        guard(budget, source.stat().st_size)
        target.mkdir(parents=True)
        shutil.copyfile(source, target / game['filename'])
    else:
        raise ValueError('Unknown installation method')
    for asset in game.get('extra_assets', []):
        staging = inside(target, asset['staging'])
        extract(verify(home, asset), staging, budget)
        shutil.copytree(staging, inside(target, asset['merge_into']), dirs_exist_ok=True)
    return target


def check_reserve(work):
    if shutil.disk_usage(work).free < 6 * GIB:
        raise RuntimeError('STOP: fewer than 6 GiB free during result processing')


def log_fragments(path, work):
    """Bound even a malformed, newline-free trace; keep the disk watch alive."""
    with path.open(errors='replace') as stream:
        while True:
            check_reserve(work)
            fragment = stream.readline(64 * 1024)
            if not fragment:
                break
            yield fragment


def classify(code, log, shot, phase, timed_out):
    # Accept strings for existing callers, or a bounded iterator for trace files.
    fragments = io.StringIO(log) if isinstance(log, str) else log
    deadline, crash, launch_error = timed_out, code < 0, code != 0
    tail = ''
    crash_report = False
    for fragment in fragments:
        text = tail + fragment
        deadline |= bool(re.search(r'NEO98 HOST deadline [0-9.]+ s reached', text))
        crash |= bool(re.search(r'Unhandled (?:guest )?exception|guest fault|AddressSanitizer:', text, re.I))
        lower = text.lower()
        marker = lower.find('crashreport ')
        if (crash_report and 'unhandled-guest-exception' in lower or
                marker >= 0 and 'unhandled-guest-exception' in lower[marker:]):
            crash = True
        crash_report |= marker >= 0
        launch_error |= 'start failed:' in text
        tail = text[-128:]
        # Summarize a number of arbitrary length, retaining a partial suffix.
        prefix = 'NEO98 HOST deadline '
        at = text.rfind(prefix)
        if at >= 0:
            number = re.match(r'[0-9.]+', text[at + len(prefix):])
            if number:
                suffix = text[at + len(prefix) + number.end():]
                if ' s reached'.startswith(suffix):
                    tail = prefix + '0' + suffix
        if fragment.endswith('\n'):
            tail = ''
            crash_report = False
    if deadline:
        return 'hang-suspected', 'Host exceeded its deadline; no proof of a guest deadlock'
    if crash:
        return 'crash', 'Inspect host.log and crash report; exclude deliberate application errors'
    if launch_error:
        return 'launch-error', 'Host rejected startup; inspect the loader error'
    if phase == 'installer':
        return 'installation-pending', 'Installer launch does not prove installation or game startup'
    if not shot.exists():
        return 'no-image', 'No screenshot captured; inspect exit status and host.log'
    from PIL import Image
    with Image.open(shot) as image:
        extrema = image.convert('RGB').getextrema()
    if max(high for low, high in extrema) <= 8:
        return 'black-image', 'Whole captured desktop is black'
    return 'image-needs-review', 'Visible pixels alone do not establish a working game'


def bounded_steps(steps, seconds):
    """The host consumes events in order. Future wizard actions must not
    hide the screenshot/quit events of a shorter smoke run."""
    events = []
    for step in steps.split(';'):
        if not step:
            continue
        when = float(step.split(':', 1)[0])
        if 0 <= when <= seconds - 3:
            events.append((when, step))
    return ';'.join(step for when, step in sorted(events, key=lambda item: item[0]))


def stage_system_libraries(writable, drive, recipe):
    """Install explicitly listed bundled libraries in the private C: copy."""
    if drive.is_symlink() or drive.resolve() == (ROOT / 'build/win98-c').resolve():
        raise ValueError('Refusing real C: for bundled libraries')
    for relative in recipe.get('system_libraries', []):
        source = inside(writable, relative)
        target = inside(drive, 'WINDOWS/SYSTEM/' + source.name)
        if target.exists():
            raise ValueError('Refusing to overwrite system library: ' + str(target))
        shutil.copyfile(source, target)


def validate_payload(payload):
    # Cached corpus data is untrusted too. cp -c -R preserves links, which
    # could otherwise expose writable host files through the guest E: drive.
    if payload.is_symlink() or not payload.is_dir():
        raise ValueError('Payload must be a real directory')
    for path in payload.rglob('*'):
        if path.is_symlink() or not path.resolve().is_relative_to(payload.resolve()):
            raise ValueError('Unsafe cached payload: ' + str(path))


def wait_host(process, seconds, work):
    """Observe the disk floor while waiting; never kill an ARM64 host."""
    deadline = time.monotonic() + seconds + 75
    reason = None
    while process.poll() is None:
        if shutil.disk_usage(work).free < 6 * GIB:
            reason = 'STOP: fewer than 6 GiB free during host run'
            break
        if time.monotonic() >= deadline:
            reason = 'timeout'
            break
        try:
            return process.wait(timeout=1), False
        except subprocess.TimeoutExpired:
            pass
    if reason is not None:
        process.terminate()
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            raise RuntimeError(f'STOP: own host {process.pid} survived SIGTERM; no further runs')
        if reason != 'timeout':
            raise RuntimeError(reason)
    return process.returncode, reason == 'timeout'


def run(home, game, label, seconds, phase, work=None, session=None):
    work = ROOT/'build/game-repertoire' if work is None else work
    work.mkdir(parents=True, exist_ok=True)
    guard(work)
    active = subprocess.run(['pgrep', '-x', 'neo98-host'], capture_output=True, text=True)
    if active.returncode == 0:
        raise RuntimeError('STOP: another neo98-host is active: ' + active.stdout.strip())
    payload = prepare(home, game, work)
    installed_home = work if (work/'installed'/game['id']).is_dir() else home
    installed = inside(installed_home, 'installed/' + game['id'])
    if phase == 'game' and session is None and installed.is_dir():
        payload = installed
    validate_payload(payload)
    out = inside(work, f"runs/{label}/{game['id']}/{phase}")
    out.mkdir(parents=True, exist_ok=False)
    reuse = session is not None and 'drive' in session
    drive = session['drive'] if reuse else out / 'c'
    writable = session['writable'] if reuse else out / 'game'
    registry = session['registry'] if reuse else out / 'registry.bin'
    source = Path(os.environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c').resolve()
    source_registry = (source.with_name(source.name + '.registry')
                       if os.environ.get('NEO98_WIN98_ROOT') else ROOT/'build/win98-c.registry')
    process = None
    try:
        if not reuse:
            if session is not None:
                session.update(drive=drive, writable=writable, registry=registry)
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(source), str(drive)], check=True)
            if source_registry.exists() or not os.environ.get('NEO98_WIN98_ROOT'):
                shutil.copyfile(source_registry, registry)
            else:
                # A reference tree can load its own SYSTEM.DAT/USER.DAT without
                # an NEO98 change journal. Never borrow the user's journal.
                registry.touch()
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(payload), str(writable)], check=True)
        if drive.is_symlink() or drive.resolve() == source:
            raise RuntimeError('Refusing real C:')
        before_dirs = {str(p.relative_to(drive)) for p in drive.rglob('*') if p.is_dir()} | {'.'}
        before_exes = {str(p.relative_to(drive)) for p in drive.rglob('*') if p.suffix.lower() == '.exe'}
        # Games may write saves/configuration next to their executable. Never expose
        # another collection or the host home to the guest.
        payload_dirs = {str(p.relative_to(writable)) for p in writable.rglob('*') if p.is_dir()} | {'.'}
        payload_exes = {str(p.relative_to(writable)) for p in writable.rglob('*') if p.suffix.lower() == '.exe'}
        recipe = game['install'] if phase == 'installer' else game['launch']
        executable = recipe['exe']
        if phase == 'game' and session is not None:
            executable = session.get('game_exe', executable)
        elif phase == 'game' and installed.is_dir():
            receipt_path = installed_home/'installed-meta'/f"{game['id']}.json"
            receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {}
            executable = 'E:\\' + receipt.get('exe', recipe.get('installed_exe', ''))
        if executable == 'E:\\_installer.exe':
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), str(verify(home, game)), str(writable/'_installer.exe')], check=True)
        if executable.startswith(('C:\\', 'E:\\')):
            relative = executable[3:].replace('\\', '/')
            if not inside(drive if executable[0] == 'C' else writable, relative).is_file():
                raise ValueError(f'Missing launch executable: {executable}')
        stage_system_libraries(writable, drive, recipe)
        steps = localized_buttons(recipe.get('input', ''))
        if recipe.get('via_explorer'):
            steps = '3:guestopen=' + executable + ';' + steps
        steps = bounded_steps(steps, seconds)
        steps += f';{seconds-2}:tops=2;{seconds-1}:shot={out}/last.ppm;{seconds}:menu=quit'
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_DEADLINE=str(seconds + 15), NEO98_HOST_INPUT=steps,
                   NEO98_HOST_PASTEBOARD='unique', NEO98_SETTINGS_DIR=str(out/'settings'),
                   NEO98_LOG_DIR=str(out/'logs'), NEO98_HOST_STATS='1')
        command = [str(ROOT/'build/native-app/neo98-host'), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON',
                   '--exe', 'C:\\WINDOWS\\EXPLORER.EXE' if recipe.get('via_explorer') else executable, '--quiet', '--offline', '--system-tree', str(drive), '--drive-c', str(drive),
                   '--registry', str(registry), '--drive', 'E='+str(writable),
                   '--directory', executable.rsplit('\\', 1)[0] + '\\',
                   '--crash-report-dir', str(out/'reports')]
        if recipe.get('cdrom'):
            # Mount only the validated private payload, never the source corpus.
            command += ['--cdrom', str(writable)]
        if recipe.get('command') and recipe.get('via_explorer'):
            raise ValueError('Explorer launch does not accept a direct command override')
        if recipe.get('command'):
            command += ['--command', recipe['command']]
        metadata = {'id':game['id'], 'phase':phase, 'package_sha256':game['sha256'],
                    'revision':source_revision(),
                    'host_sha256':digest(ROOT/'build/native-app/neo98-host'), 'command':command,
                    'environment':{k:v for k,v in env.items() if k.startswith(('NEO98_', 'SDL_'))}}
        (out/'command.json').write_text(json.dumps(metadata,indent=2)+'\n')
        started = time.monotonic()
        timed_out = False
        with (out/'host.log').open('wb') as log:
            process = subprocess.Popen(command, cwd=out, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            if session is not None:
                session['process'] = process
            code, timed_out = wait_host(process, seconds, work)
        from PIL import Image
        check_reserve(work)
        for ppm in out.glob('*.ppm'):
            check_reserve(work)
            with Image.open(ppm) as image:
                image.save(ppm.with_suffix('.png'))
            ppm.unlink()
        status, note = classify(code,log_fragments(out/'host.log', work),out/'last.png',phase,timed_out)
        check_reserve(work)
        metadata.update(returncode=code,elapsed_seconds=round(time.monotonic()-started,2),status=status,note=note,
                        screenshot=str(out/'last.png') if (out/'last.png').exists() else None)
        (out/'result.json').write_text(json.dumps(metadata,indent=2)+'\n')
        # Only persist files created by this installer, never an unrelated
        # pre-existing game from the user's source C:.
        if phase == 'installer':
            metadata['new_executables'] = sorted(
                letter + ':\\' + str(p.relative_to(tree)).replace('/', '\\')
                for tree, old_exes, letter in ((drive, before_exes, 'C'), (writable, payload_exes, 'E'))
                for p in tree.rglob('*')
                if p.is_file() and p.suffix.lower() == '.exe'
                and str(p.relative_to(tree)) not in old_exes
                and p.relative_to(tree).parts[0].lower() not in ('windows', 'temp'))
            (out/'result.json').write_text(json.dumps(metadata, indent=2)+'\n')
            names = game['install'].get('discover_exe', [game['launch'].get('installed_exe', '')])
            found = None
            for install_drive, old_dirs, old_exes, letter in (
                    (drive, before_dirs, before_exes, 'C'),
                    (writable, payload_dirs, payload_exes, 'E')):
                for name in names:
                    matches = [p for p in install_drive.rglob('*') if p.is_file() and p.name.lower() == name.lower()
                               and str(p.relative_to(install_drive)) not in old_exes
                               and p.relative_to(install_drive).parts[0].lower() not in ('windows', 'temp')]
                    if len(matches) == 1:
                        found = matches[0]
                        break
                if found:
                    break
            if found:
                if session is not None:
                    session['game_exe'] = letter + ':\\' + str(found.relative_to(install_drive)).replace('/', '\\')
                    metadata['discovered_exe'] = session['game_exe']
                    (out/'result.json').write_text(json.dumps(metadata, indent=2)+'\n')
                    return metadata
                base = found.parent
                while base != install_drive and base.parent != install_drive and str(base.parent.relative_to(install_drive)) not in old_dirs:
                    base = base.parent
                if base == drive:
                    raise ValueError('Refusing to retain the whole C: drive; define a game subdirectory')
                saved = inside(work, 'installed/' + game['id'])
                saved.parent.mkdir(parents=True, exist_ok=True)
                if saved.exists():
                    raise ValueError('Refusing to overwrite installed game')
                retained_size = int(subprocess.check_output(['du', '-sk', str(base)], text=True).split()[0]) * 1024
                guard(work, retained_size)
                subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(base), str(saved)], check=True)
                receipt = {'package_sha256': game['sha256'], 'original_drive': letter,
                           'original_path': str(base.relative_to(install_drive)),
                           'exe': str(found.relative_to(base)).replace('/', '\\'), 'exe_sha256': digest(found),
                           'run': str(out), 'note': 'Game subtree retained; system DLL/registry changes are not replayed.'}
                receipts = work/'installed-meta'
                receipts.mkdir(exist_ok=True)
                (receipts/f"{game['id']}.json").write_text(json.dumps(receipt, indent=2)+'\n')
                metadata['installed'] = receipt
                (out/'result.json').write_text(json.dumps(metadata, indent=2)+'\n')
        print(json.dumps({'id':game['id'],'phase':phase,'status':status,'out':str(out)}),flush=True)
        return metadata
    finally:
        # Leave files intact only if our host could still be using them.
        if process is None or process.poll() is not None:
            for ppm in out.glob('*.ppm'):
                ppm.unlink()
        if session is None and (process is None or process.poll() is not None):
            for scratch in (writable, drive):
                if scratch.exists():
                    shutil.rmtree(scratch)


# Setup programs and message boxes take the button names of the Windows they
# run on. With NEO98_RECIPE_LANGUAGE=DE (a German reference tree) a scripted
# press also accepts the German name; the host reads "A|B" as either.
BUTTON_NAMES = {'DE': {'Next >': 'Weiter >', '< Back': '< Zurück', 'Yes': 'Ja', 'No': 'Nein',
                       'Finish': 'Fertig stellen', 'Install': 'Installieren', 'Cancel': 'Abbrechen'}}


def localized_buttons(steps):
    names = BUTTON_NAMES.get(os.environ.get('NEO98_RECIPE_LANGUAGE', '').upper(), {})
    def one(step):
        at, _, rest = step.partition(':')
        action, _, value = rest.partition('=')
        if action != 'button' or value not in names:
            return step
        return f'{at}:button={value}|{names[value]}'
    return ';'.join(one(step) for step in steps.split(';')) if names and steps else steps


def play(home, game, label, seconds, work):
    """Install and launch in the same private drives and registry, then discard.

    A discovered EXE proves only that a file was installed. Screenshots still
    need human review; an incomplete installer never counts as gameplay.
    """
    session = {}
    try:
        if game['install'].get('required', True):
            result = run(home, game, label, seconds or game['install'].get('seconds', 90),
                         'installer', work, session)
            if result['status'] != 'installation-pending' or 'game_exe' not in session:
                return result
        return run(home, game, label, seconds or game['launch'].get('seconds', 45),
                   'game', work, session)
    finally:
        process = session.get('process')
        if process is None or process.poll() is not None:
            for key in ('writable', 'drive'):
                scratch = session.get(key)
                if scratch is not None and scratch.exists():
                    shutil.rmtree(scratch)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['list','fetch','verify','prepare','run','install','play'])
    parser.add_argument('--only', action='append', help='Exact id; repeat for multiple games')
    parser.add_argument('--home',type=Path,default=HOME)
    parser.add_argument('--work',type=Path,default=ROOT/'build/game-repertoire',
                        help='Writable results and installations; separate from the source corpus')
    parser.add_argument('--manifest',type=Path,default=MANIFEST)
    parser.add_argument('--label',default=time.strftime('%Y%m%d-%H%M%S'))
    parser.add_argument('--seconds',type=int,help='Override each phase duration (otherwise recipe/default)')
    args=parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', args.label):
        parser.error('--label must be a simple directory name')
    if args.seconds is not None and not 5 <= args.seconds <= 600:
        parser.error('--seconds must be between 5 and 600')
    home=(args.home or external_path('NEO98_GAME_CORPUS')).expanduser().resolve()
    if home.is_relative_to(ROOT) or home == Path.home():
        parser.error('Corpus must be outside the repository in a dedicated directory')
    work=args.work.expanduser().resolve()
    protected = [(ROOT/'build/win98-c').resolve()]
    if os.environ.get('NEO98_WIN98_ROOT'):
        protected.append(Path(os.environ['NEO98_WIN98_ROOT']).resolve())
    if (work.is_relative_to(home) or home.is_relative_to(work) or
            any(work.is_relative_to(p) or p.is_relative_to(work) for p in protected)):
        parser.error('--work must be separate from the corpus and the real C:')
    if args.action == 'fetch':
        home.mkdir(parents=True,exist_ok=True)
    work.mkdir(parents=True,exist_ok=True)
    manifest=json.loads(args.manifest.read_text())
    games=manifest['games']
    if args.only:
        unknown=set(args.only)-{g['id'] for g in games}
        if unknown: parser.error('Unknown ids: '+', '.join(sorted(unknown)))
        games=[g for g in games if g['id'] in args.only]
    failures=[]
    for game in games:
        try:
            if args.action=='list':
                print(game['id'],game['renderer'],game['title'])
            elif args.action=='fetch':
                fetch(home,game)
                for asset in game.get('extra_assets', []): fetch(home,asset)
            elif args.action=='verify':
                verify(home,game)
                for asset in game.get('extra_assets', []): verify(home,asset)
                print(game['id'],'SHA-256 OK')
            elif args.action=='prepare': prepare(home,game,work)
            else:
                if args.action == 'install' and not game['install'].get('required', True):
                    prepare(home, game, work)
                    print(game['id'], 'portable package prepared; no installer required')
                    continue
                # serial.py is the only entry point to a host process. One
                # separate child per title releases the shared lock in between.
                phase='installer' if args.action=='install' else game['launch'].get('phase','game')
                if args.action=='run' and any((base/'installed'/game['id']).is_dir() for base in (work, home)):
                    phase='game'
                if os.environ.get('NEO98_REPERTOIRE_SERIAL_CHILD')=='1':
                    if args.action == 'play':
                        play(home,game,args.label,args.seconds,work)
                    else:
                        recipe = game['install'] if phase == 'installer' else game['launch']
                        run(home,game,args.label,args.seconds or recipe.get('seconds', 20),phase,work)
                else:
                    command=[str(external_path('NEO98_SERIAL_RUNNER')),'env','NEO98_REPERTOIRE_SERIAL_CHILD=1',
                             sys.executable,'-I',str(Path(__file__).resolve()),args.action,
                             '--manifest',str(args.manifest.resolve()),'--home',str(home),'--work',str(work),
                             '--only',game['id'],'--label',args.label]
                    if args.seconds is not None:
                        command += ['--seconds',str(args.seconds)]
                    result=subprocess.run(command)
                    if result.returncode: raise RuntimeError('STOP: serialized host operation failed')
        except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
            print(game['id'],str(error),file=sys.stderr,flush=True)
            failures.append({'id':game['id'],'error':str(error)})
            if 'STOP:' in str(error): break
    if failures:
        (work/f'failures-{args.action}-{args.label}.json').write_text(json.dumps(failures,indent=2)+'\n')
    return bool(failures)


if __name__=='__main__':
    sys.exit(main())
