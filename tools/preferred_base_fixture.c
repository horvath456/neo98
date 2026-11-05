/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An EXE whose preferred base is 10000000h and that carries no relocations,
   like the 44 programs of the collection in It checks that the
   image keeps that, that the process heap lies outside the image, and
   that two DLLs with the same preferred base move out of the way one after
   the other. Exit code zero means every check held; codes 4400 to 4499. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U LoadLibraryA(const char *);
API U FreeLibrary(U);
API U GetProcAddress(U, const char *);
API U LocalAlloc(U, U);
API U LocalFree(U);

typedef U(__stdcall *Query)(void);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
/* SizeOfImage out of the mapped PE header, so the test does not carry a size
   the linker may change. */
static U image_size(U base)
{
    const U nt = *(const U *)(base + 60);
    return *(const U *)(base + nt + 80);
}
static U call(U module, const char *name, U missing_code)
{
    const U address = GetProcAddress(module, name);
    check(address != 0, missing_code);
    if (!address)
        return 0;
    check(address > module && address < module + image_size(module), missing_code + 1);
    return ((Query)address)();
}

void mainCRTStartup(void)
{
    const U self = GetModuleHandleA(0);
    check(self == 0x10000000u, 4401);
    const U self_size = image_size(self);

    /* The heap must have moved out of the image, not the other way round. */
    const U first = LocalAlloc(64, 4096);
    check(first != 0, 4402);
    check(first < self || first >= self + self_size, 4403);
    const U second = LocalAlloc(64, 4096);
    check(second != 0, 4404);
    check(second < self || second >= self + self_size, 4405);
    /* Both blocks come from one arena, so they stay within 32 MiB of each other. */
    check(second > first ? second - first < 0x2000000u : first - second < 0x2000000u, 4406);
    *(U *)first = 0x12345678u;
    *(U *)(second + 4092) = 0x9abcdef0u;
    check(*(const U *)first == 0x12345678u, 4407);
    check(*(const U *)(second + 4092) == 0x9abcdef0u, 4408);
    check(LocalFree(first) == 0, 4409);

    const U a = LoadLibraryA("C:\\WINDOWS\\PREFA.DLL");
    check(a != 0, 4410);
    const U b = LoadLibraryA("C:\\WINDOWS\\PREFB.DLL");
    check(b != 0, 4411);
    if (!a || !b)
        ExitProcess(failure ? failure : 4412);
    check(a != 0x10000000u && b != 0x10000000u, 4413);
    check(a != b, 4414);
    const U size_a = image_size(a), size_b = image_size(b);
    check(a + size_a <= b || b + size_b <= a, 4415);
    /* Neither copy may land inside the image or inside the heap arena. */
    check(a + size_a <= self || a >= self + self_size, 4416);
    check(b + size_b <= self || b >= self + self_size, 4417);
    check(a + size_a <= second || a > second, 4418);
    check(b + size_b <= second || b > second, 4419);

    check(call(a, "Value", 4420) == 0xa5a50001u, 4422);
    check(call(b, "Value", 4423) == 0xa5a50002u, 4425);
    /* The relocated pointer in writable data must point into its own image. */
    const U pointer_a = call(a, "PointerValue", 4426), pointer_b = call(b, "PointerValue", 4429);
    check(pointer_a > a && pointer_a < a + size_a, 4428);
    check(pointer_b > b && pointer_b < b + size_b, 4431);
    check(call(a, "CounterValue", 4432) == 0x5a5a0001u, 4434);
    check(call(b, "CounterValue", 4435) == 0x5a5a0002u, 4437);
    check(call(a, "Instance", 4438) == a, 4440);
    check(call(b, "Instance", 4441) == b, 4443);
    check(call(a, "Attachments", 4444) == 1, 4446);
    check(call(b, "Attachments", 4447) == 1, 4449);

    check(GetModuleHandleA("PREFA.DLL") == a, 4450);
    check(GetModuleHandleA("PREFB.DLL") == b, 4451);
    check(FreeLibrary(a) != 0, 4452);
    check(FreeLibrary(b) != 0, 4453);
    /* The freed range must be usable again by a third copy of the same file. */
    const U again = LoadLibraryA("C:\\WINDOWS\\PREFA.DLL");
    check(again != 0, 4454);
    check(again != 0x10000000u, 4455);
    check(call(again, "Value", 4456) == 0xa5a50001u, 4458);
    check(FreeLibrary(again) != 0, 4459);

    ExitProcess(failure);
}
