// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 bridges preserve Win32 frames while marshaling arguments and
// callbacks across Win16 boundaries. Pointer masks select translated
// arguments. Restore exception registration and callee-saved registers on
// return; exception handling uses the recorded error-result policy. Callback
// pages track ownership, free entries, target pointers, and selector aliases.
// Validate alignment and ownership before querying or freeing a callback.
#pragma once
namespace Win98ThunkBridges
{
// FT_Prolog frame, offsets below EBP.
enum Frame : unsigned
{
    SavedEbx = 0x04,
    SavedEsi = 0x08,
    SavedEdi = 0x0c,
    SavedEcx = 0x10,
    PointerMask = 0x14,
    StackDelta = 0x20,
    ThunkReturn = 0x24,
    Handler = 0x28,
    PreviousHandler = 0x2c,
    SavedEax = 0x30,
    Target = 0x34,
    FrameSize = 0x40,
};
struct Registers
{
    unsigned eax = 0, ecx = 0, edx = 0, ebx = 0, esp = 0, ebp = 0, esi = 0, edi = 0;
};
// "memory" offers Write32(address, value) and Read32(address).
// "return_address" is the dword at ESP, "previous" the current fs:[0] and
// "handler" the address recorded for the frame. Returns the register state
// the prolog leaves behind: EBP at the saved-EBP slot, ESP 0x40 below it, ESI
// holding the return address (the original jumps through ESI and never
// restores it) and EAX holding the registration record it linked into fs:[0].
template <class Memory>
inline Registers Prolog(Memory &memory, const Registers &in, unsigned return_address, unsigned previous,
                        unsigned handler)
{
    Registers out = in;
    const unsigned ebp = in.esp;      // lea ebp, [esp+0x10] after four pushes
    memory.Write32(ebp, in.ebp);      // xchg [esp], ebp
    memory.Write32(ebp - SavedEbx, in.ebx);
    memory.Write32(ebp - SavedEsi, in.esi);
    memory.Write32(ebp - SavedEdi, in.edi);
    memory.Write32(ebp - SavedEcx, in.ecx);
    memory.Write32(ebp - PointerMask, 0);
    memory.Write32(ebp - SavedEax, in.eax);
    memory.Write32(ebp - Target, in.edx);
    memory.Write32(ebp - Handler, handler);
    memory.Write32(ebp - PreviousHandler, previous);
    out.ebp = ebp;
    out.esp = ebp - FrameSize;
    out.esi = return_address;
    out.eax = ebp - PreviousHandler; // "lea eax, [ebp-0x2c]" before the fs:[0] store
    return out;
}
// The fs:[0] record the prolog links in: it is the frame slot itself.
inline unsigned HandlerRecord(unsigned ebp)
{
    return ebp - PreviousHandler;
}
// FT_Exitn. Returns the registers after LEAVE; the caller pops
// the return address and "bytes" more.
template <class Memory>
inline Registers Exit(Memory &memory, const Registers &in)
{
    Registers out = in;
    out.eax = in.ebx;
    out.edi = memory.Read32(in.ebp - SavedEdi);
    out.esi = memory.Read32(in.ebp - SavedEsi);
    out.ebx = memory.Read32(in.ebp - SavedEbx);
    out.ebp = memory.Read32(in.ebp);
    out.esp = in.ebp + 4;
    return out;
}
// The digits of an FT_Exitn or Callbackn name; 0 when there are none.
inline unsigned NameNumber(const char *name)
{
    unsigned value = 0;
    for (const char *p = name; *p; ++p)
        if (*p >= '0' && *p <= '9')
            value = value * 10 + unsigned(*p - '0');
    return value;
}
inline bool ExitBytesValid(unsigned bytes)
{
    return bytes <= 56 && !(bytes & 3);
}
// the thunk's error result when its 16-bit call faults, chosen by
// bits 0-1 of the CH saved at [ebp-0x10]. Bits 2-7 of CH count argument
// dwords; the templates set CX to (CH << 8) | index.
inline unsigned FaultResult(unsigned saved_ecx, unsigned saved_eax)
{
    switch ((saved_ecx >> 8) & 3)
    {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return saved_eax;
    default:
        return 0xffffffffu;
    }
}
inline unsigned FaultArgumentDwords(unsigned saved_ecx)
{
    return ((saved_ecx >> 8) & 0xff) >> 2;
}
// 16-bit argument bytes below a QT frame, measured after the
// return address is popped. The generated stubs build 0x40-byte frames.
inline unsigned QuickArgumentBytes(unsigned ebp, unsigned esp)
{
    return ebp - esp - FrameSize;
}
// Bit n of the FT pointer mask names the argument dword 2n bytes above the
// argument base.
inline unsigned MaskedArgument(unsigned base, unsigned bit)
{
    return base + 2 * bit;
}
// Both thunk bodies return AX in EAX and DX in EDX; the stub widens.
inline unsigned ResultLow(unsigned value)
{
    return value & 0xffffu;
}
inline unsigned ResultHigh(unsigned value)
{
    return value >> 16;
}

// Callbackn: "sp" is ESP at entry with the return address on
// top; the procedure is the argument at sp + n and its slot receives the
// return address. The stub then jumps to the procedure with a return to a
// plain RET on top of the stack. Returns the procedure.
template <class Memory>
inline unsigned Callback(Memory &memory, unsigned sp, unsigned bytes, unsigned ret_stub)
{
    const unsigned return_address = memory.Read32(sp);
    const unsigned procedure = memory.Read32(sp + bytes);
    memory.Write32(sp + bytes, return_address);
    memory.Write32(sp, ret_stub);
    return procedure;
}
inline bool CallbackBytesValid(unsigned bytes)
{
    return bytes >= 4 && bytes <= 64 && !(bytes & 3);
}

// callback pages.
enum Page : unsigned
{
    PageNext = 0,
    PageFreeHead = 4,
    PageSelector = 8,
    PageEntries = 0xc,
    EntrySize = 0x20,
    EntryCount = 100,
    PageSize = PageEntries + EntryCount * EntrySize, // 0xc8c
    EntryNextFree = 4,
    EntryOwner = 0x18,
    EntryPage = 0x1c,
    FreeMarker = 0xcc,
    LsMarker = 0x5a, // pop edx
    SlMarker = 0x66, // operand-size prefix of pop dx
    LsUserData = 2,
    LsTargetQuirk = 4, // GetLSCallbackTarget reads here
    LsRelative = 8,
    LsBias = 12, // the jump is relative to the end of the 12-byte stub
    SlUserData = 4,
    SlTarget = 0xb,
    LsLength = 12,
    SlLength = 15,
};
inline unsigned EntryAddress(unsigned page, unsigned index)
{
    return page + PageEntries + index * EntrySize;
}
// a flat pointer names an entry of "page" when its distance from
// the first entry is a multiple of 0x20 and the quotient is not above 100.
// The inclusive upper bound admits the slot just past the page.
inline bool EntryOf(unsigned page, unsigned pointer)
{
    const unsigned distance = pointer - page - PageEntries;
    return !(distance & (EntrySize - 1)) && (distance / EntrySize) <= EntryCount;
}
// the segmented form compares the selector word and applies the
// same rule to the offset word.
inline bool SegmentedEntryOf(unsigned selector, unsigned segmented)
{
    if ((segmented >> 16) != (selector & 0xffffu))
        return false;
    const unsigned distance = (segmented & 0xffffu) - PageEntries;
    return !(distance & (EntrySize - 1)) && (distance / EntrySize) <= EntryCount;
}
inline unsigned SegmentedAddress(unsigned selector, unsigned offset)
{
    return (selector << 16) | (offset & 0xffffu);
}
inline bool SegmentedOffsetValid(unsigned offset)
{
    return offset <= 0x10000u; // Reject offsets outside the segment.
}
// 5a / 68 <data> / 52 / e9 <target - (entry + 12)>.
inline void LsTemplate(unsigned char (&out)[LsLength], unsigned entry, unsigned target, unsigned data)
{
    const unsigned relative = target - (entry + LsBias);
    out[0] = 0x5a;
    out[1] = 0x68;
    for (unsigned i = 0; i < 4; ++i)
        out[2 + i] = (unsigned char)(data >> (8 * i));
    out[6] = 0x52;
    out[7] = 0xe9;
    for (unsigned i = 0; i < 4; ++i)
        out[8 + i] = (unsigned char)(relative >> (8 * i));
}
// 66 5a / 66 68 <data> / 66 52 / ea <target16>.
inline void SlTemplate(unsigned char (&out)[SlLength], unsigned target, unsigned data)
{
    out[0] = 0x66;
    out[1] = 0x5a;
    out[2] = 0x66;
    out[3] = 0x68;
    for (unsigned i = 0; i < 4; ++i)
        out[4 + i] = (unsigned char)(data >> (8 * i));
    out[8] = 0x66;
    out[9] = 0x52;
    out[10] = 0xea;
    for (unsigned i = 0; i < 4; ++i)
        out[11 + i] = (unsigned char)(target >> (8 * i));
}
inline unsigned LsTemplateTarget(unsigned entry, unsigned relative)
{
    return entry + LsBias + relative;
}
// and 0x2379d: "or al, 1" on the entry address.
inline unsigned IsResult(unsigned entry)
{
    return entry | 1u;
}

// MapHInstLS: zero takes the current task's instance; below 0x10000
// the value is already a 16-bit handle; otherwise the module database is
// searched and the module's 16-bit alias is answered. The lookup's
// zero result stays in EAX when nothing is found, so an unknown module maps
// to zero.
inline bool InstanceIsCurrentTask(unsigned instance)
{
    return instance == 0;
}
inline bool InstanceIsSegmented(unsigned instance)
{
    return instance < 0x10000u;
}
inline unsigned UnknownModuleLS(unsigned)
{
    return 0;
}
// UTRegister marshals four pointers: lpsz16BITDLL, lpszInitName,
// lpszProcName and ppfn32Thunk, the frame slots 12, 16, 20 and 24. The
// 16-bit result comes back through CWDE.
inline unsigned UtMappedSlot(unsigned n)
{
    return 12 + 4 * n;
}
inline unsigned SignExtendWord(unsigned value)
{
    return (value & 0x8000u) ? (value | 0xffff0000u) : (value & 0xffffu);
}
} // namespace Win98ThunkBridges
