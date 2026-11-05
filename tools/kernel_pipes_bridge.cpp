// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for tools/test_kernel_pipes_original.py: runs the header-only
// KERNEL32 implementation (src/win98_kernel_pipes.h) against a scripted
// environment that answers exactly like the Unicorn hooks of the original.
// Guest memory is a 64 KB window at 0x120000 shared by value with the test.
#include "../src/win98_kernel_pipes.h"
#include <string.h>
namespace
{
typedef unsigned U;
struct Scripted
{
    const U *cfg; unsigned char *memory; U &error; U dos_calls = 0; U socket_context; U pipe_handle;
    static bool Inside(U p, U n) { return p >= 0x120000 && p + n <= 0x130000; }
    bool Writable(U p, U n) { return Inside(p, n); }
    void Write(U p, U v) { if (Inside(p, 4)) memcpy(memory + p - 0x120000, &v, 4); }
    U Read(U p) { U v = 0; if (Inside(p, 4)) memcpy(&v, memory + p - 0x120000, 4); return v; }
    void Byte(U p, U v) { if (Inside(p, 1)) memory[p - 0x120000] = (unsigned char)v; }
    void Error(U e) { error = e; }
    // cfg[0]: 0 no object, 1 object without DOS handle, 2 object with DOS handle 5; cfg[1]: type byte
    U SocketObject(U) { if (!cfg[0]) { error = 6; return 6; } return cfg[1] == 0x13 ? 0 : 66; }
    U SocketContext(U) { return socket_context; }
    void SetSocketContext(U, U v) { socket_context = v; }
    U PipeObject(U, U, U &dos) { if (!cfg[0]) { error = 6; return 6; } dos = cfg[0] == 1 ? ~0u : 5; return cfg[0] == 1 ? 12 : 0; }
    // DOS results in call order: cfg[3], cfg[4], cfg[5]; register/buffer fills from cfg[6], cfg[8..11].
    U NetPipe(Win98KernelPipes::PipeRequest &r)
    {
        const U result = cfg[3 + (dos_calls < 2 ? dos_calls : 2)];
        ++dos_calls;
        if (r.function == 0x5f36 || r.function == 0x5f37) r.bytes = cfg[6];
        if (r.function == 0x5f32 && !result) { r.out_buffer = cfg[8] & 0xffff; r.in_buffer = cfg[8] >> 16; r.max_instances = cfg[9] & 0xff; r.instances = (cfg[9] >> 8) & 0xff; }
        if (r.function == 0x5f33 && !result) r.state = cfg[10] & 0xffff;
        if (r.function == 0x5f3c && !result) { r.collect_timeout = cfg[11] & 0xffff; r.collect = cfg[11] >> 16; }
        return result;
    }
    U MailslotObject(U) { if (!cfg[0]) { error = 6; return 6; } return cfg[0] == 1 ? 12 : 0; }   // no DOS handle
    // cfg[14] bit 0: object allocated; cfg[3]: DOS result; cfg[6]: DOS handle; cfg[12]: table insert ok
    U MailslotCreate(U, U, U, bool, U &handle)
    {
        if (!(cfg[14] & 1)) return ~0u;
        if (cfg[3]) return cfg[3];
        if (!cfg[12]) return error;
        handle = 0x60000123;
        return 0;
    }
    U MailslotInfo(U, U &maximum, U &next, U &count, U &timeout)
    {
        if (cfg[3]) return cfg[3];
        maximum = cfg[6]; next = cfg[8]; count = cfg[9]; timeout = cfg[10];
        return 0;
    }
    U MailslotSetTimeout(U, U) { return cfg[3]; }
    U DeviceObject(U handle) { const bool ok = handle == 0x60000123 ? cfg[0] != 0 : cfg[12] != 0; if (!ok) error = 6; return ok ? 0 : 6; }
    bool OverlappedHandle(U) { return (cfg[11] & 0x1000) != 0; }
    U DeviceType(U) { return cfg[1]; }
    U VxdControl(U, const U *) { return cfg[13]; }
    U Drives() { return cfg[7]; }
    // cfg[8]: subst query text index (0 none); cfg[9]: remove ok; cfg[10]: create ok
    bool SubstQuery(U, char *out) { if (!cfg[8]) return false; strcpy(out, cfg[8] == 1 ? "C:" : cfg[8] == 2 ? "D:\\SUB" : "X"); return true; }
    bool SubstRemove(U) { return cfg[9] != 0; }
    bool SubstCreate(U, const char *) { return cfg[10] != 0; }
    // cfg[12]: full path result (0 fail with error cfg[13], 1 text)
    bool FullPath(const char *, char *out) { if (!cfg[12]) { error = cfg[13]; return false; } strcpy(out, "C:\\FULL\\PATH"); return true; }
};
}
extern "C" U kernel_pipes_run(U which, const U *cfg, const U *a, unsigned char *memory, U *error, const char *name, const char *target)
{
    Scripted e{cfg, memory, *error};
    e.socket_context = cfg[2];
    using namespace Win98KernelPipes;
    switch (which)
    {
    case 1: return GetQueuedCompletionStatus(e, a);
    case 2: return GetHandleContext(e, a);
    case 3: { const U r = SetHandleContext(e, a); memcpy(memory + 0xfff0, &e.socket_context, 4); return r; }
    case 4: return QueryDosDeviceA(e, name, a[1], a[2]);
    case 5: return DefineDosDeviceA(e, a[0], name, target);
    case 6: return CallNamedPipeA(e, a);
    case 7: return WaitNamedPipeA(e, a);
    case 8: return TransactNamedPipe(e, a);
    case 9: return GetNamedPipeHandleStateA(e, a);
    case 10: return SetNamedPipeHandleState(e, a);
    case 11: return GetNamedPipeInfo(e, a);
    case 12: return CreateMailslotA(e, a);
    case 13: return GetMailslotInfo(e, a);
    case 14: return SetMailslotInfo(e, a);
    case 15: return DeviceIoControl(e, a);
    }
    return 0xdeadbeef;
}
