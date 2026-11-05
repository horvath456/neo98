/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared heaps and named views that come and go many times.
   RPCRT4 makes two HEAP_SHARED heaps and a named view on every load and
   destroys them on unload; MSI loads it with each DLL custom action, and
   after some thirty loads HeapCreate failed and RPCRT4 refused to load.
   Win98 hands a range _PageFree gave back out again. A heap stays readable
   right after HeapDestroy (MPR). */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U HeapCreate(U, U, U);
API U HeapDestroy(U);
API void *HeapAlloc(U, U, U);
API U CreateFileMappingA(U, U, U, U, U, const char *);
API void *MapViewOfFile(U, U, U, U, U);
API U UnmapViewOfFile(void *);
API U CloseHandle(U);
API void ExitProcess(U);
void mainCRTStartup(void)
{
    for (U i = 0; i < 400; ++i)
    {
        U view = CreateFileMappingA(~0u, 0, 4, 0, 0x400, "NEO98 shared heap reuse");
        if (!view) ExitProcess(1000 + i);
        volatile U *data = (volatile U *)MapViewOfFile(view, 2, 0, 0, 0);
        if (!data || data[0]) ExitProcess(2000 + i); /* a new view is zero */
        data[0] = i + 1;
        U a = HeapCreate(0x04000000, 0x1000, 0), b = HeapCreate(0x04000000, 1, 0x200000);
        if (!a || !b) ExitProcess(3000 + i);
        volatile U *p = (volatile U *)HeapAlloc(a, 8, 0x58), *q = (volatile U *)HeapAlloc(b, 8, 0x100);
        if (!p || !q || p[0] || q[0]) ExitProcess(4000 + i); /* HEAP_ZERO_MEMORY */
        p[0] = 0x5a5a0000 + i;
        q[0] = i;
        if (!HeapDestroy(b) || !HeapDestroy(a)) ExitProcess(5000 + i);
        if (p[0] != 0x5a5a0000 + i) ExitProcess(6000 + i); /* still readable */
        if (!UnmapViewOfFile((void *)data) || !CloseHandle(view)) ExitProcess(7000 + i);
    }
    ExitProcess(0);
}
