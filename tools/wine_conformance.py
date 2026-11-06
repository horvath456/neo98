#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build pinned Wine tests and compare original Win98 with the normal NEO98 host.

Use the analysis venv (pefile) for build/run commands. Nothing runs inside Wine.
Compare results against a Windows 98 reference VM.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / 'tools/wine_conformance'
DEFAULT_OUT = ROOT / 'build/wine-conformance'
GROUPS = ['heap', 'alloc', 'virtual', 'file', 'sync', 'thread', 'process',
          'atom', 'directory', 'environ', 'profile']
GROUP_SOURCES = {name: ('kernel32', name) for name in GROUPS}
GROUP_SOURCES.update(gdi_pen=('gdi32', 'pen'), gdi_brush=('gdi32', 'brush'),
                     user_fmt=('user32', 'wsprintf'), contracts=('neo98', 'contracts'),
                     metatext=('neo98', 'metatext'), controls=('neo98', 'controls'),
                     strings=('neo98', 'strings'), regions=('neo98', 'regions'),
                     windows=('neo98', 'windows'), color_contract=('neo98', 'color_contract'),
                     listview_contract=('neo98', 'listview_contract'),
                     user_events=('neo98', 'user_events'),
                     global_contract=('neo98', 'global_contract'),
                     close_handles=('neo98', 'close_handles'),
                     rtl_memory=('neo98', 'rtl_memory'),
                     global_realloc=('neo98', 'global_realloc'),
                     mdi_default=('neo98', 'mdi_default'),
                     menu_destroy=('neo98', 'menu_destroy'),
                     menu_item_rect=('neo98', 'menu_item_rect'),
                     dialog_end_create=('neo98', 'dialog_end_create'),
                     child_no_parent=('neo98', 'child_no_parent'),
                     edit_default_rect=('neo98', 'edit_default_rect'),
                     file_open_edges=('neo98', 'file_open_edges'),
                     command_tail=('neo98', 'command_tail'),
                     menu_idle=('neo98', 'menu_idle'), native_procedures=('neo98', 'native_procedures'),
                     menu_lifetime=('neo98', 'menu_lifetime'),
                     menu_key_state=('neo98', 'menu_key_state'),
                     accelerator_modifiers=('neo98', 'accelerator_modifiers'),
                     oem_bitmaps=('neo98', 'oem_bitmaps'),
                     console_edges=('neo98', 'console_edges'),
                     console_clip=('neo98', 'console_clip'),
                     foreground_state=('neo98', 'foreground_state'),
                     emf_path=('neo98', 'emf_path'),
                     text_rotation=('neo98', 'text_rotation'),
                     create_minmax=('neo98', 'create_minmax'),
                     create_position=('neo98', 'create_position'),
                     show_order=('neo98', 'show_order'),
                     show_focus=('neo98', 'show_focus'),
                     max_position=('neo98', 'max_position'),
                     show_paint=('neo98', 'show_paint'),
                     process_wait=('neo98', 'process_wait'),
                     extpen_output=('neo98', 'extpen_output'),
                     cbt_activation=('neo98', 'cbt_activation'),
                     cbt_activation_thread=('neo98', 'cbt_activation_thread'),
                     locale_validation=('neo98', 'locale_validation'),
                     socket_interface=('neo98', 'socket_interface'),
                     debug_events=('neo98', 'debug_events'),
                     edit_superclass=('neo98', 'edit_superclass'),
                     window_region_events=('neo98', 'window_region_events'),
                     marlett_pixels=('neo98', 'marlett_pixels'),
                     calendar_contract=('neo98', 'calendar_contract'),
                     defer_contract=('neo98', 'defer_contract'),
                     frame_pixels=('neo98', 'frame_pixels'),
                     quit_contract=('neo98', 'quit_contract'),
                     mouse_merge=('neo98', 'mouse_merge'),
                     loader_layout=('neo98', 'loader_layout'),
                     repertoire_pe=('neo98', 'repertoire_pe'),
                     setting_post=('neo98', 'setting_post'),
                     quit_metadata=('neo98', 'quit_metadata'),
                     image_protection=('neo98', 'image_protection'),
                     combo_contract=('neo98', 'combo_contract'),
                     button_check=('neo98', 'button_check'),
                     socket_contract=('neo98', 'socket_contract'),
                     frame_update=('neo98', 'frame_update'),
                     file_lookup=('neo98', 'file_lookup'),
                     thread_windows=('neo98', 'thread_windows'),
                     cursor_state=('neo98', 'cursor_state'),
                     hook_delivery=('neo98', 'hook_delivery'),
                     mdi_state=('neo98', 'mdi_state'),
                     mdi_menu=('neo98', 'mdi_menu'),
                     system_menu=('neo98', 'system_menu'),
                     show_commands=('neo98', 'show_commands'),
                     button_messages=('neo98', 'button_messages'),
                     create_order=('neo98', 'create_order'),
                     edit_messages=('neo98', 'edit_messages'),
                     edit_input=('neo98', 'edit_input'),
                     combo_keys=('neo98', 'combo_keys'),
                     listbox_messages=('neo98', 'listbox_messages'),
                     redraw_messages=('neo98', 'redraw_messages'),
                     scroll_messages=('neo98', 'scroll_messages'),
                     paint_queue=('neo98', 'paint_queue'),
                     frame_paint=('neo98', 'frame_paint'),
                     display_contract=('neo98', 'display_contract'),
                     config_create=('neo98', 'config_create'), window_words=('neo98', 'window_words'), bitmap_copy=('neo98', 'bitmap_copy'), bitmap_load=('neo98', 'bitmap_load'), stretch_modes=('neo98', 'stretch_modes'), stretch_reduce=('neo98', 'stretch_reduce'), stretch_axis=('neo98', 'stretch_axis'), glyph_raster=('neo98', 'glyph_raster'), font_choice=('neo98', 'font_choice'), glyph_contract=('neo98', 'glyph_contract'), heap_interop=('neo98', 'heap_interop'), static_contract=('neo98', 'static_contract'),
                     icon_metafile=('neo98', 'icon_metafile'),
                     locale_contract=('neo98', 'locale_contract'),
                     fpu_integer=('neo98', 'fpu_integer'), fpu_compare=('neo98', 'fpu_compare'),
                     fpu_nan=('neo98', 'fpu_nan'),
                     path_contract=('neo98', 'path_contract'), metafile_contract=('neo98', 'metafile_contract'))
SYSTEM_DLLS = ['KERNEL32.DLL', 'USER32.DLL', 'GDI32.DLL', 'MSVCRT.DLL', 'ADVAPI32.DLL',
               'COMCTL32.DLL', 'DDRAW.DLL']
