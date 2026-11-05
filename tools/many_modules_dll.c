/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved) { return TRUE; }
/* Keep an absolute data reference so each copy can be relocated. */
__declspec(dllexport) volatile int counter=37;
__declspec(dllexport) int Value(void) { return counter; }
