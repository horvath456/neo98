/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* retain a small musl heap after freeing its allocation. */
#include <stdlib.h>
__declspec(dllexport) void *MakeBlock(void)
{
    unsigned char *p = malloc(1000);
    if (p) { p[0] = 42; p[999] = 7; }
    return p;
}
__declspec(dllexport) void FreeBlock(void *p) { free(p); }
