#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The write layer of a game machine against a plain drive, and what it costs.

compare (the default): for each seed a tree of folders and files is made,
copied twice, and tools/test_layer_random.cpp runs the same random sequence
of file operations on one copy as a plain writable drive C: and on the other
as a read-only drive with a write layer above it. Every call must answer the
same and the visible tree must be the same at the end. A failure names its
seed; `--seed N` runs that one again and keeps its folders.

After a layered run the base folder is compared with its copy from before,
file for file: the layer never writes below itself.

Before the seeds tools/test_layer_rules.cpp runs: the rules of the layer no
comparison with a plain drive shows (aliases against a drive below that
changes, open files, changes below while the layer is in use, the DOS
machine's way into the layer).

--bench: the processor time (not the wall clock) to list and to read a tree of 6000 small files in 60
directories and one directory of 3000 files, and a tree of six files of
48 MiB, as a plain drive and through an empty layer; the median of
--rounds runs with the least and the most seen. `--baseline REV` builds the same program from the sources of
that revision as well, to show what the change did to a plain drive.

    build/analysis-venv/bin/python tools/test_layer_random.py [--seeds 12] [--operations 900] [--seed N] [--bench] [--baseline REV]

Results in build/layer-random/results.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import FAST_FLAGS, FLAGS, ROOT  # noqa: E402

DIRECTORIES = ['ALPHA', 'Beta Dir Long Name', 'ALPHA/INNER', 'gamma', 'ALPHA/INNER/Deep Folder',
               'Beta Dir Long Name/Beta Dir Long Sub']
FILES = ['ONE.TXT', 'two.txt', 'Three Long Name.txt', 'Three Long Name Second.txt', 'Three Long Name Third.text',
         'data.bin', 'READ ME', 'x.y.z', 'UPPER.DAT', 'Mixed.Case']


def build(out, sources=ROOT, name='test-layer-random', flags=FLAGS, program='test_layer_random.cpp'):
    """The comparison runs with the sanitizers, the measurement as the host is built."""
    binary = out / name
    subprocess.run(['clang++', *flags, '-I' + str(sources / 'src'), str(ROOT / 'tools' / program),
                    str(sources / 'src/files.cpp'), str(sources / 'src/system_tree.cpp'), str(sources / 'src/volume.cpp'),
                    '-o', str(binary)], cwd=ROOT, env=neo98.tool_environment(), check=True)
    return binary


def make_tree(folder, seed):
    """Folders and files a program installed: about two thirds of the names
    the operations use, and one file larger than the store reads at once."""
    choice = random.Random(seed)
    folder.mkdir(parents=True)
    for directory in DIRECTORIES:
        if choice.random() < 0.8:
            (folder / directory).mkdir(parents=True, exist_ok=True)
    for directory in [''] + DIRECTORIES:
        if not (folder / directory).is_dir():
            continue
        for name in FILES:
            if choice.random() < 0.6:
                (folder / directory / name).write_bytes(choice.randbytes(choice.randrange(0, 4000)))
    (folder / 'gamma').mkdir(exist_ok=True)
    (folder / 'gamma' / 'big.bin').write_bytes(choice.randbytes(5 * 1024 * 1024 + 123))


def digest(folder):
    listing = {}
    for entry in sorted(folder.rglob('*')):
        listing[str(entry.relative_to(folder))] = ('directory' if entry.is_dir()
                                                   else hashlib.sha256(entry.read_bytes()).hexdigest())
    return listing


def compare(binary, out, seed, operations, keep=False):
    work = out / f'seed-{seed}'
    shutil.rmtree(work, ignore_errors=True)
    make_tree(work / 'start', seed)
    subprocess.run([str(binary), 'prepare', str(work / 'start')], check=True, capture_output=True)
    for name in ('plain', 'base'):
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(work / 'start'), str(work / name)], check=True)
    before = digest(work / 'base')
    # Every second seed copies up without the block clone of APFS, as a
    # medium does that has none.
    environment = dict(os.environ, NEO98_COPY_PLAIN='1' if seed % 2 == 0 else '0')
    result = subprocess.run([str(binary), 'compare', str(seed), str(operations), str(work / 'plain'), str(work / 'base'),
                             str(work / 'layer')], capture_output=True, text=True, errors='replace', env=environment)
    untouched = digest(work / 'base') == before
    summary = re.search(r'LAYER RANDOM seed=\d+ operations=\d+ lines=(\d+) succeeded=(\d+) different=(\d+) (PASS|FAIL)',
                        result.stdout)
    okay = result.returncode == 0 and summary and summary[4] == 'PASS' and untouched
    if not okay:
        print(result.stdout + result.stderr, end='')
        print(f'seed {seed}: FAILED (base untouched: {untouched}); again with --seed {seed}, folders in {work}')
    elif not keep:
        shutil.rmtree(work, ignore_errors=True)
    return {'seed': seed, 'passed': bool(okay), 'base_untouched': untouched,
            'lines': int(summary[1]) if summary else 0, 'succeeded': int(summary[2]) if summary else 0,
            'different': int(summary[3]) if summary else -1}


