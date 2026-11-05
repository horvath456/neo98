// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
/* The part of <windowsx.h> the samples use: the coordinates of a mouse
   message. Windows keeps these two macros in this header, not in windows.h. */
#ifndef NEO98_WINDOWSX_H
#define NEO98_WINDOWSX_H
#include <windows.h>
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif
