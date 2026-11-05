#!/usr/bin/env python3
"""Import the installed Win95/98 CREG hives (SYSTEM.DAT, USER.DAT) as native registry data.

Copies the two hives out of the Win98 folder of setup, decodes the RGKN key tree
and the RGDB value blocks, selects the subtrees the port actually serves and emits a
compact byte-stream image plus a provenance report. Nothing is invented here: every
key, value name, type and payload byte comes from the installed hive.
"""
from pathlib import Path
import argparse, hashlib, json, struct, subprocess, sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from provenance import stamp as stamp_provenance    # noqa: E402
import win98_c    # noqa: E402

# Outside a checkout (the setup files of the app bundle) the folder above tools/.
ROOT = next((p for p in Path(__file__).resolve().parents if (p / 'neo98.py').is_file()),
            Path(__file__).resolve().parents[1])
# Destination indices in Registry::Store::roots.
HKCR, HKCU, HKLM, HKU, HKCC = 0, 1, 2, 3, 5
# Subtrees the implemented shell, Explorer, WordPad, Paint, TIMEDATE and the ported
# KERNEL32/SHELL32/SHLWAPI services read. Everything else is left out on purpose; the
# reasons and the resulting gaps are listed in
# src/registry_hives.h holds the same list for the import at run time;
# tools/test_registry_hives_native.py compares the two imports.
SELECTION = [
    ('SYSTEM.DAT', 'Software\\Classes', HKLM, 'Software\\Classes'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\Windows\\CurrentVersion', HKLM,
     'Software\\Microsoft\\Windows\\CurrentVersion'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\OLE', HKLM, 'Software\\Microsoft\\OLE'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\Rpc', HKLM, 'Software\\Microsoft\\Rpc'),
    # DirectX as the installation left it: the version programs check, the
    # DirectMusic ports (the default synthesizer DirectMusic's AddPort(NULL)
    # opens), DirectInput, DirectPlay, and DirectDraw's compatibility
    # list and Direct3D's driver list.
    ('SYSTEM.DAT', 'Software\\Microsoft\\DirectX', HKLM, 'Software\\Microsoft\\DirectX'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\DirectMusic', HKLM, 'Software\\Microsoft\\DirectMusic'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\DirectInput', HKLM, 'Software\\Microsoft\\DirectInput'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\DirectPlay', HKLM, 'Software\\Microsoft\\DirectPlay'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\DirectDraw', HKLM, 'Software\\Microsoft\\DirectDraw'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\Direct3D', HKLM, 'Software\\Microsoft\\Direct3D'),
    ('SYSTEM.DAT', 'System\\CurrentControlSet\\Control', HKLM, 'System\\CurrentControlSet\\Control'),
    ('SYSTEM.DAT', 'Config\\0001', HKLM, 'Config\\0001'),
    ('SYSTEM.DAT', 'Network', HKLM, 'Network'),
    # The device tree and the device classes, about 24 KB: the serial ports
    # of the KERNEL32 comm family (Enum\<bus>\<device>\<instance> and
    # Services\Class\Ports), the software key of the
    # display adapter whose "ICMProfile" GDI32!EnumICMProfilesA reads, the
    # network installation, and every devnode the configuration manager hands
    # SYSDM's Device Manager (src/config_manager.cpp).
    ('SYSTEM.DAT', 'Enum', HKLM, 'Enum'),
    ('SYSTEM.DAT', 'System\\CurrentControlSet\\Services\\Class', HKLM,
     'System\\CurrentControlSet\\Services\\Class'),
    # The Winsock 2 catalogs. WS2_32.DLL reads Protocol_Catalog9 and
    # NameSpace_Catalog5 under Parameters at its first call and, when the key
    # is missing, writes Num_Catalog_Entries = 0 into a fresh one; every name
    # lookup then ends in WSASERVICE_NOT_FOUND and gethostbyname returns null.
    # This installation carries the catalogs the Winsock setup wrote,
    # so they come out of the hive like everything else.
    ('SYSTEM.DAT', 'System\\CurrentControlSet\\Services\\WinSock2', HKLM,
     'System\\CurrentControlSet\\Services\\WinSock2'),
    # The network installation: the adapters, protocols and clients Setup
    # recorded, and the TCP/IP parameters RNR20, MSWSOCK and IPCONFIG read.
    # tools/network_setup.py adds the LAN card on top.
    ('SYSTEM.DAT', 'System\\CurrentControlSet\\Services\\VxD\\MSTCP', HKLM,
     'System\\CurrentControlSet\\Services\\VxD\\MSTCP'),
    ('SYSTEM.DAT', 'System\\CurrentControlSet\\Services\\VxD\\VNETSUP', HKLM,
     'System\\CurrentControlSet\\Services\\VxD\\VNETSUP'),
    # SYSDM keeps its pending hardware-profile change in VPOWERD's Flags.
    ('SYSTEM.DAT', 'System\\CurrentControlSet\\Services\\VxD\\VPOWERD', HKLM,
     'System\\CurrentControlSet\\Services\\VxD\\VPOWERD'),
    # (DirectPlay above also serves the network: DPLAYX loads the TCP/IP
    # provider named under Service Providers. It stood here
    # a second time, and the image carried every key below it twice.)
    # The machine half of Internet Explorer: MSHTML reads ActiveX
    # Compatibility before it runs a script engine or a control, and the
    # options pages read AdvancedOptions.
    ('SYSTEM.DAT', 'Software\\Microsoft\\Internet Explorer', HKLM, 'Software\\Microsoft\\Internet Explorer'),
    # Authenticode: WINTRUST finds its trust providers (SOFTPUB) and CRYPT32
    # its OID functions and the machine certificate stores here; IE checks a
    # control's cab with WinVerifyTrust before it installs it.
    ('SYSTEM.DAT', 'Software\\Microsoft\\Cryptography', HKLM, 'Software\\Microsoft\\Cryptography'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\SystemCertificates', HKLM, 'Software\\Microsoft\\SystemCertificates'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\DirectPlay8', HKLM, 'Software\\Microsoft\\DirectPlay8'),
    # USER.EXE opens this key at start, loads the module its value
    # "cool.dll" names and keeps the handle at DS:264h; SystemParametersInfo
    # answers the font smoothing and full drag actions only with it.
    ('SYSTEM.DAT', 'Software\\Microsoft\\Plus!', HKLM, 'Software\\Microsoft\\Plus!'),
    # Active Setup. EXPLORER.EXE 4.72.3110 calls SHDOCVW ordinal 125 at every
    # start of the shell (not in safe mode); it runs RunInstallUninstallStubs,
    # which compares Installed Components here with the same key under HKCU
    # and runs the StubPath of every component the user does not have yet:
    # the per-user parts of Win98 setup, IE4UINIT (the sound scheme's
    # Navigating entry, the Quick Launch bar) and so on.
    ('SYSTEM.DAT', 'Software\\Microsoft\\Active Setup', HKLM, 'Software\\Microsoft\\Active Setup'),
    # Mail and the address book, 160 keys. MSOE.DLL reads the path of
    # WAB32.DLL from WAB\DLLPath and ends with "The Address Book failed to
    # load" without it; its own key holds the store's settings, Internet
    # Account Manager the accounts, Clients the registered mail, news and
    # contacts programs, Windows Messaging Subsystem the MAPI switches.
    ('SYSTEM.DAT', 'Software\\Microsoft\\Outlook Express', HKLM, 'Software\\Microsoft\\Outlook Express'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\WAB', HKLM, 'Software\\Microsoft\\WAB'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\Internet Account Manager', HKLM,
     'Software\\Microsoft\\Internet Account Manager'),
    ('SYSTEM.DAT', 'Software\\Microsoft\\Windows Messaging Subsystem', HKLM,
     'Software\\Microsoft\\Windows Messaging Subsystem'),
    ('SYSTEM.DAT', 'Software\\Clients', HKLM, 'Software\\Clients'),
    # MSINFO32 builds its tree of categories from MSInfo\\Categories (the
    # CLSID of the control that fills a category, its view and rank) and its
    # menu Tools from MSInfo\\Tools; without the key it shows only the
    # category it registers itself. 95 keys.
    ('SYSTEM.DAT', 'Software\\Microsoft\\Shared Tools', HKLM, 'Software\\Microsoft\\Shared Tools'),
    ('USER.DAT', '.DEFAULT', HKU, '.Default'),
]


def hive_files(folder, out):
    """Copy the two hives out of the Win98 folder; it is only read."""
    out.mkdir(parents=True, exist_ok=True)
    files = {}
    for name in ('SYSTEM.DAT', 'USER.DAT'):
        files[name] = win98_c.copy(f'::/WINDOWS/{name}', out / name, folder)
    return files


def records(data):
    """Index every live RGDB key record by its block-qualified identifier."""
    first = struct.unpack_from('<I', data, 8)[0]
    found, block, index = {}, first, 0
    while block < len(data):
        assert data[block:block + 4] == b'RGDB', 'Expected an RGDB block'
        size = struct.unpack_from('<I', data, block + 4)[0]
        at, end = block + 32, block + size
        assert size >= 32 and end <= len(data)
        while at + 20 <= end:
            total, identifier, used, name_length, count = struct.unpack_from('<IIIHH', data, at)
            if total < 20 or at + total > end:
                break
            if identifier != 0xffffffff and 20 + name_length <= used <= total:
                found[index << 16 | identifier] = (at, used, name_length, count)
            at += total
        block, index = end, index + 1
    return first, found


def parse(path):
    """Decode a CREG hive into a key tree with its values, in stored order."""
    data = path.read_bytes()
    assert data[:4] == b'CREG' and data[0x20:0x24] == b'RGKN', f'{path.name} is not a CREG hive'
    first, found = records(data)

    def node(offset):
        assert 0x40 <= offset <= first - 28
        return struct.unpack_from('<7I', data, offset)

    def values(word):
        record = found.get(word)
        if not record:
            return []
        at, used, name_length, count = record
        limit, cursor, out = at + used, at + 20 + name_length, []
        for _ in range(count):
            kind, reserved, n, length = struct.unpack_from('<IIHH', data, cursor)
            cursor += 12
            name = data[cursor:cursor + n].decode('cp1252')
            cursor += n
            payload = data[cursor:cursor + length]
            cursor += length
            assert cursor <= limit, 'Value run leaves its RGDB record'
            assert not any(name.lower() == e[0].lower() for e in out), 'Repeated value name in one key'
            out.append((name, kind, payload))
        return out

    tree, seen = {}, set()

    def walk(offset, path_text):
        """Follow the stored child/sibling links; a linear scan misses the page breaks."""
        while offset != 0xffffffff:
            absolute = 0x20 + offset
            assert absolute not in seen and len(seen) < 1 << 20
            seen.add(absolute)
            word = node(absolute)
            record = found.get(word[6])
            name = data[record[0] + 20:record[0] + 20 + record[2]].decode('cp1252') if record else ''
            assert '\\' not in name, 'A key name may not contain a separator'
            full = f'{path_text}\\{name}' if path_text else name
            assert full.lower() not in tree, f'Duplicate key {full}'
            tree[full.lower()] = (full, name, path_text, values(word[6]))
            if word[4] != 0xffffffff:
                walk(word[4], full)
            offset = word[5]

    sys.setrecursionlimit(20000)
    walk(node(0x40)[4], '')
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data), 'tree': tree}


def varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7f
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def selection(hives, whole_hives=False):
    if not whole_hives:
        return SELECTION
    return [(hive, full, root, full)
            for hive, root in (('SYSTEM.DAT', HKLM), ('USER.DAT', HKU))
            for full, name, parent, values in hives[hive]['tree'].values() if not parent]


def select(hives, whole_hives=False):
    """Collect the chosen subtrees, every parent ahead of its children."""
    keys, index_of, skipped = [], {}, []
    for hive, source, root, destination in selection(hives, whole_hives):
        tree = hives[hive]['tree']
        prefix = source.lower()
        if prefix not in tree:
            skipped.append({'hive': hive, 'path': source, 'reason': 'absent from this installation'})
            continue
        index_of[(hive, prefix)] = len(keys)
        keys.append({'anchor': (root, destination), 'name': destination.rsplit('\\', 1)[-1],
                     'source': f'{hive}\\{source}', 'values': tree[prefix][3]})
        for lowered, (full, name, parent, values) in tree.items():
            if not lowered.startswith(prefix + '\\'):
                continue
            index_of[(hive, lowered)] = len(keys)
            keys.append({'parent': index_of[(hive, parent.lower())], 'name': name,
                         'source': f'{hive}\\{full}', 'values': values})
    # The keys of a LAN installation that this hive does not have
    # (tools/network_setup.py): new keys hang under imported parents, values
    # of an existing key replace the ones of the same name.
    # The display adapter of the port writes its mode the same way
    # (tools/display_setup.py).
    # The devices of the installation machine become the port's own
    # (tools/hardware_setup.py).
    import network_setup
    import display_setup
    import hardware_setup
    extra = [*network_setup.additions(hives['SYSTEM.DAT']['tree'], hives['USER.DAT']['tree']),
             *display_setup.additions(hives['SYSTEM.DAT']['tree'], hives['USER.DAT']['tree']),
             *hardware_setup.additions(hives['SYSTEM.DAT']['tree'], hives['USER.DAT']['tree'])]
    for hive, path, values, source in sorted(extra, key=lambda a: a[1].count('\\')):
        def imported_above(path):
            while path:
                if (hive, path.lower()) in index_of:
                    return True
                path = path.rpartition('\\')[0]
            return False

        def key(path):
            at = index_of.get((hive, path.lower()))
            if at is None:
                parent, _, name = path.rpartition('\\')
                if imported_above(parent):
                    above = key(parent)
                    at = len(keys)
                    keys.append({'parent': above, 'name': name, 'source': f'setup: {source}', 'values': []})
                else:
                    # No imported key above it (Services\VxD\VREDIR of
                    # the network client): a subtree of its own, anchored at
                    # its full path like the entries of SELECTION.
                    assert hive == 'SYSTEM.DAT', path
                    at = len(keys)
                    keys.append({'anchor': (HKLM, path), 'name': name, 'source': f'setup: {source}', 'values': []})
                index_of[(hive, path.lower())] = at
            return at
        at = key(path)
        merged = [v for v in keys[at]['values'] if all(v[0].lower() != n.lower() for n, _, _ in values)]
        keys[at]['values'] = merged + list(values)
    # Keys the setup scripts take away, with everything below them: the
    # Ndi\Default of Microsoft Family Logon when Windows Logon becomes the
    # primary network logon (tools/network_setup.py).
    removed = set()
    for hive, path, source in [*network_setup.removals(), *hardware_setup.removals(hives['SYSTEM.DAT']['tree'])]:
        at = index_of.get((hive, path.lower()))
        if at is not None:
            removed.add(at)
    # Single values that go with the old devices.
    for hive, path, name, source in hardware_setup.value_removals(hives['SYSTEM.DAT']['tree']):
        at = index_of.get((hive, path.lower()))
        if at is not None:
            keys[at]['values'] = [v for v in keys[at]['values'] if v[0].lower() != name.lower()]
    if removed:
        for index, key in enumerate(keys):
            if 'parent' in key and key['parent'] in removed:
                removed.add(index)
        remap, kept = {}, []
        for index, key in enumerate(keys):
            if index in removed:
                continue
            remap[index] = len(kept)
            kept.append(key)
        for key in kept:
            if 'parent' in key:
                key['parent'] = remap[key['parent']]
        keys = kept
    return keys, skipped


