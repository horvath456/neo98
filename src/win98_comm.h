// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 serial-port APIs. The environment supplies VCOMM operations, buffers,
// events, and error reporting.
#pragma once
namespace Win98Comm
{
enum Export
{
    SetupComm, ClearCommBreak, EscapeCommFunction, PurgeComm, SetCommBreak, SetCommMask, TransmitCommChar,
    ClearCommError, GetCommMask, GetCommProperties, GetCommModemStatus, GetCommState, GetCommTimeouts,
    SetCommState, SetCommTimeouts, WaitCommEvent, GetCommConfig, SetCommConfig,
    GetDefaultCommConfigA, SetDefaultCommConfigA, CommConfigDialogA, Count
};
// VCOMM service numbers pushed before VxDCall.
constexpr unsigned ServiceSetupComm = 0x2b0002, ServiceEscape = 0x2b0003, ServiceGetMask = 0x2b0004,
                   ServiceGetProperties = 0x2b0005, ServiceGetState = 0x2b0006, ServiceGetTimeouts = 0x2b0007,
                   ServicePurge = 0x2b0008, ServiceSetMask = 0x2b0009, ServiceSetState = 0x2b000a,
                   ServiceSetTimeouts = 0x2b000b, ServiceTransmitChar = 0x2b000c, ServiceWaitEvent = 0x2b000d,
                   ServiceModemStatus = 0x2b000e, ServiceClearError = 0x2b0011, ServiceLastError = 0x2b0013,
                   ServiceGetConfig = 0x2b0016, ServiceSetConfig = 0x2b0017,
                   ServiceResetEvent = 0x2a0062, ServiceSetEvent = 0x2a0060;
// One SEH probe of the original stub: first and last byte of a caller buffer.
struct Probe { unsigned char argument, size; bool write, optional; };
struct Entry { unsigned char argc, probes; Probe probe[2]; unsigned service; };
inline const Entry &Table(unsigned id)
{
    static const Entry table[] = {
        {3, 0, {}, ServiceSetupComm},                                           // SetupComm
        {1, 0, {}, ServiceEscape},                                              // ClearCommBreak
        {2, 0, {}, ServiceEscape},                                              // EscapeCommFunction
        {2, 0, {}, ServicePurge},                                               // PurgeComm
        {1, 0, {}, ServiceEscape},                                              // SetCommBreak
        {2, 0, {}, ServiceSetMask},                                             // SetCommMask
        {2, 0, {}, ServiceTransmitChar},                                        // TransmitCommChar
        {3, 2, {{1, 4, true, false}, {2, 12, true, true}}, ServiceClearError},  // ClearCommError
        {2, 1, {{1, 4, true, false}}, ServiceGetMask},                          // GetCommMask
        {2, 1, {{1, 64, true, false}}, ServiceGetProperties},                   // GetCommProperties
        {2, 1, {{1, 4, true, false}}, ServiceModemStatus},                      // GetCommModemStatus
        {2, 1, {{1, 28, true, false}}, ServiceGetState},                        // GetCommState
        {2, 1, {{1, 20, true, false}}, ServiceGetTimeouts},                     // GetCommTimeouts
        {2, 1, {{1, 28, false, false}}, ServiceSetState},                       // SetCommState
        {2, 1, {{1, 20, false, false}}, ServiceSetTimeouts},                    // SetCommTimeouts
        {3, 2, {{1, 4, true, false}, {2, 20, false, true}}, ServiceWaitEvent},  // WaitCommEvent
        {3, 0, {}, ServiceGetConfig},                                           // GetCommConfig
        {3, 0, {}, ServiceSetConfig},                                           // SetCommConfig
        {3, 0, {}, 0}, {3, 0, {}, 0}, {3, 0, {}, 0}};
    return table[id];
}
// The stub raises the probe exception before any handle work; turns
// it into SetLastError(87) and a zero return.
template <class E> bool Probes(E &e, unsigned id, const unsigned *a)
{
    const Entry &t = Table(id);
    for (unsigned i = 0; i < t.probes; ++i)
    {
        const Probe &p = t.probe[i];
        if (p.optional && !a[p.argument]) continue;
        if (!e.Access(a[p.argument], p.size, p.write)) return false;
    }
    return true;
}
// Common body of the sixteen thin exports. E supplies:
// unsigned Object(unsigned handle), 0 when not a VCOMM handle
// bool Service(unsigned service, const unsigned *args, unsigned count) VxDCall, false on failure
// void SetError(unsigned)
// unsigned Read32(unsigned) / void Write32(unsigned, unsigned)
template <class E> unsigned Thin(E &e, unsigned id, const unsigned *a)
{
    if (!Probes(e, id, a)) { e.SetError(87); return 0; }
    const Entry &t = Table(id);
    const unsigned object = e.Object(a[0]);
    if (object)
    {
        unsigned args[4] = {object, 0, 0, 0};
        unsigned count = 1;
        switch (id)
        {
        case SetupComm: args[1] = a[1]; args[2] = a[2]; count = 3; break;
        case ClearCommBreak: args[1] = 13; count = 2; break;
        case SetCommBreak: args[1] = 12; count = 2; break;
        case EscapeCommFunction:
        {
            unsigned function = a[1];
            if (function == 8) function = 12;
            if (function == 9) function = 13;
            if (function == 14) return object;
            args[1] = function; count = 2; break;
        }
        case TransmitCommChar: args[1] = unsigned(int(static_cast<signed char>(a[1] & 0xff))); count = 2; break;
        case ClearCommError: args[1] = a[2]; args[2] = a[1]; count = 3; break; // VCOMM takes (lpStat, lpErrors)
        case SetCommState: e.Write32(a[1] + 8, e.Read32(a[1] + 8) | 1); args[1] = a[1]; count = 2; break;
        default: args[1] = a[1]; count = 2; break;
        }
        if (e.Service(t.service, args, count)) return 1;
        // and its siblings: the VCOMM error is fetched and dropped.
        e.Service(ServiceLastError, args, 1);
    }
    e.SetError(6);
    return 0;
}
// / : no probes; a VCOMM failure reports error 1.
template <class E> unsigned Config(E &e, unsigned id, const unsigned *a)
{
    const unsigned object = e.Object(a[0]);
    if (!object) { e.SetError(6); return 0; }
    const unsigned args[3] = {object, a[1], a[2]};
    if (e.Service(Table(id).service, args, 3)) return 1;
    e.SetError(1);
    return 0;
}
// Additional E members:
// bool Lookup(unsigned handle, unsigned &object, bool &overlapped, unsigned &event), type 80002000h
// unsigned EventObject(unsigned handle), type 80000002h; 0 when invalid
// void Wait(unsigned event), infinite
template <class E> unsigned Wait(E &e, const unsigned *a)
{
    if (!Probes(e, WaitCommEvent, a)) { e.SetError(87); return 0; }
    e.Write32(a[1], 0);
    unsigned object = 0, event = 0;
    bool overlapped = false;
    if (!e.Lookup(a[0], object, overlapped, event)) { e.SetError(6); return 0; }
    bool own_event = true;
    if (overlapped && a[2])
    {
        const unsigned handle = e.EventObject(e.Read32(a[2] + 16));
        if (handle) { event = handle; own_event = false; }
    }
    const unsigned reset[1] = {event};
    e.Service(ServiceResetEvent, reset, 1);
    const unsigned args[4] = {object, a[1], a[2], event};
    if (!e.Service(ServiceWaitEvent, args, 4)) { e.SetError(1); return 0; }
    if (e.Read32(a[1])) { e.Service(ServiceSetEvent, reset, 1); return 1; }
    if (own_event) { e.Wait(event); return 1; }
    e.Write32(a[2], 0);
    e.Write32(a[2] + 4, a[1]);
    e.SetError(997);
    return 0;
}
// case-insensitive ASCII compare used by the provider walk.
inline unsigned Fold(unsigned c) { return c >= 'a' && c <= 'z' ? c & 0xdf : c; }
inline bool Same(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        const unsigned x = Fold((unsigned char)*a), y = Fold((unsigned char)*b);
        if (x != y) return false;
        if (!x) return true;
    }
}
inline unsigned Length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
inline void Copy(char *out, const char *in, unsigned capacity)
{
    unsigned i = 0;
    for (; in[i] && i + 1 < capacity; ++i) out[i] = in[i];
    out[i] = 0;
}
// walk HKLM\Enum three levels deep, match FriendlyName (mode 1) or
// PortName (mode 2), then resolve the instance's Driver value.
// R supplies:
// bool OpenKey(unsigned parent, const char *path, unsigned &key) VxD 10011h, parent 80000002h = HKLM
// bool EnumKey(unsigned key, unsigned index, char *name, unsigned capacity) VxD 10018h
// bool QueryString(unsigned key, const char *name, char *out, unsigned capacity) VxD 1001Ah
// void CloseKey(unsigned key) VxD 10013h
constexpr unsigned NameCapacity = 100;
// The walk itself; resolve(driver, mode) stands for and reports
// whether the instance is the answer. A refusal continues with the next one.
template <class R, class F> bool Walk(R &r, const char *name, F &resolve)
{
    unsigned enumerate = 0;
    if (!r.OpenKey(0x80000002u, "Enum", enumerate)) return false;
    bool found = false;
    char bus[NameCapacity], device[NameCapacity], instance[NameCapacity], value[NameCapacity];
    for (unsigned i = 0; !found && r.EnumKey(enumerate, i, bus, NameCapacity); ++i)
    {
        unsigned bus_key = 0;
        if (!r.OpenKey(enumerate, bus, bus_key)) continue; // next sibling
        for (unsigned j = 0; !found && r.EnumKey(bus_key, j, device, NameCapacity); ++j)
        {
            unsigned device_key = 0;
            if (!r.OpenKey(bus_key, device, device_key)) continue; //
            for (unsigned k = 0; !found && r.EnumKey(device_key, k, instance, NameCapacity); ++k)
            {
                unsigned key = 0;
                if (!r.OpenKey(device_key, instance, key)) continue; //
                unsigned mode = 0;
                if (r.QueryString(key, "FriendlyName", value, NameCapacity) && Same(value, name)) mode = 1;
                else if (r.QueryString(key, "PortName", value, NameCapacity) && Same(value, name)) mode = 2;
                if (mode && r.QueryString(key, "Driver", value, NameCapacity)) found = resolve(value, mode);
                r.CloseKey(key);
            }
            r.CloseKey(device_key);
        }
        r.CloseKey(bus_key);
    }
    r.CloseKey(enumerate);
    return found;
}
// Services\Class\<Driver> -> PortConfigDialog (mode 2, falling
// back to ConfigDialog) or ConfigDialog (mode 1).
template <class R> struct DialogResolver
{
    R &r; char *out; unsigned capacity;
    bool operator()(const char *driver, unsigned mode) const
    {
        char path[NameCapacity + 40] = "System\\CurrentControlSet\\Services\\Class\\";
        Copy(path + 40, driver, NameCapacity);
        unsigned key = 0;
        if (!r.OpenKey(0x80000002u, path, key)) return false;
        bool found = r.QueryString(key, mode == 2 ? "PortConfigDialog" : "ConfigDialog", out, capacity);
        if (!found && mode == 2) found = r.QueryString(key, "ConfigDialog", out, capacity);
        r.CloseKey(key);
        return found;
    }
};
// R additionally supplies SetError(unsigned).
template <class R> bool Provider(R &r, const char *name, char *out, unsigned capacity)
{
    DialogResolver<R> resolve{r, out, capacity};
    const bool found = Walk(r, name, resolve);
    if (!found) r.SetError(87); // the caller sets 87 a second time
    return found;
}
// / /. D supplies the registry members above plus:
// unsigned ModuleType(const char *path); 8 selects the Win32 path
// unsigned LoadLibrary(const char *path); 0 on failure
// unsigned ProcAddress(unsigned module, const char *entry)
// unsigned CallDriver(unsigned procedure, unsigned x, unsigned y, unsigned z) error code, 0 = success
// unsigned Thunk16(unsigned mode, const char *path, const char *entry, unsigned x, unsigned y, unsigned z, unsigned w)
// void FreeLibrary(unsigned module)
// void SetError(unsigned)
inline const char *EntryName(unsigned id)
{
    return id == GetDefaultCommConfigA ? "drvGetDefaultCommConfig"
         : id == SetDefaultCommConfigA ? "drvSetDefaultCommConfig" : "drvCommConfigDialog";
}
template <class D> unsigned Dispatch(D &d, unsigned id, const char *name, const unsigned *a)
{
    char path[256];
    unsigned error = 87, module = 0, result = 1;
    if (!Provider(d, name, path, sizeof path)) { d.SetError(87); return 0; }
    const char *entry = EntryName(id);
    bool direct = false;
    if (d.ModuleType(path) == 8)
    {
        module = d.LoadLibrary(path);
        if (module)
        {
            direct = true;
            const unsigned procedure = d.ProcAddress(module, entry);
            // (name, lpCC, lpdwSize), (name, lpCC, dwSize) and (name, hwnd, lpCC) all push a[0..2].
            error = procedure ? d.CallDriver(procedure, a[0], a[1], a[2]) : 87;
        }
    }
    if (!direct)
    {
        if (id == GetDefaultCommConfigA) error = d.Thunk16(1, path, entry, a[0], a[1], a[2], 0);
        else if (id == SetDefaultCommConfigA) error = d.Thunk16(2, path, entry, a[0], a[1], 0, a[2]);
        else error = d.Thunk16(0, path, entry, a[0], a[2], 0, a[1]);
    }
    if (error) { d.SetError(error); result = 0; }
    if (module) d.FreeLibrary(module);
    return result;
}
} // namespace Win98Comm
