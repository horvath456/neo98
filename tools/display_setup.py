#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The display mode of this port's display adapter in the registry.

Windows 98 keeps the mode the display comes up in under
HKEY_CURRENT_CONFIG\\Display\\Settings, which is Config\\0001\\Display\\Settings
of SYSTEM.DAT: "Resolution" as "width,height" and "BitsPerPixel", both REG_SZ.
USER reads them when the driver starts and ChangeDisplaySettings with
CDS_UPDATEREGISTRY writes them.

The hive of build/win98-c was written by Setup on the machine it ran on, a
Cirrus Logic card in QEMU, whose driver installs 640 by 480 at 8 bits
(Services\\Class\\Display\\0000\\DEFAULT, Mode "8,640,480"). The adapter this
port plays is the frame buffer of the host window, 800 by 600 at 32 bits. When
a Win98 machine gets another display adapter, the class installer writes the
new driver's mode into these two values; this module does the same for the
port's adapter, so the registry names the mode the display actually comes up
in. Every other value of the key (fonts, DPI) stays as Setup wrote it.

tools/import_registry.py merges the result into the imported image.
"""
SZ = 1
SETTINGS = 'Config\\0001\\Display\\Settings'
WIDTH, HEIGHT, DEPTH = 800, 600, 32


def additions(system_tree, user_tree=None):
    """[(hive, key path, [(name, type, payload)], source)] like tools/network_setup.py."""
    values = [('Resolution', SZ, f'{WIDTH},{HEIGHT}'.encode('ascii')),
              ('BitsPerPixel', SZ, f'{DEPTH}'.encode('ascii'))]
    return [('SYSTEM.DAT', SETTINGS, values, 'display adapter of the port, 800x600x32')]


if __name__ == '__main__':
    for hive, path, values, source in additions({}):
        print(f'[{hive}\\{path}]  ; {source}')
        for name, kind, payload in values:
            print(f'  {name} = {kind}:{payload!r}')
