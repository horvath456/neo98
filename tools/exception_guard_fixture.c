/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A handler that calls the guard frame of the dispatch itself, as an unwind
   loop of the original does (RPCRT4 calls each fs:[0] frame's
   handler; ). During the handler, fs:[0] is the guard links
   around it: {previous, guard handler, frame of the handler}. Called while
   unwinding the guard answers ExceptionContinueSearch (1); called in the
   search it is the nested-exception guard: ExceptionNestedException (2) with
   the handler's frame in the dispatcher context. The exit code says which
   answers came: 7 for both right. tools/test_exception_guard_guest.py */
typedef unsigned U;
__declspec(dllimport) void __stdcall RaiseException(U, U, U, const U *);
__declspec(dllimport) void __stdcall ExitProcess(U);

typedef U(__cdecl *Handler)(U *, void *, void *, U *);
static volatile U result = 9;

static U __cdecl handler(U *record, void *frame, void *context, U *dispatcher)
{
    (void)frame; (void)dispatcher;
    if (record[1] & 6) return 1;
    U *guard;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(guard));
    U unwind[20] = {0}, search[20] = {0}, out = 0;
    unwind[1] = 2;
    const U first = ((Handler)guard[1])(unwind, guard, context, &out);
    const U second = ((Handler)guard[1])(search, guard, context, &out);
    result = first == 1 && second == 2 && out == guard[2] ? 7 : 8;
    return 0;
}

void __cdecl mainCRTStartup(void)
{
    U frame[2];
    __asm__ volatile("movl %%fs:0, %0" : "=r"(frame[0]));
    frame[1] = (U)handler;
    __asm__ volatile("movl %0, %%fs:0" : : "r"(frame) : "memory");
    RaiseException(0xe0000001u, 0, 0, 0);
    __asm__ volatile("movl %0, %%fs:0" : : "r"(frame[0]) : "memory");
    ExitProcess(result);
}
