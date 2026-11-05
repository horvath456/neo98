#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run an application from tools/app_programs.py in the SDL2 host window.

`python3 neo98.py` and `neo98.py run` land here; without --app or --exe the
host starts Explorer. --build-only builds build/native-app/neo98-host and
stops, which is `neo98.py build`.

Drive C: is the Win98 folder `python3 neo98.py setup --iso...` created once
(build/win98-c, README.md). A profile only names the program; the
host starts it by its guest path out of that folder, and the loader finds every
DLL there the way Win98 searches. Nothing is imported or copied at start.

"""
from pathlib import Path
import argparse, subprocess, sys
ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'neo98.py').exists())
sys.path.insert(0, str(ROOT / 'tools'))
import win98_c


def run_logged(command):
    """Run the host; every line also goes to build/host-session.log so a bug
    report can be checked afterwards without the terminal. The previous run
    moves to host-session.prev.log."""
    log_path = ROOT / 'build/host-session.log'
    previous = ROOT / 'build/host-session.prev.log'
    if log_path.exists():
        try:
            log_path.replace(previous)
        except FileNotFoundError:
            pass
    print(f'Protokoll: {log_path}', flush=True)
    with log_path.open('wb') as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in process.stdout:
            log.write(line)
            log.flush()
            sys.stdout.buffer.write(line)
            sys.stdout.buffer.flush()
        return process.wait()


def machine_runs(drive_c):
    """Whether a host already runs on this drive C: (tools/host_single.h): it
    holds a lock on the file <folder>.host-lock beside the folder."""
    import fcntl
    lock = Path(str(Path(drive_c).resolve()) + '.host-lock')
    try:
        with lock.open('r') as handle:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                return True
    except OSError:
        pass
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', default='explorer')
    parser.add_argument('--exe', help='start any program without a profile, by guest path '
                                      '("C:\\\\WINDOWS\\\\SOL.EXE") out of the Win98 folder, '
                                      'or by host path')
    parser.add_argument('--system-tree', action='store_true',
                        help='accepted for older command lines; the Win98 folder is always there')
    parser.add_argument('--command', help='guest command line')
    parser.add_argument('--net-listen', metavar='ADDRESS',
                        help='guest listener address, default 127.0.0.1; any allows LAN peers')
    parser.add_argument('--scale', type=int)
    parser.add_argument('--desktop', metavar='WIDTHxHEIGHT',
                        help='size of the Windows desktop instead of the one the registry names')
    parser.add_argument('--quiet', action='store_true', help='hide the per-call APP lines')
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--build-only', action='store_true',
                        help='build build/native-app/neo98-host and stop')
    from host_build_options import add_arguments, configure
    add_arguments(parser)
    # Drive C: is a host directory; these reach neo98-host unchanged.
    #
    parser.add_argument('--drive-c', help='host directory behind drive C:, default the Win98 folder')
    parser.add_argument('--drive', action='append', default=[],
                        help='mount another host directory, as in D=/pfad')
    parser.add_argument('--cdrom', help='mount a host directory, a CUE sheet or an ISO file as a read-only CD-ROM drive')
    parser.add_argument('--ram-drive', action='store_true', help='use the volatile RAM volume instead')
    parser.add_argument('--registry', help='file the registry changes are written to')
    parser.add_argument('--no-registry-file', action='store_true', help='do not store registry changes')
    # A persistent writable clone of the program collection is mounted as D:
    # unless the caller takes that letter itself.
    parser.add_argument('--no-programs-drive', action='store_true',
                        help='leave D: empty instead of mounting the program collection')
    parser.add_argument('--programs-drive-writable', action='store_true',
                        help='deprecated: the default already uses a persistent writable APFS clone')
    parser.add_argument('--programs-drive-readonly', action='store_true',
                        help='mount the source collection read only, so programs that save beside their EXE may fail')
    parser.add_argument('--seamless', action='store_true',
                        help='start in the seamless view: the windows of the guest beside the '
                             'host windows ')
    parser.add_argument('--no-seamless', action='store_true', help='start with the guest desktop in one window')
    parser.add_argument('--game-machine', metavar='FOLDER', help='start a game machine with its own write layers')
    args = parser.parse_args()
    try:
        target, gpu = configure(args.target, args.gpu)
    except RuntimeError as error:
        parser.error(str(error))
    if args.build_only:
        from native_app import build_host
        print(f'Host {target}, GPU {gpu}: {build_host(vulkan=gpu == "vulkan")}', flush=True)
        if target == 'linux-x64':
            from native_app import build_kms_host
            print(f'KMS host: {build_kms_host(vulkan=gpu == "vulkan")}', flush=True)
        return 0
    folder = win98_c.require()
    drives = []
    # The folder is drive C: itself. Another C: or the RAM volume gets the
    # folder as the read-only tree below it.
    if args.drive_c or args.ram_drive:
        drives += ['--system-tree', str(folder)]
    if args.drive_c:
        drives += ['--drive-c', args.drive_c]
    for mount in args.drive:
        drives += ['--drive', mount]
    import programs_drive
    if not args.no_programs_drive and not args.ram_drive and not args.cdrom \
            and not programs_drive.mounts_letter(args.drive):
        if args.programs_drive_readonly:
            mounted = programs_drive.mount_arguments()
            drives += mounted
            if mounted:
                print(f'Program collection {programs_drive.COLLECTION} as drive '
                      f'{programs_drive.LETTER}:, read only.', flush=True)
        else:
            mounted = programs_drive.writable_snapshot()
            drives += mounted
            if mounted:
                print(f'Program collection {programs_drive.COLLECTION} as drive '
                      f'{programs_drive.LETTER}:, persistent writable APFS clone.', flush=True)
    if args.cdrom:
        drives += ['--cdrom', args.cdrom]
    if args.ram_drive:
        drives.append('--ram-drive')
    if args.registry:
        drives += ['--registry', args.registry]
    if args.no_registry_file:
        drives.append('--no-registry-file')
    from app_programs import application
    from native_app import build_host
    binary = ROOT / 'build/native-app/neo98-host' if args.no_build else build_host(vulkan=gpu == 'vulkan')
    if args.game_machine:
        command = [str(binary), '--game-machine', args.game_machine]
        if args.scale is not None: command += ['--scale', str(args.scale)]
        if args.net_listen: command += ['--net-listen', args.net_listen]
        if args.seamless: command.append('--seamless')
        if args.no_seamless: command.append('--no-seamless')
        return run_logged(command)
    font = str(ROOT / 'build/generated/notepad-fixedsys.fnt')
    if args.exe:
        exe = args.exe
        hint = f'Original Win98 program {args.exe} on the native host.'
    else:
        entry = application(args.app)
        if not entry.get('source'):
            raise SystemExit(f'{args.app} has no program for the host window')
        source = win98_c.path(entry['source'], folder)
        if source is None:
            raise SystemExit(f"{entry['source']} is missing from Win98 folder {folder}")
        exe = 'C:\\' + str(source.relative_to(folder)).replace('/', '\\')
        hint = entry.get('hint', '')
    command = [str(binary), '-', font, '--exe', exe, *drives]
    if args.net_listen: command += ['--net-listen', args.net_listen]
    if args.scale is not None: command += ['--scale', str(args.scale)]
    if args.desktop:
        command += ['--desktop', args.desktop]
    if args.command:
        command += ['--command', args.command]
    if args.quiet:
        command.append('--quiet')
    if args.seamless:
        command.append('--seamless')
    if args.no_seamless:
        command.append('--no-seamless')
    # The hints of app_programs.py describe the Pi kernel under QEMU.
    platform = 'Linux x64' if sys.platform.startswith('linux') else 'macOS'
    menu_hint = 'Ctrl+Shift+M opens the machine menu.' if sys.platform.startswith('linux') else 'Use the Machine menu for restart, media and reports.'
    print(hint if args.exe else
          f"Windows 98 {entry['window']} in a {platform} host window. {menu_hint} Close the window to exit.", flush=True)
    if not args.ram_drive and machine_runs(args.drive_c or folder):
        # The host hands the program to the one that runs, brings its window
        # to the front and ends; the log of that session stays.
        return subprocess.run(command, cwd=ROOT).returncode
    return run_logged(command)


if __name__ == '__main__':
    sys.exit(main())
