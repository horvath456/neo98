// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 miscellaneous system-query, debugging, and compatibility behavior.
#pragma once
namespace Win98KernelMisc
{
// The tag table in level order 0..3.
inline const char *DebugTag(unsigned level)
{
    static const char *const tags[] = {"fatl", "err", "wn", "t"};
    return tags[level & 3];
}
inline bool DebugPrints(unsigned level) { return (int)level <= 2; }
inline bool DebugBreaks(unsigned level) { return (int)level <= 1; }
// Appends without libc; capacity includes the terminator.
inline void DebugLine(char *out, unsigned capacity, unsigned level, const char *text)
{
    unsigned n = 0;
    const auto append = [&](const char *s)
    {
        while (*s && n + 1 < capacity) out[n++] = *s++;
    };
    append(DebugTag(level));
    append(" K32 ");
    append(text);
    append("\r\n");
    out[n] = 0;
}
constexpr unsigned FatalExitCode = 0xffffffffu, FatalCaptionString = 0x65, FatalTextString = 0x66;
constexpr unsigned NoUiProcessFlag = 0x10000000u, NoFaultBox = 2;
// True when the box is skipped and the process ends at once.
inline bool FatalTerminatesSilently(unsigned process_flags, unsigned error_mode, bool debugger)
{
    if (process_flags & NoUiProcessFlag) return true;
    return (error_mode & NoFaultBox) && !debugger;
}
// True when the process keeps running: only a debugger's Cancel (button 1).
inline bool FatalReturns(unsigned error_mode, bool debugger, unsigned box_result)
{
    if (error_mode & NoFaultBox) return debugger;
    return box_result == 1;
}
// RequestWakeupLatency ring-3 state. Driver(enable) is the NTKERN service
// 0x4B0009 the original calls on the first request and the last release; a
// nonzero driver result is error 50.
template <class E> unsigned WakeupLatency(E &e, unsigned latency)
{
    if (latency > 1)
    {
        e.Error(87);
        return 0;
    }
    if (latency == 1)
    {
        if (e.Flag()) return 1;
        if (!e.Count() && e.Driver(true))
        {
            e.Error(50);
            return 0;
        }
        e.Count(e.Count() + 1);
        e.Flag(true);
        return 1;
    }
    if (!e.Flag()) return 1;
    if (e.Count() == 1 && e.Driver(false))
    {
        e.Error(50);
        return 0;
    }
    e.Count(e.Count() - 1);
    e.Flag(false);
    return 1;
}
inline unsigned PowerStateError(unsigned driver_status)
{
    return driver_status == 0x80000002u ? 1140 : 1141;
}
struct SysMsgHandler
{
    unsigned handler = 0, thread = 0, minimum = 0, maximum = 0, synchronous = 0, references = 0;
};
template <unsigned N> struct SysMsgList
{
    SysMsgHandler entries[N];
    // Flag 1 removes every matching entry; the original still answers 1.
    unsigned Unregister(unsigned handler, unsigned thread, unsigned minimum, unsigned maximum)
    {
        for (auto &entry : entries)
            if (entry.references && entry.thread == thread &&
                (!handler || (entry.handler == handler && entry.minimum == minimum && entry.maximum == maximum)))
                entry = SysMsgHandler{};
        return 1;
    }
    unsigned Register(unsigned handler, unsigned thread, unsigned minimum, unsigned maximum, unsigned flags)
    {
        for (auto &entry : entries)
            if (!entry.references)
            {
                entry.handler = handler;
                entry.thread = thread;
                entry.minimum = minimum;
                entry.maximum = maximum;
                entry.synchronous = flags & 2;
                entry.references = 1;
                return 1;
            }
        return 0; // The original's node allocation failure: error 8 by the caller.
    }
    // an entry matches when minimum <= message <= maximum, and a
    // flag-1 signal only visits entries registered with flag 2.
    unsigned Matching(unsigned message, unsigned flags) const
    {
        unsigned n = 0;
        for (const auto &entry : entries)
            if (entry.references && entry.minimum <= message && entry.maximum >= message &&
                (!(flags & 1) || entry.synchronous))
                ++n;
        return n;
    }
};
inline bool SysMsgRegisterFlagsValid(unsigned flags) { return !(flags & ~3u); }
inline bool SysMsgSignalFlagsValid(unsigned flags) { return !(flags & ~7u); }
constexpr unsigned SysMsgFailure = 0xffffffffu;
// KRNL386 GETPRODUCTNAME. The size travels as a 16-bit word; lstrcpyn copies
// at most size-1 characters and terminates. The 16-bit return is lstrlen's
// length for size 0 and otherwise lstrcpyn's DX:AX destination, of which the
// caller keeps AX: the low word of the mapped buffer offset.
inline const char *ProductName() { return "Windows 98 "; }
inline unsigned ProductNameLength()
{
    unsigned n = 0;
    while (ProductName()[n]) ++n;
    return n;
}
template <class E> unsigned ProductNameCopy(E &e, unsigned buffer, unsigned size)
{
    size &= 0xffff;
    if (!size) return ProductNameLength();
    const char *s = ProductName();
    unsigned n = 0;
    while (n + 1 < size && s[n])
    {
        e.Byte(buffer + n, s[n]);
        ++n;
    }
    e.Byte(buffer + n, 0);
    return buffer & 0xffff;
}
} // namespace Win98KernelMisc
