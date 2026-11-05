#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Install the PostScript printer of the Win98 SE CD into drive C: the way "Add Printer" does.

The Win98 wizard is MSPRINT2.DLL, which thunks to the class installer MSPRINT.DLL. For
"Apple LaserWriter Plus" (MSPRINT.INF: APPLE380.SPD,Apple_LaserWriter_Plus) it copies the
files of the [PSCRIPT] section into WINDOWS\\SYSTEM and calls AddPrinterDriverA level 3,
AddMonitorA level 2 and AddPrinterA level 2 of the spooler. The local provider of SPOOLSS
(LocalAddPrinterDriver, LocalAddPrinter at, the WIN.INI writer at  and the
default-printer writer at ) turns those calls into the registry keys and WIN.INI lines
this tool writes. Nothing runs in the guest: the result is the state the wizard leaves behind.


Target is drive C:, the Win98 folder of setup (build/win98-c), and its registry change
journal (build/win98-c.registry); a test passes its own pair.

    build/analysis-venv/bin/python tools/install_printer.py [--drive-c DIR] [--registry FILE]
"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from win98_folder import JOURNAL_SIGNATURE, journal_walk  # noqa: E402

MODEL = 'Apple LaserWriter Plus'
PORT = 'LPT1:'
DRIVER = 'PSCRIPT.DRV'
# MSPRINT.INF [PSCRIPT]: every file of the CopyFiles list goes to directory 11,
# WINDOWS\SYSTEM. [PSCRIPT_DATA] names the driver and the help file, the data file is the
# section name APPLE380.SPD, and the configuration file of a Win9x driver is the driver.
FILES = ['PSCRIPT.DRV', 'PSCRIPT.HLP', 'PSCRIPT.INI', 'TESTPS.TXT', 'APPLE380.SPD', 'FONTS.MFM',
         'ICONLIB.DLL', 'PSMON.DLL']
DEPENDENT = ['PSCRIPT.INI', 'TESTPS.TXT', 'FONTS.MFM', 'ICONLIB.DLL', 'PSMON.DLL']
MONITOR = 'PostScript Language Monitor'  # %PS_MONITOR% = "PostScript Language Monitor,PSMON.DLL"
PRINT = 'HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\Print'
CONFIG = 'HKEY_LOCAL_MACHINE\\Config\\0001\\System\\CurrentControlSet\\Control\\Print\\Printers'
REG_SZ, REG_BINARY, REG_DWORD = 1, 3, 4


def value_record(path, name, kind, data):
    """Record 3 of the journal of src/registry_persist.cpp: set value."""
    out = bytearray([3])
    for text in (path, name):
        raw = text.encode('cp1252')
        out += struct.pack('<H', len(raw)) + raw
    if kind == REG_SZ:
        data = data.encode('cp1252') + b'\0'
    elif kind == REG_DWORD:
        data = struct.pack('<I', data)
    return bytes(out) + struct.pack('<II', kind, len(data)) + data


def registry_records():
    driver_key = PRINT + '\\Environments\\Windows 4.0\\Drivers\\' + MODEL
    printer_key = PRINT + '\\Printers\\' + MODEL
    records = [
        # AddPrinterDriverA level 3 (DRIVER_INFO_3). SPOOLSS drops a driver whose Driver,
        # Configuration File or Data File value is missing.
        value_record(driver_key, 'Driver', REG_SZ, DRIVER),
        value_record(driver_key, 'Configuration File', REG_SZ, DRIVER),
        value_record(driver_key, 'Data File', REG_SZ, 'APPLE380.SPD'),
        value_record(driver_key, 'Help File', REG_SZ, 'PSCRIPT.HLP'),
        value_record(driver_key, 'Monitor', REG_SZ, MONITOR),
        value_record(driver_key, 'Dependent Files', REG_BINARY,
                     b''.join(name.encode() + b'\0' for name in DEPENDENT) + b'\0'),
        # AddMonitorA level 2.
        value_record(PRINT + '\\Monitors\\' + MONITOR, 'Driver', REG_SZ, 'PSMON.DLL'),
        # AddPrinterA level 2 (PRINTER_INFO_2) as LocalAddPrinter writes it. The loader keeps
        # a printer only with Port, Print Processor, Printer Driver, Location and Description.
        # PostScript has no DefaultDataType in [PSCRIPT_DATA], so the
        # spool format is RAW.
        value_record(printer_key, 'Name', REG_SZ, MODEL),
        value_record(printer_key, 'Port', REG_SZ, PORT),
        value_record(printer_key, 'Print Processor', REG_SZ, 'WinPrint'),
        value_record(printer_key, 'Printer Driver', REG_SZ, MODEL),
        value_record(printer_key, 'Datatype', REG_SZ, 'RAW'),
        value_record(printer_key, 'Location', REG_SZ, ''),
        value_record(printer_key, 'Description', REG_SZ, ''),
        value_record(printer_key, 'Share Name', REG_SZ, ''),
        value_record(printer_key, 'Parameters', REG_SZ, ''),
        value_record(printer_key, 'Separator File', REG_SZ, ''),
        value_record(printer_key, 'Attributes', REG_DWORD, 0),
        value_record(printer_key, 'Status', REG_DWORD, 0),
        value_record(printer_key, 'Priority', REG_DWORD, 1),
        value_record(printer_key, 'StartTime', REG_DWORD, 0),
        value_record(printer_key, 'UntilTime', REG_DWORD, 0),
        # SetPrinterA level 5: DeviceNotSelected and TransmissionRetry in milliseconds.
        value_record(printer_key, 'dnsTimeout', REG_DWORD, 15000),
        value_record(printer_key, 'txTimeout', REG_DWORD, 45000),
        # [Environment_Reg] of MSPRINT.INF.
        value_record(PRINT + '\\Printers', 'StartOnBoot', REG_DWORD, 1),
        # The first printer becomes the default : HKEY_CURRENT_CONFIG, which is
        # Config\0001 of HKEY_LOCAL_MACHINE.
        value_record(CONFIG, 'Default', REG_SZ, MODEL),
    ]
    # LocalAddPrinter creates the subkey PrinterDriverData itself, and the printer's key in
    # the current hardware profile: a printer that has none there is set to work offline
    # when the spooler starts (SPOOLSS, except on FILE:). Record 1
    # creates a key.
    for key in (printer_key + '\\PrinterDriverData', CONFIG + '\\' + MODEL):
        raw = key.encode('cp1252')
        records.append(bytes([1]) + struct.pack('<H', len(raw)) + raw)
    return records


