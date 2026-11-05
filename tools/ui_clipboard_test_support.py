# SPDX-License-Identifier: GPL-2.0-or-later
"""Private machines and serial, silent runs for the UI/clipboard regressions."""
from contextlib import contextmanager
from pathlib import Path
import fcntl
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import neo98


@contextmanager
def serial():
    lock_path = os.environ.get('NEO98_TEST_LOCK', '/tmp/neo98-test.lock')
    with open(lock_path, 'a') as lock:
        print('Waiting for the shared test lock', flush=True)
        fcntl.flock(lock, fcntl.LOCK_EX)
        subprocess.run(['df', '-h', '/System/Volumes/Data'], check=True)
        if shutil.disk_usage(ROOT).free < 8 * 1024**3:
            raise RuntimeError('STOP: fewer than 8 GiB free')
        active = subprocess.run(['pgrep', '-x', 'neo98-host'], capture_output=True, text=True)
        if active.returncode == 0:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active: ' + active.stdout.strip())
        yield


def fixture(out, source, imports, entry='mainCRTStartup', defines=()):
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    libs = []
    for dll, names in imports.items():
        definition = out / (dll + '.def')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in names.items()))
        lib = definition.with_suffix('.lib')
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(lib)], env=env, check=True)
        libs.append(str(lib))
    obj = out / 'fixture.obj'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', *defines, '-c', str(source), '-o', str(obj)], env=env, check=True)
    exe = out / 'TEST.EXE'
    subprocess.run(['lld-link', '/entry:' + entry, '/subsystem:windows,4.0', '/nodefaultlib',
                    '/machine:x86', '/timestamp:0', '/out:' + str(exe), str(obj), *libs], env=env, check=True)
    return exe


@contextmanager
def machine(out):
    out.mkdir(parents=True, exist_ok=True)
    drive = out / 'c'
    if drive.exists():
        raise RuntimeError('Remove the previous private machine before retrying: ' + str(drive))
    source = (ROOT / 'build/win98-c').resolve()
    subprocess.run(['cp', '-cR', str(source), str(drive)], check=True)
    shutil.copyfile(ROOT / 'build/win98-c.registry', out / 'c.registry')
    try:
        yield drive
    finally:
        shutil.rmtree(drive)


def host(out, drive, exe, steps='', seconds=30, extra=()):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_DEADLINE=str(seconds + 15), NEO98_HOST_PASTEBOARD='unique',
               NEO98_HOST_INPUT=steps + f';{seconds}:menu=quit',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    command = [str(ROOT / 'build/native-app/neo98-host'), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
               '--exe', exe, '--quiet', '--system-tree', str(drive), '--drive-c', str(drive),
               '--registry', str(out / 'c.registry'), *extra]
    with (out / 'host.log').open('w') as log:
        p = subprocess.Popen(command, env=env, cwd=out, stdout=log, stderr=subprocess.STDOUT)
        try:
            code = p.wait(timeout=seconds + 60)
        except subprocess.TimeoutExpired:
            p.terminate()
            p.wait(timeout=20)
            raise
    log = (out / 'host.log').read_text(errors='replace')
    assert code == 0, (code, out / 'host.log')
    assert 'NEO98 HOST failed' not in log and 'This GDI call' not in log and 'EXCEPTION c0000005' not in log, out / 'host.log'
    return log