CORE16 = ['USER.EXE', 'GDI.EXE', 'KRNL386.EXE']


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def capture_source(root):
    patch = subprocess.check_output(['git', 'diff', '--binary', 'HEAD', '--', 'src', 'tools', 'fex'], cwd=root)
    # Newly added runtime sources matter just as much as tracked edits.
    untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z',
                                         '--', 'src', 'tools', 'fex'], cwd=root).split(b'\0')
    hashes = {}
    for encoded in sorted(p for p in untracked if p):
        relative = os.fsdecode(encoded)
        addition = subprocess.run(['git', 'diff', '--no-index', '--binary', '--', '/dev/null', relative],
                                  cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if addition.returncode not in (0, 1):
            raise RuntimeError(addition.stderr.decode(errors='replace'))
        patch += addition.stdout
        hashes[relative] = digest(root / relative)
    return patch, hashes


def host_provenance(host, prior=None):
    """Tie repeated runs of a frozen host to its recorded source snapshot."""
    if prior is None:
        patch, untracked = capture_source(ROOT)
        info = {'git_revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                'git_status': subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True),
                'untracked_source_sha256': untracked}
        return info, patch
    prior = prior.resolve()
    record = json.loads(prior.read_text())
    if record.get('runtime') != 'neo98' or record.get('host_sha256') != digest(host):
        raise RuntimeError('Host provenance does not identify this NEO98 executable')
    patch_path = prior.parent / 'source.patch'
    if not patch_path.is_file() or record.get('source_patch_sha256') != digest(patch_path):
        raise RuntimeError('Host provenance source patch is missing or changed')
    keys = ('git_revision', 'git_status', 'untracked_source_sha256')
    if any(key not in record for key in keys):
        raise RuntimeError('Host provenance lacks source identity')
    info = {key: record[key] for key in keys}
    info['host_provenance'] = {'path': str(prior), 'sha256': digest(prior)}
    return info, patch_path.read_bytes()


def clone(source, target):
    if Path(target).exists():
        raise RuntimeError(f'Refusing to overwrite {target}')
    Path(target).parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == 'darwin':
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(source), str(target)], check=True)
    elif Path(source).is_dir():
        shutil.copytree(source, target)
    else:
        shutil.copy2(source, target)


def groups_argument(text):
    names = text.split(',')
    if len(set(names)) != len(names) or any(n not in GROUP_SOURCES for n in names):
        raise argparse.ArgumentTypeError('Choose distinct groups from ' + ','.join(GROUP_SOURCES))
    return names


def source_tree(out):
    pin = json.loads((SUPPORT / 'pin.json').read_text())
    archive = out / 'downloads/wine-1.0.tar.gz'
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists() or digest(archive) != pin['sha256']:
        partial = archive.with_suffix('.part')
        with urllib.request.urlopen(pin['url'], timeout=60) as response, partial.open('wb') as target:
            shutil.copyfileobj(response, target)
        if digest(partial) != pin['sha256']:
            raise RuntimeError('Wine archive checksum mismatch')
        partial.replace(archive)
    source = out / 'source'
    # Recreate from the verified archive, including on warm builds. Local edits
    # to downloaded sources must never silently change the test expectations.
    if source.exists():
        shutil.rmtree(source)
    source.mkdir()
    with tarfile.open(archive) as tar:
        for member in tar:
            parts = Path(member.name).parts[1:]
            if not parts or member.name.startswith('/') or '..' in parts:
                continue
            path = source.joinpath(*parts)
            if member.isfile():
                path.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as src, path.open('wb') as dst:
                    shutil.copyfileobj(src, dst)
    return pin, source


