// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_environment_public.h"
#include <stdlib.h>
#include <string.h>
namespace X86
{
// Each invocation owns its scratch storage. A blocked native mutex acquisition
// cannot expose one fiber's variable name or host spans to another invocation.
struct Application::ProcessEnvironment
{
    using U = unsigned;
    static constexpr U Local = 0xf3000000, Temporary = 0xf4000000,
                       Input = 0xf5000000, Output = 0xf6000000, SpanLimit = 0x01000000;
    Application &app;
    unsigned char local[100]{};
    unsigned char *temporary = nullptr;
    U temporary_size = 0;
    const unsigned char *input = nullptr;
    unsigned char *output = nullptr;
    U input_size = 0, output_size = 0;
    explicit ProcessEnvironment(Application &a) : app(a) {}
    ~ProcessEnvironment() { free(temporary); }
    static bool Private(U p) { return p >= Local && p < Output + SpanLimit; }
    unsigned char *Native(U p, bool write)
    {
        if (p >= Local && p - Local < sizeof local) return local + (p - Local);
        if (p >= Temporary && p - Temporary < temporary_size) return temporary + (p - Temporary);
        if (!write && p >= Input && p - Input < input_size)
            return const_cast<unsigned char *>(input + (p - Input));
        if (p >= Output && p - Output < output_size) return output + (p - Output);
        app.Fail("Process environment native span exceeded its allocation"); return nullptr;
    }
    U Read8(U p)
    {
        if (Private(p)) { const auto *b = Native(p, false); return b ? *b : 0; }
        unsigned char b = 0; app.Read(p, &b, 1); return b;
    }
    void Write8(U p, U v)
    {
        if (Private(p)) { if (auto *b = Native(p, true)) *b = v; }
        else { const unsigned char b = v; app.Store(p, &b, 1); }
    }
    U Read32(U p) { U v = 0; for (U i = 0; i < 4; ++i) v |= Read8(p + i) << (i * 8); return v; }
    void Write32(U p, U v) { for (U i = 0; i < 4; ++i) Write8(p + i, v >> (i * 8)); }
    bool ProbeRead(U p, U &value)
    {
        unsigned char b;
        if (!app.virtual_memory.Access(p, 1, false) || app.cpu->Read(p, &b, 1) != CpuError::Ok) return false;
        value = b; return true;
    }
    bool ProbeWrite(U p)
    {
        U value;
        if (!ProbeRead(p, value) || !app.virtual_memory.Access(p, 1, true)) return false;
        const unsigned char b = value; return app.cpu->Write(p, &b, 1) == CpuError::Ok;
    }
    void Lock() { app.CrtLock(true); }
    void Unlock() { app.CrtLock(false); }
    void Error(U value) { app.last_error = value; }
    // Native PDB identity. It is private to this process's owned address space.
    U CurrentProcess() { return 1; }
    U ProcessBlock() { return app.process_environment; }
    U VirtualAllocate(U p, U bytes, U flags, U protection)
    {
        const U args[] = {p, bytes, flags, protection}; return app.MemoryApi("VirtualAlloc", args);
    }
    U VirtualRelease(U p, U bytes, U flags)
    {
        const U args[] = {p, bytes, flags}; return app.MemoryApi("VirtualFree", args);
    }
    U LocalName() { return Local; }
    U AllocateTemporary(U bytes)
    {
        if (temporary || bytes > SpanLimit) return 0;
        temporary = static_cast<unsigned char *>(malloc(bytes));
        if (!temporary) return 0;
        temporary_size = bytes; return Temporary;
    }
    void FreeTemporary(U p)
    {
        if (p != Temporary || !temporary) { app.Fail("Invalid process environment temporary free"); return; }
        free(temporary); temporary = nullptr; temporary_size = 0;
    }
    bool Source(const char *text, U bytes)
    {
        if (!text || bytes > SpanLimit) { Error(87); return false; }
        input = reinterpret_cast<const unsigned char *>(text); input_size = bytes; return true;
    }
};
bool Application::InitializeProcessEnvironment()
{
    if (process_environment) return state != Failed;
    // This is the ARM process profile, not an implementation of CONFIG.SYS,
    // AUTOEXEC.BAT or process inheritance. Both Mac and Pi use these same bytes.
    // TEMP and TMP are the values IO.SYS of Win98 sets when AUTOEXEC.BAT does
    // not name them: the TEMP folder Setup creates below the Windows folder

    // MSVCRT snapshots COMSPEC into its own environment at process startup.
    // Its system(NULL) checks that interpreter, independently of later
    // SetEnvironmentVariable calls. Keep the standard Win98 command processor
    // in the default profile; an explicitly inherited block stays untouched.
    static const char initial[] =
        "PATH=C:\\WINDOWS;C:\\WINDOWS\\SYSTEM\0TEMP=C:\\WINDOWS\\TEMP\0TMP=C:\\WINDOWS\\TEMP\0windir=C:\\WINDOWS\0COMSPEC=C:\\WINDOWS\\COMMAND.COM\0";
    ProcessEnvironment e(*this);
    e.Source(initial_environment ? initial_environment : initial,
             initial_environment ? initial_environment_size : sizeof initial);
    process_environment = Win98Environment::CreateBlock(e, ProcessEnvironment::Input, 0);
    return process_environment && state != Failed;
}
void Application::FreeProcessEnvironment()
{
    if (!process_environment) return;
    const unsigned args[] = {process_environment - 32, 0, 0x8000};
    MemoryApi("VirtualFree", args); process_environment = 0;
}
unsigned Application::EnvironmentApi(ApiName name, const unsigned *a)
{
    using namespace Win98Environment;
    if (!InitializeProcessEnvironment()) { last_error = 8; return 0; }
    ProcessEnvironment e(*this);
    unsigned result = 0;
    if (API_IS(name,"GetEnvironmentStrings") || API_IS(name,"GetEnvironmentStringsA")) result = GetStrings(e);
    else if (API_IS(name,"FreeEnvironmentStringsA")) result = FreeStrings(e, a[0]);
    else if (API_IS(name,"GetEnvironmentVariableA")) result = GetPublic(e, a[0], a[1], a[2]);
    else if (API_IS(name,"SetEnvironmentVariableA")) result = SetPublic(e, a[0], a[1]);
    else if (API_IS(name,"ExpandEnvironmentStringsA")) result = ExpandPublic(e, a[0], a[1], a[2]);
    else { Fail("Unknown process environment API"); return 0; }
    return state == Failed ? 0 : result;
}
unsigned Application::ExpandProcessEnvironment(const char *source, char *output, unsigned capacity)
{
    if (!InitializeProcessEnvironment()) { last_error = 8; return 0; }
    ProcessEnvironment e(*this);
    const auto length = source ? strlen(source) : 0;
    if (length >= ProcessEnvironment::SpanLimit || capacity > ProcessEnvironment::SpanLimit ||
        (!output && capacity) || !e.Source(source, static_cast<unsigned>(length + 1)))
    { last_error = 87; return 0; }
    e.output = reinterpret_cast<unsigned char *>(output); e.output_size = capacity;
    const unsigned result = Win98Environment::Expand(e, ProcessEnvironment::Input, ProcessEnvironment::Output, capacity);
    return state == Failed ? 0 : result;
}
unsigned Application::FindProcessEnvironment(const char *name)
{
    if (!InitializeProcessEnvironment()) return 0;
    ProcessEnvironment e(*this);
    const auto length = name ? strlen(name) : 0;
    if (length >= ProcessEnvironment::SpanLimit || !e.Source(name, static_cast<unsigned>(length + 1))) return 0;
    unsigned token = ProcessEnvironment::Input, value = 0;
    e.Lock(); Win98Environment::Find(e, token, value, process_environment); e.Unlock();
    return state == Failed ? 0 : value;
}
} // namespace X86
