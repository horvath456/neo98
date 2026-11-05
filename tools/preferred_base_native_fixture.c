/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OLE32.DLL (preferred, C1000h bytes) and CRTDLL.DLL
 *  load at their preferred bases, as in every Win98 process. The
 * port used to reserve those addresses for descriptors, TEBs and process data. A second thread checks its
 * own TEB through FS and TLS, so the moved pages are exercised.
 * Exit code: 0 on success, 5601.. for the first failed check. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U CreateThread(U, U, void *, U, U, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API U TlsAlloc(void);
API U TlsSetValue(U, U);
API U TlsGetValue(U);
API void ExitProcess(U);

static U slot;

static U self(void)
{
    U teb;
    __asm__ volatile("movl %%fs:0x18, %0" : "=r"(teb));
    return teb;
}

static U __stdcall worker(U parameter)
{
    if (!TlsSetValue(slot, parameter) || TlsGetValue(slot) != parameter)
        return 1;
    /* Two threads never share a TEB. */
    return self() == parameter ? 2 : 0;
}

void mainCRTStartup(void)
{
    if (LoadLibraryA("OLE32.DLL") != 0x7ff20000u)
        ExitProcess(5601);
    if (LoadLibraryA("CRTDLL.DLL") != 0x7fc00000u)
        ExitProcess(5602);
    slot = TlsAlloc();
    if (slot == 0xffffffffu || !TlsSetValue(slot, 0x1234))
        ExitProcess(5603);
    U id = 0, code = 99;
    const U thread = CreateThread(0, 0, (void *)worker, self(), 0, &id);
    if (!thread || WaitForSingleObject(thread, 10000) != 0 || !GetExitCodeThread(thread, &code))
        ExitProcess(5604);
    if (code != 0)
        ExitProcess(5610 + code);
    if (TlsGetValue(slot) != 0x1234)
        ExitProcess(5605);
    ExitProcess(0);
}