def pe_details(path):
    import pefile
    pe = pefile.PE(str(path))
    for section in pe.sections:
        if section.VirtualAddress + max(section.Misc_VirtualSize, section.SizeOfRawData) > pe.OPTIONAL_HEADER.SizeOfImage:
            pe.close()
            raise RuntimeError('PE section outside SizeOfImage: '+str(path))
    result = {'machine': pe.FILE_HEADER.Machine,
              'subsystem': pe.OPTIONAL_HEADER.Subsystem,
              'subsystem_version': [pe.OPTIONAL_HEADER.MajorSubsystemVersion,
                                    pe.OPTIONAL_HEADER.MinorSubsystemVersion],
              'imports': {entry.dll.decode(): [symbol.name.decode() if symbol.name else
                         '#' + str(symbol.ordinal) for symbol in entry.imports]
                          for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', [])}}
    pe.close()
    return result


def group_executable(name):
    # Groups sharing a seven-character prefix need distinct 8.3 names when
    # built together. Existing recorded manifests retain their original names.
    return {'window_region_events': 'KWINREG.EXE', 'console_clip': 'KCONCLIP.EXE',
            'process_wait': 'KPROCWT.EXE', 'locale_validation': 'KLOCVAL.EXE',
            'cbt_activation_thread': 'KCBTTHR.EXE',
            'socket_interface': 'KSOCKIF.EXE',
            'show_paint': 'KSHOWPNT.EXE', 'max_position': 'KMAXPOS.EXE', 'show_order': 'KSHOWORD.EXE', 'show_focus': 'KSHOWFOC.EXE', 'create_position': 'KCPOS.EXE', 'create_minmax': 'KCMINMAX.EXE', 'bitmap_load': 'KBITLOAD.EXE', 'stretch_reduce': 'KSTRRED.EXE', 'stretch_axis': 'KSTRAXIS.EXE',
            'button_messages': 'KBUTMSG.EXE', 'frame_paint': 'KFRMPNT.EXE', 'global_realloc': 'KGLBREAL.EXE'}.get(name, 'K' + name.upper()[:7] + '.EXE')


def build(args):
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    pin, source = source_tree(out)
    include = out / 'include/wine'
    include.mkdir(parents=True, exist_ok=True)
    header = (source / 'include/wine/test.h').read_text()
    # One atomic, machine-readable record per assertion, including successes.
    # TLS, expected values, broken, skips and todo semantics stay upstream.
    anchor = '    tls_data* data=get_tls_data();\n\n    if (data->todo_level)'
    replacement = ('    tls_data* data=get_tls_data();\n'
                   '    DWORD check_last_error = GetLastError();\n'
                   '    fprintf(stdout, "NEO98_CHECK %s:%d %s\\n", data->current_file,\n'
                   '            data->current_line, data->todo_level ?\n'
                   '            (condition ? "TODO_PASS" : "TODO_FAIL") :\n'
                   '            (condition ? "PASS" : "FAIL"));\n'
                   '    if (!condition) fflush(stdout);\n'
                   '    SetLastError(check_last_error);\n\n'
                   '    if (data->todo_level)')
    if header.count(anchor) != 1:
        raise RuntimeError('Pinned Wine test.h instrumentation anchor changed')
    buffering = 'setvbuf (stdout, NULL, _IONBF, 0);'
    if header.count(buffering) != 1:
        raise RuntimeError('Pinned Wine stdout buffering anchor changed')
    (include / 'test.h').write_text(header.replace(anchor, replacement).replace(
        buffering, 'setvbuf (stdout, NULL, _IOFBF, 4096);'))
    binary_dir = out / 'bin'
    binary_dir.mkdir(exist_ok=True)
    # Always use original MSVCRT imports; current MinGW's default CRT/startup
    # may import newer functions that prevent every EXE from loading on Win98.
    flags = ['-std=gnu89', '-O1', '-fno-builtin', '-fno-strict-aliasing',
             '-D__USE_MINGW_ANSI_STDIO=0', '-D__MSVCRT_VERSION__=0x600',
             '-I' + str(out / 'include'), '-nostdlib', '-Wl,--entry,_mainCRTStartup',
             '-Wl,--no-insert-timestamp', '-Wl,--major-os-version,4',
             '-Wl,--minor-os-version,0', '-Wl,--subsystem,console:4.0']
    libraries = ['-Wl,--start-group', '-lmsvcrt-os', '-lkernel32', '-ladvapi32',
                 '-luser32', '-lgdi32', '-lmingwex', '-lgcc', '-Wl,--end-group']
    manifest = {'schema': 1, 'pin': pin, 'groups': [], 'flags': flags,
                'compiler': subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
                'support_sha256': {p.name: digest(p) for p in sorted(SUPPORT.glob('*')) if p.is_file()},
                'instrumented_test_h_sha256': digest(include / 'test.h')}
    for name in args.groups:
        filename = group_executable(name)
        dll, test = GROUP_SOURCES[name]
        relative_source = f'tools/wine_conformance/{test}.c' if dll == 'neo98' else f'dlls/{dll}/tests/{test}.c'
        src = ROOT / relative_source if dll == 'neo98' else source / relative_source
        command = [args.cc, *flags, '-DSTANDALONE', str(SUPPORT / 'startup.c'),
                   str(src), '-o', str(binary_dir / filename),
                   *(['-lws2_32'] if name in ('socket_contract', 'socket_interface') else []), *libraries]
        result = subprocess.run(command, capture_output=True, text=True)
        (out / f'{name}-build.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name} failed to build; see {out / (name + "-build.log")}')
        details = pe_details(binary_dir / filename)
        permitted = ('kernel32.dll', 'msvcrt.dll', 'advapi32.dll', 'user32.dll', 'gdi32.dll')
        if name in ('socket_contract', 'socket_interface'): permitted += ('ws2_32.dll',)
        if details['machine'] != 0x14c or any(d.lower() not in permitted for d in details['imports']):
            raise RuntimeError(f'Unexpected architecture/DLL dependency in {filename}: {details}')
        manifest['groups'].append({'name': name, 'exe': filename, 'source': relative_source,
            'sha256': digest(binary_dir / filename), 'source_sha256': digest(src),
            'runtime_dlls': ['CFGMGR32.DLL'] if name == 'config_create' else [],
            'timeout_ms': args.timeout * 1000, **details})
        print('BUILT', name, flush=True)
    entries = ''.join('    {"%s", "%s", %d},\n' % (g['name'], g['exe'], g['timeout_ms'])
                      for g in manifest['groups'])
    (out / 'include/suite.h').write_text('static const struct { const char *name, *exe; '
        'unsigned long timeout_ms; } suite[] = {\n' + entries + '    {0, 0, 0}\n};\n')
    supervisor = binary_dir / 'SUITE.EXE'
    subprocess.run([args.cc, *flags, str(SUPPORT / 'supervisor.c'), '-o', str(supervisor),
                    *libraries], check=True)
    manifest['supervisor_sha256'] = digest(supervisor)
    save(out / 'manifest.json', manifest)
    print(out / 'manifest.json')


def verified_manifest(out):
    manifest = json.loads((out / 'manifest.json').read_text())
    expected = {g['exe']: g['sha256'] for g in manifest['groups']}
    expected.update(manifest.get('assets', {}))
    expected['SUITE.EXE'] = manifest['supervisor_sha256']
    for name, sha in expected.items():
        if digest(out / 'bin' / name) != sha:
            raise RuntimeError(f'Test binary changed since build: {name}')
    return manifest


def binaries(manifest):
    return ['SUITE.EXE', *[group['exe'] for group in manifest['groups']], *manifest.get('assets', {})]


def tested_dlls(manifest):
    names = set(SYSTEM_DLLS)
    for group in manifest['groups']:
        names.update(name.upper() for name in group.get('imports', {}))
        names.update(name.upper() for name in group.get('runtime_dlls', []))
        if group.get('family'):
            names.add(group['family'].upper() + '.DLL')
    return sorted(names)


def installation(folder, names=None):
    """Hashes and embedded file versions, without distributing Windows bytes."""
    import pefile
    system = folder / 'WINDOWS/SYSTEM'
    details = {}
    for name in names or SYSTEM_DLLS:
        path = next((p for p in system.iterdir() if p.name.upper() == name), None)
        if path:
            try:
                pe = pefile.PE(str(path))
            except pefile.PEFormatError:
                data = path.read_bytes()
                offset = int.from_bytes(data[60:64], 'little')
                if data[:2] != b'MZ' or data[offset:offset+2] != b'NE':
                    raise
                details[name] = {'sha256': digest(path), 'version': None, 'format': 'NE',
                                 'version_note': '16-bit NE resource version not decoded'}
                continue
            info = getattr(pe, 'VS_FIXEDFILEINFO', [])
            v = info[0] if info else None
            details[name] = {'sha256': digest(path), 'version':
                [v.FileVersionMS >> 16, v.FileVersionMS & 65535,
                 v.FileVersionLS >> 16, v.FileVersionLS & 65535] if v else None}
            pe.close()
    return details


def core_hashes(folder):
    system = folder / 'WINDOWS/SYSTEM'
    return {name: digest(path) for name in CORE16
            if (path := next((p for p in system.iterdir() if p.name.upper() == name), None))}


# Wine prints a trace's source prefix separately from its body. Another thread's
# atomic assertion record can land between them. Accept those prefixes, but not
# arbitrary diagnostic text quoting an assertion or a truncated machine record.
CHECK = re.compile(r'^(?:[^:\r\n]+\.(?:c|cpp|h):\d+:\s+)*NEO98_CHECK ([^:\r\n]+):(\d+) (PASS|FAIL|TODO_PASS|TODO_FAIL)$')
SKIP = re.compile(r'^(.+\.(?:c|cpp|h)):(\d+): Tests skipped: (.*)$')


def fixture_timeout_ms(manifest):
    shell = manifest.get('fixtures', {}).get('shell', {})
    return shell.get('startup_timeout_ms', 0) + shell.get('idle_timeout_ms', 0)


def host_observations(path):
    result = {'deadline_reached': False, 'missing_dlls': []}
    if path.exists():
        with path.open(errors='replace') as log:
            for line in log:
                result['deadline_reached'] |= bool(re.match(r'^NEO98 HOST deadline \d+ s reached', line))
                if line.startswith('MISSING DLL ') and line.strip() not in result['missing_dlls']:
                    result['missing_dlls'].append(line.strip())
    return result


def parse_logs(folder, manifest):
    journal = folder / 'RUN.TSV'
    lines = journal.read_text(errors='replace').splitlines() if journal.exists() else []
    group_ends, started, pids = {}, set(), {}
    for line in lines:
        fields = line.split('\t')
        if fields[0] == 'BEGIN' and len(fields) == 2:
            started.add(fields[1])
        elif fields[0] == 'END' and len(fields) == 4:
            group_ends[fields[1]] = (fields[2], int(fields[3]))
        elif fields[0] == 'PID' and len(fields) == 3:
            pids[fields[1]] = fields[2]
    result = {'info': [line for line in lines if line.startswith('INFO\t')],
              'suite_done': 'DONE' in lines, 'groups': {}}
    for group in manifest['groups']:
        name, exe = group['name'], group['exe']
        counts = defaultdict(Counter)
        skips, processes = [], []
        for log in sorted(folder.glob(exe + '.*.LOG')):
            began, completed = False, False
            with log.open(errors='replace') as file:
                for line in file:
                    line = line.rstrip('\r\n')
                    began |= line.startswith('NEO98_WINE_BEGIN ')
                    completed |= line.startswith('NEO98_WINE_END ')
                    match = CHECK.match(line)
                    if match:
                        counts[match[1] + ':' + match[2]][match[3]] += 1
                    match = SKIP.match(line)
                    if match:
                        skips.append({'site': match[1] + ':' + match[2], 'reason': match[3]})
            processes.append({'log': log.name, 'began': began,
                              'completed': completed,
                              'sha256': digest(log)})
        kind, code = group_ends.get(name, ('INCOMPLETE' if name in started else 'NOT_RUN', None))
        # A normal nonzero Wine return value is an assertion count, not a crash.
        if kind == 'EXIT':
            has_completed_log = any(p['completed'] and
                p['log'] == exe + '.' + pids.get(name, '') + '.LOG' for p in processes)
            status = ('PASS' if code == 0 else 'FAIL') if has_completed_log and code <= 255 else 'CRASH'
        else:
            status = kind
        deadline = kind == 'HOST_ERROR' and host_observations(folder/(name+'-host.log'))['deadline_reached']
        if deadline: status = 'TIMEOUT'
        result['groups'][name] = {'status': status, 'exit_code': code,
            'sites': {key: dict(value) for key, value in sorted(counts.items())},
            'counts': dict(sum(counts.values(), Counter())), 'skips': skips, 'processes': processes}
        if deadline: result['groups'][name]['timeout_source'] = 'neo98-host-watchdog'
    return result


def finish_run(destination, manifest, extra):
    save(destination / 'manifest.json', manifest)
    result = {**extra, **parse_logs(destination, manifest)}
    timed_out = extra.get('host_timeout_group')
    if extra.get('host_timeout'):
        unfinished = [name for name, group in result['groups'].items() if group['status'] == 'INCOMPLETE']
        if len(unfinished) == 1:
            result['last_observed_timeout_group'] = timed_out
            timed_out = result['host_timeout_group'] = unfinished[0]
    if timed_out in result['groups'] and result['groups'][timed_out]['status'] == 'INCOMPLETE':
        result['groups'][timed_out].update(status='TIMEOUT', timeout_source='86box-host-watchdog')
    save(destination / 'results.json', result)
    for name, group in result['groups'].items():
        print(name, group['status'], group['counts'], 'skips', len(group['skips']), flush=True)
    return result


def reparse_run(args):
    """Reinterpret immutable raw evidence in a new directory, retaining provenance."""
    source, destination = args.source.resolve(), args.results.resolve()
    if destination == source or source in destination.parents:
        raise RuntimeError('Reparse destination must be outside the original run')
    original = json.loads((source / 'results.json').read_text())
    manifest = json.loads((source / 'manifest.json').read_text())
    recorded = {process['log']: process['sha256']
                for group in original['groups'].values() for process in group['processes']}
    logs = {path.name: path for group in manifest['groups']
            for path in source.glob(group['exe'] + '.*.LOG')}
    if set(logs) != set(recorded) or any(digest(path) != recorded[name] for name, path in logs.items()):
        raise RuntimeError('Raw logs differ from the original recorded evidence')
    destination.mkdir(parents=True, exist_ok=False)
    files = [source / 'RUN.TSV', *logs.values(), *source.glob('*-host.log')]
    files += [source / name for name in ('source.patch',) if (source / name).exists()]
    for path in files:
        shutil.copy2(path, destination / path.name)
    if (source / 'system-provenance').is_dir():
        shutil.copytree(source / 'system-provenance', destination / 'system-provenance')
    extra = {key: value for key, value in original.items()
             if key not in ('info', 'suite_done', 'groups')}
    extra['reparsed_from'] = {'path': str(source),
        'results_sha256': digest(source / 'results.json'),
        'manifest_sha256': digest(source / 'manifest.json'),
        'journal_sha256': digest(source / 'RUN.TSV'),
        'parser_sha256': digest(Path(__file__))}
    return finish_run(destination, manifest, extra)


def reference_skip_reason(reference, group):
    if not any(line.startswith('INFO\tos=4.10.2222\tplatform=1\t') for line in reference.get('info', [])):
        return 'Reference did not identify Windows 98 SE build 2222'
    status = reference['groups'].get(group['name'], {}).get('status', 'NOT_RUN')
    if status not in ('PASS', 'FAIL') and group.get('assessment') != 'INTERACTIVE_REVIEW':
        return 'No completed original Win98 reference: ' + status
    return None


def reference_dll_overlay(reference_path, reference, system_root, overrides=()):
    """Select verified originals; replacing an installed version needs an explicit name."""
    overrides = {name.upper() for name in overrides}
    if overrides - reference.get('windows_dlls', {}).keys():
        raise RuntimeError('Requested DLL was not captured in the reference: ' + ', '.join(sorted(overrides - reference.get('windows_dlls', {}).keys())))
    existing = {p.name.upper() for p in (system_root/'WINDOWS/SYSTEM').iterdir()}
    originals = reference_path.parent/'system-provenance/WINDOWS/SYSTEM'
    overlay = {}
    for name, details in reference.get('windows_dlls', {}).items():
        if name in existing and name not in overrides: continue
        if not re.fullmatch(r'[A-Z0-9_.-]+\.(?:DLL|DRV)', name):
            raise RuntimeError('Invalid reference DLL filename: '+name)
        path = originals/name
        if not path.is_file() or digest(path) != details['sha256']:
            raise RuntimeError('Reference DLL content differs from recorded hash: '+name)
        overlay[name] = path
    return overlay


def select_run_groups(manifest, names):
    available = {g['name'] for g in manifest['groups']}
    if names is None:
        return available
    selected = {name.strip() for name in names.split(',') if name.strip()}
    if not selected or selected - available:
        raise RuntimeError('Unknown or empty group selection: ' + str(names))
    return selected


def run_neo98(args):
    manifest = verified_manifest(args.out)
    selected = select_run_groups(manifest, getattr(args, 'names', None))
    reference = None
    reference_path = getattr(args, 'reference_results', None)
    overrides = getattr(args, 'reference_dll', [])
    if (getattr(args, 'reference_dlls', False) or overrides) and reference_path is None:
        raise RuntimeError('Reference DLL options require --reference-results')
    if reference_path:
        reference = json.loads(reference_path.read_text())
        ref_manifest = json.loads((reference_path.parent / 'manifest.json').read_text())
        identity = lambda m: ({g['name']: g['sha256'] for g in m['groups']}, m['supervisor_sha256'], m.get('assets', {}))
        if reference.get('runtime') != '86box-win98' or identity(ref_manifest) != identity(manifest):
            raise RuntimeError('Selection needs an original Win98 reference with identical inputs')
    destination = args.results.resolve()
    destination.mkdir(parents=True, exist_ok=False)
    host_source = args.host.resolve()
    provenance, patch = host_provenance(host_source, getattr(args, 'host_provenance', None))
    host = destination / 'neo98-host'
    clone(host_source, host)
    source_root = args.win98_root.resolve()
    overlay = reference_dll_overlay(reference_path, reference, source_root, overrides) if getattr(args, 'reference_dlls', False) or overrides else {}
    if overrides and not getattr(args, 'reference_dlls', False):
        overlay = {name: path for name, path in overlay.items() if name in {n.upper() for n in overrides}}
    extra = {'runtime': 'neo98', 'host_sha256': digest(host), 'host_source': str(host_source),
             **provenance,
             'windows_dlls': installation(source_root, tested_dlls(manifest)), 'host_runs': []}
    extra['selection_groups'] = sorted(selected)
    extra['cpu_environment'] = {key: value for key, value in os.environ.items()
                                if key.startswith('NEO98_FEX_') or key == 'NEO98_CPU'}
    if getattr(args, 'trace_calls', False):
        extra['trace_calls'] = True
    if getattr(args, 'trace_activation', False):
        extra['trace_activation'] = True
    extra['not_attempted'] = {}
    extra['windows_core16_sha256'] = core_hashes(source_root)
    extra['windows_registry_hives'] = {
        name: digest(path) if path.is_file() else None
        for name in ('SYSTEM.DAT', 'USER.DAT')
        for path in [source_root / 'WINDOWS' / name]}
    extra['win98_source_root'] = str(source_root)
    if getattr(args, 'fixture_cdrom', False):
        extra['fixture_cdrom'] = {'letter': 'D', 'files': [], 'readonly': True}
    if overlay:
        extra['windows_dlls_before_overlay'] = dict(extra['windows_dlls'])
        extra['explicit_reference_dlls'] = sorted(name.upper() for name in overrides)
        extra['reference_dll_overlay'] = {name: {'path': str(path), 'sha256': digest(path)} for name, path in overlay.items()}
        extra['windows_dlls'].update({name: reference['windows_dlls'][name] for name in overlay})
    if reference is not None:
        extra['selection_reference_sha256'] = digest(reference_path)
        extra['not_attempted'] = {}
    (destination / 'source.patch').write_bytes(patch)
    extra['source_patch_sha256'] = digest(destination / 'source.patch')
    # A new host process and C: for each group also survive a runtime abort.
    all_journal = []
    save(destination/'manifest.json', manifest)

    def persist_progress():
        pending = destination/'RUN.TSV.next'
        pending.write_text('\n'.join(all_journal)+'\n')
        pending.replace(destination/'RUN.TSV')
        save(destination/'progress.json', extra)

    persist_progress()
    for group in manifest['groups']:
        name = group['name']
        if name not in selected:
            extra['not_attempted'][name] = 'Not selected for this diagnostic run'
            all_journal.append('END\t%s\tSKIP\t0' % name)
            persist_progress()
            continue
        if reference is not None:
            reason = reference_skip_reason(reference, group)
            if reason:
                extra['not_attempted'][name] = reason
                all_journal.append('END\t%s\tSKIP\t0' % name)
                persist_progress()
                print('NEO98', name, 'SKIP:', extra['not_attempted'][name], flush=True)
                continue
        session = destination / ('session-' + name)
        drive = session / 'c'
        clone(source_root, drive)
        for dll, original in overlay.items():
            system = drive/'WINDOWS/SYSTEM'
            target = next((p for p in system.iterdir() if p.name.upper() == dll), system/dll)
            shutil.copy2(original, target)
        tests = drive / 'WTEST'
        tests.mkdir()
        for name_to_copy in binaries(manifest):
            shutil.copy2(args.out / 'bin' / name_to_copy, tests / name_to_copy)
        environment = os.environ.copy()
        for key in list(environment):
            if key.startswith(('NEO98_HOST_', 'NEO98_TRACE_', 'WINETEST_')):
                del environment[key]
        runtime_seconds = (group['timeout_ms'] + fixture_timeout_ms(manifest) + 999) // 1000
        environment.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
            NEO98_HOST_DEADLINE=str(runtime_seconds + 30),
            NEO98_SETTINGS_DIR=str(session / 'settings'), NEO98_LOG_DIR=str(session / 'logs'))
        if getattr(args, 'trace_calls', False):
            environment['NEO98_TRACE_CALLS_FROM'] = '1'
        if getattr(args, 'trace_activation', False):
            environment['NEO98_TRACE_ACTIVATE'] = '1'
        if group.get('assessment') == 'INTERACTIVE_REVIEW':
            environment['NEO98_HOST_SHOT'] = '5:' + str(destination / (name + '.bmp'))
        command = [str(host), '-', str(args.font.resolve()), '--system-tree', str(drive),
                   '--drive-c', str(drive), '--exe', r'C:\WTEST\SUITE.EXE', '--command',
                   r'C:\WTEST\SUITE.EXE ' + name, '--no-registry-file', '--quiet',
                   '--crash-report-dir', str(session / 'reports')]
        if getattr(args, 'fixture_cdrom', False):
            disc = session / 'empty-cdrom'
            disc.mkdir()
            command.extend(['--cdrom', str(disc)])
        before = time.monotonic()
        with (destination / (name + '-host.log')).open('wb') as log:
            try:
                run = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT,
                    timeout=runtime_seconds + 60)
                code = run.returncode
            except subprocess.TimeoutExpired:
                code = 'TIMEOUT'
        extra['host_runs'].append({'group': name, 'exit_code': code,
                                  'seconds': round(time.monotonic() - before, 3), 'command': command})
        for log in tests.glob('*.LOG'):
            shutil.copy2(log, destination / log.name)
        journal = tests / 'RUN.TSV'
        part = journal.read_text(errors='replace') if journal.exists() else ''
        all_journal.extend(line for line in part.splitlines() if line != 'DONE')
        # If the entire NEO98 host fails, record the failure even when the guest
        # supervisor could not write BEGIN or END.
        if not any(line.startswith('END\t' + name + '\t') for line in part.splitlines()):
            all_journal.append('END\t%s\t%s\t%d' %
                (name, 'TIMEOUT' if code == 'TIMEOUT' else 'HOST_ERROR', code if isinstance(code, int) else 248))
        persist_progress()
        print('NEO98', name, 'host exit', code, flush=True)
        # Keep reports even when the caller discards the bulky private C:.
        if args.discard_sessions:
            if (session / 'reports').exists():
                shutil.copytree(session / 'reports', destination / (name + '-reports'))
            shutil.rmtree(session)
    all_journal.append('DONE')
    (destination / 'RUN.TSV').write_text('\n'.join(all_journal) + '\n')
    finish_run(destination, manifest, extra)


