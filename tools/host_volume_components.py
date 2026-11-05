# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
IMPORTS = {'KERNEL32': {'ExitProcess': 4, 'GetLastError': 0, 'GetCommandLineA': 0, 'CreateFileA': 28,
                        'ReadFile': 20, 'WriteFile': 20, 'CloseHandle': 4, 'GetFileSize': 8,
                        'SetFilePointer': 16, 'FlushFileBuffers': 4, 'GetFileAttributesA': 4,
                        'SetFileAttributesA': 8, 'CreateDirectoryA': 8, 'RemoveDirectoryA': 4,
                        'DeleteFileA': 4, 'MoveFileA': 8, 'FindFirstFileA': 8, 'FindNextFileA': 8,
                        'FindClose': 4, 'GetShortPathNameA': 12, 'GetLongPathNameA': 12,
                        'SetCurrentDirectoryA': 4, 'GetCurrentDirectoryA': 8, 'GetFullPathNameA': 16,
                        'GetDriveTypeA': 4, 'GetLogicalDrives': 0, 'GetVolumeInformationA': 32,
                        'GetDiskFreeSpaceA': 20, 'GetFileTime': 16},
           'ADVAPI32': {'RegCreateKeyExA': 36, 'RegSetValueExA': 24, 'RegQueryValueExA': 24,
                        'RegFlushKey': 4, 'RegCloseKey': 4}}
