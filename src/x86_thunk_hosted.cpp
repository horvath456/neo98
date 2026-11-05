// SPDX-License-Identifier: GPL-2.0-or-later
// Flat thunks between a 32-bit process and hosted guest NE modules. MSVFW32
// and MSVIDEO use LS01 for calls into 16-bit codecs and SL01 for calls into
// 32-bit codecs. Both modules execute from the user's installation; this
// provider implements the transition services.
// ThunkConnect32 links target tables and block pointers. ThunkConnect16
// validates signatures and accepts twelve argument words. C16ThkSL01 patches
// the stub to the transition, preserving segment/index registers, flags and
// lock state; the target returns its result in EAX/EDX and argument-byte
// count in CL. KERNEL.471 supplies the session process identifier used for
// per-process records.
#include "win16_machine.h"
#include "win98_flat_thunk.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned SignatureLs = 0x3130534c, SignatureSl = 0x31304c53; // "LS01", "SL01"
}

// The 16-bit thunk data block `name` of the hosted module `dll16`, loading
// the module (and running its initialization) when the process has not yet.
unsigned Application::Win16HostedThunkData(const char *dll16_text, const char *name_text, unsigned &module)
{
    module = 0;
    // Both names may sit in the rotating slots of ReadString; loading the
    // module and running its initialization reads many strings and would
    // overwrite them (W95INF16's block was looked up as "FONTS").
    char dll16[260], name[128];
    snprintf(dll16, sizeof dll16, "%s", dll16_text);
    snprintf(name, sizeof name, "%s", name_text);
    const char *leaf = dll16;
    for (const char *p = dll16; *p; ++p)
        if (*p == '\\' || *p == '/' || *p == ':')
            leaf = p + 1;
    unsigned error = 0;
    const char *roots[] = {"C:\\WINDOWS\\SYSTEM", "C:\\WINDOWS"};
    for (const char *root : roots)
    {
        char path[260];
        snprintf(path, sizeof path, "%s\\%s", root, leaf);
        module = Win16HostedLibrary(path, error);
        if (module || (error != 2 && error != 3))
            break;
    }
    if (!module)
    {
        if (log)
        {
            char text[200];
            snprintf(text, sizeof text, "FLATTHUNK hosted module %s not loaded, error %u", leaf, error);
            log(text);
        }
        return 0;
    }
    for (unsigned i = 0; i < win16.state->module_count; ++i)
    {
        Win16Module &found = win16.state->modules[i];
        if (found.handle != module)
            continue;
        const unsigned number = Win16Ordinal(found, name);
        Win98Ne::Entry entry;
        if (!number || !found.image.EntryAt(number, entry) || !entry.segment || entry.segment > found.segments ||
            !found.selector[entry.segment])
        {
            if (log)
            {
                char text[200];
                snprintf(text, sizeof text, "FLATTHUNK hosted block %s!%s not found (ordinal %u)", leaf, name, number);
                log(text);
            }
            return 0;
        }
        return Win16Linear(found.selector[entry.segment], entry.offset);
    }
    return 0;
}

// for a block whose 16-bit side is hosted. `name` is the address of
// the thunk data name the 32-bit caller passed (ThunkConnect32 argument two).
bool Application::FlatThunkHostedLink(ThunkConnection &connection, unsigned block16, unsigned name)
{
    const unsigned block = connection.block;
    if (connection.signature == SignatureLs)
    {
        const unsigned far = Read32(block16 + 8);
        const unsigned table = far >> 16 ? Win16Linear(far >> 16, far & 0xffff) : 0;
        if (!table)
            return false;
        Write32(block + 8, table);
        connection.tokens = table;
        for (unsigned shape = 0; shape < 2; ++shape)
            if (!FlatThunkArm(connection, shape))
                return false;
        return true;
    }
    if (connection.signature == SignatureSl)
    {
        Write32(block + 0xc, block16);
        Write32(block16 + 0x10, name + Read32(block + 0x20));
        return true;
    }
    return false;
}