def encode(keys):
    """Pack the selection into five blobs: names, anchor paths, keys, values, payloads."""
    frequency = {}
    for key in keys:
        frequency[key['name']] = frequency.get(key['name'], 0) + 1
        for name, _, _ in key['values']:
            frequency[name] = frequency.get(name, 0) + 1
    names, pool = {}, bytearray()
    # Frequent names first, so their pool offsets stay one or two varint bytes.
    for name in sorted(frequency, key=lambda n: (-frequency[n], n)):
        names[name] = len(pool)
        pool += name.encode('cp1252') + b'\0'
    anchors, key_stream, value_stream, payloads = bytearray(), bytearray(), bytearray(), bytearray()
    value_count, previous = 0, 0
    for index, key in enumerate(keys):
        if 'anchor' in key:
            root, path = key['anchor']
            key_stream += varint(0) + varint(root) + varint(len(anchors))
            anchors += path.encode('cp1252') + b'\0'
        else:
            key_stream += varint(index - key['parent'])
        key_stream += varint(names[key['name']])
        for name, kind, payload in key['values']:
            # CREG keeps text payloads without a terminator; the native REG_SZ
            # convention stores one, exactly as the timezone import already does.
            if kind in (1, 2, 7) and (not payload or payload[-1]):
                payload = payload + b'\0'
            assert len(payload) <= 0xffff and kind < 0x100
            value_stream += varint(index - previous) + varint(names[name]) + bytes([kind]) + varint(len(payload))
            payloads += payload
            previous, value_count = index, value_count + 1
    return {'names': bytes(pool), 'anchors': bytes(anchors), 'keys': bytes(key_stream),
            'values': bytes(value_stream), 'payloads': bytes(payloads),
            'key_count': len(keys), 'value_count': value_count}


