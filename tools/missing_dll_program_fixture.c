/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The program of tools/test_missing_dll_native.py. It imports DINPUT8.dll the
   way Krokomania does, and the test drive has no such file, so it never runs;
   exit code 4719 would mean it did. */
typedef unsigned U;
__declspec(dllimport) U __stdcall DirectInput8Create(U, U, U, U, U);
__declspec(dllimport) void __stdcall ExitProcess(U);

void mainCRTStartup(void)
{
    DirectInput8Create(0, 0, 0, 0, 0);
    ExitProcess(4719);
}
