/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* The initializers and exit functions of C++ objects with static lifetime.
   What the compiler calls by itself (memory moves, the stack probe) is in
   files of its own, one function each: a program that brings its own memset
   keeps it. */
#include <windows.h>

/* The compiler for the MSVC target puts a pointer per initializer into.CRT$XCU; the linker sorts the parts of.CRT by name, so the two marks
   enclose them. */
typedef void (*Function)(void);
#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)
__declspec(allocate(".CRT$XCA")) static const Function first_initializer = 0;
__declspec(allocate(".CRT$XCZ")) static const Function last_initializer = 0;

/* The compiler for the MinGW target (C++ with the SDK) puts them into.ctors,
   and the linker, in its MinGW mode, makes one list of them: a count or -1,
   the pointers, a null. They run from the last to the first, which is the
   order the compiler arranged them for. A program linked without that mode
   has no such list and gets the empty one. */
const Function __neo98_no_ctors[2] = {(Function)(LONG_PTR)-1, 0};
extern const Function __CTOR_LIST__[];
#pragma comment(linker, "/alternatename:__CTOR_LIST__=__neo98_no_ctors")

void __neo98_no_stdio_init(void) {}
void __neo98_stdio_init(void);
#pragma comment(linker, "/alternatename:__neo98_stdio_init=__neo98_no_stdio_init")

void __neo98_run_initializers(void)
{
    __neo98_stdio_init();
    ULONG_PTR count = (ULONG_PTR)__CTOR_LIST__[0], i;
    for (const Function *p = &first_initializer + 1; p < &last_initializer; ++p)
        if (*p) (*p)();
    if (count == (ULONG_PTR)-1)
        for (count = 0; __CTOR_LIST__[count + 1]; ++count) {}
    for (i = count; i >= 1; --i) __CTOR_LIST__[i]();
}

/* A destructor of a static object registers itself here, and so does every
   other function for the end of the program. The first block is part of the
   program; more come from the heap of the process. */
#define BLOCK 62
struct Block
{
    struct Block *previous;
    int count;
    Function functions[BLOCK];
};
static struct Block first_block, *last_block = &first_block;

int atexit(Function function)
{
    if (last_block->count == BLOCK)
    {
        struct Block *more = (struct Block *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct Block));
        if (!more) return 1;
        more->previous = last_block;
        last_block = more;
    }
    last_block->functions[last_block->count++] = function;
    return 0;
}

void __neo98_run_exit_functions(void)
{
    /* A function may register another one while it runs. */
    for (;;)
    {
        if (last_block->count)
        {
            last_block->functions[--last_block->count]();
            continue;
        }
        if (!last_block->previous) break;
        {
            struct Block *empty = last_block;
            last_block = empty->previous;
            HeapFree(GetProcessHeap(), 0, empty);
        }
    }
}

/* What the C++ ABI passes with a destructor it registers (__cxa_atexit,
   __cxa_thread_atexit): a mark of the module the object belongs to. */
void *__dso_handle = &__dso_handle;
