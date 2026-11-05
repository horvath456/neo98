/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for one process keeps many reservations at once. NEO98's
   address space held at most 256 regions (and 576 interval descriptors),
   a limit of the prototype and not of Win98's VMM; SpongeBob's OMT.dll, with
   its DLLs, heaps and many 16-bit DIB sections, ran out, CreateDIBSection
   answered NULL and the game's screen device fell apart. The fixture keeps
   600 DIB sections and 300 VirtualAlloc reservations alive together.
   Exit code zero means every check held; codes 4840..4859. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U VirtualAlloc(U, U, U, U);
API U VirtualFree(U, U, U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U DeleteObject(U);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
static U sections[600], blocks[300];
void mainCRTStartup(void)
{
    /* 16-bit BI_BITFIELDS with 5-5-5 masks, top-down, as OMT asks. */
    U info[13] = {40, 4, (U) -1, 1 | 16 << 16, 3, 0, 0, 0, 0, 0, 0x7c00, 0x03e0, 0x001f};
    U made = 0;
    for (U i = 0; i < 600; ++i)
    {
        void *bits = 0;
        sections[i] = CreateDIBSection(0, info, 0, &bits, 0, 0);
        made += sections[i] != 0 && bits != 0;
    }
    check(made == 600, 4840);
    U reserved = 0;
    for (U i = 0; i < 300; ++i)
    {
        blocks[i] = VirtualAlloc(0, 0x10000, 0x2000, 4); /* MEM_RESERVE */
        reserved += blocks[i] != 0;
    }
    check(reserved == 300, 4841);
    for (U i = 0; i < 600; ++i)
        if (sections[i])
            check(DeleteObject(sections[i]) != 0, 4842);
    for (U i = 0; i < 300; ++i)
        if (blocks[i])
            check(VirtualFree(blocks[i], 0, 0x8000) != 0, 4843);
    /* Everything went back: the same number fits again. */
    made = 0;
    for (U i = 0; i < 600; ++i)
    {
        void *bits = 0;
        sections[i] = CreateDIBSection(0, info, 0, &bits, 0, 0);
        made += sections[i] != 0;
    }
    check(made == 600, 4844);
    ExitProcess(failure);
}
