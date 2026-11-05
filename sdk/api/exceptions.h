// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
/* Exceptions and unwinding of Windows on ARM64: the records a 64-bit program
   and the system exchange. Names, layouts and values are those of winnt.h;
   tools/test_arm64_sdk_host.py compares them with mingw-w64. The functions
   are in 70-exceptions.api. */
#define EXCEPTION_MAXIMUM_PARAMETERS 15
#define EXCEPTION_NONCONTINUABLE 0x1
#define EXCEPTION_UNWINDING 0x2
#define EXCEPTION_EXIT_UNWIND 0x4
#define EXCEPTION_STACK_INVALID 0x8
#define EXCEPTION_NESTED_CALL 0x10
#define EXCEPTION_TARGET_UNWIND 0x20
#define EXCEPTION_COLLIDED_UNWIND 0x40
#define EXCEPTION_UNWIND 0x66
#define IS_UNWINDING(f) (((f) & EXCEPTION_UNWIND) != 0)
#define IS_DISPATCHING(f) (((f) & EXCEPTION_UNWIND) == 0)
#define IS_TARGET_UNWIND(f) ((f) & EXCEPTION_TARGET_UNWIND)
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define STATUS_BREAKPOINT ((DWORD)0x80000003L)
#define STATUS_SINGLE_STEP ((DWORD)0x80000004L)
#define STATUS_ACCESS_VIOLATION ((DWORD)0xC0000005L)
#define STATUS_ILLEGAL_INSTRUCTION ((DWORD)0xC000001DL)
#define STATUS_NONCONTINUABLE_EXCEPTION ((DWORD)0xC0000025L)
#define STATUS_INVALID_DISPOSITION ((DWORD)0xC0000026L)
#define STATUS_UNWIND ((DWORD)0xC0000027L)
#define STATUS_BAD_STACK ((DWORD)0xC0000028L)
#define STATUS_INVALID_UNWIND_TARGET ((DWORD)0xC0000029L)
#define STATUS_INTEGER_DIVIDE_BY_ZERO ((DWORD)0xC0000094L)
#define STATUS_STACK_OVERFLOW ((DWORD)0xC00000FDL)
#define STATUS_STACK_BUFFER_OVERRUN ((DWORD)0xC0000409L)
#define STATUS_ASSERTION_FAILURE ((DWORD)0xC0000420L)
#define EXCEPTION_BREAKPOINT STATUS_BREAKPOINT
#define EXCEPTION_SINGLE_STEP STATUS_SINGLE_STEP
#define EXCEPTION_ACCESS_VIOLATION STATUS_ACCESS_VIOLATION
#define EXCEPTION_ILLEGAL_INSTRUCTION STATUS_ILLEGAL_INSTRUCTION
#define EXCEPTION_NONCONTINUABLE_EXCEPTION STATUS_NONCONTINUABLE_EXCEPTION
#define EXCEPTION_INVALID_DISPOSITION STATUS_INVALID_DISPOSITION
#define EXCEPTION_INT_DIVIDE_BY_ZERO STATUS_INTEGER_DIVIDE_BY_ZERO
#define EXCEPTION_STACK_OVERFLOW STATUS_STACK_OVERFLOW
#define UNW_FLAG_NHANDLER 0x0
#define UNW_FLAG_EHANDLER 0x1
#define UNW_FLAG_UHANDLER 0x2
#define UNWIND_HISTORY_TABLE_SIZE 12

typedef LONG NTSTATUS;
#define STATUS_SEVERITY_SUCCESS 0x0
#define STATUS_SEVERITY_INFORMATIONAL 0x1
#define STATUS_SEVERITY_WARNING 0x2
#define STATUS_SEVERITY_ERROR 0x3

/* What a language handler answers. The headers of MinGW make it an int with
   a fifth value of their own, and code written for them returns numbers. */
#ifdef __MINGW32__
#define EXCEPTION_DISPOSITION int
#define ExceptionContinueExecution 0
#define ExceptionContinueSearch 1
#define ExceptionNestedException 2
#define ExceptionCollidedUnwind 3
#define ExceptionExecuteHandler 4
#else
typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind
} EXCEPTION_DISPOSITION;
#endif