def compare(args):
    reference = json.loads((args.reference / 'results.json').read_text())
    candidate = json.loads((args.candidate / 'results.json').read_text())
    if reference.get('runtime') != '86box-win98' or candidate.get('runtime') != 'neo98':
        raise RuntimeError('Expected an original 86Box/Win98 reference and an NEO98 candidate')
    if not any(line.startswith('INFO\tos=4.10.2222\tplatform=1\t') for line in reference.get('info', [])):
        raise RuntimeError('Reference did not identify itself as Windows 98 SE build 2222')
    ref_manifest = json.loads((args.reference / 'manifest.json').read_text())
    arm_manifest = json.loads((args.candidate / 'manifest.json').read_text())
    ref_hashes = {g['name']: g['sha256'] for g in ref_manifest['groups']}
    if ref_hashes != {g['name']: g['sha256'] for g in arm_manifest['groups']} or \
            ref_manifest['supervisor_sha256'] != arm_manifest['supervisor_sha256'] or \
            ref_manifest.get('assets', {}) != arm_manifest.get('assets', {}):
        raise RuntimeError('Reference and candidate did not run identical test binaries')
    report = {'schema': 1, 'reference': str(args.reference.resolve()),
              'candidate': str(args.candidate.resolve()), 'regressions': [],
              'unreached': [], 'count_changes': [], 'additional_skips': [],
              'mixed_count_shortfalls': [],
              'group_status': {}, 'dll_differences': [], 'core16_differences': [], 'incomplete_reference': [],
              'environment': {'reference': sorted(set(reference.get('info', []))),
                              'candidate': sorted(set(candidate.get('info', [])))},
              'source_pin': ref_manifest.get('pin', {}),
              'binary_sha256': ref_hashes,
              'supervisor_sha256': ref_manifest['supervisor_sha256'],
              'candidate_revision': candidate.get('git_revision'),
              'candidate_host_sha256': candidate.get('host_sha256'),
              'reference_emulator_sha256': reference.get('emulator_sha256'),
              'reference_summary': {}, 'candidate_summary': {}}
    for dll, value in reference.get('windows_dlls', {}).items():
        other = candidate.get('windows_dlls', {}).get(dll)
        if value != other:
            report['dll_differences'].append({'dll': dll, 'reference': value, 'candidate': other})
    for name, value in reference.get('windows_core16_sha256', {}).items():
        other = candidate.get('windows_core16_sha256', {}).get(name)
        if value != other:
            report['core16_differences'].append({'module': name, 'reference': value, 'candidate': other})
    for name, ref in reference['groups'].items():
        arm = candidate['groups'].get(name, {'sites': {}, 'skips': [], 'status': 'NOT_RUN'})
        report['group_status'][name] = {'reference': ref['status'], 'candidate': arm['status']}
        for key, group in [('reference_summary', ref), ('candidate_summary', arm)]:
            report[key][name] = {'status': group['status'], 'counts': group.get('counts', {}),
                                'skip_chunks': len(group['skips']), 'assertion_sites': len(group['sites'])}
        if ref['status'] not in ('PASS', 'FAIL'):
            report['incomplete_reference'].append(name)
            continue
        for site, counts in ref['sites'].items():
            # Thread order cannot identify individual cases. Mixed sites still
            # impose a lower bound on the number of observed native passes.
            if not counts.get('PASS'):
                continue
            actual = arm['sites'].get(site, {})
            item = {'group': name, 'site': site, 'reference': counts, 'candidate': actual}
            if set(counts) != {'PASS'}:
                if actual.get('PASS', 0) < counts['PASS']:
                    report['mixed_count_shortfalls'].append(item)
                if sum(actual.values()) != sum(counts.values()):
                    report['count_changes'].append(item)
                continue
            if actual.get('FAIL'):
                report['regressions'].append(item)
            if not actual:
                report['unreached'].append(item)
            elif actual.get('PASS', 0) + actual.get('FAIL', 0) != counts['PASS']:
                report['count_changes'].append(item)
        ref_skips = Counter(s['site'] for s in ref['skips'])
        arm_skips = Counter(s['site'] for s in arm['skips'])
        for site, count in (arm_skips - ref_skips).items():
            report['additional_skips'].append({'group': name, 'site': site, 'count': count})
    save(args.report, report)
    if not getattr(args, 'quiet', False):
        print(f"{len(report['regressions'])} assertion sites pass on Win98 and fail on NEO98; "
              f"{len(report['unreached'])} sites unreached; "
              f"{len(report['mixed_count_shortfalls'])} mixed-site pass-count shortfalls; "
              f"{len(report['additional_skips'])} additional skip sites")
        print(args.report)



