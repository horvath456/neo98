// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 pipe, mailslot, device, and socket-handle services. Handle lookup
// validates object type and access, optionally adds a reference, and reports DOS
// errors through LastError. Ring-zero operations are native providers.
#pragma once
namespace Win98KernelPipes
{
typedef unsigned U;
inline U Length(const char *s) { U n = 0; while (s[n]) ++n; return n; }
inline bool DriveName(const char *s, U &drive)
{
    // "X:" exactly; /.
    if (!s[0] || s[1] != ':' || s[2]) return false;
    const U letter = (unsigned char)s[0] & 0xdf;
    if (letter < 'A' || letter > 'Z') return false;
    drive = letter - 'A';
    return true;
}
inline bool Prefix(const char *s, const char *p)
{
    for (; *p; ++s, ++p) if (((unsigned char)*s & 0xdf) != ((unsigned char)*p & 0xdf)) return false;
    return true;
}
// with : names of four or more characters starting with
// COM or LPT, followed by digits without a leading zero and an optional
// trailing colon, are serial/printer devices. The LPT branch also asks a
// spooler module and the "\\.\" branch asks VCOMM (service 0x2B0015);
// neither exists natively, so only the syntax decision is kept.
inline bool SerialDeviceName(const char *s)
{
    if (Length(s) < 4 || !(Prefix(s, "COM") || Prefix(s, "LPT"))) return false;
    const char *p = s + 3;
    if (*p == '0') return false;
    while (*p >= '0' && *p <= '9') ++p;
    return !*p || (*p == ':' && !p[1]);
}

// every completion-port query is refused. The port creator
// (CreateIoCompletionPort) already carries the original failure contract.
template <class E> U GetQueuedCompletionStatus(E &e, const U *a)
{
    if (a[3]) e.Write(a[3], 0);
    e.Error(50);
    return 0;
}

// any object resolves; only type 0x13 (socket) is accepted. The
// resolver notes 6 (no object) or 66 (other type), but both callers overwrite
// that with 6, so 66 is never visible.
// E::SocketObject(handle) returns 0 (ok), 6 (no object) or 66 (other type).
template <class E> U GetHandleContext(E &e, const U *a)
{
    if (e.SocketObject(a[0])) { e.Error(6); return 0; }
    return e.SocketContext(a[0]);
}
template <class E> U SetHandleContext(E &e, const U *a)
{
    if (e.SocketObject(a[0])) { e.Error(6); return 0; }
    if (e.SocketContext(a[0])) { e.Error(183); return 0; }
    e.SetSocketContext(a[0], a[1]);
    return 1;
}

// security attributes. Returns 0/0x80000000 or ~0u for a bad length.
inline U SecurityFlags(bool present, U length, U inherit)
{
    if (!present) return 0;
    if (length != 12) return ~0u;
    return inherit ? 0x80000000u : 0;
}

// Named pipe client calls. E::PipeObject(handle, access, dos) resolves a
// type-0x10 object (0 ok, 6 none, 12 no DOS handle); E::NetPipe(function,
// arguments) is the INT 21h 5Fxxh provider returning the DOS error (0 ok).
struct PipeRequest
{
    U function = 0, handle = 0, name = 0, in = 0, in_size = 0, out = 0, out_size = 0, timeout = 0, level = 0;
    U bytes = 0;          // ECX after 5F36h/5F37h
    U state = 0, instances = 0, max_instances = 0, out_buffer = 0, in_buffer = 0, mode = 0, collect = 0, collect_timeout = 0;
};
template <class E> U CallNamedPipeA(E &e, const U *a)
{
    // Windows 98 stores zero through lpBytesRead before the DOS call.
    if (!e.Writable(a[5], 4)) { e.Error(87); return 0; }
    e.Write(a[5], 0);
    PipeRequest r; r.function = 0x5f37; r.name = a[0]; r.in = a[1]; r.in_size = a[2]; r.out = a[3]; r.out_size = a[4]; r.timeout = a[6];
    const U error = e.NetPipe(r);
    if (!error || error == 0xea) e.Write(a[5], r.bytes);
    if (!error) return 1;
    e.Error(error);
    return 0;
}
template <class E> U WaitNamedPipeA(E &e, const U *a)
{
    PipeRequest r; r.function = 0x5f38; r.name = a[0]; r.timeout = a[1];
    const U error = e.NetPipe(r);
    if (!error) return 1;
    e.Error(error);
    return 0;
}
template <class E> U TransactNamedPipe(E &e, const U *a)
{
    U dos = 0;
    const U access = (a[2] ? 0x110u : 0u) | (a[4] ? 0x20u : 0u);
    U error = e.PipeObject(a[0], access, dos);
    if (!error && a[6]) error = 87;   // no overlapped transactions
    if (error) { e.Error(error); return 0; }
    PipeRequest r; r.function = 0x5f36; r.handle = dos; r.in = a[1]; r.in_size = a[2]; r.out = a[3]; r.out_size = a[4];
    error = e.NetPipe(r);
    if (error && error != 0xea) r.bytes = 0;
    if (e.Writable(a[5], 4)) e.Write(a[5], r.bytes);
    if (!error) return 1;
    e.Error(error);
    return 0;
}
template <class E> U GetNamedPipeHandleStateA(E &e, const U *a)
{
    U dos = 0;
    U error = e.PipeObject(a[0], 0x20, dos);
    if (!error && a[5]) error = 87;   // user names are never available
    if (error) { e.Error(error); return 0; }
    PipeRequest r; r.function = 0x5f32; r.handle = dos; r.level = 0x107;
    if (!(error = e.NetPipe(r))) { r.function = 0x5f33; error = e.NetPipe(r); }
    if (!error) { r.function = 0x5f3c; r.level = 6; error = e.NetPipe(r); }
    if (error) { e.Error(error); return 0; }
    if (a[1]) e.Write(a[1], ((r.state & 0x100) ? 2u : 0u) | ((r.state & 0x8000) ? 1u : 0u));
    if (a[2]) e.Write(a[2], r.instances);
    if (a[4]) e.Write(a[4], r.collect_timeout);
    if (a[3]) e.Write(a[3], r.collect & 0xffff);
    return 1;
}
template <class E> U SetNamedPipeHandleState(E &e, const U *a)
{
    U dos = 0;
    U error = e.PipeObject(a[0], 0x110, dos);
    if (error) { e.Error(error); return 0; }
    PipeRequest r; r.handle = dos;
    if (a[1])
    {
        const U mode = e.Read(a[1]);
        r.function = 0x5f34; r.mode = ((mode & 2) ? 0x100u : 0u) | ((mode & 1) ? 0x8000u : 0u);
        if ((error = e.NetPipe(r))) { e.Error(error); return 0; }
    }
    // which collection values travel with 5F3Bh.
    U kind = 4;
    if (a[2] && a[3]) { kind = 0; r.collect = e.Read(a[2]) >= 0x10000 ? 0xffff : e.Read(a[2]); r.collect_timeout = e.Read(a[3]); }
    else if (a[2]) { kind = 2; r.collect = e.Read(a[2]) >= 0x10000 ? 0xffff : e.Read(a[2]); }
    else if (a[3]) { kind = 1; r.collect_timeout = e.Read(a[3]); }
    if (kind == 4) return 1;
    r.function = 0x5f3b; r.level = 6; r.state = kind;
    if ((error = e.NetPipe(r))) { e.Error(error); return 0; }
    return 1;
}
template <class E> U GetNamedPipeInfo(E &e, const U *a)
{
    U dos = 0;
    U error = e.PipeObject(a[0], 0x20, dos);
    if (error) { e.Error(error); return 0; }
    PipeRequest r; r.function = 0x5f32; r.handle = dos; r.level = 0x107;
    if (!(error = e.NetPipe(r))) { r.function = 0x5f33; error = e.NetPipe(r); }
    if (error) { e.Error(error); return 0; }
    if (a[2]) e.Write(a[2], r.out_buffer & 0xffff);
    if (a[3]) e.Write(a[3], r.in_buffer & 0xffff);
    if (a[4]) e.Write(a[4], (r.max_instances & 0xff) ? (r.max_instances & 0xff) : 0xff);
    if (a[1]) e.Write(a[1], (r.state & 0x300) ? 4u : 0u);
    return 1;
}

// Mailslots. E::MailslotObject(handle) resolves a type-0x11 object (0 ok,
// 6 none); E::MailslotCreate/Info/SetTimeout are the IFSMGR provider.
template <class E> U CreateMailslotA(E &e, const U *a)
{
    const U flags = SecurityFlags(a[3] != 0, a[3] ? e.Read(a[3]) : 0, a[3] ? e.Read(a[3] + 8) : 0);
    if (flags == ~0u) { e.Error(13); return ~0u; }
    U handle = 0;
    const U error = e.MailslotCreate(a[0], a[1], a[2], (flags & 0x80000000u) != 0, handle);
    if (error == ~0u) return ~0u;   // no object, no error of its own
    if (error) { e.Error(error); return ~0u; }
    return handle;
}
template <class E> U GetMailslotInfo(E &e, const U *a)
{
    U error = e.MailslotObject(a[0]);
    if (error) { e.Error(error); return 0; }
    U maximum = 0, next = 0, count = 0, timeout = 0;
    if ((error = e.MailslotInfo(a[0], maximum, next, count, timeout))) { e.Error(error); return 0; }
    if (a[1]) e.Write(a[1], maximum);
    if (a[2]) e.Write(a[2], next);
    if (a[3]) e.Write(a[3], count);
    if (a[4]) e.Write(a[4], timeout);
    return 1;
}
template <class E> U SetMailslotInfo(E &e, const U *a)
{
    U error = e.MailslotObject(a[0]);
    if (!error) error = e.MailslotSetTimeout(a[0], a[1]);
    if (error) { e.Error(error); return 0; }
    return 1;
}

// lpBytesReturned may be NULL; the VxD receives it as given.
// E::DeviceObject(handle) resolves any object: 0 ok, 6 none;
// E::OverlappedHandle(handle) is bit 0x1000 of the handle flags;
// E::DeviceType(handle) is the K32 type byte (0xF = VxD device);
// E::VxdControl(handle, a) is the VWIN32 service 0x2A001F provider and
// returns 0 (done), ~0u (asynchronous) or the error code.
template <class E> U DeviceIoControl(E &e, const U *a)
{
    if (!a[1]) { e.Error(87); return 0; }   // control code 0 is refused before any lookup
    U error = e.DeviceObject(a[0]);
    if (error) { e.Error(error); return 0; }
    // only an overlapped-capable handle looks at lpOverlapped; its
    // hEvent must then resolve, and the object reference lands in Internal.
    U event = 0;
    if (e.OverlappedHandle(a[0]) && a[7])
    {
        event = e.Read(a[7] + 16);
        if (e.DeviceObject(event)) { e.Error(87); return 0; }
        e.Write(a[7], event);
    }
    const U type = e.DeviceType(a[0]);
    if (type != 0xf) { e.Error(87); return 0; }   // serial (0xE) and type 0x14 have no native object
    error = e.VxdControl(a[0], a);
    if (!error) return 1;
    // -1 means pending only when the overlapped event resolved.
    e.Error(error == ~0u ? (event ? 997u : 87u) : error);
    return 0;
}

// E::Drives is GetLogicalDrives; E::SubstQuery(drive1, buffer)
// is INT 21h 71AAh BH=2 (false on carry); the buffer holds 0x104 bytes.
template <class E> U QueryDosDeviceA(E &e, const char *name, U target, U capacity)
{
    if (!name || !target || capacity > 0x104) { e.Error(87); return 0; }
    char subst[0x105] = {};
    const char *source = name;
    if (!SerialDeviceName(name))
    {
        U drive = 0;
        if (!DriveName(name, drive) || !((e.Drives() >> drive) & 1)) { e.Error(2); return 0; }
        if (e.SubstQuery(drive + 1, subst))
        {
            if (subst[0] && subst[1] == ':' && !subst[2]) { subst[2] = '\\'; subst[3] = 0; }
            source = subst;
        }
    }
    const U n = Length(source);
    if (n + 1 > capacity) { e.Error(122); return 0; }
    for (U i = 0; i <= n; ++i) e.Byte(target + i, (unsigned char)source[i]);
    return n;   // characters copied, the terminator is not counted
}

// E::SubstRemove(drive1) / E::SubstCreate(drive1, path) are INT 21h
// 71AAh BH=1 / BH=0 (false on carry); E::FullPath(text, out) is the internal
// path resolver (0 on failure with the error already set).
template <class E> U DefineDosDeviceA(E &e, U flags, const char *name, const char *target)
{
    if (!name) { e.Error(87); return 0; }
    const bool remove = (flags & 2) != 0;
    if (!remove && !target) { e.Error(87); return 0; }
    U drive = 0;
    if (!DriveName(name, drive)) { e.Error(161); return 0; }
    if ((e.Drives() >> drive) & 1)
    {
        if (!e.SubstRemove(drive + 1)) { e.Error(5); return 0; }
        if (remove) return ~0u;
    }
    else if (remove) return 1;
    char full[0x104] = {};
    if (!e.FullPath(target, full)) return 0;
    if (!e.SubstCreate(drive + 1, full)) { e.Error(161); return 0; }
    return ~0u;
}
}
