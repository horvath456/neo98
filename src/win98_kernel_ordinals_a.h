// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 ordinal exports 1–60. Native memory, heap, NLS, and thread
// providers are in src/x86_kernel_ordinals_a.cpp.
#pragma once
namespace Win98KernelOrdinals
{
typedef unsigned U;

// CommonUnimpStub sets ERROR_CALL_NOT_IMPLEMENTED. ECX bits 4–7 select the
// return value; bits 0–3 specify argument DWORDs to remove.
constexpr U UnimpStubError = 120;
inline U UnimpStubPop(U ecx) { return (ecx & 15) * 4; }
inline U UnimpStubResult(U ecx)
{
    switch ((ecx >> 4) & 15)
    {
    case 15: return 0xffffffffu; //
    case 14: return 120;         //
    case 13: return 50;          //
    case 1: return 1;            //
    default: return 0;           //
    }
}

// k32CharToOemA, k32CharToOemBuffA, k32OemToCharA and k32OemToCharBuffA
// translate through byte tables. Initialization round-trips bytes 0x80..0xff
// through MultiByteToWideChar and WideCharToMultiByte between CP_ACP and
// CP_OEMCP. OEM bytes 0x00..0x1f map to themselves except for the three DOS
// glyphs 0x0f, 0x14 and 0x15.
static const unsigned char OemToCharControl[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0xa4,
    0x10, 0x11, 0x12, 0x13, 0xb6, 0xa7, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
inline unsigned char CharToOemByte(const unsigned char *high, unsigned char c)
{
    return c >= 0x80 ? high[c - 0x80] : c; //
}
inline unsigned char OemToCharByte(const unsigned char *high, unsigned char c)
{
    if (c < 0x20) return OemToCharControl[c]; //
    return c >= 0x80 ? high[c - 0x80] : c;   //
}
// The string forms stop at the NUL and write a terminator; the Buff forms
// copy exactly `count` bytes without looking for a NUL.
template <class Read, class Write>
inline void ConvertString(const unsigned char *high, bool to_oem, Read read, Write write)
{
    U i = 0;
    for (;; ++i)
    {
        const unsigned char c = read(i);
        if (!c) break;
        write(i, to_oem ? CharToOemByte(high, c) : OemToCharByte(high, c));
    }
    write(i, 0);
}
template <class Read, class Write>
inline void ConvertBuffer(const unsigned char *high, bool to_oem, U count, Read read, Write write)
{
    for (U i = 0; i < count; ++i)
    {
        const unsigned char c = read(i);
        write(i, to_oem ? CharToOemByte(high, c) : OemToCharByte(high, c));
    }
}
constexpr U InvalidParameter = 87; // a probe failure sets 87 and returns 0.

// Ordinal 14, k32LoadStringA. Zero buffer or zero capacity returns
// zero before any resource lookup. The string block is
// resource type 6, id (id >> 4) + 1, entry id & 15; the copy is limited to
// capacity - 1 characters and terminated.
inline bool LoadStringArguments(U buffer, U capacity) { return buffer && capacity; }

// Ordinal 43, ThunkInitLS, and the common connector
// which ordinal 41 enters with flag 1 and ordinal 43 with flag 0. Arguments:
// (thunk block, 16-bit export name, checksum, 16-bit DLL, 32-bit DLL).
// With a null 32-bit DLL name, a null block
// answers 1, otherwise the 16-bit DLL is loaded and the result is OR-ed with
// one. A 16-bit module handle below 0x20 answers zero.
inline bool ThunkInitDirectPath(U dll32) { return !dll32; }
inline U ThunkInitDirectResult(U block, U load16_result) { return block ? (load16_result | 1) : 1; }
constexpr U Win16ModuleMinimum = 0x20;

// Ordinal 55, K32WOWCallback16Ex: more than 16 argument bytes is
// rejected with 87. A thread without a 16-bit stack
// (TIB flag bit 0 at fs:[0x1c]) answers zero without touching the
// return-code slot. Ordinal 54, has the same thread test.
constexpr U CallbackArgumentLimit = 16;

// Ordinal 56, K32WOWGetVDMPointer. A real-mode pointer is
// segment * 16 + offset. A protected-mode pointer needs an LDT
// selector (table indicator bit 2) with a nonzero index; descriptors are then read from the LDT.
inline U RealModePointer(U pointer) { return ((pointer >> 16) << 4) + (pointer & 0xffffu); }
inline bool LdtSelector(U pointer)
{
    const U selector = pointer >> 16;
    return (selector & 4) && (selector & ~7u);
}

// Ordinals 57 and 58, K32WOWHandle32 and K32WOWHandle16
// (0x2f65e). Type 13, WOW_TYPE_HTASK, is converted through the task and
// process databases; type 3, WOW_TYPE_HDROP, sets ERROR_NOT_SUPPORTED and
// answers zero; every other type passes the low word through.
constexpr U WowTypeHdrop = 3, WowTypeHtask = 13, NotSupported = 50;
enum HandleRule
{
    HandlePassThrough,
    HandleTask,
    HandleUnsupported
};
inline HandleRule WowHandleRule(U type)
{
    return type == WowTypeHtask ? HandleTask : type == WowTypeHdrop ? HandleUnsupported : HandlePassThrough;
}

// Ordinal 34, GetWin16DOSEnv: the stored pointer or zero.
// Ordinals 48 and 49, ReleaseThunkLock and RestoreThunkLock
// (0x21ee): the Win16 lock count is the word at fs:[0x1e]; releasing stores
// it sign-extended and leaves 0xffff behind.
constexpr U ThunkLockReleased = 0xffffu;
inline U ThunkLockStored(U word) { return (word & 0x8000u) ? (word | 0xffff0000u) : (word & 0xffffu); }

// VxDCall0..VxDCall8 enter ring 0 through the call gate. The VxD's Win32
// service table declares the argument count, and the service removes those
// arguments before returning. The high word of the service number is the VxD
// identifier; the low word is the service index.
// VxD identifiers seen at the KERNEL32 call sites of VxDCall; the names are
// the DDK device ids.
struct Device
{
    U id;
    const char *name;
};
static const Device Devices[] = {{0x0001, "VMM"}, {0x0002, "DEBUG"}, {0x0005, "VTD"}, {0x002a, "VWIN32"},
                                 {0x002b, "VCOMM"}, {0x0038, "VCOND"}, {0x0040, "IFSMGR"}};
inline const char *DeviceName(U service)
{
    for (const auto &device : Devices)
        if (device.id == (service >> 16)) return device.name;
    return "unnamed VxD";
}
inline U ServiceDevice(U service) { return service >> 16; }
inline U ServiceIndex(U service) { return service & 0xffffu; }
enum Service
{
    // VMM Win32 services, argument counts read at the KERNEL32 call sites.
    VmmPageReserve = 0x00010000,           // VirtualAlloc: (page, npages, flags)
    VmmPageCommit = 0x00010001,            // (page, npages, hpd, pagerdata, flags)
    VmmPageDecommit = 0x00010002,          // VirtualFree: (page, npages, flags)
    VmmPageFree = 0x0001000a,              // VirtualAlloc: (hmem, flags)
    VmmPageModifyPermissions = 0x0001000d, // (page, npages, permand, permor)
    // VMM also carries the registry. ADVAPI32 reaches it through the same
    // gate: RegFlushKey is service 1Ch with the key as
    // its only argument and returns the Win32 status in EAX, and it falls
    // back to "Remote_RegFlushKey" in the network provider when the key is
    // not local. RegEnumValueA uses service 19h the same way, with
    // the fallback name "Remote_RegEnumValue". The names in ADVAPI32's own
    // string table are what identifies these two.
    VmmRegFlushKey = 0x0001001c,           // (hKey)
    // VWIN32 Win32 services.
    Vwin32Sleep = 0x002a0009,              // Sleep: (milliseconds)
    Vwin32Int21Dispatch = 0x002a0010,      // (eax, ecx), other registers in place, CF result
    Vwin32PerformanceCounter = 0x002a0044, // (LARGE_INTEGER *)
    Vwin32SemaphoreSignal = 0x002a005e,    // (object, count, previous), K32OBJ pointer
};
struct ServiceEntry
{
    U service, arguments;
    const char *name;
    bool served;
};
// Services the native dispatcher knows. `served` marks the ones a provider
// answers; the others are identified so the failure names the service.
static const ServiceEntry Services[] = {
    {VmmPageReserve, 3, "VMM _PageReserve", true},
    {VmmPageCommit, 5, "VMM _PageCommit", true},
    {VmmPageDecommit, 3, "VMM _PageDecommit", true},
    {VmmPageFree, 2, "VMM _PageFree", true},
    {VmmPageModifyPermissions, 4, "VMM _PageModifyPermissions", true},
    {VmmRegFlushKey, 1, "VMM registry flush (ADVAPI32 RegFlushKey)", true},
    {Vwin32Sleep, 1, "VWIN32 Sleep", true},
    {Vwin32Int21Dispatch, 2, "VWIN32 Int21Dispatch", true},
    {Vwin32PerformanceCounter, 1, "VWIN32 QueryPerformanceCounter", true},
    {Vwin32SemaphoreSignal, 3, "VWIN32 semaphore signal on a K32OBJ", false},
};
inline const ServiceEntry *FindService(U service)
{
    for (const auto &entry : Services)
        if (entry.service == service) return &entry;
    return nullptr;
}
// VMM page reservation flags and permissions, from the DDK definitions the
// KERNEL32 call sites use: PR_PRIVATE/PR_SHARED/PR_SYSTEM arena selectors,
// PR_FIXED, PC_WRITEABLE and PC_USER.
constexpr U PrPrivate = 0x80000400u, PrShared = 0x80060000u, PrSystem = 0x80080000u, PrFixed = 8;
constexpr U PcWriteable = 0x00020000u, PcUser = 0x00040000u;
constexpr U PageFailure = 0xffffffffu;
inline bool ArenaRequest(U page) { return page == PrPrivate || page == PrShared || page == PrSystem; }
inline U PageProtection(U permissions) { return (permissions & PcWriteable) ? 4 : 2; } // PAGE_READWRITE : PAGE_READONLY

// Int 21h functions the Int21Dispatch provider answers. AH=19h current drive
// (AL = 0-based drive), AH=36h free space (DL = 1-based drive or 0; AX
// sectors per cluster or 0xffff, BX free clusters, CX bytes per sector, DX
// total clusters).
constexpr U Int21CurrentDrive = 0x19, Int21FreeSpace = 0x36;
constexpr U Int21InvalidDrive = 0xffffu, Int21SectorBytes = 512, Int21ClusterLimit = 0xffffu;

// HouseCleanLogicallyDeadHandles returns immediately when task flag 0x20 is
// clear. Retail logging entries LogApiThkLSF, LogApiThkSL, and LogCBThkSL do
// nothing.
constexpr U RetailNoOpArguments = 1;

// Win16 global heap bridge, ordinals 24..32, 59 and 60. The original enters
// KRNL386 through the thunk table (index in CL). NEO98 keeps its
// own block table; handles carry the Win16 shape (index * 8 with the LDT bits
// 6 for moveable and 7 for fixed blocks) without claiming a selector.
constexpr U GmemMoveable = 2, GmemZeroInit = 0x40;
inline U Win16Handle(U index, bool moveable) { return 0x100 + index * 8 + (moveable ? 6 : 7); }
inline bool Win16HandleIndex(U handle, U &index)
{
    if (handle < 0x100 || ((handle & 7) != 6 && (handle & 7) != 7)) return false;
    index = (handle - 0x100) / 8;
    return true;
}
} // namespace Win98KernelOrdinals
