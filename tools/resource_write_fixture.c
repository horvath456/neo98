// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U VirtualQuery(const void *, void *, U);
API U VirtualProtect(void *, U, U, U *);
API U VirtualAlloc(void *, U, U, U);
API U SetUnhandledExceptionFilter(void *);
API int UnhandledExceptionFilter(void *);
API U SetErrorMode(U);
API U LoadLibraryA(const char *);
API U LoadLibraryExA(const char *, U, U);
API U FreeLibrary(U);
API U CreateThread(void *, U, void *, void *, U, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API void RaiseException(U, U, U, const U *);
// The test driver points the PE resource directory into this read-only
// section. Its name deliberately differs from.rsrc.
__declspec(allocate(".testres")) const unsigned char resource_bytes[0x5000] = {1};
static const char other_readonly[] = "ordinary readonly data";
struct Record { U code, flags, previous, address, count, args[15]; };
struct Registration { struct Registration *next; void *handler; };
static U filter_calls, handler_calls;
// The compiler may turn a zeroed array into a call to memset; there is no
// C library to link (/nodefaultlib).
void *memset(void *p, int v, U n) { char *q = p; while (n--) *q++ = (char)v; return p; }
static void check(int yes, U code) { if (!yes) ExitProcess(code); }
static U protection(const void *at)
{
    U info[7]; check(VirtualQuery(at, info, sizeof info) == sizeof info, 608401);
    return info[5];
}
static int __stdcall filter(void *p) { (void)p; ++filter_calls; return 1; }
static int dispatch(struct Record *r, struct Registration *f, U *context, U *dispatcher)
{
    U pointers[2] = {(U)r, (U)context};
    (void)f; (void)dispatcher; ++handler_calls;
    check(r->code == 0xc0000005 && r->args[0] == 1 &&
          protection((void *)r->args[1]) == 2, 608439);
    return UnhandledExceptionFilter(pointers) == -1 ? 0 : 1;
}
static int repair(void *at, U code, U kind)
{
    struct Record record = {0}; U context[51] = {0x10007};
    U pointers[2] = {(U)&record, (U)context};
    record.code = code; record.count = 2; record.args[0] = kind; record.args[1] = (U)at;
    return UnhandledExceptionFilter(pointers);
}
static U __stdcall worker(void *at)
{
    // No application SEH frame: Win98's thread-start frame must retry.
    *(volatile unsigned short *)at = 0xabcd;
    return 42;
}
static int cleanup(struct Record *r, struct Registration *f, U *context, U *dispatcher)
{
    (void)f; (void)dispatcher;
    if (!(r->flags & 6)) {
        check(r->code == 0xe123abcd, 608440);
        return 1;
    }
    // The positive thread-start filter calls _global_unwind2, which passes
    // NULL to RtlUnwind. Win98 generates C0000026 (not the fault record).
    check(r->code == 0xc0000026 && r->flags == 2, 608441);
    check(r->previous == 0 && r->count == 0 && context[44] == 0, 608442);
    ExitProcess(0);
    return 1;
}
void mainCRTStartup(void)
{
    U base = GetModuleHandleA(0), nt = *(U *)(base + 60);
    U begin = base + *(U *)(base + nt + 0x88), size = *(U *)(base + nt + 0x8c);
    check(size == 0x2200 && begin == (U)resource_bytes + 0x1100, 608402);
    volatile unsigned short *target = (void *)(begin + 0x612);
    check(protection((void *)target) == 2, 608403);
    SetErrorMode(2); SetUnhandledExceptionFilter(filter);
    check(repair((void *)target, 0xc0000005, 0) == 1, 608404);
    check(repair((void *)target, 0xe1234567, 1) == 1, 608405);
    check(repair((void *)(begin - 1), 0xc0000005, 1) == 1, 608406);
    check(repair((void *)(begin + size), 0xc0000005, 1) == 1, 608407);
    check(repair((void *)other_readonly, 0xc0000005, 1) == 1, 608408);
    check(filter_calls == 5 && protection((void *)target) == 2, 608409);
    struct Registration frame;
    __asm__ volatile("movl %%fs:0,%0" : "=r"(frame.next));
    frame.handler = dispatch;
    __asm__ volatile("movl %0,%%fs:0" :: "r"(&frame) : "memory");
    // Same word-sized AND as InstallShield's temporary UTF-16 terminator.
    __asm__ volatile("andw $0,(%0)" :: "r"(target) : "memory", "cc");
    check(handler_calls == 1 && filter_calls == 5, 608410);
    check(protection((void *)target) == 4 && *target == 0, 608411);
    check(protection((void *)(begin + 0x1800)) == 2, 608412);
    *(volatile unsigned short *)(begin + 0x1800) = 0x1234;
    check(handler_calls == 2 && protection((void *)(begin + 0x1800)) == 4, 608413);
    U old = 0;
    check(VirtualProtect((void *)target, 2, 2, &old) && old == 4, 608414);
    *target = 0x5678;
    check(handler_calls == 3 && *target == 0x5678 && filter_calls == 5, 608415);
    __asm__ volatile("movl %0,%%fs:0" :: "r"(frame.next) : "memory");
    check(VirtualProtect((void *)target, 2, 2, &old), 608433);
    *target = 0x9876; // The primary thread also owns a startup frame.
    check(*target == 0x9876 && filter_calls == 5, 608434);
    check(VirtualProtect((void *)target, 2, 2, &old), 608435);
    U thread = CreateThread(0, 0, worker, (void *)target, 0, 0), status = 0;
    check(thread && WaitForSingleObject(thread, 10000) == 0, 608436);
    check(GetExitCodeThread(thread, &status) && status == 42 && *target == 0xabcd, 608437);
    check(filter_calls == 5, 608438);
    check(VirtualProtect((void *)target, 2, 1, &old), 608416);
    check(repair((void *)target, 0xc0000005, 1) == 1 && protection((void *)target) == 1, 608417);
    void *heap = (void *)VirtualAlloc(0, 4096, 0x3000, 2);
    check(heap && repair(heap, 0xc0000005, 1) == 1 && protection(heap) == 2, 608418);
    check(filter_calls == 7, 608419);
    U module = LoadLibraryA("D:\\RSRC.DLL");
    check(module != 0, 608420);
    U dll_nt = *(U *)(module + 60), rva = *(U *)(module + dll_nt + 0x88);
    volatile unsigned char *data = (void *)(module + rva + 0x612);
    check(protection((void *)data) == 2 && *data == 0, 608421);
    check(repair((void *)data, 0xc0000005, 1) == -1, 608422);
    *data = 0x5a;
    check(protection((void *)data) == 4 && *data == 0x5a, 608423);
    check(FreeLibrary(module), 608424);
    module = LoadLibraryA("D:\\RSRC.DLL");
    check(module != 0, 608425);
    data = (void *)(module + rva + 0x612);
    check(protection((void *)data) == 2 && *data == 0, 608426);
    check(FreeLibrary(module), 608427);
    module = LoadLibraryExA("D:\\RSRC.DLL", 0, 2);
    check(module & 1, 608428);
    U view = module & ~1u, file_nt = *(U *)(view + 60);
    U table = view + file_nt + 24 + *(unsigned short *)(view + file_nt + 20);
    U count = *(unsigned short *)(view + file_nt + 6), offset = 0;
    for (U i = 0; i < count; ++i) {
        U s = table + i * 40, start = *(U *)(s + 12), length = *(U *)(s + 16);
        if (rva >= start && rva - start < length) offset = *(U *)(s + 20) + rva - start;
    }
    check(offset && protection((void *)(view + offset)) == 2, 608429);
    check(repair((void *)(view + offset), 0xc0000005, 1) == 1, 608430);
    check(protection((void *)(view + offset)) == 2 && FreeLibrary(module), 608431);
    check(filter_calls == 8, 608432);
    __asm__ volatile("movl %%fs:0,%0" : "=r"(frame.next));
    frame.handler = cleanup;
    __asm__ volatile("movl %0,%%fs:0" :: "r"(&frame) : "memory");
    RaiseException(0xe123abcd, 0, 0, 0);
    ExitProcess(608443);
}