// A transition through a hosted block: the token the template loaded from
// the 16-bit target table is the far address of the routine. QT_Thunk copies
// the argument words the stub pushed onto the 16-bit stack, and the routine
// removes them with its far return.
bool Application::FlatThunkHostedCall(unsigned token, unsigned arguments, unsigned bytes, unsigned &result,
                                      unsigned &words)
{
    if (!win16.state || !(token >> 16))
        return false;
    Win16Module *owner = nullptr;
    for (unsigned i = 0; i < win16.state->module_count && !owner; ++i)
    {
        Win16Module &module = win16.state->modules[i];
        for (unsigned s = 1; s <= module.segments; ++s)
            if (module.selector[s] == (token >> 16))
            {
                owner = &module;
                break;
            }
    }
    // A gate of a module of the system (KERNEL32 ordinal 37 on "KERNEL",
    // "USER", "GDI", src/x86_modules.cpp): the far call ends in the native
    // routine, which takes its words off the 16-bit stack like the original.
    // The same holds for a selector the program made itself: MSISYS.OCX
    // allocates one over 26 bytes of 16-bit code of its own (1000B0F0, the
    // DPMI call "simulate real mode interrupt") and calls it the same way.
    bool gate = false;
    for (unsigned selector : win16.state->gate_selector)
        gate = gate || (selector && selector == (token >> 16));
    if (!owner && !gate && (token >> 16 & 4) && selectors.At(token >> 16))
        gate = true;
    if (!owner && !gate)
        return false;
    words = bytes / 2;
    if (words > 64)
    {
        Fail("Flat thunk into a hosted 16-bit module has more than 64 argument words");
        return false;
    }
    unsigned push[64];
    for (unsigned i = 0; i < words; ++i)
    {
        unsigned short value = 0;
        Read(arguments + bytes - 2 - i * 2, &value, 2);
        push[i] = value;
    }
    ++thunk_transitions;
    bool returned = false;
    // A stub of the thunk compiler has exactly the arguments between its
    // stack pointer and its frame. A hand-written one (MSISYS.OCX 1000B10A)
    // keeps saved registers there too: the 16-bit routine runs on the same
    // stack under Win98, and what it removes is what is gone afterwards.
    unsigned removed = words;
    result = Win16HostedCall(token, push, words, owner ? owner->instance : 0, nullptr, &returned, &removed);
    if (returned && gate)
        words = removed;
    if (log)
    {
        char text[200];
        snprintf(text, sizeof text, "FLATTHUNK hosted %s %04x:%04x words=%u result=%08x", owner ? owner->name : "gate", token >> 16,
                 token & 0xffff, words, result);
        log(text);
    }
    return returned && state != Failed;
}

// KERNEL.651, ThunkConnect16.
bool Application::Win16ThunkConnect16(const Win16Frame &frame, unsigned &result)
{
    const unsigned reason = frame.Long(5), block16 = frame.Pointer(3);
    result = 0;
    if (reason != 0 && reason != 1)
    {
        result = 1;
        return true;
    }
    const unsigned signature = block16 ? Read32(block16) : 0;
    if (signature != SignatureLs && signature != SignatureSl)
        return true;
    if (reason == 1 && signature == SignatureSl && !Read32(block16 + 0x10))
    {
        // The 32-bit side links the blocks when its DLL connects. It is either
        // the DLL that is loading this module right now or one the process
        // does not have yet; KRNL386 loads it then, and its DllMain
        // makes the connection.
        const char *dll32 = ReadString(frame.Pointer(8));
        char name[260];
        snprintf(name, sizeof name, "%s", dll32 ? dll32 : "");
        const unsigned text = Allocate(260);
        if (!text)
            return true;
        PutString(text, name);
        const unsigned loaded[] = {text};
        // In the process of a Win16 program the DllMain of the 32-bit side
        // runs on the flat stack of the thread, the way the thunk layer of
        // Win98 switches to the Win32 stack. A hosted module is
        // already called from flat code.
        CpuContext *saved = nullptr;
        unsigned top = 0;
        if (!Win16FlatEnter(saved, top))
        {
            Free(text);
            return false;
        }
        const bool present = Api("GetModuleHandleA", loaded) || Api("LoadLibraryA", loaded);
        Win16FlatLeave(saved, top);
        Free(text);
        if (state == Failed)
            return false;
        if (!present)
            return true;
    }
    result = 1;
    return true;
}

// KERNEL.631, C16ThkSL01.
bool Application::Win16SlThunk(const Win16Frame &frame, unsigned &result, unsigned &words)
{
    const unsigned edx = Register(Reg::Edx), index = Register(Reg::Ecx) & 0xffff;
    const unsigned block16 = edx >> 16 ? Win16Linear(edx >> 16, edx & 0xffff) : 0;
    const unsigned table = block16 && Read32(block16) == SignatureSl ? Read32(block16 + 0x10) : 0;
    if (!table)
    {
        Fail("C16ThkSL01: the 16-bit block has no connected 32-bit side");
        return false;
    }
    const unsigned target = Read32(table + index);
    if (!target)
    {
        Fail("C16ThkSL01: the 32-bit target table has no entry at this index");
        return false;
    }
    // The transition saves five dwords below the far return address and
    // points EBX two bytes above them: EBX+16h is the word above the return
    // address, the last argument.
    CallRegisters registers;
    registers.ebx = frame.stack - 0x12;
    registers.frame = true;
    ++thunk_transitions;
    // The target runs on the flat stack of the thread in the process of a
    // Win16 program (Win16FlatEnter); EBX keeps the linear address of the
    // 16-bit arguments.
    CpuContext *saved = nullptr;
    unsigned top = 0;
    if (!Win16FlatEnter(saved, top))
        return false;
    const unsigned eax = Call(target, nullptr, 0, &registers);
    Win16FlatLeave(saved, top);
    if (state == Failed || state == Exited)
        return false;
    words = (registers.ecx & 0xff) / 2;
    result = (eax & 0xffff) | (registers.edx & 0xffff) << 16;
    if (log)
    {
        char text[200];
        snprintf(text, sizeof text, "FLATTHUNK sl index=%u target=%08x bytes=%u result=%08x", index / 4, target,
                 registers.ecx & 0xff, result);
        log(text);
    }
    return true;
}
} // namespace X86
