/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Static TLS over the public API: an EXE with its own TLS directory, a
   statically imported DLL with a second one, and a third copy that only
   LoadLibrary brings in. Exit code zero means every check held. */
#include "static_tls_support.h"
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U GetTickCount(void);
API U GetCurrentThreadId(void);
API U GetLastError(void);
API U SetLastError(U);
API U CreateThread(void *, U, void *, void *, U, U *);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U LoadLibraryA(const char *);
API U FreeLibrary(U);
API U GetProcAddress(U, const char *);
API U TlsGetValue(U);
API U TlsAlloc(void);
API U TlsFree(U);

typedef U(__stdcall *Probe)(U, U);
__declspec(dllimport) U __stdcall TlsProbe(U, U);

#define WORKERS 4
static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
static U tls_array(void)
{
    U value;
    __asm__ volatile("movl %%fs:0x2c, %0" : "=r"(value));
    return value;
}

/* The EXE template: an initialised object, one that lives on zeroed storage
   and a larger block, so a short copy or a missing fill shows up. */
__declspec(thread) U value = 0x11223344;
__declspec(thread) U zero;
__declspec(thread) unsigned char room[200] = {9, 8, 7};

static U exe_callback_count;
static void __stdcall exe_callback_body(void *module, U reason, void *reserved)
{
    (void)module;
    (void)reason;
    (void)reserved;
    ++exe_callback_count;
}
__attribute__((section(".CRT$XLB"))) TlsCallback exe_callback = exe_callback_body;

static U worker_value_address[WORKERS], worker_dll_address[WORKERS], worker_array[WORKERS];

static U __stdcall worker(void *parameter)
{
    const U slot = (U)parameter;
    /* The template, not whatever another thread left behind. */
    check(value == 0x11223344, 100 + slot);
    check(zero == 0, 110 + slot);
    check(room[0] == 9 && room[1] == 8 && room[2] == 7 && room[3] == 0 && room[199] == 0, 120 + slot);
    check(TlsProbe(2, 0) == 0xd0000001u, 130 + slot);
    check(TlsProbe(4, 0) == 0 && TlsProbe(6, 1) == 2, 140 + slot);
    worker_value_address[slot] = (U)&value;
    worker_dll_address[slot] = TlsProbe(1, 0);
    worker_array[slot] = tls_array();
    check((U)&value - TlsGetValue(_tls_index) < 512, 145 + slot);
    value = 0x1000 + slot;
    zero = 0x2000 + slot;
    room[7] = (unsigned char)(0x40 + slot);
    TlsProbe(3, 0xe000 + slot);
    TlsProbe(5, 0xf000 + slot);
    /* Survive yields and API calls. */
    for (U i = 0; i < 4; ++i)
    {
        Sleep(1);
        GetTickCount();
        SetLastError(0x2a);
        check(GetLastError() == 0x2a, 150 + slot);
    }
    check(value == 0x1000 + slot, 160 + slot);
    check(zero == 0x2000 + slot, 170 + slot);
    check(room[7] == 0x40 + slot, 180 + slot);
    check(TlsProbe(2, 0) == 0xe000 + slot, 190 + slot);
    check(TlsProbe(4, 0) == 0xf000 + slot, 200 + slot);
    return 0;
}

void mainCRTStartup(void)
{
    U handles[WORKERS];
    /* Indices come from the process TLS pool in load order: the EXE first,
       then the DLLs the import table pulled in. */
    check(_tls_index == 0, 1);
    check(TlsProbe(0, 0) == 1 && TlsProbe(11, 0) == 1, 2);
    /* Win98 keeps its static TLS in the same 80 slots as TlsAlloc, and
       FS:[2Ch] is that slot array. */
    check(tls_array() != 0, 3);
    /* The slot holds the block base; the thread objects sit inside it. */
    check(TlsGetValue(_tls_index) != 0 && (U)&value - TlsGetValue(_tls_index) < 512, 4);
    check(TlsGetValue(TlsProbe(0, 0)) == TlsProbe(12, 0), 5);
    const U first = TlsAlloc();
    check(first == 2 && TlsFree(first), 6);

    check(value == 0x11223344 && zero == 0, 7);
    check(room[0] == 9 && room[2] == 7 && room[3] == 0 && room[199] == 0, 8);
    check(TlsProbe(2, 0) == 0xd0000001u && TlsProbe(4, 0) == 0 && TlsProbe(6, 1) == 2, 9);
    const U main_value = (U)&value, main_dll = TlsProbe(1, 0);
    check(main_value && main_dll && main_value != main_dll, 10);
    value = 0x55667788;
    zero = 0x99aabbcc;
    room[9] = 0x5a;
    TlsProbe(3, 0xabcdef01);

    for (U i = 0; i < WORKERS; ++i)
    {
        U id = 0;
        handles[i] = CreateThread(0, 0, (void *)worker, (void *)i, 0, &id);
        check(handles[i] != 0 && id != 0, 11 + i);
    }
    for (U i = 0; i < WORKERS; ++i)
    {
        check(WaitForSingleObject(handles[i], 5000) == 0, 20 + i);
        check(CloseHandle(handles[i]) != 0, 30 + i);
    }
    /* Separate blocks: no worker shares an address with this thread or with
       another worker, and every worker saw its own slot array. */
    for (U i = 0; i < WORKERS; ++i)
    {
        check(worker_value_address[i] != main_value, 40 + i);
        check(worker_dll_address[i] != main_dll, 44 + i);
        check(worker_array[i] != 0 && worker_array[i] != tls_array(), 48 + i);
        for (U j = 0; j < i; ++j)
            check(worker_value_address[i] != worker_value_address[j] &&
                      worker_dll_address[i] != worker_dll_address[j],
                  52 + i);
    }
    /* Nothing a worker wrote reached this thread. */
    check(value == 0x55667788 && zero == 0x99aabbcc, 60);
    check(room[7] == 0 && room[9] == 0x5a, 61);
    check(TlsProbe(2, 0) == 0xabcdef01 && TlsProbe(4, 0) == 0, 62);

    /* Win98 never reads AddressOfCallBacks. Neither the EXE's list nor the
       DLL's ran, for any reason and on any thread. */
    check(exe_callback_count == 0, 70);
    const U records = TlsProbe(7, 0);
    check(records != 0, 71);
    for (U i = 0; i < records; ++i)
    {
        check(TlsProbe(8, i) >= 0x100, 72);
        /* The block was in place before the notification that reported it. */
        if (TlsProbe(8, i) == 0x102)
            check(TlsProbe(10, i) == 0xd0000001u, 73);
    }
    check(TlsProbe(8, 0) == 0x101, 74);

    /* A module that arrives after process start gets no index and no block.
       It keeps the zero its file carries and therefore reads slot 0, which
       belongs to the EXE. This is Win98's result, not an improvement. */
    const U late = LoadLibraryA("TLSLATE.DLL");
    check(late != 0, 80);
    const Probe probe = (Probe)GetProcAddress(late, "TlsProbe");
    check(probe != 0, 81);
    if (probe)
    {
        check(probe(11, 0) == 2, 82);
        check(probe(0, 0) == 0, 83);
        check(probe(1, 0) == main_value, 84);
        check(probe(2, 0) == 0x55667788, 85);
        probe(3, 0x0badf00d);
        check(value == 0x0badf00d, 86);
        value = 0x55667788;
    }
    check(FreeLibrary(late) != 0, 87);
    check(value == 0x55667788, 88);

    ExitProcess(failure);
}
