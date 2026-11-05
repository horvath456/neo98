# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""SHLWAPI ANSI registry package import manifest, in stdcall stack bytes."""
SHELL_IMPORTS = {
    'SHRegOpenUSKeyA':20, 'SHRegCreateUSKeyA':20, 'SHRegQueryUSValueA':32,
    'SHRegGetUSValueA':32, 'SHRegSetUSValueA':24, 'SHRegWriteUSValueA':24,
    'SHRegDeleteUSValueA':12, 'SHRegDeleteEmptyUSKeyA':12, 'SHRegCloseUSKey':4,
    'SHRegEnumUSKeyA':20, 'SHRegEnumUSValueA':32, 'SHRegQueryInfoUSKeyA':24,
    'SHGetValueA':24, 'SHSetValueA':24, 'SHDeleteValueA':12, 'SHDeleteEmptyKeyA':8,
    'SHDeleteOrphanKeyA':8, 'SHRegDuplicateHKey':4, 'SHQueryValueExA':24,
    'SHEnumKeyExA':16, 'SHEnumValueA':28, 'SHQueryInfoKeyA':20,
}
KERNEL_IMPORTS = {'ExitProcess':4, 'SetLastError':4, 'GetLastError':0}
ADVAPI_IMPORTS = {'RegOpenKeyExA':20, 'RegCloseKey':4, 'RegDeleteKeyA':8}
FIXTURE = 'shell_registry_fixture.c'
ENTRY = 'run_shell_registry_test'
