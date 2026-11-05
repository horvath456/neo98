#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The registry of a Win98 installation whose devices are the port's own.

The hive of build/win98-c was written by Setup on the machine it ran on, a
PC of QEMU: the display adapter is "Cirrus Logic" (Enum\\Root\\*PNP0905,
DXCIRRUS.INF), the CD-ROM drive "QEMU QEMU DVD-ROM" and the BIOS string of
the system board the build line of SeaBIOS. None of them is the machine a
program runs on here. The display driver is the port's (the DIB engine on
the compositor's frame buffer with its DirectDraw, Direct3D and OpenGL
parts), the CD-ROM drive is the host's disc image.

This module writes what the class installers of Win98 write when a device is
exchanged for another: a new instance under Enum, the description in the
class key of the driver, and the old instance gone.

- The display adapter becomes Enum\\Root\\*NEO9800\\0000 with the class key
  Services\\Class\\Display\\0000 it had (the key GDI reads "ICMProfile"
  from). DriverDesc, ProviderName and MatchingDeviceId name the port, DEFAULT
  names its mode; InfPath, InfSection and the drv, vdd and minivdd of
  DEFAULT, which named the files of the Cirrus driver, go: the driver is
  part of the port. drv and vdd name the files of Win98 it stands in for
  (framebuf.drv over the DIB engine, the virtual display devices), so the
  page Driver lists files the folder has. The list of modes under MODES
  stays as it is; nothing of the port reads it.
- Every CD-ROM drive under Enum\\SCSI becomes
  Enum\\SCSI\\NEO98___CD-ROM__________1\\<instance> with the values of the
  old instance (drive letter, class key CDROM\\0000) and the port's inquiry
  strings.
- BIOSVersion of the system board (Enum\\Root\\*PNP0C01\\0000).
- The sound card (Enum\\Root\\*NEO9810\\0000, class MEDIA) and the monitor
  (Enum\\MONITOR\\NEO9830 below the display adapter) come new: Setup found
  neither on its machine. The network card is named in tools/network_setup.py.
- The floppy disk controller and its drive go with their driver keys: there
  is no floppy disk drive here.
- The marks of the old instances under Control\\ASD\\Prob go with them.

The names are the constants of src/neo98_hardware.h, read from that file;
src/registry_hives.h does the same at run time (Hardware, HardwareRemovals),
and tools/test_registry_hives_native.py compares the two.

tools/import_registry.py merges the result into the imported image.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SZ = 1
CLASS = 'System\\CurrentControlSet\\Services\\Class'
DISPLAY_CLASS = CLASS + '\\Display\\0000'
BOARD = 'Enum\\Root\\*PNP0C01\\0000'
MARKS = 'System\\CurrentControlSet\\Control\\ASD\\Prob'


def names():
    """The constants of src/neo98_hardware.h as {name: text}."""
    text = (ROOT / 'src/neo98_hardware.h').read_text()
    return {name: value.replace('\\\\', '\\')
            for name, value in re.findall(r'constexpr char (\w+)\[\] = "((?:[^"\\]|\\.)*)";', text)}


def resources():
    """Neo98Hardware::DeviceResources of src/neo98_hardware.h in its order: [(devnode, [(kind, first, last)])]."""
    text = (ROOT / 'src/neo98_hardware.h').read_text()
    return [(device.replace('\\\\', '\\'), [(kind, int(first, 0), int(last, 0)) for kind, first, last in
                                               re.findall(r"\{'(\w)',\s*(\w+),\s*(\w+)\}", body)])
            for device, body in re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*\{((?:\s*\{[^{}]*\},?)+)\}\}', text)]


def boot_config(entries):
    """The value BootConfig for a list of resources, as Setup's detection writes it:
    040Ah, priority 0, a record a resource (size, type, descriptor), 0."""
    import struct
    out = struct.pack('<II', 0x040a, 0)
    for kind, first, last in entries:
        if kind == 'P':
            out += struct.pack('<IIHHHHHBB', 0x14, 2, 0, 0x0c, first, last, 0, 0, 0xff if last < 0x100 else 0x03)
        elif kind == 'Q':
            out += struct.pack('<IIHHI', 0x10, 4, 0, first, 0)
        elif kind == 'D':
            out += struct.pack('<IIBBH', 0x0c, 3, 0, first, 0)
        elif kind == 'M':
            out += struct.pack('<IIHHIII', 0x18, 1, 0, 0x14, first, last, 0)
    return out + struct.pack('<I', 0)


def _text(values, name):
    for n, kind, payload in values:
        if n.lower() == name.lower():
            return payload.rstrip(b'\0').decode('cp1252')
    return None


