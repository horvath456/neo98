/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest fixture. Under Win98 a KERNEL32 function has one address in every
   process, and a shared image may carry it: DDRAW.DLL lies in the shared arena
   with one bound import table, and DDHELP.EXE runs DDRAW code whose imports a
   game bound. This launcher takes an address no process has used yet
   (GetProcAddress of GetSystemDefaultLangID), leaves it in a named mapping and
   starts the child, which calls it. The child ends with the answer plus one;
   NEO98 used to stop the child with "Invalid API gate". Codes 4740..4759. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U GetProcAddress(U, const char *);
API U CreateFileMappingA(U, U, U, U, U, const char *);
API U MapViewOfFile(U, U, U, U, U);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}

void mainCRTStartup(void)
{
    const U kernel = GetModuleHandleA("KERNEL32.DLL");
    const U function = GetProcAddress(kernel, "GetSystemDefaultLangID");
    check(function != 0, 4741);
    const U mapping = CreateFileMappingA(~0u, 0, 4, 0, 4096, "NEO98SessionGate");
    check(mapping != 0, 4742);
    U *view = (U *) MapViewOfFile(mapping, 2, 0, 0, 0);
    check(view != 0, 4743);
    if (!view) ExitProcess(failure);
    view[0] = function;
    view[1] = ((U (__stdcall *)(void)) function)() & 0xffff;
    view[2] = GetProcAddress(kernel, "GetLastError");
    view[3] = GetProcAddress(kernel, "SetLastError");
    view[4] = GetProcAddress(kernel, "TlsGetValue");
    view[5] = GetProcAddress(kernel, "TlsSetValue");
    check(view[2] && view[3] && view[4] && view[5], 4748);
    if (failure) ExitProcess(failure);
    ((U (__stdcall *)(U, U))view[5])(79, 0x12345678);
    U startup[17] = {68};
    U information[4] = {0};
    char command[] = "D:\\GATECHLD.EXE";
    check(CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, information) != 0, 4744);
    check(WaitForSingleObject(information[0], 20000) == 0, 4745);
    U code = 0;
    check(GetExitCodeProcess(information[0], &code) != 0, 4746);
    check(code == view[1] + 1, 4747);
    check(((U (__stdcall *)(U))view[4])(79) == 0x12345678, 4749);
    ExitProcess(failure);
}
