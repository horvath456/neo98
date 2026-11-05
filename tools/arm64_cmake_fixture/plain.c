/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#if !__STDC_HOSTED__
#error CMake application sources must use hosted C with compiler builtins
#endif
/* A C source beside the C++ ones, with a definition of its own. */
#include <windows.h>
#include <mmsystem.h>
static int constructed;
__attribute__((constructor)) static void Construct(void) { constructed = 1; }
int PlainValue(void) { return PLAIN_VALUE + constructed; }
/* A function of a system library the project names (winmm). */
unsigned PlainTime(void) { return timeGetTime(); }
