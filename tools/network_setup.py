#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The registry of a Win98 installation with a network card and TCP/IP on it.

The installed hive of this tree knows TCP/IP only on the Dial-Up Adapter. A
machine on a LAN has more: the card under Enum and Services\\Class\\Net, a
second TCP/IP instance bound to it, and the Internet Connection Wizard marked
as done, so that Internet Explorer opens its page instead of the wizard. This
module writes exactly those keys, the way Windows 98 Setup and the wizard do:

- The card is the AMD PCnet (NETAMD.INF, model *pnp828c, install section
  PCIpcnet.ndi). Its class key gets the AddReg sections of that install
  section, read from the INF of the tree by the small AddReg interpreter
  below, and the values the device installer adds (DriverDesc, InfSection,
  InfPath, ProviderName, MatchingDeviceId).
- The TCP/IP instance for the card is written like the instance Setup wrote
  for the Dial-Up Adapter (Services\\Class\\NetTrans\\0000 and
  Enum\\Network\\MSTCP\\0000 of the hive): the same INF section, MSTCP.ndi
  of NETTRANS.INF, produced it. IPAddress 0.0.0.0 means "obtain an address
  automatically"; the lease comes from VDHCP.
- The wizard: INETCFG.DLL writes Completed = 01 00 00 00 (REG_BINARY) under
  HKCU\\Software\\Microsoft\\Internet Connection Wizard when it finds a
  connection, and CheckConnectionWizard skips the wizard when the
  value is there.
