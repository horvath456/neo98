/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The guest of tools/test_execute_pages_native.py.
 *
 * NEO98 hands Unicorn the execute right only for pages that hold code and adds
 * it when the guest really jumps somewhere else.
 * For the program nothing may change: on the i386 of Win98 every readable page
 * is executable. This guest runs code out of the process heap, out of a
 * VirtualAlloc block with PAGE_READWRITE, off its own stack and out of its
 * `.data`, modifies such code and runs it again, protects the page read-only
 * and back, lets a window procedure sit in a heap thunk, and finally jumps into
 * a PAGE_NOACCESS page, which has to stay an access violation with the right
 * address. Every failure leaves through ExitProcess with its own code. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetProcessHeap(void);
API U HeapAlloc(U, U, U);
API U HeapFree(U, U, U);
API U VirtualAlloc(U, U, U, U);
API U VirtualFree(U, U, U);
API U VirtualProtect(U, U, U, U *);
API U VirtualQuery(U, void *, U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U SendMessageA(U, U, U, U);
API U DestroyWindow(U);

static void check(int yes, U code) { if (!yes) ExitProcess(code); }

/* mov eax, value / ret: six bytes that answer a constant to a cdecl caller. */
static void emit(unsigned char *at, U value)
{
    at[0] = 0xb8;
    at[1] = (unsigned char)value; at[2] = (unsigned char)(value >> 8);
    at[3] = (unsigned char)(value >> 16); at[4] = (unsigned char)(value >> 24);
    at[5] = 0xc3;
}
/* jmp rel32 to `target`, five bytes: the shape of an ATL window-procedure
   thunk, which lives in allocated memory and not in any image. */
static void emit_jump(unsigned char *at, U target)
{
    const U relative = target - ((U)at + 5);
    at[0] = 0xe9;
    at[1] = (unsigned char)relative; at[2] = (unsigned char)(relative >> 8);
    at[3] = (unsigned char)(relative >> 16); at[4] = (unsigned char)(relative >> 24);
}
typedef U (*Answer)(void);
static U call(unsigned char *at) { return ((Answer)(void *)at)(); }

static unsigned char data_code[64];

struct Basic { U base, allocation, protect_allocation, size, state, protect, type; };
struct Record { U code, flags, previous, address, count, args[15]; };
struct Registration { struct Registration *next; void *handler; };
struct WndClass { U style, proc, class_extra, window_extra, instance, icon, cursor, brush;
                  const char *menu, *name; };

static U head(void) { U p; __asm__ volatile("movl %%fs:0,%0" : "=r"(p)); return p; }
static void link_handler(struct Registration *r, void *handler)
{
    r->next = (void *)head();
    r->handler = handler;
    __asm__ volatile("movl %0,%%fs:0" ::"r"(r) : "memory");
}
static void unlink_handler(struct Registration *r)
{ __asm__ volatile("movl %0,%%fs:0" ::"r"(r->next) : "memory"); }

/* The jump into the unreadable page and the place the handler continues at.
   The jump pushes nothing, so the stack pointer of the handler's context is
   already the one the resume needs. */
extern char execute_resume;
static U fault_calls, fault_address, fault_access;
static U forbidden;
static int fault(struct Record *r, struct Registration *f, U *c, U *d)
{
    (void)f; (void)d;
    if (r->code != 0xc0000005u) return 1;
    ++fault_calls;
    fault_access = r->count > 0 ? r->args[0] : ~0u;
    fault_address = r->count > 1 ? r->args[1] : 0;
    c[46] = (U)&execute_resume;
    return 0;
}
static __attribute__((noinline)) void jump_forbidden(void)
{
    __asm__ volatile("jmp *%0\n\t.globl _execute_resume\n_execute_resume:\n\tnop" ::"r"(forbidden));
}

static U __stdcall Procedure(U window, U message, U wp, U lp)
{
    if (message == 0x0400u) return wp * 2u;
    return DefWindowProcA(window, message, wp, lp);
}
static const char class_name[] = "Execute.OnDemand";

void mainCRTStartup(void)
{
    const U heap = GetProcessHeap();
    check(heap != 0, 23001);

    /* 1. Code in a block of the process heap. */
    unsigned char *const block = (unsigned char *)HeapAlloc(heap, 0, 256);
    check(block != 0, 23002);
    emit(block, 0x11111111u);
    check(call(block) == 0x11111111u, 23003);

    /* 2. Code in a VirtualAlloc block that asked for PAGE_READWRITE only. */
    unsigned char *const pages = (unsigned char *)VirtualAlloc(0, 8192, 0x3000u, 4u);
    check(pages != 0, 23004);
    /* What VirtualQuery answers must not depend on whether the page has run
       code, so the answer is taken before and compared after. */
    struct Basic before = {0}, after = {0};
    check(VirtualQuery((U)pages, &before, sizeof before) == sizeof before, 23005);
    emit(pages, 0x22222222u);
    check(call(pages) == 0x22222222u, 23006);
    check(VirtualQuery((U)pages, &after, sizeof after) == sizeof after, 23007);
    check(before.protect == after.protect && before.state == after.state &&
          before.type == after.type && before.base == after.base &&
          before.size == after.size && before.allocation == after.allocation, 23008);

    /* 3. Self-modifying code in that page: write, run, write again, run again.
          The second round has to answer the new constant, so the translated
          block of the first round must have been thrown away. */
    emit(pages, 0x33333333u);
    check(call(pages) == 0x33333333u, 23009);
    emit(pages, 0x44444444u);
    check(call(pages) == 0x44444444u, 23010);

    /* 4. Read-only and back. Reading and running stay allowed; after the page
          is writable again the next version of the code answers. */
    U previous = 0;
    check(VirtualProtect((U)pages, 4096, 2u, &previous) != 0, 23011);
    check(call(pages) == 0x44444444u, 23012);
    check(VirtualProtect((U)pages, 4096, 4u, &previous) != 0, 23013);
    emit(pages, 0x55555555u);
    check(call(pages) == 0x55555555u, 23014);

    /* 5. Code on the stack of this thread. */
    unsigned char stack_code[64];
    emit(stack_code, 0x66666666u);
    check(call(stack_code) == 0x66666666u, 23015);

    /* 6. Code in the `.data` section of the image. The loader gives that
          section no execute right, so this is the on-demand path again. */
    emit(data_code, 0x77777777u);
    check(call(data_code) == 0x77777777u, 23016);

    /* 7. A window procedure that lives in a heap thunk, called by the native
          USER32 of this port out of SendMessage. */
    {
        const U instance = GetModuleHandleA(0);
        unsigned char *const thunk = (unsigned char *)HeapAlloc(heap, 0, 64);
        check(thunk != 0, 23017);
        emit_jump(thunk, (U)&Procedure);
        struct WndClass klass = {0};
        klass.proc = (U)thunk;
        klass.instance = instance;
        klass.name = class_name;
        check(RegisterClassA(&klass) != 0, 23018);
        const U window = CreateWindowExA(0, class_name, "execute", 0x00cf0000u, 0, 0, 120, 80,
                                         0, 0, instance, 0);
        check(window != 0, 23019);
        check(SendMessageA(window, 0x0400u, 21u, 0) == 42u, 23020);
        DestroyWindow(window);
        HeapFree(heap, 0, (U)thunk);
    }

    /* 8. A jump into a page the process may not read stays an access
          violation, and the record names the address. */
    {
        unsigned char *const denied = (unsigned char *)VirtualAlloc(0, 4096, 0x3000u, 4u);
        check(denied != 0, 23021);
        emit(denied, 0x88888888u);
        check(VirtualProtect((U)denied, 4096, 1u, &previous) != 0, 23022);
        forbidden = (U)denied;
        struct Registration guard;
        link_handler(&guard, fault);
        jump_forbidden();
        unlink_handler(&guard);
        check(fault_calls == 1, 23023);
        check(fault_address == (U)denied, 23024);
        /* Readable again, and then it runs like any other page. */
        check(VirtualProtect((U)denied, 4096, 4u, &previous) != 0, 23025);
        check(call(denied) == 0x88888888u, 23026);
        VirtualFree((U)denied, 0, 0x8000u);
    }

    /* 9. The way an ASPack stub finds out how long a section's first
          instruction is (Quiz.exe of KIDS50): it saves the first
          dword of the section on the stack, writes a RET there, calls it and
          takes the dword back with POP into memory. That POP writes into a
          page that has just run code. The stack must be where it was and
          the dword back in place. */
    {
        static const unsigned char stub[] = {
            0x57,                   /* push edi */
            0x8b, 0x7c, 0x24, 0x08, /* mov edi, [esp+8] */
            0x89, 0xe0,             /* mov eax, esp */
            0xff, 0x37,             /* push dword [edi] */
            0xc6, 0x07, 0xc3,       /* mov byte [edi], 0C3h */
            0xff, 0xd7,             /* call edi */
            0x8f, 0x07,             /* pop dword [edi] */
            0x29, 0xe0,             /* sub eax, esp */
            0x5f,                   /* pop edi */
            0xc3};                  /* ret */
        unsigned char *const code = (unsigned char *)VirtualAlloc(0, 4096, 0x3000u, 4u);
        U *const section = (U *)VirtualAlloc(0, 4096, 0x3000u, 4u);
        check(code != 0 && section != 0, 23027);
        for (U i = 0; i < sizeof stub; ++i) code[i] = stub[i];
        for (U round = 0; round < 3; ++round)
        {
            section[0] = 0x2ab44320u + round;
            section[1] = 0x9e3779b9u;
            check(((U (*)(U *))(void *)code)(section) == 0, 23028 + round);
            check(section[0] == 0x2ab44320u + round && section[1] == 0x9e3779b9u, 23031 + round);
        }
        VirtualFree((U)section, 0, 0x8000u);
        VirtualFree((U)code, 0, 0x8000u);
    }

    HeapFree(heap, 0, (U)block);
    VirtualFree((U)pages, 0, 0x8000u);
    ExitProcess(0);
}