def qualify(args):
    """Freeze expectations using only completed original Windows runs."""
    result = json.loads((args.reference / 'results.json').read_text())
    manifest = json.loads((args.reference / 'manifest.json').read_text())
    selected = select_run_groups(manifest, getattr(args, 'names', None))
    if result.get('runtime') != '86box-win98' or not result.get('suite_done') or not any(
            line.startswith('INFO\tos=4.10.2222\tplatform=1\t') for line in result.get('info', [])):
        raise RuntimeError('Qualification requires a completed original Windows 98 SE run')
    alternatives = json.loads((SUPPORT / 'alternatives.json').read_text())
    alternative_hash = digest(SUPPORT / 'alternatives.json')
    count_path = SUPPORT / 'variable-counts.json'
    variable_counts = json.loads(count_path.read_text()) if count_path.exists() else {}
    bounded_path = SUPPORT / 'bounded-counts.json'
    bounded_counts = json.loads(bounded_path.read_text()) if bounded_path.exists() else {}
    if manifest['pin']['revision'] != alternatives['source_commit']:
        reactos_pin = json.loads((SUPPORT / 'campaign-pins.json').read_text())['reactos']
        if manifest['pin'] != reactos_pin:
            raise RuntimeError('Qualification requires a recognized pinned source')
        alternatives = {'groups': {}}
        alternative_hash = None
    baseline = {'schema': 1, 'source_pin': manifest['pin'],
        'alternatives_sha256': alternative_hash,
        'variable_counts_sha256': digest(count_path) if count_path.exists() else None,
        'bounded_counts_sha256': digest(bounded_path) if bounded_path.exists() else None,
        'assets': manifest.get('assets', {}),
        'reference_results_sha256': digest(args.reference / 'results.json'),
        'reference_emulator_sha256': result.get('emulator_sha256'),
        'windows_dlls': result.get('windows_dlls'),
        'windows_core16_sha256': result.get('windows_core16_sha256'),
        'environment': sorted(set(result.get('info', []))),
        'supervisor_sha256': manifest['supervisor_sha256'], 'groups': {}, 'non_runtime_groups': {}}
    if getattr(args, 'names', None) is not None:
        baseline['reference_selection'] = {
            'selected': sorted(selected),
            'excluded': {g['name']: result['groups'][g['name']]['status']
                         for g in manifest['groups'] if g['name'] not in selected}}
        baseline['excluded_groups'] = {
            g['name']: {'sha256': g['sha256'], 'reference_status': result['groups'][g['name']]['status']}
            for g in manifest['groups'] if g['name'] not in selected}
    for group in manifest['groups']:
        if group['name'] not in selected:
            continue
        ref = result['groups'][group['name']]
        if ref['status'] not in ('PASS', 'FAIL'):
            raise RuntimeError('Incomplete reference group: ' + group['name'])
        if not ref['sites']:
            if not ref.get('skips'):
                raise RuntimeError('Incomplete reference group: ' + group['name'])
            baseline['non_runtime_groups'][group['name']] = {
                'sha256': group['sha256'], 'assessment': 'NO_REFERENCE_ASSERTIONS',
                'reason': 'The completed Win98 run skipped every assertion',
                'skips': ref['skips']}
            continue
        required, advisory = {}, {}
        for site, counts in ref['sites'].items():
            (required if set(counts) == {'PASS'} else advisory)[site] = counts
        if Path(group.get('source', '')).name == 'generated.c' or \
                group.get('assessment', 'ASSERTIONS') != 'ASSERTIONS':
            baseline['non_runtime_groups'][group['name']] = {
                'sha256': group['sha256'], 'assessment': group.get('assessment', 'ABI_LAYOUT'),
                'reason': 'Compiled structure/layout checks do not qualify OS behavior'}
            continue
        if not required:
            raise RuntimeError('No qualifying runtime assertions: ' + group['name'])
        baseline['groups'][group['name']] = {'sha256': group['sha256'],
            'required': required, 'advisory': advisory,
            'alternatives': {site: rule for site, rule in alternatives['groups'].get(group['name'], {}).items() if site in required},
            'skips': dict(Counter(item['site'] for item in ref['skips']))}
        policy = variable_counts.get(group['sha256'], {})
        for site, reason in policy.items():
            if site not in required or not isinstance(reason, str) or not reason.strip():
                raise RuntimeError('Variable count policy requires a pure-pass reference site and a reason: '+site)
        if policy:
            baseline['groups'][group['name']]['variable_counts'] = policy
        bounded = bounded_counts.get(group['sha256'], {})
        for site, rule in bounded.items():
            if (site not in required or type(rule.get('maximum_deficit')) is not int or
                    not 0 < rule['maximum_deficit'] < required[site]['PASS'] or
                    not isinstance(rule.get('reason'), str) or not rule['reason'].strip() or
                    not rule.get('requires') or any(advisory.get(other) != {'FAIL': 1}
                                                  for other in rule['requires'])):
                raise RuntimeError('Bounded count policy requires reviewed native failures: '+site)
        if bounded:
            baseline['groups'][group['name']]['bounded_counts'] = bounded
    if not baseline["groups"]:
        raise RuntimeError("No qualifying runtime assertions")
    if getattr(args, 'message_sequences', False):
        from compare_message_sequences import qualify_contract
        qualify_contract(baseline, args.reference, getattr(args, 'native_defect_review', None))
    elif getattr(args, 'native_defect_review', None):
        raise ValueError('Native defect review is only supported for message sequences')
    save(args.baseline, baseline)
    print('QUALIFIED', sum(len(g['required']) for g in baseline['groups'].values()),
          'Win98 assertion sites:', args.baseline)


