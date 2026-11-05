/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The child of tools/session_gate_fixture.c: it calls the function address the
   launcher left in the mapping and ends with its answer plus one. It imports
   nothing but what it needs to reach the mapping. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U OpenFileMappingA(U, U, const char *);
API U MapViewOfFile(U, U, U, U, U);

void mainCRTStartup(void)
{
    const U mapping = OpenFileMappingA(4, 0, "NEO98SessionGate");
    U *view = mapping ? (U *) MapViewOfFile(mapping, 4, 0, 0, 0) : 0;
    if (!view || !view[0]) ExitProcess(4758);
    /* The child never imports these names. Shared function pointers must
       reach its own TLS and error word, including the invalid-index gate. */
    U (__stdcall *get_error)(void) = (void *)view[2];
    void (__stdcall *set_error)(U) = (void *)view[3];
    U (__stdcall *get_tls)(U) = (void *)view[4];
    U (__stdcall *set_tls)(U, U) = (void *)view[5];
    if (get_tls(79)) ExitProcess(4760);
    for (U i = 0; i < 2; ++i) {
        set_error(123);
        if (get_error() != 123 || !set_tls(79, 0x87654321) || get_error() != 123) ExitProcess(4761);
        if (get_tls(79) != 0x87654321 || get_error()) ExitProcess(4762);
        if (set_tls(80, 1) || get_error() != 87) ExitProcess(4763);
        if (get_tls(~0u) || get_error() != 87) ExitProcess(4764);
    }
    ExitProcess((((U (__stdcall *)(void)) view[0])() & 0xffff) + 1);
}
