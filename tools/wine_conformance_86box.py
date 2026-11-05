#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Optional adapter to the existing win98-agent 86Box automation tool.

Only a new VM clone is modified. Disk transfers happen while 86Box is stopped;
the original VM, agent project and shared automation home remain untouched.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

from wine_conformance import CORE16, SYSTEM_DLLS, binaries, clone, core_hashes, digest, finish_run, fixture_timeout_ms, installation, save, tested_dlls, verified_manifest


def suite_is_running(processes, pid):
    # Win9x can immediately reuse a terminated process's ID for WMIEXE or
    # another background process. A PID alone does not identify our supervisor.
    return any(p['pid'] == pid and
               p['executable'].replace('/', '\\').upper() == r'C:\WTEST\SUITE.EXE'
               for p in processes)


def fat_image(image):
    with image.open('rb') as file:
        mbr = file.read(512)
    if mbr[510:512] != b'\x55\xaa':
        raise RuntimeError('Reference disk has no valid MBR')
    partitions = [mbr[446 + i * 16:462 + i * 16] for i in range(4)]
    fat = [p for p in partitions if p[4] in (0x01, 0x04, 0x06, 0x0b, 0x0c, 0x0e)]
    if len(fat) != 1:
        raise RuntimeError('Expected exactly one primary FAT partition in reference disk')
    return str(image) + '@@' + str(struct.unpack_from('<I', fat[0], 8)[0] * 512)