- The client: a LAN installation gets Client for Microsoft Networks
  (NETDEF.INF [LAN_TYPICAL] NetClient=VREDIR; [TYPICAL] without a card has
  only FAMILY). NETCLI.INF model VREDIR, install section VREDIR.ndi, and its
  Ndi\\Install section VREDIR.Install give the class key NetClient\\0001;
  every TCP/IP instance binds its own instance of each client
  (Enum\\Network\\<client>\\000n, MasterCopy the first). Without a client
  that counts, NETDI calls a LAN card with TCP/IP an
  incomplete network and NETCPL asks "Your network is not complete". FAMILY
  does not count: its Ndi\\NetType is "1". The primary
  network logon becomes "Windows Logon", an empty PrimaryProvider (NETDI
  ), the one of the three that logs on without a dialog on a
  real Win98 once the user's password list has an empty password (MPRSERV, OpenPasswordCache with the name of Network\\Logon\\username).
  NEO98 runs no logon at start at all (nothing starts MSGSRV32.EXE, which
  calls WNetLogon). The files of the client (VREDIR.VXD,
  MSNP32.DLL and the rest of VREDIR.Install's CopyFiles) are in the NET
  cabinets of the CD, not in the tree, so the lines of VREDIR.Install that
  make an original load them stay out, see below.

tools/import_registry.py merges the result into the imported image.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
INF = ROOT / 'build/win98-c/WINDOWS/INF'
SZ, BINARY, DWORD = 1, 3, 4
CLASS = 'System\\CurrentControlSet\\Services\\Class'


def sections(path):
    text = path.read_bytes().decode('cp1252')
    out, name = {}, None
    for raw in text.splitlines():
        line = raw.split(';', 1)[0].strip() if not raw.lstrip().startswith('"') else raw.strip()
        if not line:
            continue
        match = re.match(r'^\[(.+)\]$', line)
        if match:
            name = match.group(1).strip().lower()
            out.setdefault(name, [])
        elif name is not None:
            out[name].append(line)
    return out


def strings(parsed):
    table = {}
    for line in parsed.get('strings', []):
        key, _, value = line.partition('=')
        table[key.strip().lower()] = value.strip().strip('"')
    return table


def expand(value, table):
    def one(match):
        token = match.group(1)
        if token == '10':
            return 'C:\\WINDOWS'
        if token == '11':
            return 'C:\\WINDOWS\\SYSTEM'
        return table.get(token.lower(), match.group(0))
    return re.sub(r'%([^%]+)%', one, value)


def fields(line):
    out, current, quoted = [], '', False
    for ch in line:
        if ch == '"':
            quoted = not quoted
        elif ch == ',' and not quoted:
            out.append(current.strip())
            current = ''
        else:
            current += ch
    out.append(current.strip())
    return out


def addreg(parsed, section, hkr, table):
    """The values one AddReg section writes: {(path, name): (type, payload)}."""
    values = {}
    for line in parsed.get(section.lower(), []):
        part = fields(line)
        root, subkey = part[0].upper(), expand(part[1], table) if len(part) > 1 else ''
        name = expand(part[2], table) if len(part) > 2 else ''
        flag = part[3] if len(part) > 3 else ''
        data = [expand(p, table) for p in part[4:]]
        if root == 'HKR':
            path = hkr + ('\\' + subkey if subkey else '')
        elif root == 'HKLM':
            path = subkey
        else:
            continue
        flags = int(flag, 0) if flag else 0
        if flags & 1:
            payload = bytes(int(b, 16) for b in data)
            kind = BINARY
        else:
            payload = (data[0] if data else '').encode('cp1252')
            kind = SZ
        values[(path, name)] = (kind, payload)
    return values


def install(inf_name, install_section, hkr):
    parsed = sections(INF / inf_name)
    table = strings(parsed)
    values = {}
    for line in parsed.get(install_section.lower(), []):
        key, _, value = line.partition('=')
        if key.strip().lower() == 'addreg':
            for name in value.split(','):
                values.update(addreg(parsed, name.strip(), hkr, table))
    return values, table


def additions(hive_tree, user_tree=None):
    """Keys and values to add: [(hive, path, [(name, type, payload)], source)]."""
    grouped = {}

    # Registry names are case-insensitive: NDI and Ndi are one key.
    def put(hive, path, name, kind, payload, source):
        entry = grouped.setdefault((hive, path.lower()), {'path': path, 'values': {}, 'source': source})
        for existing in list(entry['values']):
            if existing.lower() == name.lower():
                del entry['values'][existing]
        entry['values'][name] = (kind, payload)

    # The card, NETAMD.INF model *pnp828c (PCIpcnet.ndi), under the driver key
    # of src/neo98_hardware.h.
    import hardware_setup
    hardware = hardware_setup.names()
    card = CLASS + '\\' + hardware['NetDriver']
    values, table = install('NETAMD.INF', 'PCIpcnet.ndi', card)
    # Without DeviceVxDs: the NDIS driver of the model is no file of the folder
    # and nothing loads it, the card's driver is the port's.
    for (path, name), (kind, payload) in values.items():
        if name.lower() != 'devicevxds':
            put('SYSTEM.DAT', path, name, kind, payload, 'NETAMD.INF [PCIpcnet.ndi] AddReg')
    # The card is the port's own (src/neo98_hardware.h); the driver key is
    # the one of the model it is compatible with.
    description = hardware['NetName']
    for name, value in (('DriverDesc', description), ('InfSection', 'PCIpcnet.ndi'), ('InfPath', 'NETAMD.INF'),
                        ('ProviderName', hardware['Maker']), ('DriverDate', hardware['DriverDate']),
                        ('MatchingDeviceId', hardware['NetCompatibleId'])):
        put('SYSTEM.DAT', card, name, SZ, value.encode('cp1252'), 'device installer, NETAMD.INF')
    # Its enumerator key, as a root-enumerated card with the Dial-Up
    # Adapter's instance (Enum\Root\Net\0000) as the pattern for the flags.
    enum_card = 'Enum\\Root\\Net\\0001'
    pattern = dict((n, (k, v)) for n, k, v in hive_tree['enum\\root\\net\\0000'][3])
    for name, value in (('Class', 'Net'), ('Driver', hardware['NetDriver']), ('DeviceDesc', description),
                        ('HardwareID', hardware['NetId']), ('CompatibleIDs', hardware['NetCompatibleId']),
                        ('Mfg', hardware['Maker']),
                        ('ClassGUID', '{4d36e972-e325-11ce-bfc1-08002be10318}')):
        put('SYSTEM.DAT', enum_card, name, SZ, value.encode('cp1252'), 'device installer, NETAMD.INF')
    for name in ('ConfigFlags', 'Capabilities'):
        kind, payload = pattern[name]
        put('SYSTEM.DAT', enum_card, name, kind, payload, 'as Enum\\Root\\Net\\0000')
    put('SYSTEM.DAT', enum_card + '\\Bindings', 'MSTCP\\0001', SZ, b'', 'binding of the new TCP/IP instance')

    # TCP/IP on the card: a second instance written like the first.
    def clone(source, target, rename=None):
        prefix = source.lower()
        for lowered, (full, name, parent, vals) in hive_tree.items():
            if lowered != prefix and not lowered.startswith(prefix + '\\'):
                continue
            path = target + full[len(source):]
            grouped.setdefault(('SYSTEM.DAT', path.lower()), {'path': path, 'values': {}, 'source': 'as ' + full})
            for n, k, v in vals:
                if rename and n in rename:
                    v = rename[n]
                put('SYSTEM.DAT', path, n, k, v, 'as ' + full)
    clone(CLASS + '\\NetTrans\\0000', CLASS + '\\NetTrans\\0001')
    clone('Enum\\Network\\MSTCP\\0000', 'Enum\\Network\\MSTCP\\0001',
          {'Driver': b'NetTrans\\0001', 'MasterCopy': b'Enum\\Network\\MSTCP\\0001'})

    # Client for Microsoft Networks, NETCLI.INF model VREDIR.
    client = CLASS + '\\NetClient\\0001'
    values, table = install('NETCLI.INF', 'VREDIR.ndi', client)
    for (path, name), (kind, payload) in values.items():
        put('SYSTEM.DAT', path, name, kind, payload, 'NETCLI.INF [VREDIR.ndi] AddReg')
    # VREDIR.Install without the lines that name a program of CopyFiles that
    # an original of the port would load outside the network settings: the
    # network provider MSNP32.DLL (MPRSERV in MPREXE.EXE loads every provider
    # of NetworkProvider\Order when Explorer starts and says "Unable to load
    # the dynamic link library: msnp32.dll" in a box "Windows Networking"
    # when it is missing), the print provider MSPP32.DLL (SPOOLSS reads
    # Print\Providers) and WinPopup under
    # OptionalComponents. The HKCR lines (the shell extensions of MSNP32.DLL)
    # are not read by addreg.
    values, _ = install('NETCLI.INF', 'VREDIR.Install', client)
    for (path, name), (kind, payload) in values.items():
        lowered = path.lower()
        if any(part in lowered for part in ('print\\providers', 'optionalcomponents', 'networkprovider')):
            continue
        put('SYSTEM.DAT', path, name, kind, payload, 'NETCLI.INF [VREDIR.Install] AddReg')
    family = dict((n, (k, v)) for n, k, v in hive_tree[(CLASS + '\\NetClient\\0000').lower()][3])
    client_description = table['vredir.devicedesc']
    for name, value in (('DriverDesc', client_description),
                        ('InfSection', 'VREDIR.ndi'), ('InfPath', 'NETCLI.INF'), ('ProviderName', table['v_ms'])):
        put('SYSTEM.DAT', client, name, SZ, value.encode('cp1252'), 'device installer, NETCLI.INF')
    put('SYSTEM.DAT', client, 'DriverDate', *family['DriverDate'], 'as NetClient\\0000 (Setup)')
    put('SYSTEM.DAT', client + '\\Ndi', 'InstallInf', SZ, b'', 'as NetClient\\0000\\Ndi')
    # The device installer turns the defaults of the interfaces into the
    # bound ones, as for FAMILY (Ndi\Interfaces Upper and Lower).
    put('SYSTEM.DAT', client + '\\Ndi\\Interfaces', 'Upper', SZ, b'winnet4,vredir', 'device installer, DefUpper')
    put('SYSTEM.DAT', client + '\\Ndi\\Interfaces', 'Lower', SZ, b'netbios,ipxDHost', 'device installer, DefLower')
    # One instance of each client per TCP/IP instance, the first the master
    # copy (as FAMILY\0000 for the Dial-Up Adapter's TCP/IP).
    pattern = dict((n, (k, v)) for n, k, v in hive_tree['enum\\network\\family\\0000'][3])
    for index in ('0000', '0001'):
        vredir = 'Enum\\Network\\VREDIR\\' + index
        for name, value in (('Class', 'NetClient'), ('Driver', 'NetClient\\0001'),
                            ('MasterCopy', 'Enum\\Network\\VREDIR\\0000'),
                            ('DeviceDesc', client_description), ('CompatibleIDs', 'VREDIR'),
                            ('Mfg', table['v_ms']), ('ClassGUID', '{4d36e973-e325-11ce-bfc1-08002be10318}')):
            put('SYSTEM.DAT', vredir, name, SZ, value.encode('cp1252'), 'device installer, NETCLI.INF')
        put('SYSTEM.DAT', vredir, 'ConfigFlags', *pattern['ConfigFlags'], 'as Enum\\Network\\FAMILY\\0000')
        grouped.setdefault(('SYSTEM.DAT', (vredir + '\\Bindings').lower()),
                           {'path': vredir + '\\Bindings', 'values': {}, 'source': 'as FAMILY\\0000\\Bindings'})
    family_copy = 'Enum\\Network\\FAMILY\\0001'
    for name, (kind, payload) in pattern.items():
        put('SYSTEM.DAT', family_copy, name, kind,
            b'Enum\\Network\\FAMILY\\0000' if name == 'MasterCopy' else payload, 'as Enum\\Network\\FAMILY\\0000')
    grouped.setdefault(('SYSTEM.DAT', (family_copy + '\\Bindings').lower()),
                       {'path': family_copy + '\\Bindings', 'values': {}, 'source': 'as FAMILY\\0000\\Bindings'})
    put('SYSTEM.DAT', 'Enum\\Network\\MSTCP\\0000\\Bindings', 'VREDIR\\0000', SZ, b'', 'binding of the client')
    lan_bindings = grouped[('SYSTEM.DAT', 'enum\\network\\mstcp\\0001\\bindings')]['values']
    lan_bindings.clear()
    for name in ('FAMILY\\0001', 'VREDIR\\0001'):
        put('SYSTEM.DAT', 'Enum\\Network\\MSTCP\\0001\\Bindings', name, SZ, b'', 'binding of the LAN TCP/IP')
    # Windows Logon as the primary network logon: no client is the default
    # of its type (see removals) and PrimaryProvider is empty, what NETDI
    # writes when no client is the primary one.
    put('SYSTEM.DAT', 'Network\\Logon', 'PrimaryProvider', SZ, b'', 'NETCPL, Primary Network Logon: Windows Logon')

    # Mark the Internet Connection Wizard as complete.
    put('USER.DAT', '.DEFAULT\\Software\\Microsoft\\Internet Connection Wizard', 'Completed', BINARY,
        b'\x01\x00\x00\x00', 'CheckConnectionWizard completion flag')
    # IE's per-user defaults. Active Setup runs IE's per-user stub at the
    # first logon of a profile, and it leaves every Advanced option of the
    # Internet Options sheet at the DefaultValue that
    # HKLM\Software\Microsoft\Internet Explorer\AdvancedOptions declares.
    # The imported.DEFAULT profile never logged on, so e.g. "Error Dlg
    # Displayed On Every Error" was missing and SHDOCVW falls back to
    # "yes": every page with a script error opened the script-error dialog.
    if user_tree is not None:
        prefix = 'software\\microsoft\\internet explorer\\advancedoptions\\'
        for lowered, (full, name, parent, vals) in sorted(hive_tree.items()):
            if not lowered.startswith(prefix):
                continue
            option = {n.lower(): (k, v) for n, k, v in vals if n}
            if not all(n in option for n in ('regpath', 'valuename', 'defaultvalue', 'hkeyroot')):
                continue
            if option['hkeyroot'][1] != b'\x01\x00\x00\x80':
                continue
            # Bitmask options (SecureProtocols, the Recycle Bin attributes)
            # share one value; their DefaultValue is not the whole value.
            if 'mask' in option or b'bitmask' in option.get('type', (0, b''))[1].lower():
                continue
            # Only the browser's own settings; shell folders, WinInet's
            # security switches and the Java VM keep what the hive has.
            regpath = option['regpath'][1].rstrip(b'\0').decode('cp1252')
            if not regpath.lower().startswith('software\\microsoft\\internet explorer'):
                continue
            path = '.DEFAULT\\' + regpath
            value = option['valuename'][1].rstrip(b'\0').decode('cp1252')
            present = user_tree.get(path.lower())
            if present and any(n and n.lower() == value.lower() for n, _, _ in present[3]):
                continue
            kind, payload = option['defaultvalue']
            checked = option.get('checkedvalue')
            if checked and checked[0] == DWORD and kind != DWORD:
                payload = int(payload.rstrip(b'\0') or b'0').to_bytes(4, 'little')
                kind = DWORD
            put('USER.DAT', path, value, kind, payload, 'IE per-user setup, AdvancedOptions\\' + full[len(prefix):])
    return [(hive, entry['path'], [(n, k, v) for n, (k, v) in entry['values'].items()], entry['source'])
            for (hive, _), entry in grouped.items()]


def removals():
    """Keys to take away: [(hive, path, source)].

    NETDI reads the primary network logon from the clients, not from
    Network\\Logon: a client whose class key has Ndi\\Default is
    the default of its type (flag 10h at +42h, ), and the page
    Configuration selects that client in "Primary Network Logon"; with none,
    it selects "Windows Logon" (, string 11006). Setup marked
    Microsoft Family Logon so, to match its PrimaryProvider. For Windows Logon
    the mark goes, and Client for Microsoft Networks gets none.
    """
    return [('SYSTEM.DAT', CLASS + '\\NetClient\\0000\\Ndi\\Default',
             'NETCPL, Primary Network Logon: Windows Logon')]


if __name__ == '__main__':
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    from import_registry import parse
    tree = parse(ROOT / 'build/registry-import/SYSTEM.DAT')['tree']
    for hive, path, values, source in additions(tree):
        print(f'[{hive}\\{path}]  ; {source}')
        for name, kind, payload in values:
            print(f'  {name or "@"} = {kind}:{payload[:60]!r}')