def set_ini(lines, section, key, value):
    """Set key=value in [section] of an INI text, keeping everything else."""
    out, in_section, done, found_section = [], False, False, False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('[') and stripped.endswith(']'):
            if in_section and not done:
                out.append(f'{key}={value}')
                done = True
            in_section = stripped[1:-1].lower() == section.lower()
            found_section |= in_section
            out.append(line)
            continue
        if in_section and '=' in stripped and stripped.split('=', 1)[0].strip().lower() == key.lower():
            if not done:
                out.append(f'{key}={value}')
                done = True
            continue
        out.append(line)
    if in_section and not done:
        out.append(f'{key}={value}')
        done = True
    if not found_section:
        out += [f'[{section}]', f'{key}={value}']
    return out


def install(drive_c, registry, files, tree):
    system = drive_c / 'WINDOWS' / 'SYSTEM'
    system.mkdir(parents=True, exist_ok=True)
    for name in FILES:
        shutil.copyfile(files / name, system / name)
    # WIN.INI of the writable layer; the first change copies the tree's file.
    win_ini = drive_c / 'WINDOWS' / 'WIN.INI'
    if not win_ini.exists():
        shutil.copyfile(tree / 'WINDOWS' / 'WIN.INI', win_ini)
    lines = win_ini.read_bytes().decode('cp1252').replace('\r\n', '\n').split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    driver = DRIVER.rsplit('.', 1)[0]
    lines = set_ini(lines, 'devices', MODEL, f'{driver},{PORT}')
    lines = set_ini(lines, 'PrinterPorts', MODEL, f'{driver},{PORT},15,45')
    lines = set_ini(lines, 'windows', 'device', f'{MODEL},{driver},{PORT}')
    win_ini.write_bytes(('\r\n'.join(lines) + '\r\n').encode('cp1252'))
    existing = registry.read_bytes() if registry.exists() else b''
    body = existing[len(JOURNAL_SIGNATURE):] if existing.startswith(JOURNAL_SIGNATURE) else b''
    list(journal_walk(body))
    registry.parent.mkdir(parents=True, exist_ok=True)
    registry.write_bytes(JOURNAL_SIGNATURE + body + b''.join(registry_records()))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--drive-c', type=Path, default=ROOT / 'build/win98-c')
    parser.add_argument('--registry', type=Path, default=None)
    parser.add_argument('--files', type=Path, default=ROOT / 'build/printer-driver/files')
    parser.add_argument('--tree', type=Path, default=ROOT / 'build/win98-c')
    args = parser.parse_args()
    registry = args.registry or args.drive_c.parent / (args.drive_c.name + '.registry')
    if not (args.files / DRIVER).is_file():
        subprocess.run([sys.executable, str(ROOT / 'tools/import_printer_driver.py')], check=True)
    install(args.drive_c, registry, args.files, args.tree)
    print(f'{MODEL} on {PORT}: {len(FILES)} files in {args.drive_c / "WINDOWS/SYSTEM"}, '
          f'WIN.INI [devices] [PrinterPorts] [windows], {len(registry_records())} journal records '
          f'in {registry}')


if __name__ == '__main__':
    main()
