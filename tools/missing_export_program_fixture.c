/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The program of tools/test_missing_export_native.py. It imports
   DirectDrawCreateEx the way Wonderland's wdlgame.exe does; DDRAW.DLL of
   DirectX 6.1 has no such export, so it never runs; exit code 4739 would
   mean it did. */
typedef unsigned U;
__declspec(dllimport) U __stdcall DirectDrawCreateEx(U, U, U, U);
__declspec(dllimport) void __stdcall ExitProcess(U);

void mainCRTStartup(void)
{
    DirectDrawCreateEx(0, 0, 0, 0);
    ExitProcess(4739);
}