def instances(hive_tree):
    """(enumerator, device, instance path, values) three levels below Enum, in stored order."""
    for lowered, (full, _name, _parent, values) in hive_tree.items():
        parts = full.split('\\')
        if len(parts) == 4 and parts[0].lower() == 'enum':
            yield parts[1], parts[2], full, values


def replaced(hive_tree):
    """The instances that go: [(kind, enumerator, device, path, values)]."""
    hardware = names()
    out = []
    for enumerator, device, full, values in instances(hive_tree):
        kind = (_text(values, 'Class') or '').lower()
        if kind == 'display' and (_text(values, 'Driver') or '').lower() == 'display\\0000' \
                and device.lower() != hardware['DisplayId'].lower():
            out.append(('display', enumerator, device, full, values))
        elif kind == 'cdrom' and enumerator.lower() == 'scsi' and device.lower() != hardware['CdId'].lower():
            out.append(('cdrom', enumerator, device, full, values))
        elif kind == 'fdc' or enumerator.lower() == 'flop':
            out.append(('floppy', enumerator, device, full, values))
    return out


def additions(hive_tree, user_tree=None):
    """[(hive, key path, [(name, type, payload)], source)] like tools/network_setup.py."""
    hardware = names()
    grouped = {}

    def put(path, name, kind, payload, source):
        entry = grouped.setdefault(path.lower(), {'path': path, 'values': {}, 'source': source})
        for existing in list(entry['values']):
            if existing.lower() == name.lower():
                del entry['values'][existing]
        entry['values'][name] = (kind, payload)

    def text(path, name, value, source):
        put(path, name, SZ, value.encode('cp1252'), source)

    for kind, _enumerator, _device, full, values in replaced(hive_tree):
        if kind == 'display':
            node, source = hardware['DisplayNode'], 'display adapter of the port'
            for name, value in (('Class', 'Display'), ('Driver', 'Display\\0000'),
                                ('DeviceDesc', hardware['DisplayName']), ('HardwareID', hardware['DisplayId']),
                                ('Mfg', hardware['Maker'])):
                text(node, name, value, source)
            for name in ('ClassGUID', 'ConfigFlags', 'Capabilities'):
                for n, k, payload in values:
                    if n.lower() == name.lower():
                        put(node, n, k, payload, 'as ' + full)
                        break
            for name, value in (('DriverDesc', hardware['DisplayName']), ('ProviderName', hardware['Maker']),
                                ('MatchingDeviceId', hardware['DisplayId'])):
                text(DISPLAY_CLASS, name, value, source)
            text(DISPLAY_CLASS + '\\DEFAULT', 'Mode', hardware['DisplayMode'], source)
            text(DISPLAY_CLASS + '\\DEFAULT', 'drv', hardware['DisplayDriverFile'], source)
            text(DISPLAY_CLASS + '\\DEFAULT', 'vdd', hardware['DisplayVdd'], source)
            text(DISPLAY_CLASS, 'DriverDate', hardware['DriverDate'], source)
            text(DISPLAY_CLASS + '\\INFO', 'ChipType', hardware['DisplayChip'], source)
            text(DISPLAY_CLASS + '\\INFO', 'DACType', hardware['DisplayDac'], source)
            text(DISPLAY_CLASS + '\\INFO', 'Revision', hardware['DisplayRevision'], source)
        elif kind == 'cdrom':
            node = hardware['CdDevice'] + '\\' + full.rsplit('\\', 1)[1]
            renamed = {'devicedesc': hardware['CdName'], 'manufacturer': hardware['CdVendor'],
                       'productid': hardware['CdProduct'], 'revisionlevel': hardware['CdRevision'],
                       'hardwareid': hardware['CdHardwareId']}
            for n, k, payload in values:
                if n.lower() in renamed:
                    payload = renamed[n.lower()].encode('cp1252')
                put(node, n, k, payload, 'CD-ROM drive of the port, as ' + full)
    board = hive_tree.get(BOARD.lower())
    if board and _text(board[3], 'BIOSVersion') is not None:
        text(BOARD, 'BIOSVersion', hardware['BiosVersion'], 'BIOS of the port')
    if board:
        # BootConfig, what Setup's detection found a device at on its machine:
        # a device that has one gets the resources of the port in that form
        # (the Device Manager's printed "System summary" reads it).
        for device, entries in resources():
            key = hive_tree.get(('Enum\\' + device).lower())
            if key and any(name.lower() == 'bootconfig' for name, _kind, _payload in key[3]):
                put(key[0], 'BootConfig', 3, boot_config(entries), 'resources of the port')
    if board:
        # The devices Setup did not find on its machine: the sound card and
        # the monitor, written as their class installers write a device.
        def device(node, kind, guid, driver, name, identifier, source):
            key = CLASS + '\\' + driver
            for n, value in (('Class', kind), ('ClassGUID', guid), ('Driver', driver), ('DeviceDesc', name),
                             ('HardwareID', identifier), ('Mfg', hardware['Maker'])):
                text(node, n, value, source)
            put(node, 'ConfigFlags', 3, b'\0\0\0\0', source)
            for n, value in (('DriverDesc', name), ('ProviderName', hardware['Maker']),
                             ('DriverDate', hardware['DriverDate']), ('MatchingDeviceId', identifier)):
                text(key, n, value, source)
            return key

        sound = device(hardware['SoundNode'], 'MEDIA', '{4d36e96c-e325-11ce-bfc1-08002be10318}',
                       hardware['SoundDriver'], hardware['SoundName'], hardware['SoundId'], 'sound card of the port')
        text(sound, 'DevLoader', hardware['SoundLoader'], 'sound card of the port')
        for part, name in (('wave', hardware['WaveOutName']), ('midi', hardware['MidiOutName']),
                           ('mixer', hardware['MixerName'])):
            key = hardware['MediaResources'] + '\\' + part + '\\' + hardware['SoundResource']
            for n, value in (('Active', '1'), ('Description', name), ('FriendlyName', name),
                             ('Driver', hardware['SoundResourceDriver'])):
                text(key, n, value, 'sound card of the port')
        monitor = device(hardware['MonitorNode'], 'Monitor', '{4d36e96e-e325-11ce-bfc1-08002be10318}',
                         hardware['MonitorDriver'], hardware['MonitorName'], 'MONITOR\\' + hardware['MonitorId'],
                         'monitor of the port')
        text(monitor, 'MaxResolution', hardware['MonitorMaxResolution'], 'monitor of the port')
        text(monitor, 'DPMS', '0', 'monitor of the port')
        text(monitor + '\\MODES\\' + hardware['MonitorMaxResolution'], 'Mode1', '30.0-130.0,50.0-160.0',
             'monitor of the port')
    return [('SYSTEM.DAT', entry['path'], [(n, k, v) for n, (k, v) in entry['values'].items()], entry['source'])
            for entry in grouped.values()]


