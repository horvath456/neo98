// SPDX-License-Identifier: GPL-2.0-or-later
// TSP3216S's broker, implemented, 6A16, 6C60 and 6CC4.
// The installed NE providers run through the existing hosted-library path.
// In particular, a machine without modems still has providers whose
// initialization succeeds; an empty device list is not an empty provider list.
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
namespace X86
{
namespace
{
// Keep the one guest callback visible to the saved-state frame audit on
// both hosts. Enumeration, initialization and shutdown must finish first.
__attribute__((noinline)) unsigned CallTspProvider(Application &app, unsigned handle, unsigned entry,
                                                 const unsigned *args, unsigned count)
{
    const unsigned target = app.Win16HostedProcedure(handle, entry);
    if (!target) return 0x80000049u; // missing TSPI export, 's fallback
    unsigned push[16];
    for (unsigned i = 0; i < count; ++i) { push[2*i] = args[i] >> 16; push[2*i+1] = args[i] & 65535; }
    bool returned = false;
    unsigned answer = app.Win16HostedCall(target, push, count*2, app.Win16HostedInstance(handle), nullptr, &returned);
    return returned ? answer : 0x80000048u;
}
}
unsigned Application::Tsp3216Words(unsigned ordinal)
{
    switch (ordinal)
    {
    case 108: case 31: return 8;
    case 41: return 2;
    case 113: return 14;
    case 30: return 13;
    // Private callback gates.
    case 114: return 12;
    case 115: return 10;
    case 116: return 4;
    default: return ~0u;
    }
}

unsigned Application::Tsp3216(unsigned ordinal, unsigned stack)
{
    auto &module = native_win16_modules[8];
    const unsigned memory = module.memory;
    constexpr unsigned Records = 0x2000, RecordSize = 16;
    auto flat = [&](unsigned at) { return selectors.Linear(Read32(stack + at)); };
    auto put = [&](unsigned address, unsigned value) {
        if (address && virtual_memory.Access(address, 4, true)) Write32(address, value);
    };
    auto callback = [&](unsigned number) {
        return Win16GatePointer(Win16Gate("NEO98TSP", nullptr, number));
    };
    switch (ordinal)
    {
    case 108: case 31:
        put(flat(0), 0x00010004);
        return 0;
    case 30:
    {
        // The broker stores its window and the flat callbacks before loading
        // each ProviderFilename entry. Failed LoadLibrary calls do not count.
        Write32(memory + 8, Win16Word(stack));
        Write32(memory + 12, Read32(stack + 6));
        Write32(memory + 16, Read32(stack + 2));
        Write32(memory + 20, Read32(stack + 10));
        put(flat(0x12), 0); put(flat(0x0e), 0);
        char value[260] = {};
        ProfileString("Providers", "NumProviders", "0", value, sizeof value, "TELEPHON.INI");
        const int configured = short(atoi(value));
        if (configured > int((65536 - Records) / RecordSize)) return 0x80000044u;
        // A new enumeration replaces the old list, including its references.
        const unsigned old_count = Read32(memory);
        Write32(memory, 0);
        for (unsigned i = 0; i < old_count; ++i)
            Win16FreeLibrary(Read32(memory + Records + RecordSize*i));
        unsigned lines = 0, phones = 0;
        for (int i = 0; i < configured && state != Failed && state != Exited; ++i)
        {
            char key[48];
            snprintf(key, sizeof key, "ProviderFilename%d", i);
            ProfileString("Providers", key, "", value, sizeof value, "TELEPHON.INI");
            const unsigned text = Allocate(260);
            if (!text) return 0x80000044u;
            PutString(text, value);
            const unsigned load[] = {text};
            const unsigned handle = Win16ModuleApi(35, load);
            Free(text);
            if (handle <= 32) continue;
            // This catalogue segment can predate the first hosted NE module.
            // Publish its data descriptor before an original provider uses it.
            Win16Descriptor(module.segment >> 16);
            const unsigned index = Read32(memory), offset = Records + index*RecordSize;
            Write32(memory + offset, handle);
            snprintf(key, sizeof key, "ProviderID%d", i);
            ProfileString("Providers", key, "0", value, sizeof value, "TELEPHON.INI");
            Write32(memory + offset + 4, unsigned(atoi(value)) & 65535);
            Write32(memory + offset + 8, 0); Write32(memory + offset + 12, 0);
            Write32(memory, index + 1);
            const unsigned args[] = {Read32(memory + offset + 4), module.segment + offset + 8,
                module.segment + offset + 12, index, callback(114), callback(115)};
            CallTspProvider(*this, handle, 595, args, 6);
            lines += Read32(memory + offset + 8); phones += Read32(memory + offset + 12);
        }
        put(flat(0x12), lines); put(flat(0x0e), phones);
        return 0;
    }
    case 113:
    {
        Write32(memory + 24, Read32(stack));
        bool success = false;
        unsigned line = Read32(stack + 16), phone = Read32(stack + 12);
        for (unsigned i = 0; i < Read32(memory) && state != Failed && state != Exited; ++i)
        {
            const unsigned record = memory + Records + i*RecordSize;
            const unsigned args[] = {Read32(stack + 24), Read32(record + 4), line, phone,
                Read32(record + 8), Read32(record + 12), callback(116)};
            const unsigned answer = CallTspProvider(*this, Read32(record), 591, args, 7);
            if (!answer) success = true;
            if (log) { char text[160]; snprintf(text, sizeof text,
                "TSP provider id=%u lines=%u phones=%u init=%08x", args[1], args[4], args[5], answer); log(text); }
            line += args[4]; phone += args[5];
        }
        // Windows 98 clears the failure flag for ANY successful initialization.
        return success ? 0 : 0x80000048;
    }
    case 41:
    {
        const unsigned count = Read32(memory), args[] = {Read32(stack)};
        for (unsigned i = 0; i < count && state != Failed && state != Exited; ++i)
            CallTspProvider(*this, Read32(memory + Records + i*RecordSize), 594, args, 1);
        Write32(memory, 0);
        for (unsigned i = 0; i < count; ++i)
            Win16FreeLibrary(Read32(memory + Records + i*RecordSize));
        return 0;
    }
    case 114: case 115: case 116:
    {
        const unsigned count = ordinal == 114 ? 6 : ordinal == 115 ? 5 : 2;
        const unsigned message = ordinal == 116 ? Read32(stack + 4) : Read32(stack + 12);
        if (ordinal == 116 && message == 0xfffffffdu) return 0; //
        if ((ordinal == 114 && (message == 6 || message == 19)) ||
            (ordinal == 115 && message == 20) || (ordinal == 116 && message >= 0xffffff00u))
        {
            Fail("TSP device creation/handle translation callback is not implemented");
            return 0;
        }
        // Normal notifications use COPYDATA: callback, size, payload pointer.
        const unsigned packet = Allocate(12 + (count+1)*4);
        if (!packet) return 0;
        Write32(packet, Read32(memory + (ordinal == 114 ? 12 : ordinal == 115 ? 16 : 24)));
        Write32(packet + 4, (count+1)*4); Write32(packet + 8, packet + 12);
        Write32(packet + 12, ordinal == 114 ? 1 : ordinal == 115 ? 2 : 0);
        for (unsigned i = 0; i < count; ++i) Write32(packet + 16 + i*4, Read32(stack + (count-1-i)*4));
        const unsigned send[] = {Read32(memory + 8), 0x4a, 0, packet};
        Api("SendMessageA", send);
        Free(packet);
        return 0;
    }
    default: return 0;
    }
}
} // namespace X86