def evaluate_baseline(baseline, candidate, manifest):
    hashes = {g['name']: g['sha256'] for g in manifest['groups']}
    expected = {name: g['sha256'] for name, g in {**baseline.get('excluded_groups', {}),
                **baseline.get('non_runtime_groups', {}), **baseline['groups']}.items()}
    if hashes != expected or \
            manifest['supervisor_sha256'] != baseline['supervisor_sha256']:
        raise RuntimeError('Candidate must use the qualified test binaries and supervisor')
    if manifest.get('assets', {}) != baseline.get('assets', {}):
        raise RuntimeError('Candidate must use the qualified input assets')
    if not any(line.startswith('INFO\tos=4.10.2222\tplatform=1\t') for line in candidate.get('info', [])):
        raise RuntimeError('Candidate is not a Win98 SE runtime')
    report = {'schema': 2, 'status': 'PASS', 'groups': {}, 'improvements': [],
              'comparison_scope': 'Source-site aggregates and minimum native pass counts; equal mixed counts do not establish individual case identity',
              'candidate_host_sha256': candidate.get('host_sha256'),
              'non_runtime_groups': baseline.get('non_runtime_groups', {})}
    if 'reference_selection' in baseline:
        report['reference_selection'] = baseline['reference_selection']
    for name, qualified in baseline['groups'].items():
        group = candidate.get('groups', {}).get(name, {})
        sites = group.get('sites', {})
        checks = {}
        for site, counts in qualified['required'].items():
            actual = sites.get(site, {})
            state = 'PASS' if set(actual) == {'PASS'} else 'FAIL' if actual else 'MISSING'
            variable = qualified.get('variable_counts', {}).get(site)
            if state == 'PASS' and actual['PASS'] < counts['PASS'] and not variable:
                state = 'COUNT_SHORTFALL'
            checks[site] = {'status': state, 'reference': counts, 'candidate': actual}
            bounded = qualified.get('bounded_counts', {}).get(site)
            if (state == 'COUNT_SHORTFALL' and bounded and
                    counts['PASS'] - actual['PASS'] <= bounded['maximum_deficit'] and
                    all(sites.get(other) == {'PASS': 1} for other in bounded['requires'])):
                checks[site].update(status='PASS', bounded_count_review=bounded)
            if variable:
                checks[site]['variable_count_reason'] = variable
            alternative = qualified.get('alternatives', {}).get(site)
            if not actual and alternative and all(set(sites.get(other, {})) == {'PASS'}
                    for other in alternative['requires']):
                checks[site].update(status='PASS_ALTERNATIVE', alternative=alternative)
        mixed = {}
        for site, native in qualified['advisory'].items():
            if not native.get('PASS'):
                continue
            actual = sites.get(site, {})
            mixed[site] = {'reference': native, 'candidate': actual,
                'status': 'PASS_COUNTS' if actual.get('PASS', 0) >= native['PASS'] else 'COUNT_SHORTFALL'}
        extra_skips = Counter(item['site'] for item in group.get('skips', [])) - Counter(qualified['skips'])
        complete = group.get('status') in ('PASS', 'FAIL') and candidate.get('suite_done')
        passed = complete and not extra_skips and all(c['status'] in ('PASS', 'PASS_ALTERNATIVE') for c in checks.values()) and all(c['status'] == 'PASS_COUNTS' for c in mixed.values())
        report['groups'][name] = {'status': 'PASS' if passed else 'FAIL',
            'runtime_status': group.get('status', 'NOT_RUN'), 'checks': checks,
            'additional_skips': dict(extra_skips), 'mixed_checks': mixed}
        if not passed: report['status'] = 'FAIL'
        for site, native in qualified['advisory'].items():
            actual = sites.get(site, {})
            if native.get('FAIL') and set(actual) == {'PASS'}:
                report['improvements'].append({'group': name, 'site': site,
                    'reference': native, 'candidate': actual})
    return report


