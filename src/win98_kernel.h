// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98
{
// KERNEL32 GetVersion, ordinal 476. Flags come.
unsigned Version(unsigned compatibility_flags);
// KERNEL32 SetHandleCount, ordinal 722.
unsigned HandleCount(unsigned requested);
// Common original unimplemented-export helper. Caller sets LastError=120.
unsigned UnsupportedResult(unsigned encoding);
// KERNEL32: guarded module version query under the process lock.
// ModuleVersion returns the legacy module record's WORD at +1A, or zero on failure.
template <class Environment> unsigned ProcessVersion(Environment &e, unsigned pid)
{
    e.Lock();
    unsigned result = 0;
    const unsigned process = pid ? e.FindProcess(pid) : e.CurrentProcess();
    if (process)
    {
        const unsigned packed = e.ModuleVersion(e.MainModule(process) << 16) & 65535;
        result = ((packed & 0xff00) << 8) | (packed & 255);
    }
    if (!result)
        e.Error(87);
    e.Unlock();
    return result;
}
// KERNEL32, GetVersionExA, including registry-backed suffix fields.
// The native seed replaces uninitialized stack bytes on failed/short service-pack queries.
template <class Environment> unsigned VersionEx(Environment &e, unsigned output)
{
    const unsigned size = e.Read(output);
    if (size != 148 && size != 156)
        return 0;
    e.Write(output + 4, 4);
    e.Write(output + 8, 10);
    e.Write(output + 12, 0x040a08ae);
    e.Write(output + 16, 1);
    unsigned service_pack = 0;
    const unsigned key = e.Open();
    bool suffix = false;
    if (key)
    {
        if (size == 156 && !e.ServicePack(key, service_pack))
            service_pack = 0;
        suffix = e.Suffix(key, output + 20, 128);
        e.Close(key);
    }
    if (!suffix)
        e.Zero(output + 20, 128);
    if (size == 156)
    {
        e.Word(output + 148, (service_pack >> 8) & 255);
        e.Word(output + 150, service_pack & 255);
        e.Word(output + 152, 0);
        e.Byte(output + 154, 0);
        e.Byte(output + 155, service_pack >> 16);
    }
    return 1;
}
// KERNEL32 713, including the WORD argument.
// The legacy task service runs before the captured process WORD is replaced.
template <typename Legacy> unsigned SetErrorMode(unsigned short &state, unsigned requested, Legacy legacy)
{
    const unsigned previous = state;
    legacy(requested & 65535);
    state = requested;
    return previous;
}
} // namespace Win98
