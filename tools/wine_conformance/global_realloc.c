/* SPDX-License-Identifier: GPL-2.0-or-later
 * GlobalReAlloc/LocalReAlloc growth of locked moveable blocks. */
#include <windows.h>
#include "wine/test.h"

static void grow_locked(BOOL global, UINT flags, const char *what)
{
    HANDLE h, blocker, grown;
    DWORD *p;
    unsigned i;
    h = global ? GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT, 132) : LocalAlloc(LMEM_MOVEABLE|LMEM_ZEROINIT, 132);
    /* A neighbour keeps the block from growing in place. */
    blocker = global ? GlobalAlloc(GMEM_FIXED, 64) : LocalAlloc(LMEM_FIXED, 64);
    ok(h && blocker, "%s: allocations\n", what);
    p = global ? GlobalLock(h) : LocalLock(h);
    ok(p != NULL, "%s: lock\n", what);
    if (!p) return;
    p[0] = 0x12345678; p[32] = 0x9abcdef0;
    SetLastError(0xdeadbeef);
    grown = global ? GlobalReAlloc(h, 4096, flags) : LocalReAlloc(h, 4096, flags);
    trace("%s: result=%p handle=%p error=%lu\n", what, grown, h, GetLastError());
    if (flags & GMEM_MOVEABLE)
    {
        ok(grown == h, "%s: locked block grows with GMEM_MOVEABLE\n", what);
        p = global ? GlobalLock(h) : LocalLock(h);
        ok(p && p[0] == 0x12345678 && p[32] == 0x9abcdef0, "%s: prefix kept\n", what);
        for (i = 33; p && i < 1024 && !p[i]; ++i) {}
        ok(p && i == 1024, "%s: zeroed growth\n", what);
        ok(((global ? GlobalFlags(h) : LocalFlags(h)) & 255) == 2, "%s: lock count kept\n", what);
    }
    else
    {
        ok(grown == NULL, "%s: locked block stays without GMEM_MOVEABLE\n", what);
        ok(GetLastError() != 0xdeadbeef, "%s: error set\n", what);
    }
    if (global) { GlobalFree(blocker); while (GlobalUnlock(h)) {} GlobalFree(h); }
    else { LocalFree(blocker); while (LocalUnlock(h)) {} LocalFree(h); }
}

START_TEST(global_realloc)
{
    grow_locked(TRUE, GMEM_MOVEABLE|GMEM_ZEROINIT, "global moveable");
    grow_locked(TRUE, GMEM_ZEROINIT, "global in place");
    grow_locked(FALSE, LMEM_MOVEABLE|LMEM_ZEROINIT, "local moveable");
    grow_locked(FALSE, LMEM_ZEROINIT, "local in place");
}
