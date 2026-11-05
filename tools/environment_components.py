# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Environment package's guest import manifest; values are stdcall stack bytes."""
KERNEL_IMPORTS = {
    'GetEnvironmentStrings': 0, 'GetEnvironmentStringsA': 0, 'FreeEnvironmentStringsA': 4,
    'GetEnvironmentVariableA': 12, 'SetEnvironmentVariableA': 8, 'ExpandEnvironmentStringsA': 12,
    'GetEnvironmentStringsW': 0, 'FreeEnvironmentStringsW': 4, 'GetEnvironmentVariableW': 12,
    'SetEnvironmentVariableW': 8, 'ExpandEnvironmentStringsW': 12, 'GetTempPathA': 8,
    'SetLastError': 4, 'GetLastError': 0, 'ExitProcess': 4, 'CreateThread': 24,
    'WaitForSingleObject': 8, 'GetExitCodeThread': 8, 'CloseHandle': 4,
}
FIXTURE = 'environment_family_fixture.c'
ENTRY = 'run_environment_family_test'
