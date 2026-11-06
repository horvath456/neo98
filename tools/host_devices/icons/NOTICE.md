# Neo98 hardware icons

Copyright (C) 2026 Andreas Horvath
SPDX-License-Identifier: GPL-2.0-or-later

`eject-tray.ico` and `eject-dialog.ico` are original geometric drawings made by
`generate.py`. Run `python3 tools/host_devices/icons/generate.py` to reproduce
them. Both contain one 16x16, 32-bit image with an AND transparency mask.
The hardware manager consumes them through `tools/build_host_devices.py`.

These replace the earlier KB242975-derived images. No pixels from SYSTRAY.EXE,
HOTPLUG.DLL or NUSB are used by the generator.

When the installed Windows has them, the tray shows Windows 98's own icons
instead, read at run time from the user's drive C: (SYSTRAY.EXE for the
hardware icon, POWERCFG.CPL for the power icons). Nothing of those files is
in this repository; these drawings stand in when the files are missing.
