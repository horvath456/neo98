# SPDX-License-Identifier: GPL-2.0-or-later
IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'GetLastError': 0, 'SetLastError': 4},
           'USER32': {'RegisterClassA': 4, 'UnregisterClassA': 8, 'CreateWindowExA': 48,
                      'DestroyWindow': 4, 'DefWindowProcA': 16, 'SendMessageA': 16,
                      'ShowWindow': 8, 'GetWindowRect': 8, 'GetSystemMetrics': 4,
                      'SystemParametersInfoA': 16, 'PostMessageA': 16, 'PeekMessageA': 20,
                      'DispatchMessageA': 4},
           'ADVAPI32': {'RegOpenKeyExA': 20, 'RegQueryValueExA': 24, 'RegCloseKey': 4}}