def run_reference(args):
    manifest = verified_manifest(args.out)
    dll_names = tested_dlls(manifest)
    destination = args.results.resolve()
    source = args.source_vm.resolve()
    if destination.is_relative_to(source):
        raise RuntimeError('Results must be outside the source VM')
    destination.mkdir(parents=True, exist_ok=False)
    vm = destination / 'vm'
    if not (source / '86box.cfg').is_file():
        raise RuntimeError('Source VM has no 86box.cfg')
    # Refuse a live source disk rather than taking an inconsistent copy.
    # The source is only inspected here; all mtools writes use the clone.
    source_config = source / '86box.cfg'
    for filename in re.findall(r'^hdd_\d+_fn\s*=\s*(.+)$', source_config.read_text(), re.M):
        disk = (source / filename.strip()).resolve()
        if shutil.which('lsof') and subprocess.run(['lsof', '-t', str(disk)],
                capture_output=True, text=True).stdout.strip():
            raise RuntimeError('Source disk is open; shut down the source VM first: ' + str(disk))
    clone(source, vm)
    # macOS Unix socket paths are limited to about 104 bytes. The worktree may
    # be much longer, so put only this run's transient control home in /tmp.
    home = Path(tempfile.mkdtemp(prefix='a98-wine-', dir='/tmp'))
    environment = os.environ.copy()
    environment.update(WIN98_AGENT_HOME=str(home), WIN98_AGENT_VM=str(vm),
                       WIN98_86BOX_BIN=str(args.emulator.resolve()))
    save(destination / 'control.json', {'home': str(home), 'vm': str(vm)})
    module_path = args.agent.resolve().parent / 'w98agent/agent.py'
    sys.path.insert(0, str(module_path.parent))
    spec = importlib.util.spec_from_file_location('wine_reference_agent', module_path)
    agent = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(agent)
    # The helper's configuration probes the explicitly selected emulator.
    previous = {key: os.environ.get(key) for key in ('WIN98_AGENT_HOME', 'WIN98_AGENT_VM', 'WIN98_86BOX_BIN')}
    os.environ.update({key: environment[key] for key in previous})
    try:
        share = destination / 'share'
        share.mkdir()
        agent.configure_agent_vm(vm, share)
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
    # Reject external disks: cloning a config with absolute image paths must
    # never result in booting or modifying the source VM's disk.
    config = agent.read_ini(vm / '86box.cfg')
    if args.fpu_softfloat:
        agent.ini_set(config, 'Machine', 'fpu_softfloat', 1)
        agent.write_ini(config, vm / '86box.cfg')
    for section in config.sections():
        for key, value in config.items(section):
            if re.match(r'hdd_\d+_fn$', key):
                disk = (vm / value).resolve()
                if not disk.is_relative_to(vm) or not disk.is_file():
                    raise RuntimeError('VM disk path escapes the private clone: ' + value)
    image = agent.disk_image_for_vm(vm)
    image_arg = fat_image(image)

    def mt(program, *arguments, check=True):
        return subprocess.run([program, '-i', image_arg, *map(str, arguments)], check=check,
                              capture_output=True, text=True)

    # An existing directory might contain old results or user files. Keep it
    # untouched and require a clean source VM instead of merging evidence.
    if mt('mdir', '::/WTEST', check=False).returncode == 0:
        raise RuntimeError('Source VM already contains C:\\WTEST; use a clean source snapshot')
    mt('mmd', '::/WTEST')
    mt('mmd', '::/WIN98AGT', check=False)
    mt('mcopy', '-o', agent.guest_executable(), '::/WIN98AGT/WIN98AGT.EXE')
    for name, patch in [('WIN.INI', agent.patch_win_ini), ('SYSTEM.INI', agent.patch_system_ini)]:
        path = destination / name
        mt('mcopy', '::/WINDOWS/' + name, path)
        patch(path)
        mt('mcopy', '-o', path, '::/WINDOWS/' + name)
    for name in binaries(manifest):
        path = args.out / 'bin' / name
        mt('mcopy', '-o', path, '::/WTEST/' + path.name)
    normalized = {}
    if args.system_root:
        originals = args.system_root.resolve() / 'WINDOWS/SYSTEM'
        # DirectX files stay with the installed display driver. Reports retain
        # their versions; differences must be reviewed before qualifying them.
        for name in SYSTEM_DLLS + CORE16:
            if name == 'DDRAW.DLL':
                continue
            original = next((p for p in originals.iterdir() if p.name.upper() == name), None)
            if not original:
                raise RuntimeError('Missing normalization input: ' + str(originals / name))
            mt('mcopy', '-o', original, '::/WINDOWS/SYSTEM/' + name)
            normalized[name] = digest(original)
    system = destination / 'system-provenance/WINDOWS/SYSTEM'
    system.mkdir(parents=True)
    for name in dll_names + CORE16:
        mt('mcopy', '::/WINDOWS/SYSTEM/' + name, system / name, check=False)
    extra = {'runtime': '86box-win98', 'source_vm': str(source),
             'source_config_sha256': digest(source / '86box.cfg'),
             'private_config_sha256': digest(vm / '86box.cfg'),
             'emulator_sha256': digest(args.emulator),
             'fpu_softfloat': config.get('Machine', 'fpu_softfloat', fallback='0'),
             'agent_sha256': digest(module_path),
             'guest_helper_sha256': digest(agent.guest_executable()),
             'normalized_system_dlls': normalized,
             'windows_core16_sha256': core_hashes(destination / 'system-provenance'),
             'windows_dlls': installation(destination / 'system-provenance', dll_names)}
    save(destination / 'provenance.json', extra)

    def control(*arguments, timeout=20, check=True):
        result = subprocess.run([str(args.agent.resolve()), '--json', *arguments],
            env=environment, capture_output=True, text=True, timeout=timeout)
        with (destination / 'control.log').open('a') as log:
            log.write(' '.join(arguments) + '\n' + result.stdout + result.stderr)
        if check and result.returncode:
            raise RuntimeError('win98-agent: ' + result.stdout + result.stderr)
        return json.loads(result.stdout) if result.returncode == 0 else None

    started = False
    failure = None
    try:
        started = True
        control('start', '--wait-ready', '0', timeout=30)
        deadline = time.monotonic() + args.boot_timeout
        while time.monotonic() < deadline:
            reply = control('ping', check=False)
            if reply:
                break
            time.sleep(2)
        else:
            raise RuntimeError('Private Win98 guest did not become ready before boot timeout')
        # WIN.INI starts the agent while Explorer is still running its startup
        # programs. Give that boot activity time to finish before the workload.
        time.sleep(15)
        control('screenshot', '--output', str(destination / 'desktop.png'))
        reply = control('run', r'C:\WTEST\SUITE.EXE', '--cwd', r'C:\WTEST')
        pid = int(re.search(r'PID (\d+)', reply['reply'])[1])
        deadline = time.monotonic() + sum(g['timeout_ms'] / 1000 for g in manifest['groups']) + fixture_timeout_ms(manifest) / 1000 + 120
        visual = {g['exe'].upper(): g['name'] for g in manifest['groups'] if g.get('assessment') == 'INTERACTIVE_REVIEW'}
        captured = set()
        by_exe = {g['exe'].upper(): g for g in manifest['groups']}
        active, active_since = None, time.monotonic()
        last_control_reply = time.monotonic()
        # Each test has a guest-side deadline. The host-side bound also catches
        # a hung Win98 kernel or a dead control channel.
        while time.monotonic() < deadline:
            processes = control('processes', check=False)
            if processes:
                last_control_reply = time.monotonic()
                if not suite_is_running(processes['processes'], pid):
                    break
            elif time.monotonic() - last_control_reply > 150:
                extra.update(host_timeout=True, host_timeout_group=by_exe[active]['name'] if active else None,
                             infrastructure_error='Guest control channel unresponsive for 150 seconds')
                break
            for process in (processes or {}).get('processes', []):
                exe = process['executable'].replace('\\', '/').split('/')[-1].upper()
                if exe in by_exe and process.get('parent_pid') == pid and active != exe:
                    active, active_since = exe, time.monotonic()
                if exe in visual and exe not in captured:
                    control('screenshot', '--output', str(destination/(visual[exe]+'.png')), check=False)
                    captured.add(exe)
            if active and time.monotonic() - active_since > by_exe[active]['timeout_ms']/500 + 30:
                # Win9x can wedge while TerminateProcess tears down a GUI test.
                # Bound that case too; a later fresh VM retries unreached tests.
                extra['host_timeout'] = True
                extra['host_timeout_group'] = by_exe[active]['name']
                break
            print('86BOX suite running', flush=True)
            time.sleep(5)
        else:
            extra['host_timeout'] = True
        control('screenshot', '--output', str(destination / 'finished.png'), check=False)
        # Ask Windows to flush its files and shut down before stopping 86Box.
        control('run', r'C:\WINDOWS\RUNDLL32.EXE shell32.dll,SHExitWindowsEx 1', check=False)
        time.sleep(15)
    except Exception as error:
        failure = error
        extra['infrastructure_error'] = str(error)
    finally:
        if started:
            stopped = control('stop', '--force', timeout=30, check=False)
            if not stopped or not stopped.get('stopped'):
                raise RuntimeError('Could not stop private 86Box; refusing to read its live disk')
        if (home / 'run').exists():
            shutil.copytree(home / 'run', destination / 'agent-run',
                            ignore=shutil.ignore_patterns('*.sock'))
    mt('mcopy', '::/WTEST/RUN.TSV', destination / 'RUN.TSV', check=False)
    mt('mcopy', '::/WTEST/*.LOG', destination, check=False)
    for name in dll_names + CORE16:
        mt('mcopy', '-o', '::/WINDOWS/SYSTEM/' + name, system / name, check=False)
    extra['windows_dlls_after_shutdown'] = installation(destination / 'system-provenance', dll_names)
    extra['windows_core16_sha256_after_shutdown'] = core_hashes(destination / 'system-provenance')
    result = finish_run(destination, manifest, extra)
    if failure:
        raise RuntimeError(str(failure)) from failure
    if not result.get('suite_done') or any(g['status'] not in ('PASS', 'FAIL')
            for g in result['groups'].values()):
        raise RuntimeError('Incomplete original Windows run; retain its logs and do not qualify it')
