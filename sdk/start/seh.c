/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* __try, __except and __finally of an ARM64 application. The compiler names
   this function as the handler of every function with such a block and puts
   the scope table behind it; the system calls it when it walks the frames
   for an exception (RaiseException, a fault of the processor) and when it
   unwinds them (RtlUnwindEx). It is linked statically, so the same file runs
   wherever KERNEL32 exports the unwinder. docs/ARM64-APPS.md. */
#include <windows.h>

/* seh_call.S: calls a filter or a termination handler with the registers of
   the frame it belongs to. */
LONG __neo98_seh_call(void *first, void *frame, void *function, const BYTE *nonvolatile);

EXCEPTION_DISPOSITION __C_specific_handler(struct _EXCEPTION_RECORD *record, PVOID frame, struct _CONTEXT *context,
                                           struct _DISPATCHER_CONTEXT *dispatch)
{
    const SCOPE_TABLE *table = (const SCOPE_TABLE *)dispatch->HandlerData;
    const ULONG_PTR base = dispatch->ImageBase;
    ULONG_PTR pc = dispatch->ControlPc;
    /* x19..x29 and d8..d15 of the frame: the dispatcher names them; its
       context of the frame holds the same values. */
    BYTE own[19 * 8];
    const BYTE *registers = dispatch->NonVolatileRegisters;
    DWORD i;
    if (!registers)
    {
        ULONG_PTR values[19];
        for (i = 0; i < 11; ++i) values[i] = dispatch->ContextRecord->X[19 + i];
        for (i = 0; i < 8; ++i) values[11 + i] = dispatch->ContextRecord->V[8 + i].Low;
        memcpy(own, values, sizeof own);
        registers = own;
    }
    /* The pc of a frame that stands behind a call is the return address; the
       call itself is the instruction before. */
    if (dispatch->ControlPcIsUnwound) pc -= 4;
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        for (i = dispatch->ScopeIndex; i < table->Count; ++i)
        {
            const ULONG_PTR begin = base + table->ScopeRecord[i].BeginAddress, end = base + table->ScopeRecord[i].EndAddress;
            if (pc < begin || pc >= end) continue;
            if (table->ScopeRecord[i].JumpTarget)
            {
                /* The __except block the unwind ends in: what encloses it stays. */
                if ((record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) && dispatch->TargetPc == base + table->ScopeRecord[i].JumpTarget)
                    break;
                continue;
            }
            /* A __finally block the target lies in is not left. */
            if ((record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) && dispatch->TargetPc >= begin && dispatch->TargetPc < end) break;
            dispatch->ScopeIndex = i + 1;
            __neo98_seh_call((void *)(ULONG_PTR)TRUE, frame, (void *)(base + table->ScopeRecord[i].HandlerAddress), registers);
        }
        return ExceptionContinueSearch;
    }
    for (i = dispatch->ScopeIndex; i < table->Count; ++i)
    {
        const ULONG_PTR begin = base + table->ScopeRecord[i].BeginAddress, end = base + table->ScopeRecord[i].EndAddress;
        if (pc < begin || pc >= end || !table->ScopeRecord[i].JumpTarget) continue;
        if (table->ScopeRecord[i].HandlerAddress != EXCEPTION_EXECUTE_HANDLER)
        {
            EXCEPTION_POINTERS pointers;
            LONG answer;
            pointers.ExceptionRecord = record;
            pointers.ContextRecord = context;
            answer = __neo98_seh_call(&pointers, frame, (void *)(base + table->ScopeRecord[i].HandlerAddress), registers);
            if (answer < 0) return ExceptionContinueExecution;
            if (!answer) continue;
        }
        /* The block takes it: unwind to it. This does not return. */
        RtlUnwindEx(frame, (PVOID)(base + table->ScopeRecord[i].JumpTarget), record, (PVOID)(ULONG_PTR)record->ExceptionCode,
                    dispatch->ContextRecord, dispatch->HistoryTable);
    }
    return ExceptionContinueSearch;
}
