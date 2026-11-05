#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Original file set per application, shared by the kernel build and the host probes.

`files` lists the unchanged original binaries an application needs as real
files. The kernel embeds them for the selected `--app` and seeds them into the
RAM volume; DLLs land in C:\\WINDOWS\\SYSTEM and programs in C:\\WINDOWS, the
same layout tools/probe_application.cpp and tools/wordpad_demo.cpp use.

`embedded` marks applications whose program is already part of the default
notepad_program.h asset set, so their image does not grow per build.

`trees` names whole folders of the installation the application needs on the
RAM volume, as (guest root, path inside the image) pairs. Folders are not
program files: `tools/import_start_menu.py` extracts one with its own
provenance report, and `tools/unicorn_assets.py` embeds it beside the program's
DLLs. Windows 98 builds the Start menu out of such a folder; see


No profile names DLLs any more that run as original x86 guest code: since
PLAN.md step 7 that is the default for every DLL outside `src/dll_routes.h`,
including MSVCRT, SHELL32, SHLWAPI, COMCTL32 and COMDLG32. A test of one of our
implementations turns a single DLL back with `--native-module`.

Since PLAN.md step 7 even NOTEPAD.EXE needs original DLLs, because it imports
SHELL32 and COMDLG32, so the four small profiles carry `system_tree` as well.