def bench_tree(folder):
    shutil.rmtree(folder, ignore_errors=True)
    choice = random.Random(98)
    for d in range(60):
        directory = folder / f'Folder Number {d:02d}'
        directory.mkdir(parents=True)
        for f in range(100):
            name = f'File Long Name {f:03d}.dat' if f % 2 else f'F{f:03d}.DAT'
            (directory / name).write_bytes(choice.randbytes(choice.randrange(200, 6000)))
    large = folder / 'LARGE'
    large.mkdir()
    for f in range(3000):
        (large / (f'Entry With A Long Name {f:04d}.txt' if f % 3 == 0 else f'E{f:04d}.TXT')).write_bytes(b'x' * (f % 300))


def bench_tree_large(folder):
    """A few large files, as a game's pack files: six of 48 MiB."""
    shutil.rmtree(folder, ignore_errors=True)
    (folder / 'Pack Files').mkdir(parents=True)
    block = random.Random(98).randbytes(1 << 20)
    for f in range(6):
        with (folder / 'Pack Files' / (f'Pack Number {f}.dat' if f % 2 else f'PACK{f}.DAT')).open('wb') as file:
            for _ in range(48):
                file.write(block)


def bench(binaries, out, rounds=9, tree=bench_tree):
    work = out / 'bench'
    tree(work / 'start')
    subprocess.run([str(binaries['now']), 'prepare', str(work / 'start')], check=True, capture_output=True)
    # Round by round through every program and mode, so that what else the
    # machine does meanwhile falls on all of them alike.
    runs = {}
    for _ in range(rounds):
        for label, binary in binaries.items():
            for mode in ('plain', 'layer'):
                for name in ('plain', 'base', 'layer'):
                    shutil.rmtree(work / name, ignore_errors=True)
                subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(work / 'start'), str(work / ('plain' if mode == 'plain' else 'base'))],
                               check=True)
                command = ([str(binary), 'bench', 'plain', str(work / 'plain')] if mode == 'plain'
                           else [str(binary), 'bench', 'layer', str(work / 'base'), str(work / 'layer')])
                text = subprocess.run(command, capture_output=True, text=True, check=True).stdout
                found = re.search(r'files=(\d+) bytes=(\d+) mount=([\d.]+) list=([\d.]+) read=([\d.]+) list_again=([\d.]+) '
                                  r'read_again=([\d.]+)', text)
                runs.setdefault(f'{label} {mode}', []).append([float(found[i]) for i in range(3, 8)] + [int(found[1]), int(found[2])])
    results = {}
    for key, measured in runs.items():
        # The median of each figure, and the least and the most seen.
        figures = {}
        for i, name in enumerate(('mount_ms', 'list_ms', 'read_ms', 'list_again_ms', 'read_again_ms')):
            values = sorted(run[i] for run in measured)
            figures[name] = values[len(values) // 2]
            figures[name + '_range'] = [values[0], values[-1]]
        results[key] = dict(figures, files=measured[0][5], bytes=measured[0][6])
    shutil.rmtree(work, ignore_errors=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seeds', type=int, default=12)
    parser.add_argument('--operations', type=int, default=900)
    parser.add_argument('--seed', type=int)
    parser.add_argument('--bench', action='store_true')
    parser.add_argument('--baseline')
    parser.add_argument('--rounds', type=int, default=9)
    args = parser.parse_args()
    out = neo98.output_directory('layer-random')
    binary = build(out)
    report = {}
    if not args.bench and args.seed is None:
        # What no comparison with a plain drive shows: aliases against a
        # drive below that changes, files that stay open, changes below
        # while the layer is in use, the DOS machine's way into the layer.
        rules = build(out, name='test-layer-rules', program='test_layer_rules.cpp')
        work = out / 'rules'
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True)
        result = subprocess.run([str(rules), str(work)], capture_output=True, text=True, errors='replace')
        summary = re.search(r'LAYER RULES passed=(\d+) failed=(\d+) (PASS|FAIL)', result.stdout)
        report['rules'] = {'passed': int(summary[1]) if summary else 0, 'failed': int(summary[2]) if summary else -1,
                           'lines': result.stdout.splitlines()}
        print('\n'.join(line for line in result.stdout.splitlines() if not line.startswith('ok  ')) + result.stderr, end='\n')
        if result.returncode or not summary or summary[3] != 'PASS':
            (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
            print(f'layer rules: FAILED, folders in {work}')
            return 1
        shutil.rmtree(work, ignore_errors=True)
    if args.bench:
        binaries = {'now': build(out, name='test-layer-random-fast', flags=FAST_FLAGS)}
        if args.baseline:
            old = out / 'baseline-sources'
            shutil.rmtree(old, ignore_errors=True)
            old.mkdir()
            archive = subprocess.run(['git', 'archive', args.baseline, 'src', 'tools'], cwd=ROOT, capture_output=True,
                                     check=True).stdout
            subprocess.run(['tar', '-x', '-C', str(old)], input=archive, check=True)
            # The program of today against the store and the provider of then.
            shutil.copyfile(ROOT / 'tools/test_layer_random.cpp', old / 'tools/test_layer_random.cpp')
            binaries[args.baseline] = out / 'test-layer-random-baseline'
            subprocess.run(['clang++', *FAST_FLAGS, '-DNEO98_LAYER_BASELINE', '-I' + str(old / 'src'),
                            str(old / 'tools/test_layer_random.cpp'), str(old / 'src/files.cpp'),
                            str(old / 'src/system_tree.cpp'), str(old / 'src/volume.cpp'), '-o',
                            str(binaries[args.baseline])], cwd=ROOT, env=neo98.tool_environment(), check=True)
        report['load'] = os.getloadavg()
        print('load averages %.2f %.2f %.2f' % report['load'])
        for name, tree in (('bench', bench_tree), ('bench_large', bench_tree_large)):
            report[name] = bench(binaries, out, args.rounds, tree)
            print('9000 small files:' if name == 'bench' else 'six files of 48 MiB:')
            for label, figures in report[name].items():
                spread = lambda key: f"{figures[key]:.1f} ({figures[key + '_range'][0]:.1f}-{figures[key + '_range'][1]:.1f})"
                print(f"  {label:18s} files={figures['files']} list={spread('list_ms')} read={spread('read_ms')} "
                      f"list again={spread('list_again_ms')} read again={spread('read_again_ms')} ms")
    else:
        seeds = [args.seed] if args.seed is not None else list(range(1, args.seeds + 1))
        report['runs'] = [compare(binary, out, seed, args.operations, keep=args.seed is not None) for seed in seeds]
        for run in report['runs']:
            print(f"{'ok  ' if run['passed'] else 'FAIL'} seed {run['seed']}: {run['lines']} lines, "
                  f"{run['succeeded']} calls succeeded, {run['different']} differ, base untouched: {run['base_untouched']}")
    (out / ('bench.json' if args.bench else 'results.json')).write_text(json.dumps(report, indent=2) + '\n')
    failed = [run['seed'] for run in report.get('runs', []) if not run['passed']]
    if failed:
        print(f'layer random: seeds {failed} failed')
        return 1
    print('layer random: passed' if not args.bench else 'layer bench: done')
    return 0


if __name__ == '__main__':
    sys.exit(main())
