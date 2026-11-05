// SPDX-License-Identifier: GPL-2.0-or-later
// Two processes over the session arena. The fixture starts itself as its own
// child; the first letter of the command line selects the role. It checks the
// three things the arena has to deliver, and the three it must not:
// a section with IMAGE_SCN_MEM_SHARED has one address and one set of bytes
// a heap from HeapCreate with HEAP_SHARED is one heap for both processes
// a pointer into either of them means the same thing on both sides
// an ordinary data section of the same image stays private
// a private heap of one process is no heap in the other
// a shared heap is still refused after HeapDestroy
//
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API U FreeLibrary(U);
API U HeapCreate(U, U, U);
API U HeapAlloc(U, U, U);
API U HeapFree(U, U, U);
API U HeapSize(U, U, U);
API U HeapDestroy(U);
API U GetProcessHeap(void);
API U CreateProcessA(const char *, char *, U, U, U, U, const char *, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U CloseHandle(U);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetCurrentProcessId(void);
static void check(U v, U code)
{
    if (!v)
        ExitProcess(code);
}
// Offsets in the shared section, in DWORDs.
#define PARENT_MARK 0
#define CHILD_MARK 1
#define SHARED_HEAP 2
#define PARENT_BLOCK 3
#define CHILD_BLOCK 4
#define SECTION_ADDRESS 5
#define PRIVATE_HEAP 6
#define PRIVATE_ADDRESS 7
#define FOREIGN_GATE 8
typedef U *(__stdcall *BlockEntry)(void);
void mainCRTStartup(void)
{
    char *line = GetCommandLineA();
    if (line[0] == 'O')
    {
        check(LoadLibraryA("C:\\COLLIDE.DLL"), 17630);
        ExitProcess(67);
    }
    U library = LoadLibraryA("C:\\SHRD.DLL");
    check(library, 17600);
    BlockEntry shared_entry = (BlockEntry)GetProcAddress(library, "SharedBlock");
    BlockEntry private_entry = (BlockEntry)GetProcAddress(library, "PrivateBlock");
    check(shared_entry && private_entry, 17601);
    U *shared = shared_entry(), *own = private_entry();
    check(shared && own, 17602);
    if (line[0] == 'C')
    {
        // The child. The section is the parent's section, at the same address.
        check(shared[PARENT_MARK] == 0xc0ffee01u, 17610);
        check(shared[SECTION_ADDRESS] == (U)shared, 17611);
        // The pointer the parent put into the shared heap still points at the
        // parent's bytes.
        check(shared[SHARED_HEAP] && shared[PARENT_BLOCK], 17612);
        check(*(U *)shared[PARENT_BLOCK] == 0xdeadbeefu, 17613);
        // The heap itself works from here: the handle is a session address.
        check(HeapSize(shared[SHARED_HEAP], 0, shared[PARENT_BLOCK]) >= 16, 17614);
        U block = HeapAlloc(shared[SHARED_HEAP], 8, 32);
        check(block, 17615);
        *(U *)block = 0x5a5a5a5au;
        shared[CHILD_BLOCK] = block;
        // An ordinary data section of the same image is this process's own.
        check(own[0] == 0, 17616);
        own[0] = 0x1234;
        shared[PRIVATE_ADDRESS] = (U)own;
        // A private heap of the parent is no heap here. The public wrapper
        // answers ERROR_INVALID_PARAMETER for a handle it does not know.
        SetLastError(0);
        check(!HeapAlloc(shared[PRIVATE_HEAP], 8, 16) && GetLastError() == 87, 17617);
        // A gate the child binds first and leaves in the section, the way
        // SHLWAPI keeps WINMM's PlaySoundA in its delay-load cell.
        U winmm = LoadLibraryA("WINMM.DLL");
        U version = GetProcAddress(winmm, "timeGetTime");
        check(winmm && version, 17619);
        shared[FOREIGN_GATE] = version;
        shared[CHILD_MARK] = GetCurrentProcessId();
        check(FreeLibrary(library), 17618);
        ExitProcess(66);
    }
    // The parent.
    check(shared[PARENT_MARK] == 0, 17500);
    shared[PARENT_MARK] = 0xc0ffee01u;
    shared[SECTION_ADDRESS] = (U)shared;
    check(own[0] == 0, 17501);
    own[0] = 0x4321;
    // HeapCreate turns HEAP_SHARED into the shared VMM arena, so
    // this heap has to live at or above 80000000h.
    U heap = HeapCreate(0x04000000, 0, 0x100000);
    check(heap, 17502);
    check(heap >= 0x80000000u, 17503);
    U block = HeapAlloc(heap, 8, 32);
    check(block && block > heap, 17504);
    *(U *)block = 0xdeadbeefu;
    shared[SHARED_HEAP] = heap;
    shared[PARENT_BLOCK] = block;
    // A private heap of this process, for the child's negative probe.
    U private_heap = HeapCreate(0, 0, 0x10000);
    check(private_heap && private_heap < 0x80000000u, 17505);
    shared[PRIVATE_HEAP] = private_heap;
    U si[17] = {68}, pi[4] = {0}, code = 0;
    check(CreateProcessA("C:\\ARENA.EXE", (char *)"C:\\ARENA.EXE C", 0, 0, 0, 0x20, 0, 0, si, pi), 17506);
    check(WaitForSingleObject(pi[0], ~0u) == 0, 17507);
    check(GetExitCodeProcess(pi[0], &code) && code == 66, 17508);
    // What the child wrote into the section is here.
    check(shared[CHILD_MARK] && shared[CHILD_MARK] != GetCurrentProcessId(), 17509);
    // The block the child allocated on the shared heap belongs to the same
    // heap, and its bytes are the child's bytes.
    check(shared[CHILD_BLOCK] > heap, 17510);
    check(*(U *)shared[CHILD_BLOCK] == 0x5a5a5a5au, 17511);
    check(HeapSize(heap, 0, shared[CHILD_BLOCK]) >= 32, 17512);
    check(HeapFree(heap, 0, shared[CHILD_BLOCK]), 17513);
    // The child's gate works here although this process never bound that
    // export: its slot lies beyond every import of this process.
    U (__stdcall *version)(void) = (U (__stdcall *)(void))shared[FOREIGN_GATE];
    check(version && version() != 0, 17531);
    // The ordinary data section stayed with each process.
    check(own[0] == 0x4321, 17514);
    check(shared[PRIVATE_ADDRESS] == (U)own, 17515);
    check(HeapFree(heap, 0, block), 17516);
    check(HeapDestroy(heap), 17517);
    // After HeapDestroy the handle is no heap any more, in this process as in
    // the session. The pages of the block stay readable, which is what MPR
    // relies on; see
    SetLastError(0);
    check(!HeapAlloc(heap, 8, 16) && GetLastError() == 87, 17518);
    check(HeapDestroy(private_heap), 17519);
    check(CloseHandle(pi[0]) && CloseHandle(pi[1]), 17520);
    U foreign = LoadLibraryA("C:\\FOREIGN.DLL");
    check(foreign, 17522);
    unsigned char *foreign_bytes = (unsigned char *)GetProcAddress(foreign, "CollisionBytes");
    check((U)foreign_bytes, 17523);
    unsigned char before[16];
    U n;
    for (n = 0; n < sizeof before; ++n) before[n] = foreign_bytes[n];
    U overlap_pi[4] = {0};
    check(CreateProcessA("C:\\ARENA.EXE", (char *)"O", 0, 0, 0, 0x20, 0, 0, si, overlap_pi), 17524);
    check(WaitForSingleObject(overlap_pi[0], ~0u) == 0, 17525);
    code = 0;
    check(GetExitCodeProcess(overlap_pi[0], &code) && code == 67, 17526);
    for (n = 0; n < sizeof before; ++n) check(foreign_bytes[n] == before[n], 17527);
    check(CloseHandle(overlap_pi[0]) && CloseHandle(overlap_pi[1]), 17528);
    check(FreeLibrary(foreign), 17529);
    check(FreeLibrary(library), 17521);
    ExitProcess(0);
}
