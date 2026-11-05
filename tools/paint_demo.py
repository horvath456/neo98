#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Draw ARM in original Paint using existing native objects; generate no runtime assets."""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import re
import struct
import subprocess
import sys
import zlib


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ppm(path):
    magic, dimensions, maximum, pixels = path.read_bytes().split(b'\n', 3)
    width, height = map(int, dimensions.split())
    if magic != b'P6' or maximum != b'255' or width != 800 or height != 600 or len(pixels) != width * height * 3:
        raise ValueError(f'Unexpected framebuffer PPM: {path}')
    return width, height, pixels


def png(path, width, height, pixels):
    """Lossless RGB encoding of captured framebuffer bytes using only the standard library."""
    def chunk(name, data):
        return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def archive(out, log):
    packets = [dict(zip(('sequence', 'x', 'y', 'buttons'), map(int, row)))
               for row in re.findall(r'INPUT n=(\d+) x=(-?\d+) y=(-?\d+) buttons=(\d+)', log)]
    (out / 'input-packets.json').write_text(json.dumps(packets, indent=2) + '\n')
    for path in sorted(out.glob('*.ppm')):
        png(path.with_suffix('.png'), *ppm(path))
    observation = {}
    match = re.search(r'^DEMO_RESULT (.*)$', log, re.M)
    if match:
        observation = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', match[1])}
        observation['state_name'] = {0: 'Running', 1: 'Idle', 2: 'Exited', 3: 'Failed'}.get(observation['state'], 'unknown')
    differences = {'framebuffer': [], 'documents': []}
    before, after = out / 'before.ppm', out / 'after.ppm'
    if before.exists() and after.exists():
        width, height, a = ppm(before)
        _, _, b = ppm(after)
        differences['framebuffer'] = [{'x': i % width, 'y': i // width,
                                        'before': list(a[i * 3:i * 3 + 3]), 'after': list(b[i * 3:i * 3 + 3])}
                                       for i in range(width * height) if a[i * 3:i * 3 + 3] != b[i * 3:i * 3 + 3]]
    for before in sorted(out.glob('document-*-before.bin')):
        after = out / before.name.replace('-before', '-after')
        if not after.exists():
            continue
        a, b = before.read_bytes(), after.read_bytes()
        if len(a) != len(b):
            raise ValueError(f'Document size changed: {before.name}')
        offsets = [i for i in range(len(a)) if a[i] != b[i]]
        differences['documents'].append({'before': before.name, 'after': after.name, 'changed_bytes': len(offsets),
                                         'offsets': offsets, 'before_values': [a[i] for i in offsets],
                                         'after_values': [b[i] for i in offsets]})
    (out / 'differences.json').write_text(json.dumps(differences, indent=2) + '\n')
    observation['framebuffer_changed_pixels'] = len(differences['framebuffer'])
    observation['document_changed_bytes'] = {d['before']: d['changed_bytes'] for d in differences['documents']}
    return observation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objects', type=Path, required=True, help='Existing native ASan/UBSan .cpp.o and fiber objects')
    parser.add_argument('--out', type=Path, required=True, help='New or empty output directory')
    parser.add_argument('--timeout', type=int, default=180, help='Demo process timeout in seconds')
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error('--timeout must be positive')
    source = Path(__file__).resolve()
    root = next((p for p in source.parents if (p / 'neo98.py').is_file()), None)
    if root is None:
        parser.error('Place this tool inside the neo98 repository')
    out, objects_dir = args.out.resolve(), args.objects.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('--out must be empty to keep earlier evidence intact')
    out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(root))
    import neo98
    env = neo98.tool_environment()
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    report = {'status': 'FAIL', 'runtime_scope': 'Current canonical headers and caller-supplied native objects on the host; not a Pi boot test.',
              'source_object_provenance': 'Source and object hashes are recorded separately; use objects built from these canonical sources.',
              'saving_reopening_undo_tested': False, 'overlay_used': False}
    inputs = []
    exit_code = 1
    try:
        cpp = source.with_suffix('.cpp')
        report['tool_sha256'] = {str(p): sha(p) for p in (source, cpp)}
        tree = ast.parse((root / 'tools/native_app.py').read_text())
        names = next(ast.literal_eval(n.value) for n in ast.walk(tree) if isinstance(n, ast.Assign) and
                     any(isinstance(t, ast.Name) and t.id == 'sources' for t in n.targets))
        objects = [objects_dir / (name + '.cpp.o') for name in names] + [objects_dir / 'fiber_aarch64.S.o']
        report['object_sha256'] = {str(p): sha(p) for p in objects}
        report['runtime_sha256'] = {str(p.relative_to(root)): sha(p) for p in sorted((root / 'src').iterdir())
                                    if p.suffix in ('.h', '.cpp', '.inc', '.S')}
        library = root / 'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib' / (
            'libunicorn.2.dylib' if sys.platform == 'darwin' else 'libunicorn.so.2')
        if not library.exists():
            candidates = sorted((root / 'build/analysis-venv/lib').glob('python*/site-packages/unicorn/lib/' + library.name))
            if not candidates:
                raise FileNotFoundError('Existing Unicorn library not found in build/analysis-venv')
            library = candidates[-1]
        programs = [root / 'build/programs' / name for name in
                    ('MSPAINT.EXE', 'MFC42.DLL', 'MSVCRT.DLL', 'OLE32.DLL', 'OLEAUT32.DLL', 'IMM32.DLL', 'RICHED20.DLL')]
        font = root / 'build/generated/notepad-fixedsys.fnt'
        inputs = programs + [font, library]
        report['input_sha256'] = {str(p): sha(p) for p in inputs}
        runner = out / 'paint-demo'
        command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-I', str(root / 'src'),
                   str(cpp), *map(str, objects), str(library), '-Wl,-rpath,' + str(library.parent), '-o', str(runner)]
        linked = subprocess.run(command, env=env, capture_output=True, text=True)
        (out / 'link.log').write_text(linked.stdout + linked.stderr)
        linked.check_returncode()
        report['runner_sha256'] = sha(runner)
        command = [str(runner), str(programs[0]), str(font), *map(str, programs[1:])]
        try:
            result = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=args.timeout)
            log = result.stdout + result.stderr
            report['runner_exit_code'] = result.returncode
        except subprocess.TimeoutExpired as exc:
            def decode(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            log = decode(exc.stdout) + decode(exc.stderr)
            report['runner_exit_code'] = 124
            report['error'] = 'Demo timed out'
        (out / 'probe.log').write_text(log)
        report['observation'] = archive(out, log)
        report['original_inputs_unchanged'] = all(sha(p) == report['input_sha256'][str(p)] for p in inputs)
        obs = report['observation']
        good = (report['runner_exit_code'] == 0 and obs.get('success') == 1 and obs.get('canvas_pixels', 0) > 0 and
                any(obs['document_changed_bytes'].values()) and report['original_inputs_unchanged'])
        report['status'] = 'PASS' if good else 'FAIL'
        exit_code = 0 if good else 1
    except (OSError, ValueError, StopIteration, subprocess.CalledProcessError) as exc:
        report['error'] = str(exc)
    finally:
        provenance = {key: report.pop(key) for key in ('tool_sha256', 'object_sha256', 'runtime_sha256', 'input_sha256') if key in report}
        (out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        report['provenance'] = 'provenance.json'
        report['output_sha256'] = {p.name: sha(p) for p in sorted(out.iterdir()) if p.is_file() and p.name != 'results.json'}
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': report['status'], 'out': str(out), 'observation': report.get('observation'),
                      'error': report.get('error')}, indent=2))
    return exit_code


if __name__ == '__main__':
    sys.exit(main())
