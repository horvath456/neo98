// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 semaphore wait, release, and wake-up behavior.
#pragma once
namespace Win98Semaphore
{
using U = unsigned;
struct State { U count, maximum; };
// Compare as signed x86 LONG without signed overflow or implementation-defined casts.
inline bool SignedLess(U a, U b) { return (a ^ 0x80000000u) < (b ^ 0x80000000u); }
// KERNEL32: the original constructor checks only maximum >= initial.
inline bool Initialize(State &s, U initial, U maximum)
{
    if (SignedLess(maximum, initial)) return false;
    s.count = initial;
    s.maximum = maximum;
    return true;
}
// Internal semaphore wait behavior.
inline bool CanAcquire(const State &s) { return s.count && !(s.count & 0x80000000u); }
inline bool Acquire(State &s)
{
    if (!CanAcquire(s)) return false;
    --s.count;
    return true;
}
// The internal semaphore release requires a writable PreviousCount pointer.
// Adapt the optional public pointer to scratch storage. Public validation,
// error mapping, and locking belong to the service provider. Publish the
// updated count before waking waiters.
template <class E> bool ReleaseCount(E &e, State &s, U amount, U previous)
{
    const U cookie = e.EnterCount();
    const U old = s.count, next = old + amount;
    if (SignedLess(s.maximum, next))
    {
        e.LeaveCount(cookie);
        return false;
    }
    s.count = next;
    e.Write32(previous, old);
    e.WakeWaiters(s);
    e.LeaveCount(cookie);
    return true;
}
// SECURITY_ATTRIBUTES descriptor pointer is ignored by this helper.
template <class E> U SecurityFlags(E &e, U security)
{
    if (!security) return 0;
    if (e.Read32(security) != 12) return ~0u;
    return e.Read32(security + 8) ? 0x80000000u : 0;
}
// Complete KERNEL32 constructor. Allocation initializes the
// original object header/name storage; InitializeObject supplies native layout.
template <class E> U Construct(E &e, U initial, U maximum)
{
    State state{};
    if (!Initialize(state, initial, maximum)) { e.Error(87); return 0; }
    U object = e.AllocateObject(0x14, 1);
    if (object) e.InitializeObject(object, initial, maximum);
    return object;
}
// The adapter handles public pointer probes and SEH.
// OpenNamed returns ~0u for absent, 0 for failure, or an allocated handle.
// BindName records the returned name in the object as part of its provider.
template <class E> U Create(E &e, U security, U initial, U maximum, U name)
{
    U result = 0;
    if (name && !e.Read8(name)) name = 0;
    U flags = SecurityFlags(e, security);
    if (flags == ~0u) { e.Error(87); return 0; }
    flags |= 0x1f0003u;
    e.LockNamespace();
    e.Error(0);
    if (name)
    {
        result = e.OpenNamed(name, 1, flags);
        if (result != ~0u)
        {
            if (result) e.Error(183);
            e.UnlockNamespace();
            return result;
        }
    }
    U object = Construct(e, initial, maximum);
    if (object)
    {
        result = e.CreateHandle(object, flags);
        if (result == ~0u)
        {
            U error = e.LastError();
            e.DestroyObject(object);
            result = 0;
            e.Error(error);
        }
        else if (name && !e.BindName(object, name))
        {
            U error = e.LastError();
            e.CloseHandle(result);
            result = 0;
            e.Error(error);
        }
    }
    e.UnlockNamespace();
    return result;
}
// Access is masked, and missing names become error 123.
template <class E> U Open(E &e, U access, U inherit, U name)
{
    e.LockNamespace();
    U flags = (access & 0x1f0003u) | (inherit ? 0x80000000u : 0);
    U result = e.OpenNamed(name, 1, flags);
    if (result == ~0u) { e.Error(123); result = 0; }
    e.UnlockNamespace();
    return result;
}
// Signal is the explicit Win32 VxDCall 0x2A005E provider;
// the separate VWIN32 ReleaseCount body above does not establish that service's
// complete argument/error translation. Public writable-pointer probe
// remains the ARM adapter's responsibility and precedes this critical section.
template <class E> U Release(E &e, U handle, U amount, U previous)
{
    U result = 0;
    e.EnterCritical();
    U object = e.ResolveHandle(handle, 0x80000001u, 0);
    if (object)
    {
        result = e.Signal(object, amount, previous);
        e.ReleaseReference(object);
    }
    e.LeaveCritical();
    return result;
}
} // namespace Win98Semaphore
