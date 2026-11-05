/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_packed_resource_guest.py: a module whose resource
   directory is in the file but whose resource bytes are not, the shape a
   runtime unpacker leaves behind. FindResourceA has to answer out of
   the directory, the data address has to point into the mapped image, and only
   an address outside the image may be refused. Every check has its own exit
   code, so the runner names the failing one. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U FreeLibrary(U);
API U FindResourceA(U, const char *, const char *);
API U LoadResource(U, U);
API U LockResource(U);
API U SizeofResource(U, U);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);
API int LoadStringA(U, U, char *, int);

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
int mainCRTStartup(void)
{
    const char *const type = (const char *)PACKED_TYPE;
    const U module = LoadLibraryA(PACKED_MODULE);
    check(module != 0, 1);
    /* The regression: the directory entry is in the file, its data address is
       not, and the answer must still be the entry. */
    const U found = FindResourceA(module, PACKED_NAME, type);
    check(found != 0, 2);
    check(SizeofResource(module, found) == PACKED_SIZE, 3);
    const U handle = LoadResource(module, found);
    check(handle != 0, 4);
    const U address = LockResource(handle);
    check(address != 0, 5);
    check(address == module + PACKED_RVA, 6);
    /* The file carries none of these bytes, so the loader left the page of the
       section tail zero. */
    const unsigned char *bytes = (const unsigned char *)address;
    for (U i = 0; i < PACKED_SIZE; ++i)
        check(bytes[i] == 0, 7);
    /* What the unpacker does: put the bytes there. The loader gives every
       section of an image read and write, so no VirtualProtect is needed, and
       the original refuses one inside an image anyway. Reading the
       bytes back through the same address proves the answer of LockResource is
       a place of this process and not a file offset. */
    unsigned char *target = (unsigned char *)address;
    for (U i = 0; i < PACKED_SIZE; ++i)
        target[i] = (unsigned char)(i * 7 + 3);
    for (U i = 0; i < PACKED_SIZE; ++i)
        check(bytes[i] == (unsigned char)(i * 7 + 3), 8);
    /* The negative probe: one entry of the same directory carries a data
       address outside the image. That one stays unknown. */
    SetLastError(0);
    check(FindResourceA(module, (const char *)PACKED_BROKEN, type) == 0, 9);
    check(GetLastError() == 1812, 10);
    /* A string table the unpacker rebuilt (UPX keeps them compressed in the
       file): USER reads the block out of the image. The file has none of
       these bytes, so a string can only come from what is written here: the
       first string of the block is "NEO98", the second "packed", the rest
       are empty. */
    {
        const U strings = FindResourceA(module, (const char *)PACKED_STRINGS, (const char *)6);
        check(strings != 0 && SizeofResource(module, strings) == PACKED_STRINGS_SIZE, 12);
        unsigned short *block = (unsigned short *)LockResource(LoadResource(module, strings));
        static const char first[] = "NEO98", second[] = "packed";
        U at = 0;
        block[at++] = 5;
        for (U i = 0; i < 5; ++i) block[at++] = (unsigned short)first[i];
        block[at++] = 6;
        for (U i = 0; i < 6; ++i) block[at++] = (unsigned short)second[i];
        for (U i = 2; i < 16; ++i) block[at++] = 0;
        char text[32];
        const U base = (PACKED_STRINGS - 1) * 16;
        check(LoadStringA(module, base, text, sizeof text) == 5, 13);
        for (U i = 0; i < 6; ++i) check(text[i] == first[i], 14);
        check(LoadStringA(module, base + 1, text, sizeof text) == 6, 13);
        for (U i = 0; i < 7; ++i) check(text[i] == second[i], 14);
        text[0] = 'x';
        check(LoadStringA(module, base + 2, text, sizeof text) == 0, 15);
    }
    check(FreeLibrary(module) != 0, 11);
    ExitProcess(0);
    return 0;
}
