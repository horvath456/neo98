/* SPDX-License-Identifier: GPL-2.0-or-later */
/* the DDRAW.DLL of DirectX 6.1 (4.06, the one of the Win98 SE
 * installation) in several processes. Its DLL_PROCESS_ATTACH starts
 * DDHELP.EXE, connects the two flat thunk blocks against DDRAW16.DLL, waits
 * for DDHELP's startup event and has DDHELP load DDRAW.DLL itself; only then
 * does it mark the shared data as ready. Every later process finds DDHELP's
 * process id in the shared data and spins in DllMain until that mark is set
 * (DDRAW.DLL ), which is where Super Collapse hung.
 *
 * The parent loads DDRAW.DLL and creates a DirectDraw object, then starts
 * itself twice as a child that does the same and ends. Each child has to end
 * within the wait; a hanging DllMain shows as a wait timeout. Lines start
 * with DD61; tools/test_ddraw61_attach.py checks them. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, const char *, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U CloseHandle(U);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *M1)(void *);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    const char *prefix = "DD61 ";
    while (prefix[i]) line[at++] = prefix[i++];
    i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}

/* Loads DDRAW.DLL, creates the object of the primary display and releases
 * it. Answers the HRESULT, or 0xdead0001 when the DLL does not load. */
static U directdraw(const char *who)
{
    const U module = LoadLibraryA("DDRAW.DLL");
    number(who[0] == 'p' ? "parent_load" : "child_load", module ? 1 : 0);
    if (!module) return 0xdead0001u;
    const Create create = (Create)GetProcAddress(module, "DirectDrawCreate");
    if (!create) return 0xdead0002u;
    void *object = 0;
    const U result = create(0, &object, 0);
    if (!result && object) ((M1)(*(void ***)object)[2])(object);
    return result;
}

static U child(U index)
{
    char command[64] = "DD61.EXE child";
    U startup[17] = {68}, info[4] = {0};
    if (!CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, info)) return 0xdead0003u;
    const U waited = WaitForSingleObject(info[0], 30000);
    U code = 0;
    GetExitCodeProcess(info[0], &code);
    number(index ? "child2_wait" : "child1_wait", waited);
    number(index ? "child2_code" : "child1_code", code);
    CloseHandle(info[0]);
    CloseHandle(info[1]);
    return waited;
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    int is_child = 0;
    for (const char *p = command; *p; ++p)
        if (p[0] == 'c' && p[1] == 'h' && p[2] == 'i' && p[3] == 'l' && p[4] == 'd') is_child = 1;
    if (is_child)
    {
        const U result = directdraw("child");
        number("child_create", result);
        ExitProcess(result ? 2 : 7);
    }
    number("parent_create", directdraw("parent"));
    child(0);
    child(1);
    OutputDebugStringA("DD61 done");
    ExitProcess(0);
}
