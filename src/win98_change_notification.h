// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 filesystem change-notification state and rearming behavior.
#pragma once
namespace Win98ChangeNotification
{
using U = unsigned;
// Removes trailing backslashes from a NUL-terminated buffer but
// keeps the separator of a bare root ("C:\" or "\\"): the byte before the last
// one decides. The original has no length argument and no zero-address check;
// the caller supplies a bounded, nonzero buffer.
template <class E> void StripTrailingSeparators(E &e, U s)
{
    U c = s;
    if (e.Read8(s))
        do
            ++c;
        while (e.Read8(c));
    --c;
    if (c > s)
    {
        const U previous = e.Read8(c - 1);
        if (previous == '\\' || previous == ':')
            return;
    }
    if (c < s)
        return;
    while (c >= s && e.Read8(c) == '\\')
    {
        e.Write8(c, 0);
        --c;
    }
}
// The IFS request carries the path in EDX, the subtree flag in EBX
// and the raw filter in ECX; the object's ring-0 signal object at offset 4 is
// split into ESI (low word) and EDI (high word). KERNEL32 itself validates
// neither the filter bits nor the subtree flag. On success EAX is the IFS
// search handle and is stored at offset 8; on carry EAX is the error.
template <class E> U Arm(E &e, U path, U subtree, U filter, U object)
{
    const U signal = e.SignalObject(object);
    U value = 0;
    if (!e.IfsOpen(path, subtree, filter, signal & 65535, signal >> 16, value))
        return value;
    e.SetDosHandle(object, value);
    return 0;
}
// Re-arming an object without an IFS handle is error 6.
template <class E> U ReArm(E &e, U object)
{
    const U handle = e.DosHandle(object);
    if (!handle)
        return 6;
    U value = 0;
    return e.IfsNext(handle, value) ? 0 : value;
}
// The handle field is exchanged with zero before the IFS close, so a
// second release is a no-op that returns success.
template <class E> U ReleaseIfs(E &e, U object)
{
    const U handle = e.DosHandle(object);
    e.SetDosHandle(object, 0);
    if (!handle)
        return 0;
    U value = 0;
    return e.IfsClose(handle, value) ? 0 : value;
}
// The IFS search is closed outside the heap lock, the object block
// inside it. The IFS result is discarded and does not change the error state.
template <class E> void Destroy(E &e, U object)
{
    ReleaseIfs(e, object);
    e.LockHeap();
    e.FreeObject(object);
    e.UnlockHeap();
}
// The implementation behind the public entry, which only read-
// probes the first path byte under the KERNEL32 exception chain; that probe and
// its Win98 fault dispatch stay in the guest-memory adapter.
template <class E> U First(E &e, U path, U subtree, U filter)
{
    e.Lock();
    const U file_api = e.FileApiMode(1);
    U result = ~0u, error = 0;
    bool report = false;
    e.Error(0);
    const U buffer = e.Scratch();
    const U length = e.FullPath(path, 0x104, buffer, 0);
    if (!length)
    {
        error = 0xa1;
        report = true;
    }
    else if (length >= 0x104)
    {
        error = 0xce;
        report = true;
    }
    else
    {
        StripTrailingSeparators(e, buffer);
        // a 12-byte type-9 object whose offset 4 receives a manual-
        // reset, initially clear ring-0 event. A failed
        // allocation keeps the allocator's own error and reports nothing here.
        const U object = e.AllocateObject(0xc, 9);
        if (object)
        {
            error = Arm(e, buffer, subtree, filter, object);
            bool destroy = error != 0;
            if (!error)
            {
                const U handle = e.CreateHandle(object);
                result = handle;
                if (handle == ~0u)
                {
                    error = e.LastError();
                    destroy = true;
                }
            }
            if (destroy)
            {
                Destroy(e, object);
                report = true;
            }
        }
    }
    if (report)
        e.Error(error);
    if (file_api)
        e.ClearFileApiState(6);
    e.Unlock();
    return result;
}
// the complete export. 0x80000100 selects object type 9. A rejected
// handle leaves the error the resolver stored; a rejected re-arm reports its
// own IFS error through the public SetLastError.
template <class E> U Next(E &e, U handle)
{
    U result = 0;
    e.Lock();
    e.Error(0);
    const U object = e.Resolve(handle, 0x80000100u);
    if (object)
    {
        const U error = ReArm(e, object);
        if (error)
            e.Error(error);
        else
            result = 1;
        e.ReleaseReference(object);
    }
    e.Unlock();
    return result;
}
// reaches the shared Win9x stub thunk with CL = 3: three
// stdcall arguments, error 120, and a zero return rather than -1.
template <class E> U FirstWide(E &e)
{
    e.Error(120);
    return 0;
}
// FindCloseChangeNotification (ordinal 281) and CloseHandle (ordinal 160) are
// the same body. The object's type-9 destructor above runs when the
// last handle reference disappears; there is no separate close routine.
} // namespace Win98ChangeNotification
