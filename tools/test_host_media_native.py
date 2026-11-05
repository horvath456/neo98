#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The host media provider behind \\\\.\\NEO98MEDIA, with an x86 guest.

tools/host_media_fixture.c talks to the provider of tools/host_media/service.h
the way Media Center 98 does; tools/test_host_media_native.cpp runs it in a
headless runner. No run uses a real Kodi or Netflix, and none plays sound.

  units JSON of Kodi's JSON-RPC and its framing without delimiters,
             Kodi's label markup, image:// addresses, fitting a picture into
             a box
  none NEO98_TEST_KODI=none, a computer without Kodi: the state names
             the reason in words, a folder fails with the same words, playing
             is refused, a short buffer and a wrong version are refused
  missing the real search for Kodi with nothing to find (NEO98_KODI names
             a program that does not exist, nothing listens on the port):
             the same words
  catalog the fixed catalogue of tools/test_kodi_server.py (NEO98_TEST_KODI
             its port): add-ons and source at the top, Netflix > TV Shows >
             Stranger Things > Season 1, labels without markup, a poster as a
             DIB, pages of a long folder, a failing and an empty add-on folder
  play an episode: playing, paused, seek, stop from Kodi's
             notifications; the screen goes to Kodi and comes back; a video
             Kodi refuses leaves the screen with NEO98
  nonetflix Kodi without the Netflix add-on and without Widevine: the
             Netflix folder says what is missing
  launch NEO98_KODI names tools/test_kodi_server.py as Kodi's program:
             the provider starts it and connects once its port opens

    build/analysis-venv/bin/python tools/test_host_media_native.py [--only PART...]


"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import json
import os
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import neo98  # noqa: E402
import native_app  # noqa: E402

IMPORTS = {'ExitProcess': 1, 'GetLastError': 0, 'CloseHandle': 1, 'CreateFileA': 7, 'DeviceIoControl': 8,
           'Sleep': 1, 'GetTickCount': 0, 'GetCommandLineA': 0, 'VirtualAlloc': 4}
PARTS = ('units', 'none', 'missing', 'catalog', 'play', 'nonetflix', 'launch')


def fixture(out, env):
    definition, library = out / 'KERNEL32.def', out / 'KERNEL32.lib'
    definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n' + ''.join(f'{name}@{count * 4}\n' for name, count in IMPORTS.items()))
    subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
    obj, exe = out / 'fixture.obj', out / 'MEDIAFIX.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                    '-Wall', '-c', str(ROOT / 'tools/host_media_fixture.c'), '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib', '/machine:x86',
                    '/timestamp:0', '/out:' + str(exe), str(obj), str(library)], env=env, check=True)
    return exe


def runner():
    if not native_app.native_x64():
        raise SystemExit('The runner is built for Linux x64; on the Mac the provider runs in the host (tools/test_media_center_host.py).')
    from native_build import compile_objects
    gtk_flags, gtk_libs = native_app.linux_gtk_flags()
    extra = compile_objects(ROOT, [ROOT / 'tools/host_media/picture_linux.cpp'], [*native_app.FAST_FLAGS, *gtk_flags],
                            neo98.tool_environment(), ROOT / 'build/native-app/object-cache-fast')
    return native_app.build_fast('test_host_media_native.cpp', 'test-host-media-native', link_flags=gtk_libs, extra_inputs=extra)


def free_port():
    """A port bound but not listening: a connection to it is refused."""
    held = socket.socket()
    held.bind(('127.0.0.1', 0))
    return held, held.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--only', nargs='+', choices=PARTS)
    args = parser.parse_args()
    out = ROOT / 'build/host-media-native'
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    env['SDL_AUDIODRIVER'] = 'dummy'
    for name in ('NEO98_TEST_KODI', 'NEO98_KODI', 'NEO98_KODI_PORT'):
        env.pop(name, None)
    exe = fixture(out, env)
    binary = runner()
    font = str(ROOT / 'build/generated/notepad-fixedsys.fnt')
    results = {}
    for part in args.only or PARTS:
        held = server = None
        extra, part_env = [], dict(env)
        if part == 'missing':
            held, port = free_port()
            extra = [str(port)]
        if part in ('catalog', 'play', 'nonetflix'):
            import test_kodi_server
            server = test_kodi_server.start(out / f'{part}-calls.jsonl', netflix=part != 'nonetflix')
            part_env['NEO98_TEST_KODI'] = str(server.port)
        command_part = 'screens' if part == 'play' else part
        if part == 'launch':
            # Kodi's program is the test server; it opens the port the
            # provider asks for, after a second, as Kodi takes a while.
            held, port = free_port()
            held.close()
            part_env['NEO98_KODI'] = str(ROOT / 'tools/test_kodi_server.py')
            part_env['NEO98_KODI_PORT'] = str(port)
            part_env['NEO98_TEST_KODI_DELAY'] = '1'
            part_env['NEO98_TEST_KODI_LOG'] = str(out / 'launch-calls.jsonl')
            (out / 'launch-calls.jsonl').unlink(missing_ok=True)
            command_part = 'catalog'
        try:
            run = subprocess.run([str(binary), str(exe), font, command_part, *extra], env=part_env,
                                 capture_output=True, text=True, timeout=240)
        finally:
            if held: held.close()
            if server: server.stop()
        log = run.stdout + run.stderr
        (out / f'{part}.log').write_text(log)
        passed = run.returncode == 0
        if part == 'play':
            calls = [json.loads(line)['method'] for line in (out / 'play-calls.jsonl').read_text().splitlines()]
            passed &= all(m in calls for m in ('Player.Open', 'Player.PlayPause', 'Player.Seek', 'Player.Stop'))
        if part == 'launch':
            # The provider started the program and, as it started Kodi,
            # ended it with Application.Quit.
            calls = [json.loads(line)['method'] for line in (out / 'launch-calls.jsonl').read_text().splitlines()]
            passed &= 'NEO98 HOST media starting Kodi' in log and calls[-1:] == ['Application.Quit']
        results[part] = 'PASS' if passed else 'FAIL'
        print(f'{part}: {results[part]}', flush=True)
        if not passed:
            print(log[-4000:], file=sys.stderr)
    (out / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    if any(r != 'PASS' for r in results.values()):
        raise SystemExit('HOST MEDIA FAIL, see ' + str(out))
    print('HOST MEDIA PASS')


if __name__ == '__main__':
    main()
