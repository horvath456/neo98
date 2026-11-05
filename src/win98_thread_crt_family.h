// SPDX-License-Identifier: GPL-2.0-or-later
// Additional original MSVCRT consumers of the same 116-byte PTD.
#pragma once
#include "win98_thread_crt.h"
namespace Win98ThreadCrt
{
// Additional providers: CreateLegacyThread(stack,ptd), ResumeThread(handle),
// CloseThread(handle), InvokeLegacyThread(cdeclFunction,argument)->bool,
// ReadByte(address), WriteByte(address,value). CreateLegacyThread supplies
// security=NULL, CREATE_SUSPENDED, the legacy trampoline, and tidOut=ptd.

// Public __threadid and __threadhandle forward directly to the OS.
// __threadhandle returns the current-thread pseudo handle, not PTD+4.
template<class E> U ThreadId(E &e) { return e.CurrentThreadId(); }
template<class E> U ThreadHandle(E &e) { return e.CurrentThreadHandle(); }

// _beginthread stores the real handle in PTD+4,
// resumes the initially suspended OS thread, and returns FFFFFFFF on failure.
template<class E> U Begin(E &e, State &s, U function, U stack, U argument)
{
    const U data = e.Calloc(1, 0x74);
    U error = 0;
    if (data)
    {
        InitializeData(e, s, data);
        e.Write(data + 0x48, function);
        e.Write(data + 0x4c, argument);
        const U handle = e.CreateLegacyThread(stack, data);
        e.Write(data + 4, handle);
        if (handle && e.ResumeThread(handle) != 0xffffffffu) return handle;
        error = e.GetLastError();
    }
    // The original does not close a handle after failed ResumeThread.
    e.Free(data);
    if (error) MapError(e, s, error);
    return 0xffffffffu;
}

// _endthread passes even a zero saved handle to CloseHandle;
// FFFFFFFF is the sole sentinel for a thread whose handle the CRT does not own.
template<class E> void End(E &e, State &s)
{
    if (s.fp_term) e.InvokeNoArgs(s.fp_term);
    const U data = GetData(e, s);
    if (!data) return;
    const U handle = e.Read(data + 4);
    if (handle != 0xffffffffu) e.CloseThread(handle);
    FreeData(e, s, data);
    e.ExitThread(0);
}

// _threadstart invokes the normal callback. The OS already wrote the
// thread ID through CreateThread's tidOut=ptd. This routine does not query it.
template<class E> void Run(E &e, State &s, U data)
{
    if (!e.TlsSet(s.tls_index, data)) { e.Fatal(16); return; }
    if (s.fp_init) e.InvokeNoArgs(s.fp_init);
    const U argument = e.Read(data + 0x4c);
    const U function = e.Read(data + 0x48);
    if (e.InvokeLegacyThread(function, argument)) End(e, s);
}

// The original export is __doserrno (two leading underscores).
// _errno and __doserrno expose per-thread error storage.
template<class E> U Errno(E &e, State &s)
{
    const U data = GetData(e, s);
    return data ? data + 8 : 0;
}
template<class E> U DosErrno(E &e, State &s)
{
    const U data = GetData(e, s);
    return data ? data + 0x0c : 0;
}

// srand and rand use unsigned arithmetic to preserve
// the low DWORD of the x86 multiply/add, including wrap at every seed value.
template<class E> void Srand(E &e, State &s, U seed)
{
    const U data = GetData(e, s);
    if (data) e.Write(data + 0x14, seed);
}
template<class E> U Rand(E &e, State &s)
{
    const U data = GetData(e, s);
    if (!data) return 0;
    const U seed = e.Read(data + 0x14) * 0x343fdu + 0x269ec3u;
    e.Write(data + 0x14, seed);
    return (seed >> 16) & 0x7fff;
}

// strtok uses a 256-bit delimiter map that includes the terminator.
// The continuation pointer belongs to PTD+18. Invalid source pointers fault
// through the memory provider, as in the original; no empty-string substitute.
template<class E> U Strtok(E &e, State &s, U text, U delimiters)
{
    const U data = GetData(e, s);
    if (!data) return 0;
    unsigned char map[32]{};
    U c;
    do
    {
        c = e.ReadByte(delimiters++);
        map[c >> 3] |= 1u << (c & 7);
    } while (c);
    U scan = text ? text : e.Read(data + 0x18);
    while ((c = e.ReadByte(scan)) && (map[c >> 3] & (1u << (c & 7)))) ++scan;
    const U first = scan;
    while ((c = e.ReadByte(scan)))
    {
        if (map[c >> 3] & (1u << (c & 7)))
        {
            e.WriteByte(scan++, 0);
            break;
        }
        ++scan;
    }
    e.Write(data + 0x18, scan);
    return first != scan ? first : 0;
}
} // namespace Win98ThreadCrt
