#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract of the ADVAPI32 registry fixture: RegConnectRegistryA and
RegNotifyChangeKeyValue with the key, event and thread calls they need."""
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'CreateEventA': 16, 'WaitForSingleObject': 8, 'ResetEvent': 4,
                 'CloseHandle': 4, 'CreateThread': 24, 'Sleep': 4,
                 'GetLastError': 0, 'SetLastError': 4},
    'ADVAPI32': {'RegConnectRegistryA': 12, 'RegNotifyChangeKeyValue': 20, 'RegCreateKeyExA': 36,
                 'RegOpenKeyA': 12, 'RegCloseKey': 4, 'RegSetValueExA': 24, 'RegDeleteValueA': 8,
                 'RegDeleteKeyA': 8, 'RegQueryValueExA': 24,
                 'CryptAcquireContextA': 20, 'CryptReleaseContext': 8,
                 'CryptContextAddRef': 12, 'CryptDestroyHash': 4, 'CryptDestroyKey': 4,
                 'CryptGenRandom': 12, 'CryptGetProvParam': 20, 'CryptSetProvParam': 16,
                 'CryptHashData': 16, 'CryptSignHashA': 24, 'CryptEnumProvidersA': 24,
                 'CryptEnumProviderTypesA': 24, 'CryptGetDefaultProviderA': 20,
                 'CryptSetProviderA': 8, 'CryptSetProviderExA': 16},
}
