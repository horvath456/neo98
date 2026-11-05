#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run each pinned ReactOS SEH case in a fresh native Win32 process."""
import argparse
import copy
from pathlib import Path
import re
import shutil
import subprocess

import conformance_campaign as campaign
import wine_conformance as wine


def select_entry(data, name):
    entry = re.search(r'START_TEST\(\s*'+name+r'\s*\)\s*\{', data)
    if not entry:
        raise ValueError('Missing expected SEH entry')
    before, body = data[:entry.end()], data[entry.end():]
    if name == 'ms_seh':
        calls = re.findall(r'run_test\((seh\d{4})\);', body)
        if len(calls) != 58 or len(set(calls)) != 58:
            raise ValueError('Pinned MS SEH call list changed')
        body = re.sub(r'run_test\((seh\d{4})\);',
                      lambda m: 'if (argc < 3 || !strcmp(argv[2], "'+m[1]+'")) '+m[0], body)
    elif name == 'pseh':
        calls = re.findall(r'USE_TEST\((test_\w+)\)', body)
        if len(calls) != 104 or len(set(calls)) != 104:
            raise ValueError('Pinned PSEH call list changed')
        anchor = 'ok(call_test(testsuite[i].func), "%s failed\\n", testsuite[i].name);'
        if body.count(anchor) != 1:
            raise ValueError('Pinned PSEH assertion changed')
        body = body.replace(anchor, 'if (argc < 3 || !strcmp(argv[2], testsuite[i].name)) '+anchor)
    else:
        raise ValueError('Unsupported entry')
    result = before+' int argc; char **argv; argc = winetest_get_mainargs(&argv);'+body
    if result.count('\n') != data.count('\n'):
        raise ValueError('Source assertion line numbers changed')
    return calls, result


def split(out, destination, cc, timeout):
    manifest = wine.verified_manifest(out)
    if manifest.get('fixtures'):
        raise ValueError('SEH isolation expects no external fixtures')
    destination.mkdir(parents=True, exist_ok=False)
    (destination/'bin').mkdir()
    for asset, sha in manifest.get('assets', {}).items():
        if wine.digest(out/'bin'/asset) != sha:
            raise ValueError('Changed source asset: '+asset)
        shutil.copyfile(out/'bin'/asset, destination/'bin'/asset)
    groups = []
    for name in ('ms_seh', 'pseh'):
        original = next(g for g in manifest['groups'] if g['name'] == 'ros_compiler_'+name)
        compile_command = list(original['compile_command'])
        source = Path(compile_command[compile_command.index('-c')+1])
        if wine.digest(source) != original['translation_unit_sha256']:
            raise ValueError('Adapted SEH source changed since its recorded build')
        calls, selected = select_entry(source.read_text(), name)
        unit = destination/(name+'.c')
        unit.write_text(selected)
        obj = destination/(name+'.o')
        previous_obj = compile_command[compile_command.index('-o')+1]
        compile_command[compile_command.index(str(source))] = str(unit)
        compile_command[compile_command.index('-o')+1] = str(obj)
        result = subprocess.run(compile_command, capture_output=True, text=True)
        (destination/(name+'-compile.log')).write_text(result.stdout+result.stderr)
        result.check_returncode()
        campaign.prepare_seh_object(obj)
        link_command = list(original['build_command'])
        link_command[link_command.index(previous_obj)] = str(obj)
        executable = destination/'bin'/(name+'.exe')
        link_command[link_command.index('-o')+1] = str(executable)
        result = subprocess.run(link_command, capture_output=True, text=True)
        (destination/(name+'-link.log')).write_text(result.stdout+result.stderr)
        result.check_returncode()
        details = wine.pe_details(executable)
        for case in calls:
            filename = 'SEH%05d.EXE' % (len(groups)+1)
            shutil.copyfile(executable, destination/'bin'/filename)
            group = dict(original, name=original['name']+'__'+case, exe=filename,
                         arguments=name+' '+case, timeout_ms=timeout*1000,
                         sha256=wine.digest(executable), **details)
            group.update(compile_command=compile_command, build_command=link_command,
                         entry_selection={'case': case, 'mode': 'fresh process per original SEH case',
                                          'original_group': original['name'],
                                          'original_translation_sha256': wine.digest(source),
                                          'translation_unit_sha256': wine.digest(unit)})
            groups.append(group)
        executable.unlink()
    result = copy.deepcopy(manifest)
    result['groups'] = groups
    result['isolation'] = {'tool_sha256': wine.digest(Path(__file__)),
                           'source_manifest_sha256': wine.digest(out/'manifest.json'),
                           'test_header_sha256': wine.digest(out/'include/wine/test.h'),
                           'scope': 'Same case bodies, wrappers and assertions; no cross-case process state'}
    result['supervisor_sha256'] = campaign.supervisor(destination, groups, cc, manifest['flags'])
    result['supervisor_source_sha256'] = wine.digest(wine.SUPPORT/'supervisor.c')
    wine.save(destination/'manifest.json', result)
    print(destination, len(groups), 'isolated SEH cases')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path('build/conformance-campaign/reactos'))
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--cc', default='i686-w64-mingw32-gcc')
    parser.add_argument('--timeout', type=int, default=15)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('timeout must be positive')
    split(args.out.resolve(), args.destination.resolve(), args.cc, args.timeout)
