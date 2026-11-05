/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for LOAD_LIBRARY_AS_DATAFILE, It loads a DLL that
   cannot be loaded the ordinary way, because one of its imports names a DLL
   that does not exist. The datafile branch maps the file without binding
   imports, so only that branch may succeed. Exit code zero means every check
   held; codes 4600 to 4699. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U LoadLibraryA(const char *);
API U LoadLibraryExA(const char *, U, U);
API U FreeLibrary(U);
API U GetProcAddress(U, const char *);
API U GetLastError(void);
API void SetLastError(U);
API U FindResourceA(U, const char *, const char *);
API U LoadResource(U, U);
API U LockResource(U);
API U SizeofResource(U, U);

#define AS_DATAFILE 2u
#define RT_RCDATA ((const char *)10)
#define PAYLOAD ((const char *)33)

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}

void mainCRTStartup(void)
{
    /* Negative probe one: the ordinary load must fail, because the file
       imports NOSUCH.DLL. If it ever succeeds the datafile checks below would
       prove nothing. */
    SetLastError(0);
    check(LoadLibraryA("DATARES.DLL") == 0, 4601);
    check(GetLastError() == 126, 4602);

    const U view = LoadLibraryExA("DATARES.DLL", 0, AS_DATAFILE);
    check(view != 0, 4603);
    if (!view)
        ExitProcess(failure);
    /* The handle carries bit 0 and the view itself starts on a page. */
    check((view & 1) != 0, 4604);
    check(((view - 1) & 4095) == 0, 4605);
    /* The file is mapped raw, so its first bytes are the MZ header and the
       PE signature sits at the offset e_lfanew names. */
    const unsigned char *bytes = (const unsigned char *)(view - 1);
    check(bytes[0] == 'M' && bytes[1] == 'Z', 4606);
    const U nt = *(const U *)(bytes + 60);
    check(*(const U *)(bytes + nt) == 0x4550u, 4607);
    /* Nothing was added to the module list and nothing was initialized. */
    check(GetModuleHandleA("DATARES.DLL") == 0, 4608);
    SetLastError(0);
    check(GetProcAddress(view, "Value") == 0, 4609);

    U resource = FindResourceA(view, PAYLOAD, RT_RCDATA);
    check(resource != 0, 4610);
    const U size = SizeofResource(view, resource);
    check(size == 6, 4611);
    const U block = LoadResource(view, resource);
    check(block != 0, 4612);
    const unsigned char *data = (const unsigned char *)LockResource(block);
    check(data != 0, 4613);
    if (data)
    {
        /* The payload the resource script wrote into the file. */
        check(data[0] == 0x11 && data[1] == 0x22 && data[2] == 0x33, 4614);
        check(data[3] == 0x44 && data[4] == 0x55 && data[5] == 0x66, 4615);
        /* A datafile resource address is a file offset inside the view, not
           an image address: it stays below the file size. */
        check((U)data > view && (U)data - (view - 1) < 0x100000u, 4616);
    }

    /* A second datafile load maps the file again and answers its own view. */
    const U again = LoadLibraryExA("DATARES.DLL", 0, AS_DATAFILE);
    check(again != 0, 4617);
    check(again != view, 4618);
    check((again & 1) != 0, 4619);
    check(FreeLibrary(again) != 0, 4620);

    /* Negative probe two: a PE without a resource directory is rejected the
       way the original rejects it, with ERROR_BAD_FORMAT. */
    SetLastError(0);
    check(LoadLibraryExA("NORES.DLL", 0, AS_DATAFILE) == 0, 4621);
    check(GetLastError() == 11, 4622);
    /* Negative probe three: a file that does not start with MZ. */
    SetLastError(0);
    check(LoadLibraryExA("NOTAPE.DLL", 0, AS_DATAFILE) == 0, 4623);
    check(GetLastError() == 11, 4624);
    /* Negative probe four: a file that is not there at all. */
    SetLastError(0);
    check(LoadLibraryExA("MISSINGDATAFILE.DLL", 0, AS_DATAFILE) == 0, 4625);
    check(GetLastError() == 2, 4626);

    /* A full path takes the other search branch. Delphi's resource-module
       lookup asks for <module>.ENU and <module>.EN beside the module before it
       falls back to the module's own resources, so both answers matter: a
       present file maps, an absent one is ERROR_FILE_NOT_FOUND and no fault.
        */
    SetLastError(0);
    const U full = LoadLibraryExA("C:\\WINDOWS\\DATARES.DLL", 0, AS_DATAFILE);
    check(full != 0 && (full & 1) != 0, 4632);
    check(FreeLibrary(full) != 0, 4633);
    SetLastError(0);
    check(LoadLibraryExA("C:\\WINDOWS\\DATARES.ENU", 0, AS_DATAFILE) == 0, 4634);
    check(GetLastError() == 2, 4635);
    SetLastError(0);
    check(LoadLibraryExA("C:\\NOSUCHDIR\\DATARES.EN", 0, AS_DATAFILE) == 0, 4636);
    check(GetLastError() == 2 || GetLastError() == 3, 4637);

    /* FreeLibrary recognizes the odd handle and unmaps the view. Afterwards
       the handle is gone and its resources with it. */
    check(FreeLibrary(view) != 0, 4627);
    SetLastError(0);
    check(FindResourceA(view, PAYLOAD, RT_RCDATA) == 0, 4628);
    check(GetLastError() == 6, 4629);
    SetLastError(0);
    check(FreeLibrary(view) == 0, 4630);
    check(GetLastError() == 6, 4631);

    ExitProcess(failure);
}
