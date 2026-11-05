/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Structured exception handling in an ARM64 program: __try, __except and
   __finally of the compiler over the unwinder of the system (RaiseException,
   RtlUnwindEx, RtlLookupFunctionEntry, RtlVirtualUnwind, RtlCaptureContext),
   faults of the processor as exceptions, the filter of the process. A mode
   on the command line makes the program end by an exception nobody takes.
   tools/test_arm64_seh_native.py; docs/ARM64-APPS.md, "Exceptions". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int checks, failures;
static void Check(int id, int condition)
{
    ++checks;
    if (condition) return;
    ++failures;
    printf("FAIL %d\n", id);
}

#define NOINLINE __declspec(noinline)
static volatile int order[32];
static volatile int order_count;
static void Mark(int value)
{
    if (order_count < 32) order[order_count++] = value;
}

NOINLINE static void Throw(DWORD code, ULONG_PTR first, ULONG_PTR second)
{
    ULONG_PTR arguments[2];
    arguments[0] = first;
    arguments[1] = second;
    RaiseException(code, 0, 2, arguments);
}
NOINLINE static int ReadThrough(volatile int *pointer) { return *pointer; }
NOINLINE static void WriteThrough(volatile int *pointer, int value) { *pointer = value; }
/* A __try block of clang covers the calls in it; an instruction of the block
   itself that faults is covered only with -fasync-exceptions. So the faults
   happen in functions of their own. */
NOINLINE static void Undefined(void) { __asm__("udf #0"); }
NOINLINE static void Breakpoint(void) { __asm__("brk #0xf000"); }
NOINLINE static void DivideCheck(void) { __asm__("brk #0xf004"); }