`system_tree` marks the applications that need no `files` list any more: the
loader finds their DLLs in the imported Win98 tree the way Win98 searches, so
the profile only names the program and the test steps. The runners mount that
tree with `--system-tree`. An application needs no
profile at all to start: `python3 neo98.py host --exe "C:\\WINDOWS\\SOL.EXE"`.
"""

ACCESSORIES = '::/PROGRA~1/ACCESS~1'
SYSTEM = '::/WINDOWS/SYSTEM'
WINDOWS = '::/WINDOWS'

APPLICATIONS = {
    'notepad': {
        'program': 'NOTEPAD.EXE', 'files': [], 'embedded': True, 'system_tree': True,
        'source': f'{WINDOWS}/NOTEPAD.EXE',
        'window': 'Notepad', 'title': 'Untitled - Notepad',
        'hint': 'Original Win98 Notepad in the ARM kernel. Type, select, Ctrl+C/X/V/Z, Edit > Word Wrap. '
                'Search > Find and the unsaved-document prompt work. Open/Save As dialogs remain unavailable. '
                'Close QEMU to stop.',
    },
    'notepad-file': {
        'program': 'NOTEPAD.EXE', 'files': [], 'embedded': True, 'system_tree': True,
        'source': f'{WINDOWS}/NOTEPAD.EXE',
        'window': 'Notepad file editing', 'title': 'NEO98.TXT - Notepad',
        'hint': 'Original Win98 Notepad opens C:\\Documents\\NEO98.TXT. Edit it and use File > Save. '
                'Files currently live in RAM and reset when the kernel restarts. Close QEMU to stop.',
    },
    'calc': {
        'program': 'CALC.EXE', 'files': [], 'embedded': True, 'system_tree': True,
        'source': f'{WINDOWS}/CALC.EXE',
        'window': 'Calculator', 'title': 'Calculator',
        'hint': 'Original Win98 Calculator in the ARM kernel. Use the buttons or View menu. Close QEMU to stop.',
    },
    'regedit': {
        'program': 'REGEDIT.EXE', 'files': [], 'embedded': True, 'system_tree': True,
        'source': f'{WINDOWS}/REGEDIT.EXE',
        'window': 'Registry Editor', 'title': 'Registry Editor',
        'hint': 'Original Win98 Registry Editor in the ARM kernel. Browse HKEY_LOCAL_MACHINE\\Software\\NEO98; '
                'Edit > Modify changes a value. Registry data is held in RAM. Close QEMU to stop.',
    },
    'taskman': {
        'program': 'TASKMAN.EXE', 'files': [], 'embedded': True, 'system_tree': True,
        'source': f'{WINDOWS}/TASKMAN.EXE',
        'window': 'Tasks', 'title': 'Tasks',
        'hint': 'Original Win98 Taskman in the ARM kernel. Close QEMU to stop.',
    },
    'wordpad': {
        # MFC42, MSVCRT, OLE32, OLEAUT32, IMM32, RICHED20 and RPCRT4 used to be
        # listed here; the Win98 search finds every one of them in SYSTEM.
        'program': 'WORDPAD.EXE', 'embedded': False, 'system_tree': True,
        'source': f'{ACCESSORIES}/WORDPAD.EXE',
        'files': [],
        # The frame caption reads "Document - WordPad" on the framebuffer, but
        # Application::MainWindow is an MFC helper window without a caption,
        # so the kernel's ready line reports an empty title.
        'window': 'WordPad', 'title': '',
        'hint': 'Original Win98 WordPad in the ARM kernel, with the original MFC42, RICHED20 and OLE DLLs '
                'seeded into C:\\WINDOWS\\SYSTEM. Type into the original RichEdit. Close QEMU to stop.',
    },
    'explorer': {
        # No DLL list any more: the Win98 search finds SHLWAPI, SHELL32,
        # COMCTL32, SHDOCVW, SHDOC401, BROWSEUI, OLE32, RPCRT4, OLEAUT32, the
        # control panel applets, RUNDLL32 and the icon sources of the desktop
        # in the Win98 folder. EXPLORER.EXE is the shell only when [boot] Shell
        # of SYSTEM.INI names it; the ISO installation does, and
        # tools/win98_folder.py keeps it that way. tools/test_explorer_shell_tree.py starts the
        # shell this way.
        'program': 'EXPLORER.EXE', 'embedded': False, 'system_tree': True,
        'source': f'{WINDOWS}/EXPLORER.EXE',
        'files': [],
        # The curated set only tools/test_explorer_regressions.py still seeds.
        # That test drives the folder window, the Show Desktop button and a
        # second process through twelve cycles, and on the whole installation
        # it needs painting and activation support; until then it keeps the
        # small RAM volume it has always used.
        'regression_files': [
                  # MSVCRT and COMDLG32 run as original x86 code since PLAN.md
                  # step 7, and CALC.EXE and NOTEPAD.EXE import them, so this
                  # curated RAM volume has to carry them too.
                  ('MSVCRT.DLL', f'{SYSTEM}/MSVCRT.DLL'),
                  ('COMDLG32.DLL', f'{SYSTEM}/COMDLG32.DLL'),
                  ('SHLWAPI.DLL', f'{SYSTEM}/SHLWAPI.DLL'),
                  ('SHELL32.DLL', f'{SYSTEM}/SHELL32.DLL'),
                  ('COMCTL32.DLL', f'{SYSTEM}/COMCTL32.DLL'),
                  ('SHDOCVW.DLL', f'{SYSTEM}/SHDOCVW.DLL'),
                  # SHDOCVW resolves SHCreateDesktop out of SHDOC401.DLL on
                  # this installation's 4.72 SHELL32.
                  ('SHDOC401.DLL', f'{SYSTEM}/SHDOC401.DLL'),
                  ('BROWSEUI.DLL', f'{SYSTEM}/BROWSEUI.DLL'),
                  ('OLE32.DLL', f'{SYSTEM}/OLE32.DLL'),
                  ('RPCRT4.DLL', f'{SYSTEM}/RPCRT4.DLL'),
                  ('OLEAUT32.DLL', f'{SYSTEM}/OLEAUT32.DLL'),
                  ('TIMEDATE.CPL', f'{SYSTEM}/TIMEDATE.CPL'),
                  # The Control Panel folder lists every applet of every.CPL
                  # in the system directory; POWRPROF is what the Run-key
                  # RUNDLL32 entry of this installation loads at boot. The
                  # reference system installation also had THEMES.CPL (Plus!) and the
                  # 3dfx applet and DLL; the ISO installation has neither.
                  *[(name, f'{SYSTEM}/{name}') for name in (
                      'MAIN.CPL', 'DESK.CPL', 'SYSDM.CPL', 'APPWIZ.CPL', 'INTL.CPL', 'MMSYS.CPL',
                      'ACCESS.CPL', 'PASSWORD.CPL', 'POWERCFG.CPL', 'MODEM.CPL', 'NETCPL.CPL',
                      'JOY.CPL', 'TELEPHON.CPL', 'INETCPL.CPL', 'STICPL.CPL',
                      'ODBCCP32.CPL', 'POWRPROF.DLL', 'SETUPAPI.DLL', 'NTDLL.DLL')],
                  ('RUNDLL32.EXE', f'{WINDOWS}/RUNDLL32.EXE'),
                  # Start menu targets: Accessories\\Notepad.lnk and Calculator.lnk
                  # resolve to these, and the shell launches them as children.
                  ('NOTEPAD.EXE', f'{WINDOWS}/NOTEPAD.EXE'),
                  ('CALC.EXE', f'{WINDOWS}/CALC.EXE'),
                  # My Documents draws its icon out of this shell extension.
                  ('MYDOCS.DLL', f'{SYSTEM}/MYDOCS.DLL'),
                  # Dial-Up Networking and Scheduled Tasks draw their icons out of these.
                  ('RNAUI.DLL', f'{SYSTEM}/RNAUI.DLL'),
                  ('MSTASK.DLL', f'{SYSTEM}/MSTASK.DLL'),
                  # The desktop's Internet Explorer item names mshtml.dll as its icon source.
                  ('MSHTML.DLL', f'{SYSTEM}/MSHTML.DLL')],
        # The Start menu is a folder, not an export: the tray asks SHELL32 for
        # the Start Menu shell folder and turns every subdirectory into a
        # submenu. The hive's Shell Folders already names this exact path. The
        # host runners read it out of the tree; the kernel has no host file
        # system yet and still embeds the folder.
        'trees': [('C:\\WINDOWS\\Start Menu', 'WINDOWS/Start Menu')],
        # Explorer's first window is the tray shell; Shell_TrayWnd carries no
        # caption, so the kernel's ready line reports an empty title, as with
        # WordPad's MFC helper window.
        'window': 'Explorer', 'title': '',
        'hint': 'Original Win98 Explorer in the ARM kernel, as original x86 code out of the imported '
                'tree, like every DLL it loads. It builds the '
                'taskbar, the Start button, the tray and the clock, and runs the Run keys of the '
                'installation. Close QEMU to stop.',
    },
    'lab': {
        'program': None, 'files': [], 'embedded': True,
        'window': 'component tests', 'title': None,
        'hint': 'Editor: type, select, Ctrl+C/X/V, Ctrl+Z, font buttons. Escape: graphics desktop. '
                'Desktop: E editor, M Minesweeper, D diagnostics, Space raster operation, Q halt.',
    },
}


# These applications use the installed x86 shell and common-control DLLs. They
# no longer list them: the Win98 search finds SHLWAPI, SHELL32, COMCTL32, OLE32,
# OLEAUT32, RPCRT4, SHDOCVW, SHDOC401, BROWSEUI, MFC42, MSVCRT and IMM32 in the
# system directory of the imported tree.
APPLICATIONS['mspaint'] = {
    'program': 'MSPAINT.EXE', 'source': f'{ACCESSORIES}/MSPAINT.EXE', 'embedded': False,
    'files': [], 'system_tree': True,
    'window': 'Paint', 'title': 'untitled - Paint',
    'hint': 'Original Windows 98 Paint on the native host.',
}
APPLICATIONS['cleanmgr'] = {
    'program': 'CLEANMGR.EXE', 'source': f'{WINDOWS}/CLEANMGR.EXE', 'embedded': False,
    'files': [], 'system_tree': True,
    'window': 'Disk Cleanup', 'title': 'Select Drive',
    'hint': 'Original Windows 98 Disk Cleanup. Drive selection is tested; cleanup is not yet verified.',
}

def application(name):
    """Look up one application, failing with the accepted names instead of a KeyError."""
    try:
        return APPLICATIONS[name]
    except KeyError:
        raise RuntimeError(f"Unknown application {name!r}; known: {', '.join(sorted(APPLICATIONS))}") from None


def libraries(name):
    """Host-probe file list: the extra original DLLs, in load order."""
    return [entry[0] for entry in application(name)['files']]


def regression_files(name):
    """The curated originals a single test still seeds itself, as (name, source) pairs."""
    return [tuple(entry) for entry in application(name).get('regression_files', [])]


def system_tree(name):
    """Whether this application's DLLs come out of the imported Win98 tree."""
    return bool(application(name).get('system_tree'))


def trees(name):
    """Folders of the installation this application needs on the RAM volume."""
    return [tuple(entry) for entry in application(name).get('trees', [])]


def guest_path(filename):
    """Where the RAM volume keeps an original file, as the host probes place it."""
    # Control panel applets are DLLs under another extension and live beside
    # them, which is where Control_RunDLL looks for TIMEDATE.CPL.
    system = filename.upper().endswith(('.DLL', '.CPL'))
    directory = 'C:\\WINDOWS\\SYSTEM' if system else 'C:\\WINDOWS'
    return f'{directory}\\{filename}'