typedef struct _EXCEPTION_RECORD {
    DWORD ExceptionCode;
    DWORD ExceptionFlags;
    struct _EXCEPTION_RECORD *ExceptionRecord;
    PVOID ExceptionAddress;
    DWORD NumberParameters;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD, *LPEXCEPTION_RECORD;

#if defined(_M_ARM64) || defined(__aarch64__)
#define CONTEXT_ARM64 0x00400000L
#define CONTEXT_CONTROL (CONTEXT_ARM64 | 0x1L)
#define CONTEXT_INTEGER (CONTEXT_ARM64 | 0x2L)
#define CONTEXT_FLOATING_POINT (CONTEXT_ARM64 | 0x4L)
#define CONTEXT_DEBUG_REGISTERS (CONTEXT_ARM64 | 0x8L)
#define CONTEXT_X18 (CONTEXT_ARM64 | 0x10L)
#define CONTEXT_FULL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#define CONTEXT_ALL (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS | CONTEXT_X18)
#define CONTEXT_UNWOUND_TO_CALL 0x20000000
#define ARM64_MAX_BREAKPOINTS 8
#define ARM64_MAX_WATCHPOINTS 2

typedef union _NEON128 {
    struct {
        ULONGLONG Low;
        LONGLONG High;
    };
    double D[2];
    float S[4];
    WORD H[8];
    BYTE B[16];
} NEON128, *PNEON128;

typedef struct __declspec(align(16)) _CONTEXT {
    DWORD ContextFlags;
    DWORD Cpsr;
    union {
        struct {
            DWORD64 X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16, X17, X18, X19, X20,
                    X21, X22, X23, X24, X25, X26, X27, X28, Fp, Lr;
        };
        DWORD64 X[31];
    };
    DWORD64 Sp;
    DWORD64 Pc;
    NEON128 V[32];
    DWORD Fpcr;
    DWORD Fpsr;
    DWORD Bcr[ARM64_MAX_BREAKPOINTS];
    DWORD64 Bvr[ARM64_MAX_BREAKPOINTS];
    DWORD Wcr[ARM64_MAX_WATCHPOINTS];
    DWORD64 Wvr[ARM64_MAX_WATCHPOINTS];
} CONTEXT, *PCONTEXT, *LPCONTEXT;

/* One entry of the exception directory (.pdata). */
typedef struct _RUNTIME_FUNCTION {
    DWORD BeginAddress;
    union {
        DWORD UnwindData;
        struct {
            DWORD Flag : 2;
            DWORD FunctionLength : 11;
            DWORD RegF : 3;
            DWORD RegI : 4;
            DWORD H : 1;
            DWORD CR : 2;
            DWORD FrameSize : 9;
        };
    };
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION, IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY, *PIMAGE_ARM64_RUNTIME_FUNCTION_ENTRY;

/* The first word of an unwind record (.xdata). */
typedef union IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY_XDATA {
    DWORD HeaderData;
    struct {
        DWORD FunctionLength : 18;
        DWORD Version : 2;
        DWORD ExceptionDataPresent : 1;
        DWORD EpilogInHeader : 1;
        DWORD EpilogCount : 5;
        DWORD CodeWords : 5;
    };
} IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY_XDATA;

typedef struct _UNWIND_HISTORY_TABLE_ENTRY {
    DWORD64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
} UNWIND_HISTORY_TABLE_ENTRY, *PUNWIND_HISTORY_TABLE_ENTRY;

typedef struct _UNWIND_HISTORY_TABLE {
    DWORD Count;
    BYTE LocalHint;
    BYTE GlobalHint;
    BYTE Search;
    BYTE Once;
    DWORD64 LowAddress;
    DWORD64 HighAddress;
    UNWIND_HISTORY_TABLE_ENTRY Entry[UNWIND_HISTORY_TABLE_SIZE];
} UNWIND_HISTORY_TABLE, *PUNWIND_HISTORY_TABLE;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS {
    DWORD64 *X19, *X20, *X21, *X22, *X23, *X24, *X25, *X26, *X27, *X28, *Fp, *Lr;
    DWORD64 *D8, *D9, *D10, *D11, *D12, *D13, *D14, *D15;
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

typedef EXCEPTION_DISPOSITION __stdcall EXCEPTION_ROUTINE(struct _EXCEPTION_RECORD *ExceptionRecord, PVOID EstablisherFrame,
                                                          struct _CONTEXT *ContextRecord, PVOID DispatcherContext);
typedef EXCEPTION_ROUTINE *PEXCEPTION_ROUTINE;

typedef struct _DISPATCHER_CONTEXT {
    ULONG_PTR ControlPc;
    ULONG_PTR ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR EstablisherFrame;
    ULONG_PTR TargetPc;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    PUNWIND_HISTORY_TABLE HistoryTable;
    DWORD ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    /* x19..x29 and d8..d15 of the frame the handler is called for. */
    BYTE *NonVolatileRegisters;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

typedef struct _EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;

typedef LONG(__stdcall *PTOP_LEVEL_EXCEPTION_FILTER)(struct _EXCEPTION_POINTERS *ExceptionInfo);
typedef PTOP_LEVEL_EXCEPTION_FILTER LPTOP_LEVEL_EXCEPTION_FILTER;

/* The scope table of a function with __try, which __C_specific_handler reads. */
typedef struct _SCOPE_TABLE {
    DWORD Count;
    struct {
        DWORD BeginAddress;
        DWORD EndAddress;
        DWORD HandlerAddress;
        DWORD JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE, *PSCOPE_TABLE;

EXCEPTION_DISPOSITION __C_specific_handler(struct _EXCEPTION_RECORD *ExceptionRecord, PVOID EstablisherFrame,
                                           struct _CONTEXT *ContextRecord, struct _DISPATCHER_CONTEXT *DispatcherContext);
#define GetExceptionCode _exception_code
#define GetExceptionInformation() ((struct _EXCEPTION_POINTERS *)_exception_info())
#define AbnormalTermination _abnormal_termination
unsigned long __cdecl _exception_code(void);
void *__cdecl _exception_info(void);
int __cdecl _abnormal_termination(void);
#endif
