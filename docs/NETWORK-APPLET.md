# OK in the Network applet

What OK in the Control Panel's Network does on Windows 95 and 98, read from
the original COMCTL32 and NETDI, and what the port showed.

## Every OK commits

- COMCTL32's PropertySheet sets its answer to 0 when the sheet is
  initialised and to 1 when OK or Apply has sent PSN_KILLACTIVE and
  PSN_APPLY to the pages and none refused. Whether a page reported a change
  (PSM_CHANGED) plays no part: an OK without a change answers 1 as well. The
  port's sheet, modelled on the COMCTL32 of Windows 98
  (`src/win98_property_sheet_modal.h`), does the same.
- NETDI's NdiCplProperties goes on only for a PropertySheet answer of
  exactly 1; then it checks the components and commits.
- The commit rebuilds the binding tree each time (every Bindings key to
  OldBindings, new instances from the masters, the old ones and OldBindings
  deleted) and, while any network is installed, runs NET.INF's
  `NET_ANYNET.add` again, whose CopyFiles queue `vnetbios.vxd`. So SETUPX
  copies files from the source path after every OK; with the cabinets on the
  drive (`WINDOWS\OPTIONS\CABS`) it shows "Copying Files" and needs no disk;
  with a CD as the source path it would ask for the CD. The commit of
  Windows 98 copied more in the port's run, also without a change: the files
  of NET.INF's `NET_NOTPRIMARY.add` and `NET_ANYNET.add` and the real-mode
  NDIS files (`protman.dos`, `protman.exe`, `ndishlp.sys`).

## When it asks to restart

After the commit NETDI ORs together the DEVICE_INFO flags of its components
and calls RestartDialog: EW_REBOOTSYSTEM for DI_NEEDREBOOT (100h), or when
components were there before and none is left; EW_RESTARTWINDOWS for
DI_NEEDRESTART (80h). The Identification page compares its fields with the
values it started with when it gets PSN_APPLY and, when they differ, sets
DI_NEEDRESTART on the first component. A changed computer name, workgroup
or description therefore ends with "You must restart your computer before
the new settings will take effect"; an OK without a change asks nothing.

## What went wrong in the port

The SETUPX of Windows 95 keeps the names of its copy queue as local atoms
of its data segment and reads them out of the segment itself. The port's
atoms lived outside the segment, every queued name was empty, no file was
found in a cabinet, and SETUPX asked for "Windows 95 CD-ROM", then reported
that the file '' could not be found. The atoms now live where KRNL386 keeps
them ([WIN16-ATOMS.md](WIN16-ATOMS.md)). The SETUPX of Windows 98 uses no
atoms and copied from the cabinets before as well. Both versions now copy
from the cabinets and ask to restart after a change, as described above.

The commit copies each network instance value by value (RegEnumValue, then
RegSetValueEx with the length of the text plus one). A value the change
journal of the drive held without its terminator, written so by a tool that
prepared the drive, was handed out without one; the copy ran past its end,
the new instance's Driver named no class key, and the next start of the
applet found NetBEUI and the client gone and called the network incomplete.
The journal's replay now gives such text its terminator, as the import of
the hives does (`src/registry_persist.cpp`,
`tools/test_registry_hives_native.py`).

## Test

`tools/test_network_identification_host.py`: the description changed and
OK (no disk question of SETUPX, files copied from the cabinets, the
description stored, the restart question), then, in a new start on the same
drive, OK without a change (no disk question, no restart question).
