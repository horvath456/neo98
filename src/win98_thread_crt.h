// SPDX-License-Identifier: GPL-2.0-or-later
// Original MSVCRT thread startup, per-thread data and cleanup.
#pragma once
namespace Win98ThreadCrt
{
using U = unsigned;
struct State
{
    U tls_index = 0xffffffffu;
    U action_table = 0;
    U fp_init = 0, fp_term = 0;
};
// Providers: Read/Write(address,DWORD), Calloc(count,bytes), Free(pointer),
// GetLastError/SetLastError, TlsAlloc/Free/Get/Set, CurrentThreadId,
// InitializeLocks/TerminateLocks, CreateCrtThread(security,stack,ptd,flags,tidOut),
// InvokeNoArgs(hook), InvokeThread(stdcallFunction,argument,result)->bool,
// ExitThread(result),
// Fatal(code). Fatal must stop execution. Thread callback exception dispatch is
// an explicit provider boundary, not an emulated SEH frame in native C++.

// The preceding calloc provides all other initial zeroes.
template<class E> void InitializeData(E &e, State &s, U data)
{
    e.Write(data + 0x50, s.action_table);
    e.Write(data + 0x14, 1);
}

// Failed process initialization retains the allocation
// state; any process-attach rollback belongs to the caller/provider.
template<class E> U Initialize(E &e, State &s)
{
    e.InitializeLocks();
    s.tls_index = e.TlsAlloc();
    if (s.tls_index == 0xffffffffu) return 0;
    const U data = e.Calloc(1, 0x74);
    if (!data || !e.TlsSet(s.tls_index, data)) return 0;
    InitializeData(e, s, data);
    const U id = e.CurrentThreadId();
    e.Write(data + 4, 0xffffffffu);
    e.Write(data, id);
    return 1;
}
// Per-thread allocation cleanup is a separate operation.
template<class E> void Terminate(E &e, State &s)
{
    e.TerminateLocks();
    if (s.tls_index != 0xffffffffu)
    {
        e.TlsFree(s.tls_index);
        s.tls_index = 0xffffffffu;
    }
}

// _getptd obtains or initializes the current thread data.
template<class E> U GetData(E &e, State &s)
{
    const U error = e.GetLastError();
    U data = e.TlsGet(s.tls_index);
    if (!data)
    {
        data = e.Calloc(1, 0x74);
        if (!data || !e.TlsSet(s.tls_index, data)) { e.Fatal(16); return 0; }
        InitializeData(e, s, data);
        const U id = e.CurrentThreadId();
        e.Write(data + 4, 0xffffffffu);
        e.Write(data, id);
    }
    e.SetLastError(error);
    return data;
}

// _freeptd: a zero pointer selects the current TLS value.
template<class E> void FreeData(E &e, State &s, U data)
{
    if (s.tls_index == 0xffffffffu) return;
    if (!data) data = e.TlsGet(s.tls_index);
    if (data)
    {
        constexpr U offsets[] = {0x24, 0x28, 0x30, 0x38, 0x40, 0x44};
        for (U offset : offsets)
        {
            const U buffer = e.Read(data + offset);
            if (buffer) e.Free(buffer);
        }
        const U actions = e.Read(data + 0x50);
        if (actions != s.action_table) e.Free(actions); // Includes free(NULL).
        e.Free(data);
    }
    e.TlsSet(s.tls_index, 0);
}

// _dosmaperr maps operating-system errors in table order.
// Duplicate error 6 is intentional: the first row wins in the original search.
inline U ErrorNumber(U error)
{
    constexpr U table[][2] = {
        {1,22},{2,2},{3,2},{4,24},{5,13},{6,9},{7,12},{8,12},{9,12},{10,7},
        {11,8},{12,22},{13,22},{15,2},{16,13},{17,18},{18,2},{33,13},{53,2},
        {65,13},{67,2},{80,17},{82,13},{83,13},{87,22},{89,11},{108,13},
        {109,32},{112,28},{114,9},{6,22},{128,10},{129,10},{130,9},{131,22},
        {132,13},{145,41},{158,13},{161,2},{164,11},{167,13},{183,17},
        {206,2},{215,11},{1816,12}
    };
    for (const auto &row : table) if (row[0] == error) return row[1];
    if (error >= 19 && error <= 36) return 13;
    if (error >= 188 && error <= 202) return 8;
    return 22;
}
template<class E> void MapError(E &e, State &s, U error)
{
    U data = GetData(e, s);
    if (!data) return;
    e.Write(data + 0x0c, error);
    const U number = ErrorNumber(error);
    data = GetData(e, s);
    if (data) e.Write(data + 8, number);
}

// _beginthreadex initializes thread data and creates the thread.
template<class E> U BeginEx(E &e, State &s, U security, U stack, U function,
                          U argument, U flags, U thread_id)
{
    const U data = e.Calloc(1, 0x74);
    U error = 0;
    if (data)
    {
        InitializeData(e, s, data);
        e.Write(data + 4, 0xffffffffu);
        e.Write(data + 0x48, function);
        e.Write(data + 0x4c, argument);
        const U handle = e.CreateCrtThread(security, stack, data, flags, thread_id);
        if (handle) return handle;
        error = e.GetLastError();
    }
    e.Free(data);
    if (error) MapError(e, s, error);
    return 0;
}

// _endthreadex: the caller owns the handle;
// unlike _endthread, this function never calls CloseHandle.
template<class E> void EndEx(E &e, State &s, U result)
{
    if (s.fp_term) e.InvokeNoArgs(s.fp_term);
    const U data = GetData(e, s);
    if (!data) return;
    FreeData(e, s, data);
    e.ExitThread(result);
}

// _threadstartex invokes the callback on the normal execution path.
// The original x86 SEH registration/filter/unwind is provided by the runtime.
template<class E> void RunEx(E &e, State &s, U data)
{
    if (!e.TlsSet(s.tls_index, data)) { e.Fatal(16); return; }
    e.Write(data, e.CurrentThreadId());
    if (s.fp_init) e.InvokeNoArgs(s.fp_init);
    const U argument = e.Read(data + 0x4c);
    const U function = e.Read(data + 0x48);
    U result = 0;
    // A callback may call _endthreadex or fault without returning to its caller.
    if (e.InvokeThread(function, argument, result)) EndEx(e, s, result);
}
} // namespace Win98ThreadCrt
