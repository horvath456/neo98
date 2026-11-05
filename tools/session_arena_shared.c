// SPDX-License-Identifier: GPL-2.0-or-later
// A DLL with a section that carries IMAGE_SCN_MEM_SHARED, the way MPR, SHELL32
// and COMCTL32 carry theirs. The link step adds the flag with
// /section:.shrd,RWS. Win98 keeps such a section once for the whole session at
// image base plus RVA.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
#define EXPORT __declspec(dllexport)
// The shared words. Index 0 is the parent marker, 1 the child marker, 2 the
// handle of the shared heap, 3 a pointer into that heap and 4 the address of
// this very array, so both sides can compare it.
#pragma section(".shrd", read, write)
__declspec(allocate(".shrd")) volatile U shared_words[64] = {0};
// An ordinary data section of the same image. It stays private per process.
static volatile U private_words[64] = {0};
EXPORT U *SharedBlock(void) { return (U *)shared_words; }
EXPORT U *PrivateBlock(void) { return (U *)private_words; }
API U DisableThreadLibraryCalls(U);
__declspec(dllexport) int __stdcall DllMain(U module, U reason, U reserved)
{
    (void)reserved;
    if (reason == 1)
        DisableThreadLibraryCalls(module);
    return 1;
}