/* 1. An exception of the program, taken by the block around the call. */
static DWORD seen_code;
static ULONG_PTR seen_first, seen_second;
static int seen_flags, seen_count;
static int Remember(EXCEPTION_POINTERS *pointers, int answer)
{
    seen_code = pointers->ExceptionRecord->ExceptionCode;
    seen_flags = (int)pointers->ExceptionRecord->ExceptionFlags;
    seen_count = (int)pointers->ExceptionRecord->NumberParameters;
    seen_first = seen_count > 0 ? pointers->ExceptionRecord->ExceptionInformation[0] : 0;
    seen_second = seen_count > 1 ? pointers->ExceptionRecord->ExceptionInformation[1] : 0;
    return answer;
}
static void Raised(void)
{
    int reached = 0, code = 0;
    __try
    {
        Throw(0xE0001234, 11, 0x123456789abcdef0ull);
        reached = 1;
    }
    __except (Remember(GetExceptionInformation(), EXCEPTION_EXECUTE_HANDLER))
    {
        code = (int)GetExceptionCode();
        reached = 2;
    }
    Check(100, reached == 2);
    Check(101, (DWORD)code == 0xE0001234 && seen_code == 0xE0001234);
    Check(102, seen_count == 2 && seen_first == 11 && seen_second == 0x123456789abcdef0ull);
    Check(103, !(seen_flags & (EXCEPTION_UNWINDING | EXCEPTION_NONCONTINUABLE)));
    /* A filter that declines hands it to the block outside. */
    reached = 0;
    __try
    {
        __try
        {
            Throw(0xE0000002, 1, 2);
        }
        __except (GetExceptionCode() == 0xE0000001 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            reached = 1;
        }
    }
    __except (GetExceptionCode() == 0xE0000002 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        reached = 2;
    }
    Check(104, reached == 2);
    /* The block that takes it by a constant, without a filter function. */
    reached = 0;
    __try
    {
        Throw(0xE0000003, 0, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        reached = (int)GetExceptionCode() == (int)0xE0000003;
    }
    Check(105, reached == 1);
}

/* 2. __finally: on the way out of the frames between the throw and the block
   that takes it, innermost first, and after the filters. */
NOINLINE static void Inner(void)
{
    __try
    {
        Mark(1);
        Throw(0xE0000010, 0, 0);
        Mark(99);
    }
    __finally
    {
        Mark(AbnormalTermination() ? 3 : 98);
    }
}
NOINLINE static void Middle(void)
{
    __try
    {
        __try
        {
            Inner();
        }
        __finally
        {
            Mark(4);
        }
    }
    __finally
    {
        Mark(5);
    }
    Mark(97);
}
static int FilterMark(void)
{
    Mark(2);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void Finally(void)
{
    int normal = 0;
    order_count = 0;
    __try
    {
        Middle();
    }
    __except (FilterMark())
    {
        Mark(6);
    }
    Mark(7);
    Check(200, order_count == 7);
    Check(201, order[0] == 1 && order[1] == 2 && order[2] == 3 && order[3] == 4 && order[4] == 5 && order[5] == 6 && order[6] == 7);
    /* Without an exception the block runs as part of the function. */
    __try
    {
        normal = 1;
    }
    __finally
    {
        normal += AbnormalTermination() ? 100 : 10;
    }
    Check(202, normal == 11);
}

/* 3. Faults of the processor. */
static ULONG_PTR fault_access, fault_address;
static int FaultFilter(EXCEPTION_POINTERS *pointers)
{
    if (pointers->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    fault_access = pointers->ExceptionRecord->ExceptionInformation[0];
    fault_address = pointers->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}
/* The handler repairs the pointer in the register the instruction uses and
   lets the program go on at the same instruction. */
static int repaired;
static volatile int landing;
static int RepairFilter(EXCEPTION_POINTERS *pointers)
{
    CONTEXT *context = pointers->ContextRecord;
    unsigned i;
    if (pointers->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    for (i = 0; i < 29; ++i)
        if (context->X[i] == 0x00000040)
        {
            context->X[i] = (ULONG_PTR)&landing;
            ++repaired;
        }
    return repaired ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_EXECUTE_HANDLER;
}
static void Faults(void)
{
    int reached = 0;
    static const int constant = 5;
    __try
    {
        reached = ReadThrough((volatile int *)0x00000010);
    }
    __except (FaultFilter(GetExceptionInformation()))
    {
        reached = -1;
    }
    Check(300, reached == -1 && fault_access == 0 && fault_address == 0x10);
    reached = 0;
    __try
    {
        WriteThrough((volatile int *)0x00000024, 1);
    }
    __except (FaultFilter(GetExceptionInformation()))
    {
        reached = -1;
    }
    Check(301, reached == -1 && fault_access == 1 && fault_address == 0x24);
    reached = 0;
    __try
    {
        WriteThrough((volatile int *)&constant, 6);
    }
    __except (FaultFilter(GetExceptionInformation()))
    {
        reached = -1;
    }
    Check(302, reached == -1 && fault_access == 1 && fault_address == (ULONG_PTR)&constant && constant == 5);
    /* A jump to nowhere: the fault is the fetch, the frame is the caller's. */
    reached = 0;
    __try
    {
        ((void (*)(void))0x66660000)();
    }
    __except (FaultFilter(GetExceptionInformation()))
    {
        reached = -1;
    }
    Check(303, reached == -1 && fault_access == 8 && fault_address == 0x66660000);
    /* A struct the system cannot read is a fault at the call. */
    reached = 0;
    __try
    {
        reached = (int)RegisterClassA((const WNDCLASSA *)0x45670000);
    }
    __except (FaultFilter(GetExceptionInformation()))
    {
        reached = -1;
    }
    Check(304, reached == -1 && fault_access == 0 && fault_address == 0x45670000);
    /* Continue after a repair. */
    __try
    {
        WriteThrough((volatile int *)0x00000040, 77);
        reached = 5;
    }
    __except (RepairFilter(GetExceptionInformation()))
    {
        reached = -1;
    }
    Check(305, reached == 5 && repaired == 1 && landing == 77);
    /* An instruction the processor does not know, and a breakpoint. */
    reached = 0;
    __try
    {
        Undefined();
    }
    __except (GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        reached = 1;
    }
    Check(306, reached == 1);
    reached = 0;
    __try
    {
        Breakpoint();
    }
    __except (GetExceptionCode() == EXCEPTION_BREAKPOINT ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        reached = 1;
    }
    Check(307, reached == 1);
    /* The division by zero the compiler checks for. */
    reached = 0;
    __try
    {
        DivideCheck();
    }
    __except (GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        reached = 1;
    }
    Check(308, reached == 1);
}

/* 4. The registers of a frame come back as the frame left them: values the
   function keeps across the call live in callee-saved registers, integer and
   floating point, and the frames between use the same registers. */
NOINLINE static double Clobber(int depth, double seed)
{
    volatile char big[5000];
    double a = seed * 1.5, b = seed + 2.25, c = seed - 0.5, d = seed * seed;
    long long p = depth * 3, q = depth + 100, r = depth * depth, s = depth - 7;
    big[0] = (char)depth;
    big[4999] = (char)seed;
    if (!depth)
    {
        Throw(0xE0000020, 0, 0);
        return 0;
    }
    a += Clobber(depth - 1, seed + 1.0);
    return a + b + c + d + (double)(p + q + r + s) + big[0] + big[4999];
}
NOINLINE static int Registers(int seed)
{
    double a = seed * 0.5, b = seed * 0.25, c = seed + 0.125, d = seed - 3.0, e = seed * 7.0, f = seed / 3.0, g = seed + 0.75,
           h = seed * 1.25;
    long long i = seed + 1, j = seed + 2, k = seed + 3, l = seed + 4, m = seed + 5, n = seed + 6, o = seed + 7, p = seed + 8,
              q = seed + 9, r = seed + 10;
    int taken = 0;
    __try
    {
        Clobber(12, 3.0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        taken = 1;
    }
    return taken && a == seed * 0.5 && b == seed * 0.25 && c == seed + 0.125 && d == seed - 3.0 && e == seed * 7.0 &&
           f == seed / 3.0 && g == seed + 0.75 && h == seed * 1.25 && i == seed + 1 && j == seed + 2 && k == seed + 3 &&
           l == seed + 4 && m == seed + 5 && n == seed + 6 && o == seed + 7 && p == seed + 8 && q == seed + 9 && r == seed + 10;
}

/* 5. An exception inside a filter, inside a __finally block and inside an
   __except block. */
static int FaultingFilter(void)
{
    int inner = 0;
    __try
    {
        ReadThrough((volatile int *)0x00000030);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        inner = 1;
    }
    Mark(inner ? 21 : 29);
    return EXCEPTION_EXECUTE_HANDLER;
}
NOINLINE static void FinallyThatThrows(void)
{
    __try
    {
        Throw(0xE0000030, 0, 0);
    }
    __finally
    {
        Mark(31);
        /* A second exception during the unwind replaces the first. */
        Throw(0xE0000031, 0, 0);
    }
}
static void Nested(void)
{
    int code = 0;
    order_count = 0;
    __try
    {
        Throw(0xE0000021, 0, 0);
    }
    __except (FaultingFilter())
    {
        Mark(22);
    }
    Check(400, order_count == 2 && order[0] == 21 && order[1] == 22);
    order_count = 0;
    __try
    {
        FinallyThatThrows();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        code = (int)GetExceptionCode();
    }
    Check(401, order_count == 1 && order[0] == 31 && (DWORD)code == 0xE0000031);
    /* Raised again from the block that took it. */
    code = 0;
    __try
    {
        __try
        {
            Throw(0xE0000040, 0, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Throw(0xE0000041, 0, 0);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        code = (int)GetExceptionCode();
    }
    Check(402, (DWORD)code == 0xE0000041);
}

/* 6. Many exceptions: nothing piles up. */
static void Many(void)
{
    int i, taken = 0;
    for (i = 0; i < 20000; ++i)
    {
        __try
        {
            __try
            {
                Throw(0xE0000050, (ULONG_PTR)i, 0);
            }
            __finally
            {
                ++taken;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++taken;
        }
    }
    Check(500, taken == 40000);
}

/* 7. A walk of the program's own: from a captured context frame by frame to
   the function that began it. */
static ULONG_PTR walk_goal_begin, walk_goal_end;
NOINLINE static int WalkFrames(int depth)
{
    volatile int local[40];
    local[0] = depth;
    if (depth) return WalkFrames(depth - 1) + local[0];
    {
        CONTEXT context;
        int frames = 0, found = 0;
        RtlCaptureContext(&context);
        while (frames < 64 && context.Pc)
        {
            DWORD64 base = 0, establisher = 0;
            PVOID data = NULL;
            RUNTIME_FUNCTION *function = RtlLookupFunctionEntry(context.Pc, &base, NULL);
            if (context.Pc >= walk_goal_begin && context.Pc < walk_goal_end)
            {
                found = 1;
                break;
            }
            if (!function) break;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Pc, function, &context, &data, &establisher, NULL);
            ++frames;
        }
        return found ? 1000 + frames : -frames;
    }
}
NOINLINE static void Walk(void)
{
    DWORD64 base = 0, other = 1;
    PVOID image = NULL;
    RUNTIME_FUNCTION *self = RtlLookupFunctionEntry((DWORD64)(ULONG_PTR)&Walk, &base, NULL);
    int answer;
    Check(600, self != NULL && base == (DWORD64)(ULONG_PTR)GetModuleHandleA(NULL));
    Check(601, RtlPcToFileHeader((PVOID)&Walk, &image) == (PVOID)GetModuleHandleA(NULL) && image == (PVOID)GetModuleHandleA(NULL));
    Check(602, RtlLookupFunctionEntry(0x10, &other, NULL) == NULL && other == 0);
    if (!self) return;
    walk_goal_begin = base + self->BeginAddress;
    /* The length of the function: in the entry itself (packed), or in the
       first word of the record behind it, in instructions. */
    walk_goal_end = walk_goal_begin + 4 * (self->Flag ? self->FunctionLength : (*(const DWORD *)(base + self->UnwindData) & 0x3ffff));
    answer = WalkFrames(9);
    /* Ten frames of WalkFrames lie between the capture and this function;
       each but the last adds its depth on the way back. */
    Check(603, answer == 1010 + 45);
}

/* 8. A thread, and a procedure the system calls. */
static DWORD WINAPI ThreadRoutine(LPVOID parameter)
{
    int taken = 0;
    __try
    {
        Throw(0xE0000060, (ULONG_PTR)parameter, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        taken = 1;
    }
    return taken ? 4242 : 1;
}
static int procedure_taken;
static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_USER + 5)
    {
        __try
        {
            WriteThrough((volatile int *)0x00000050, 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++procedure_taken;
        }
        return 31;
    }
    if (message == WM_USER + 6) Throw(0xE0000070, 0, 0);
    return DefWindowProcA(window, message, wparam, lparam);
}
static void System(void)
{
    DWORD code = 0;
    WNDCLASSA c;
    HWND window;
    HANDLE thread = CreateThread(NULL, 0, ThreadRoutine, (LPVOID)5, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    GetExitCodeThread(thread, &code);
    CloseHandle(thread);
    Check(700, code == 4242);
    memset(&c, 0, sizeof c);
    c.lpfnWndProc = Procedure;
    c.hInstance = GetModuleHandleA(NULL);
    c.lpszClassName = "Neo98Seh";
    RegisterClassA(&c);
    window = CreateWindowExA(0, "Neo98Seh", "", 0, 0, 0, 10, 10, NULL, NULL, c.hInstance, NULL);
    Check(701, window != NULL);
    Check(702, SendMessageA(window, WM_USER + 5, 0, 0) == 31 && procedure_taken == 1);
    Check(703, SendMessageA(window, WM_USER + 5, 0, 0) == 31 && procedure_taken == 2);
    DestroyWindow(window);
}

/* 9. The filter of the process. */
static int filter_calls;
static LONG WINAPI ContinueFilter(EXCEPTION_POINTERS *pointers)
{
    ++filter_calls;
    /* A program-raised exception continues behind RaiseException. */
    if (pointers->ExceptionRecord->ExceptionCode == 0xE0000080) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI QuietFilter(EXCEPTION_POINTERS *pointers)
{
    printf("filter %08lx\n", (unsigned long)pointers->ExceptionRecord->ExceptionCode);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void ProcessFilter(void)
{
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(ContinueFilter);
    /* The start code installed the filter of the C runtime. */
    Check(800, previous != NULL);
    Throw(0xE0000080, 0, 0);
    Check(801, filter_calls == 1);
    Check(802, SetUnhandledExceptionFilter(previous) == ContinueFilter);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    if (!strcmp(mode, "unhandled"))
    {
        printf("before\n");
        Throw(0xE00000AA, 1, 2);
        printf("after\n");
        return 0;
    }
    if (!strcmp(mode, "declined"))
    {
        /* Every handler declines; the fault is final and names its address. */
        printf("before\n");
        __try
        {
            WriteThrough((volatile int *)0x00000060, 1);
        }
        __except (EXCEPTION_CONTINUE_SEARCH)
        {
            printf("wrong\n");
        }
        printf("after\n");
        return 0;
    }
    if (!strcmp(mode, "quiet"))
    {
        SetUnhandledExceptionFilter(QuietFilter);
        printf("before\n");
        WriteThrough((volatile int *)0x00000070, 1);
        printf("after\n");
        return 0;
    }
    if (!strcmp(mode, "callback"))
    {
        /* The block around SendMessage catches the callback's exception. */
        WNDCLASSA c;
        HWND window;
        memset(&c, 0, sizeof c);
        c.lpfnWndProc = Procedure;
        c.hInstance = GetModuleHandleA(NULL);
        c.lpszClassName = "Neo98Seh";
        RegisterClassA(&c);
        window = CreateWindowExA(0, "Neo98Seh", "", 0, 0, 0, 10, 10, NULL, NULL, c.hInstance, NULL);
        printf("before\n");
        __try
        {
            SendMessageA(window, WM_USER + 6, 0, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            printf("caught\n");
        }
        printf("after\n");
        return 0;
    }
    Raised();
    Finally();
    Faults();
    Check(350, Registers(40));
    Nested();
    Many();
    Walk();
    System();
    ProcessFilter();
    printf("SEH checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
