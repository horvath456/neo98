# Neo98 hardware icon

Copyright (C) 2026 Andreas Horvath
SPDX-License-Identifier: GPL-2.0-or-later

`eject.ico` is an original pixel drawing in the style of the Unplug or Eject
Hardware icon: a green arrow that points up out of a grey card. `generate.py`
holds the drawing as a pixel map; run `python3 tools/host_devices/icons/generate.py`
to reproduce the file. It contains one 16x16, 32-bit image with an AND
transparency mask. The hardware manager consumes it through
`tools/build_host_devices.py`, for its tray icon and its dialogs.

No pixels from SYSTRAY.EXE, HOTPLUG.DLL or NUSB are used by the generator.
The power icons in the tray are Windows 98's own, read at run time from the
user's drive C: (POWERCFG.CPL); nothing of that file is in this repository.