def header(image):
    def array(name, blob):
        body = ''.join(' ' + ','.join(f'{b:#04x}' for b in blob[i:i + 20]) + ',\n' for i in range(0, len(blob), 20))
        return f'static constexpr unsigned char {name}[]={{\n{body}}};\n'

    text = ('// SPDX-License-Identifier: GPL-2.0-or-later\n'
            '// Installed Win98 registry subtrees decoded from the local SYSTEM.DAT and USER.DAT.\n'
            '// Generated by tools/import_registry.py.\n'
            '#pragma once\nnamespace Win98RegistryData {\n')
    text += f'static constexpr unsigned KeyCount={image["key_count"]},ValueCount={image["value_count"]};\n'
    for name in ('names', 'anchors', 'keys', 'values', 'payloads'):
        text += array(name.capitalize(), image[name])
    return text + '}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder', type=Path, default=win98_c.FOLDER,
                        help='the Win98 folder of setup (default build/win98-c)')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/registry-import')
    parser.add_argument('--generated', type=Path, default=ROOT / 'build/generated')
    parser.add_argument('--whole-hives', action='store_true', help='import every subtree, as the host does')
    parser.add_argument('--dump', metavar='HIVE\\PATH', action='append', default=[],
                        help='Print imported keys as JSON instead of generating the data image')
    args = parser.parse_args()
    files = hive_files(args.folder.resolve(), args.output)
    hives = {name: parse(path) for name, path in files.items()}
    keys, skipped = select(hives, args.whole_hives)
    if args.dump:
        lookup, children = {}, {}
        for index, key in enumerate(keys):
            lookup[key['source'].lower()] = key
            parent = key.get('parent')
            if parent is not None:
                children.setdefault(keys[parent]['source'].lower(), []).append(key['name'])
        print(json.dumps([{'path': path, 'found': path.lower() in lookup,
                           'children': children.get(path.lower(), []),
                           'values': [{'name': n, 'type': t, 'hex': v.hex()}
                                      for n, t, v in lookup.get(path.lower(), {'values': []})['values']]}
                          for path in args.dump], indent=2))
        return
    image = encode(keys)
    args.generated.mkdir(parents=True, exist_ok=True)
    generated = args.generated / 'win98_registry_data.h'
    generated.write_text(header(image))
    report = {
        'source': {'folder': str(args.folder.resolve()),
                   'access': 'read-only copy out of the Win98 folder'},
        'hives': {name: {'sha256': hive['sha256'], 'bytes': hive['bytes'], 'keys': len(hive['tree']),
                         'values': sum(len(v[3]) for v in hive['tree'].values())}
                  for name, hive in hives.items()},
        'selection': [{'hive': h, 'source': s, 'root': r, 'destination': d} for h, s, r, d in selection(hives, args.whole_hives)],
        'skipped': skipped,
        'imported': {'keys': image['key_count'], 'values': image['value_count'],
                     'names_bytes': len(image['names']), 'anchors_bytes': len(image['anchors']),
                     'key_stream_bytes': len(image['keys']), 'value_stream_bytes': len(image['values']),
                     'payload_bytes': len(image['payloads']),
                     'total_bytes': sum(len(image[n]) for n in ('names', 'anchors', 'keys', 'values', 'payloads'))},
        'representation': ('REG_SZ/REG_EXPAND_SZ/REG_MULTI_SZ payloads receive the trailing NUL the native '
                           'RAM registry stores; every other byte, name and type is the hive byte.'),
        'generated_sha256': {generated.name: hashlib.sha256(generated.read_bytes()).hexdigest()},
    }
    (args.output / 'registry-import.json').write_text(json.dumps(report, indent=2) + '\n')
    stamp_provenance(report, 'registry-provenance.json')
    (args.generated / 'registry-provenance.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['imported']))


if __name__ == '__main__':
    main()
