#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Check native PE32 terminal input, output, exit code and runtime dependencies."""
if __name__ == '__main__':
    import sys as _sys
    if not _sys.platform.startswith('linux'):
        print('SKIP: tests the Linux command-line host')  # Linux only; skipped elsewhere
        _sys.exit(77)
from pathlib import Path
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
import native_app


def main():
    out = ROOT / 'build/native-linux'
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    exports = {'GetStdHandle': 4, 'WriteFile': 20, 'ReadFile': 20, 'ExitProcess': 4,
               'GetCommandLineA': 0, 'GetCurrentDirectoryA': 8}
    definition = out / 'cli-kernel32.def'
    definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n' + ''.join(f'{name}@{size}\n' for name, size in exports.items()))
    library, obj, exe = out / 'cli-kernel32.lib', out / 'cli-fixture.obj', out / 'CLI.EXE'
    commands = [
        ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library],
        ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
         '-c', ROOT / 'tools/linux_cli_fixture.c', '-o', obj],
        ['lld-link', '/machine:x86', '/entry:mainCRTStartup', '/subsystem:console', '/nodefaultlib', '/safeseh:no',
         '/fixed', '/timestamp:0', obj, library, '/out:' + str(exe)],
    ]
    for command in commands:
        subprocess.run(list(map(str, command)), env=env, check=True)
    binary = ROOT / 'build/native-app/neo98-cli'
    if not binary.exists():
        binary = native_app.build_fast('neo98_cli.cpp', 'neo98-cli')
    inputs = ('input works\n', '', 'no newline', 'Grüße aus Österreich\n', 'a' * 700 + '\n' + 'ü' * 150 + '\r\n')
    for number, stdin in enumerate(inputs):
        result = subprocess.run([str(binary), '--exe', str(exe), '--ram-drive', '--no-registry-file'],
                                cwd=ROOT, env=env, input=stdin.encode(), capture_output=True, timeout=30)
        (out / f'cli-fixture-{number}.log').write_bytes(result.stdout + result.stderr)
        assert result.returncode == 7, (result.returncode, result.stderr.decode(errors='replace'))
        expected = b'NEO98 native Linux CLI\r\n' + stdin.replace('\r\n', '\n').replace('\n', '\r\n').encode()
        assert result.stdout == expected, (number, result.stdout, expected)
        assert b'native stderr\r\n' in result.stderr, result.stderr
    dependencies = subprocess.check_output(['ldd', str(binary)], text=True).lower()
    import win98_c
    explicit = subprocess.run([str(binary), '--drive-c', str(win98_c.use_pristine()), '--exe', str(exe),
        '--ram-drive', '--no-registry-file'], env=dict(env, NEO98_WIN98_ROOT=str(out/'missing-machine')),
        input=b'explicit root\n', capture_output=True, timeout=30)
    assert explicit.returncode == 7, explicit.stderr.decode(errors='replace')
    assert explicit.stdout == b'NEO98 native Linux CLI\r\nexplicit root\r\n'
    # --directory names the guest folder the program starts in, as on macOS.
    for folder, shown in (('C:\\WINDOWS\\SYSTEM', b'C:\\WINDOWS\\SYSTEM\r\n'), ('C:\\', b'C:\\\r\n')):
        placed = subprocess.run([str(binary), '--drive-c', str(win98_c.use_pristine()), '--exe', str(exe),
            '--command', 'CLI.EXE cwd', '--directory', folder, '--ram-drive', '--no-registry-file'],
            env=env, input=b'', capture_output=True, timeout=30)
        assert placed.returncode == 7, placed.stderr.decode(errors='replace')
        assert placed.stdout == b'NEO98 native Linux CLI\r\n' + shown, (folder, placed.stdout)
    absent = subprocess.run([str(binary), '--drive-c', str(win98_c.use_pristine()), '--exe', str(exe),
        '--directory', 'C:\\NO-SUCH-FOLDER', '--ram-drive', '--no-registry-file'],
        env=env, input=b'', capture_output=True, timeout=30)
    assert absent.returncode == 7 and b'Could not open guest working directory: C:\\NO-SUCH-FOLDER' in absent.stderr, absent.stderr
    assert all(name not in dependencies for name in ('unicorn', 'fex', 'sdl', 'vulkan', 'gtk', 'gdk')), dependencies
    report = {'status': 'PASS', 'engine': 'native', 'guest_exit_code': result.returncode,
              'checks': ['hardware executes own PE32', 'stdin', 'stdout', 'stderr', 'UTF-8 and OEM text',
                         'input exceeding the console queue', 'EOF', 'line endings', 'exit code', 'explicit drive C overrides default',
                         'start directory'],
              'dependencies_exclude': ['FEX', 'Unicorn', 'SDL', 'Vulkan', 'GTK', 'GDK']}
    (out / 'cli-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print('LINUX CLI PASS: native PE32, stdin/stdout/stderr, UTF-8, long input, EOF, exit code 7, no emulator or GUI dependency')


if __name__ == '__main__':
    main()
