#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check guest TCP/UDP listeners on loopback and a host interface, without remote peers.

Uses the standalone Linux CLI and original Win98 Winsock DLLs. No firewall,
router, host Wi-Fi or user Windows folder is changed.
"""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(1, str(Path(__file__).resolve().parent))
import neo98
from native_app import ROOT
import win98_c

OUT = ROOT / 'build/network-listen'
IMPORTS = {
    'KERNEL32': {'OutputDebugStringA': 4, 'ExitProcess': 4, 'GetCommandLineA': 0},
    'WSOCK32': {'WSAStartup': 8, 'socket': 12, 'bind': 12, 'listen': 8, 'accept': 12,
                'recv': 16, 'send': 16, 'recvfrom': 24, 'sendto': 24, 'closesocket': 4},
}


def fixture(env):
    libraries = []
    for dll, imports in IMPORTS.items():
        definition, library = OUT / (dll + '.def'), OUT / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in imports.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libraries.append(str(library))
    obj, exe = OUT / 'listen.obj', OUT / 'LISTEN.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/network_listen_fixture.c'), '-o', str(obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows,4.0', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=env, check=True)
    return exe


def free_port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('0.0.0.0', 0))
        return sock.getsockname()[1]


def wait_ready(process, path):
    until = time.monotonic() + 20
    while time.monotonic() < until:
        text = path.read_text(errors='replace')
        if 'LISTEN READY' in text:
            return
        assert process.poll() is None, text[-3000:]
        time.sleep(.05)
    raise AssertionError(path.read_text(errors='replace')[-3000:])


def main():
    win98_c.use_pristine()
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    for key in ('NEO98_NET_LISTEN', 'NEO98_NET_OFFLINE', 'NEO98_HOST_INPUT', 'NEO98_LOG_TO_FILE'):
        env.pop(key, None)
    exe = fixture(env)
    binary = ROOT / 'build/native-app/neo98-cli'
    # Choosing a route does not send a datagram. Exercise an actual local IPv4
    # interface rather than accepting a loopback-only test as proof of LAN bind.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect(('192.0.2.1', 9))
        address = probe.getsockname()[0]
    assert not address.startswith('127.'), 'No non-loopback IPv4 interface available'
    cases = [('default', [], {}, False), ('cli-any', ['--net-listen', 'any'], {}, True),
             ('cli-address', ['--net-listen', address], {}, True),
             ('cli-wildcard', ['--net-listen', '0.0.0.0'], {}, True),
             ('env-any', [], {'NEO98_NET_LISTEN': 'any'}, True),
             ('env-local', ['--net-listen', 'any'], {'NEO98_NET_LISTEN': '127.0.0.1'}, False)]
    results = {}
    for name, options, extra_env, lan in cases:
        tcp_port, udp_port = free_port(socket.SOCK_STREAM), free_port(socket.SOCK_DGRAM)
        path = OUT / (name + '.log')
        drive = OUT / (name + '-c')
        shutil.rmtree(drive, ignore_errors=True)
        with path.open('w') as log:
            process = subprocess.Popen([str(binary), '--exe', str(exe), '--ram-drive', '--no-registry-file',
                                        '--system-tree', str(win98_c.FOLDER), '--drive-c', str(drive),
                                        '--verbose', '--time-limit', '30', '--command',
                                        f'LISTEN.EXE {tcp_port} {udp_port}', *options],
                                       cwd=ROOT, env=dict(env, **extra_env), stdout=log, stderr=log)
            try:
                wait_ready(process, path)
                target = address if lan else '127.0.0.1'
                if not lan:
                    with socket.socket() as rejected:
                        rejected.settimeout(1)
                        assert rejected.connect_ex((address, tcp_port)) != 0, name
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as rejected:
                        rejected.settimeout(.3)
                        rejected.sendto(b'nope', (address, udp_port))
                        try:
                            rejected.recv(16)
                            raise AssertionError('Loopback-only UDP answered on the LAN address')
                        except socket.timeout:
                            pass
                with socket.create_connection((target, tcp_port), timeout=3) as tcp:
                    tcp.sendall(b'ping')
                    with tcp.makefile('rb') as reply:
                        assert reply.read(4) == b'ping', name
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                    udp.settimeout(3)
                    udp.sendto(b'pong', (target, udp_port))
                    assert udp.recv(16) == b'pong', name
                process.wait(timeout=10)
                assert process.returncode == 0, path.read_text()[-3000:]
                assert 'LISTEN PASS' in path.read_text(), name
                results[name] = {'tcp': True, 'udp': True, 'lan': lan}
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
    invalid = subprocess.run([str(binary), '--net-listen', 'invalid'], env=env, capture_output=True)
    assert invalid.returncode == 2
    invalid_env = subprocess.run([str(binary), '--ram-drive', '--no-registry-file',
                                  '--system-tree', str(win98_c.FOLDER), '--exe', str(exe)],
                                 env=dict(env, NEO98_NET_LISTEN='invalid'), capture_output=True)
    assert invalid_env.returncode != 0
    assert 'Invalid guest-network listen address' in invalid_env.stderr.decode()
    assert not win98_c.pristine_problems(win98_c.FOLDER)
    (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print('NETWORK LISTEN PASS: guest TCP/UDP, default local, CLI/address/environment opt-in')


if __name__ == '__main__':
    main()