def removals(hive_tree):
    """Keys that go with everything below them: [(hive, path, source)]."""
    gone = replaced(hive_tree)
    paths = {full.lower() for _k, _e, _d, full, _v in gone}
    out = []
    for _kind, _enumerator, _device, full, _values in gone:
        device = full.rsplit('\\', 1)[0]
        others = any(lowered.startswith(device.lower() + '\\') and lowered.count('\\') == 3 and lowered not in paths
                     for lowered in hive_tree)
        out.append(('SYSTEM.DAT', full if others else device, 'device of the installation machine'))
    for kind, _enumerator, _device, _full, values in gone:
        if kind == 'floppy' and _text(values, 'Driver'):
            out.append(('SYSTEM.DAT', CLASS + '\\' + _text(values, 'Driver'), 'driver of a device that is not there'))
    return out


def value_removals(hive_tree):
    """Values that go: [(hive, key path, value name, source)]."""
    gone = replaced(hive_tree)
    out = []
    for _kind, _enumerator, _device, full, _values in gone:
        for lowered, (path, _name, parent, _vals) in hive_tree.items():
            if parent.lower() == MARKS.lower():
                out.append(('SYSTEM.DAT', path, full[len('Enum\\'):], 'mark of the old instance'))
    if any(kind == 'display' for kind, *_ in gone):
        for name in ('InfPath', 'InfSection'):
            out.append(('SYSTEM.DAT', DISPLAY_CLASS, name, 'INF of the old display driver'))
        out.append(('SYSTEM.DAT', DISPLAY_CLASS + '\\DEFAULT', 'minivdd', 'file of the old display driver'))
    return out


if __name__ == '__main__':
    import import_registry
    import win98_c
    tree = import_registry.parse(win98_c.path('C:\\WINDOWS\\SYSTEM.DAT'))['tree']
    for hive, path, values, source in additions(tree):
        print(f'[{hive}\\{path}]  ; {source}')
        for name, kind, payload in values:
            print(f'  {name} = {kind}:{payload!r}')
    for hive, path, source in removals(tree):
        print(f'-[{hive}\\{path}]  ; {source}')
    for hive, path, name, source in value_removals(tree):
        print(f'-[{hive}\\{path}] {name}  ; {source}')