def check_baseline(args):
    baseline = json.loads(args.baseline.read_text())
    candidate = json.loads((args.candidate / 'results.json').read_text())
    manifest = json.loads((args.candidate / 'manifest.json').read_text())
    if baseline.get('comparison') == 'message_sequences':
        from compare_message_sequences import compare
        reference = getattr(args, 'reference', None)
        report = (evaluate_baseline(baseline, candidate, manifest) if baseline.get('assertion_contract')
                  else {'status': 'PASS', 'groups': {}, 'improvements': []})
        if reference is None or digest(reference / 'results.json') != baseline['reference_results_sha256']:
            report['status'] = 'FAIL'
            report['error'] = 'Matching --reference is required for message sequences'
        else:
            try:
                report['sequence_checks'] = compare(reference, args.candidate)
                if report['sequence_checks']['status'] != 'PASS':
                    report['status'] = 'FAIL'
            except (ValueError, KeyError, OSError) as error:
                report['status'] = 'FAIL'
                report['error'] = str(error)
    else:
        report = evaluate_baseline(baseline, candidate, manifest)
    if baseline.get('trace_checks'):
        from compare_probe_traces import check_trace_contracts
        reference = getattr(args, 'reference', None)
        if reference is None or digest(reference / 'results.json') != baseline['reference_results_sha256']:
            report['trace_checks'] = [{'status': 'CHECK_ERROR', 'error': 'Matching --reference is required for pixel contracts'}]
        else:
            report['trace_checks'] = check_trace_contracts(baseline, reference, args.candidate)
        if any(item['status'] != 'PASS' for item in report['trace_checks']):
            report['status'] = 'FAIL'
    report['baseline_sha256'] = digest(args.baseline)
    report['candidate_results_sha256'] = digest(args.candidate / 'results.json')
    save(args.report, report)
    for name, group in report['groups'].items():
        counts = Counter(c['status'] for c in group['checks'].values())
        mixed = Counter(c['status'] for c in group['mixed_checks'].values())
        print(name, group['status'], dict(counts), 'mixed:', dict(mixed), 'runtime:', group['runtime_status'])
    print('WIN98 CONTRACT', report['status'], '- improvements:', len(report['improvements']))
    if report['status'] != 'PASS': raise SystemExit(1)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('reparse', help='Parse verified raw logs into a new evidence directory')
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--results', type=Path, required=True)
    p.set_defaults(func=reparse_run)
    p = sub.add_parser('qualify')
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--names', help='Qualify only these completed groups; record other reference groups as excluded')
    p.add_argument('--message-sequences', action='store_true', help='Require every instrumented message invocation and all other native-pass assertions')
    p.add_argument('--native-defect-review', type=Path, help='Pinned native defect evidence for an explicitly excluded assertion')
    p.set_defaults(func=qualify)
    p = sub.add_parser('check')
    p.add_argument('--reference', type=Path, help='Required original reference for trace/pixel contracts')
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.set_defaults(func=check_baseline)
    p = sub.add_parser('build')
    p.add_argument('--groups', type=groups_argument, default=GROUPS)
    p.add_argument('--timeout', type=int, default=120)
    p.add_argument('--cc', default='i686-w64-mingw32-gcc')
    p.set_defaults(func=build)
    p = sub.add_parser('neo98')
    p.add_argument('--names', help='Comma-separated diagnostic subset; retains identical full reference manifest and records omitted groups')
    p.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    p.add_argument('--host-provenance', type=Path, help='Prior NEO98 results.json for this exact host hash; reuse its verified source snapshot while work continues')
    p.add_argument('--font', type=Path, default=ROOT / 'build/generated/notepad-fixedsys.fnt')
    p.add_argument('--win98-root', type=Path, required=True)
    p.add_argument('--results', type=Path, required=True)
    p.add_argument('--discard-sessions', action='store_true')
    p.add_argument('--trace-calls', action='store_true', help='Explicit diagnostic API trace, recorded in provenance; ambient trace variables remain excluded')
    p.add_argument('--trace-activation', action='store_true', help='Diagnostic foreground/queue trace, recorded in provenance')
    p.add_argument('--fixture-cdrom', action='store_true', help='Mount an empty, read-only data CD as D: for tests requiring multiple drives; recorded in results')
    p.add_argument('--reference-results', type=Path, help='Only run groups completed on the identical Win98 reference; retain interactive reviews')
    p.add_argument('--reference-dlls', action='store_true', help='Add only missing, hash-verified original DLL files from this reference to private guest copies')
    p.add_argument('--reference-dll', action='append', default=[], metavar='NAME', help='Use this captured original DLL version in private guest copies, replacing an installed version explicitly; repeatable')
    p.set_defaults(func=run_neo98)
    p = sub.add_parser('reference')
    p.add_argument('--agent', type=Path, required=True, help='Existing tools/win98-agent executable')
    p.add_argument('--emulator', type=Path, required=True, help='86Box executable with the agent VNC patches')
    p.add_argument('--source-vm', type=Path, required=True)
    p.add_argument('--fpu-softfloat', action='store_true', help='Enable 86Box SoftFloat in the private clone for accurate x87 reference results')
    p.add_argument('--system-root', type=Path, help='Copy core Win98 DLLs and their paired USER/GDI/KRNL386 modules into the private VM')
    p.add_argument('--results', type=Path, required=True)
    p.add_argument('--boot-timeout', type=int, default=180)
    p.set_defaults(func=lambda a: __import__('wine_conformance_86box').run_reference(a))
    p = sub.add_parser('compare')
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--candidate', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    p.set_defaults(func=compare)
    args = parser.parse_args()
    try:
        args.func(args)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
