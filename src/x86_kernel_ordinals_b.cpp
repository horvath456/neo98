// SPDX-License-Identifier: GPL-2.0-or-later
// Native providers for KERNEL32 ordinal exports 61–122. Bind helpers in
// win98_kernel_ordinals_b.h to heaps, threads, critical sections, NLS, and
// flat thunks; report unsupported services.
#include "win98_critical.h"
#include "win98_thunk_prime.h"
#include "win98_applog.h"
#include "win98_kernel_ordinals_b.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace O = Win98KernelOrdinalsB;
struct Application::KernelOrdinalsBState
{
    // 59..65: 16-bit global handles for the native allocations behind them.
    struct Block
    {
        unsigned address = 0, size = 0, locks = 0;
    } blocks[64];
    unsigned char slot_mask = 0;      // 102/103, the eight reserved thread slots
    unsigned notify_window = 0;       // 122 mode 0
    unsigned wow_requests = 0, thunk_requests = 0;
};
namespace
{
constexpr unsigned HandleBase = 0x1000; // handle16 = HandleBase + 8 * index
unsigned HandleOf(unsigned index) { return HandleBase + 8 * index; }
bool HandleIndex(unsigned handle16, unsigned &index)
{
    handle16 &= 0xffff;
    if (handle16 < HandleBase || (handle16 - HandleBase) % 8)
        return false;
    index = (handle16 - HandleBase) / 8;
    return index < 64;
}
} // namespace

void Application::FreeKernelOrdinalsB()
{
    delete kernel_ordinals_b;
    kernel_ordinals_b = nullptr;
}

unsigned Application::KernelOrdinalsBApi(unsigned ordinal, const unsigned *a)
{
    if (!kernel_ordinals_b)
        kernel_ordinals_b = new KernelOrdinalsBState;
    auto &s = *kernel_ordinals_b;
    auto note = [&](const char *text) {
        if (!log)
            return;
        char line[512];
        snprintf(line, sizeof line, "KERNELORDINAL %u %s call=%u", ordinal, text, api_calls);
        log(line);
    };
    auto stop = [&](const char *reason) {
        char text[256];
        snprintf(text, sizeof text, "KERNEL32 ordinal %u: %s", ordinal, reason);
        Fail(text);
        return 0u;
    };
    auto pair = [&](O::Pair v) {
        Register(Reg::Edx, v.high);
        return v.low;
    };

    // ---- 72..85: large integers, pure arithmetic --------------------------
    switch (ordinal)
    {
    case 72: return pair(O::LargeIntegerAdd(a[0], a[1], a[2], a[3]));
    case 73: return pair(O::EnlargedIntegerMultiply(a[0], a[1]));
    case 74: return pair(O::EnlargedUnsignedMultiply(a[0], a[1]));
    case 75:
    {
        unsigned quotient = 0, remainder = 0;
        if (!O::EnlargedUnsignedDivide(a[0], a[1], a[2], quotient, remainder))
        {
            // Division by zero raises
            // STATUS_INTEGER_DIVIDE_BY_ZERO through the guest's SEH.
            const unsigned raise[] = {0xc0000094u, 0, 0, 0};
            ExceptionApi("RaiseException", raise);
            return 0;
        }
        if (a[3])
            Write32(a[3], remainder);
        Register(Reg::Edx, remainder);
        return quotient;
    }
    case 76:
    {
        bool store = false;
        unsigned remainder = 0;
        const auto v = O::ExtendedLargeIntegerDivide(a[0], a[1], a[2], store, remainder);
        if (store && a[3])
            Write32(a[3], remainder);
        return pair(v);
    }
    case 77: return pair(O::ExtendedMagicDivide(a[0], a[1], a[2], a[3], a[4]));
    case 78: return pair(O::ExtendedIntegerMultiply(a[0], a[1], a[2]));
    case 79: return pair(O::LargeIntegerShiftLeft(a[0], a[1], a[2]));
    case 80: return pair(O::LargeIntegerShiftRight(a[0], a[1], a[2]));
    case 81: return pair(O::LargeIntegerArithmeticShift(a[0], a[1], a[2]));
    case 82: return pair(O::LargeIntegerNegate(a[0], a[1]));
    case 83: return pair(O::LargeIntegerSubtract(a[0], a[1], a[2], a[3]));
    case 84: return pair(O::ConvertLongToLargeInteger(a[0]));
    case 85: return pair(O::ConvertUlongToLargeInteger(a[0]));
    default: break;
    }

    // ---- 56-style pointers, descriptors and WOW handles ---------------------
    struct NativeLdt final : O::DescriptorSource
    {
        Win98Selectors::Table &table;
        explicit NativeLdt(Win98Selectors::Table &t):table(t){}
        bool Descriptor(unsigned selector, unsigned char (&bytes)[8]) override { return table.Descriptor(selector,bytes); }
    } ldt(selectors);
    // Ordinals 56, 57 and 58 belong to the 1..60 batch; the header carries
    // their decisions (VdmPointer, Handle32, Handle16) for that connection.
    switch (ordinal)
    {
    case 68:
        // the fix increments the arena lock byte of an LDT
        // selector under the heap lock. There is no arena here; the pointer
        // result is the shared body of ordinal 56.
        ++s.wow_requests;
        if(a[2])selectors.Fix(a[0]);
        return O::VdmPointer(ldt, a[0], a[2]);
    case 69:
        // the matching unfix; nothing to release. The original
        // leaves EAX undefined after the lock release.
        ++s.wow_requests;
        selectors.Unfix(a[0]);
        return 0;
    case 70:
    {
        ++s.wow_requests;
        unsigned char bytes[8] = {};
        const unsigned ok = O::Descriptor(ldt, a[0], bytes);
        if (ok)
            Store(a[1], bytes, sizeof bytes);
        return ok;
    }
    default: break;
    }

    // ---- 59..67, 91: QT_Thunk transitions into KRNL386 ----------------------
    // These entries load a thunk index into CL and enter QT_Thunk through
    // KERNEL32's own target table ( holds the pointer).
    // The 16-bit heap targets use native global blocks with LDT selectors.
    // Ordinals 59 (K32WOWGlobalAlloc16) and 60 (K32WOWGlobalLock16,
    // ) belong to the 1..60 batch, which keeps the Win16 global
    // heap as Application::win16_globals. Where that batch is present, 61..65
    // work on its table, so a handle from 59 can be unlocked and freed here.
    // The local table below serves a tree without that batch.
#if __has_include("win98_kernel_ordinals_a.h")
    if (ordinal >= 61 && ordinal <= 65)
    {
        ++s.thunk_requests;
        switch (ordinal)
        {
        case 61: return Win16GlobalApi(26, a) ? 1 : 0;
        case 62: return Win16GlobalApi(31, a);
        case 63:
        {
            const unsigned handle = Win16GlobalApi(59, a);
            if (!handle)
                return 0;
            const unsigned lock[] = {handle};
            const unsigned pointer = Win16GlobalApi(60, lock);
            if (a[2])
            {
                const unsigned short h = (unsigned short)handle;
                Store(a[2], &h, 2);
            }
            return pointer;
        }
        case 64:
            for (auto &block : win16_globals)
                if (block.pointer && block.pointer == selectors.Linear(a[0]))
                {
                    block.locks = 0;
                    const unsigned release[] = {block.handle};
                    return Win16GlobalApi(31, release);
                }
            return 0;
        default:
        {
            const unsigned pointer = Win16GlobalApi(60, a);
            if (pointer && a[1])
                Write32(a[1], Win16GlobalApi(32, a));
            return pointer;
        }
        }
    }
#endif
    auto find = [&](unsigned handle16, unsigned &index) {
        return HandleIndex(handle16, index) && s.blocks[index].address;
    };
    auto by_pointer = [&](unsigned pointer, unsigned &index) {
        for (index = 0; index < 64; ++index)
            if (s.blocks[index].address && s.blocks[index].address == pointer)
                return true;
        return false;
    };
    auto alloc16 = [&](unsigned flags, unsigned size) -> unsigned {
        unsigned index = 0;
        while (index < 64 && s.blocks[index].address)
            ++index;
        if (index == 64)
        {
            note("handle table exhausted");
            return 0;
        }
        const unsigned args[] = {flags & 0xffff, size};
        const unsigned address = Api("GlobalAlloc", args);
        if (!address)
            return 0;
        s.blocks[index] = {address, size, 0};
        return HandleOf(index);
    };
    auto free16 = [&](unsigned index) -> unsigned {
        const unsigned args[] = {s.blocks[index].address};
        const unsigned failed = Api("GlobalFree", args);
        if (!failed)
            s.blocks[index] = {};
        return failed ? HandleOf(index) : 0;
    };
    switch (ordinal)
    {
    case 61: // K32WOWGlobalUnlock16(WORD handle), thunk 0x48, BOOL: still locked
    {
        ++s.thunk_requests;
        unsigned index;
        if (!find(a[0], index) || !s.blocks[index].locks)
            return 0;
        return --s.blocks[index].locks ? 1 : 0;
    }
    case 62: // K32WOWGlobalFree16(WORD handle), thunk 0x4a, zero on success
    {
        ++s.thunk_requests;
        unsigned index;
        if (!find(a[0], index))
            return a[0] & 0xffff;
        return free16(index);
    }
    case 63: // K32WOWGlobalAllocLock16(WORD flags, DWORD size, LPWORD handle), thunk 0x47
    {
        ++s.thunk_requests;
        const unsigned handle = alloc16(a[0], a[1]);
        if (!handle)
            return 0;
        unsigned index;
        find(handle, index);
        ++s.blocks[index].locks;
        if (a[2])
        {
            const unsigned short h = (unsigned short)handle;
            Store(a[2], &h, 2);
        }
        return s.blocks[index].address;
    }
    case 64: // K32WOWGlobalUnlockFree16(DWORD vp), thunk 0x46
    {
        ++s.thunk_requests;
        unsigned index;
        if (!by_pointer(a[0], index))
            return 0;
        s.blocks[index].locks = 0;
        return free16(index);
    }
    case 65: // K32WOWGlobalLockSize16(WORD handle, LPDWORD size), thunk 0x45
    {
        ++s.thunk_requests;
        unsigned index;
        if (!find(a[0], index))
            return 0;
        ++s.blocks[index].locks;
        if (a[1])
            Write32(a[1], AllocationSize(s.blocks[index].address));
        return s.blocks[index].address;
    }
    case 66: // K32WOWYield16, thunk 0x4d
    case 67: // K32WOWDirectedYield16(WORD htask), thunk 0x4c
    {
        // There is no Win16 task to yield to; the native scheduler gets the
        // same chance a Win16 yield gives the Win32 side.
        ++s.thunk_requests;
        const unsigned args[] = {0};
        return Api("Sleep", args);
    }
    case 91: // PK16FNF(LPSTR buffer), thunk 5
    {
        // KRNL386 answers with the name of the file its loader could not
        // find. The NE loader keeps it in win16_not_found; an empty
        // name is what SHELL32's caller at reads as "nothing to
        // report".
        ++s.thunk_requests;
        if (a[0])
            Store(a[0], win16_not_found, strlen(win16_not_found) + 1);
        return 0;
    }
    default: break;
    }

    // ---- 86, 93..98: system levels ------------------------------------------
    // (_EnterSysLevel) and 0x41fb (_LeaveSysLevel) run the same
    // uniprocessor fast paths as the critical-section core in
    // win98_critical.h; the records live in NEO98's critical-section table,
    // keyed by the caller's pointer.
    struct SysLevel final : Win98Critical::Environment
    {
        Application &app;
        Win98Critical::Mutex invalid;
        explicit SysLevel(Application &v) : app(v) {}
        Critical *Record(unsigned id)
        {
            if (auto *p = app.FindCritical(id))
                return p;
            auto *record = new Critical;
            if (!record)
                return nullptr;
            record->id = id;
            auto &head = app.critical_buckets[(id >> 5) & 63];
            record->next = head;
            head = record;
            return record;
        }
        Win98Critical::Client ReadClient(unsigned) override { return {}; }
        bool ProbeClient(unsigned) override { return false; }
        void TouchClient(unsigned) override {}
        void WriteTag(unsigned, unsigned char) override {}
        void WriteObject(unsigned, unsigned) override {}
        Win98Critical::Mutex *FindMutex(unsigned id) override
        {
            auto *p = app.FindCritical(id);
            return p ? &p->data : nullptr;
        }
        Win98Critical::Mutex &Data(unsigned id) override
        {
            if (auto *p = Record(id))
                return p->data;
            app.Fail("System level record allocation failed");
            return invalid;
        }
        bool HeapContains(unsigned) override { return false; }
        unsigned CurrentThread() override { return app.ThreadId(); }
        void GlobalLock() override {}
        void GlobalUnlock() override {}
        unsigned AllocateMutex() override { return 0; }
        void InitializeMutex(unsigned) override {}
        bool ProcessPresent() override { return true; }
        unsigned CreateProcessList() override { return 0; }
        bool BindProcess(unsigned) override { return true; }
        void DestroyProcessList(unsigned) override {}
        void DestroyMutex(unsigned) override {}
        void FreeMutex(unsigned) override {}
        void Wait(unsigned id) override { app.WaitCritical(id); }
        void Wake(unsigned id) override { app.WakeCritical(id); }
        bool Fault(unsigned) override { return false; }
    } levels(*this);
    switch (ordinal)
    {
    case 86:
        // Both leave-system-level entries share the native release operation;
        // no separate Win16 scheduler needs waking.
    case 98:
        if (!a[0] || !levels.FindMutex(a[0]))
            return stop("_LeaveSysLevel on a level that was never entered");
        Win98Critical::LeaveMutex(levels, a[0]);
        return Register(Reg::Eax);
    case 97:
        if (!a[0])
            return stop("_EnterSysLevel with a null level");
        Win98Critical::EnterMutex(levels, a[0]);
        return ThreadId();
    // Ordinals 93 and 96 answer over an ordinary per-process system level.
    // Since PLAN.md step 8 no native service takes it; only a guest that
    // calls EnterSysLevel on the record ever owns it.
    case 93: // GetpWin16Lock(LPVOID *out)
    {
        const unsigned lock = Win16LockRecord();
        if (!lock)
            return stop("Win16 lock record allocation failed");
        Write32(a[0], lock);
        return a[0];
    }
    case 96: // _ConfirmWin16Lock
    {
        const unsigned lock = Win16LockRecord();
        auto *p = lock ? FindCritical(lock) : nullptr;
        return p && p->data.owner == ThreadId() ? p->data.recursion : 0;
    }
    case 94: // _CheckNotSysLevel(ptr): ret 4, EAX untouched
    case 95: // _ConfirmSysLevel(ptr): ret 4, EAX untouched
        return Register(Reg::Eax);
    default: break;
    }

    // ---- 102, 103: reserved thread slots -----------------------------------
    if (ordinal == 102)
    {
        unsigned error = 0;
        const unsigned slot = O::SlotAlloc(s.slot_mask, error);
        if (error)
            last_error = error;
        return slot;
    }
    if (ordinal == 103)
    {
        unsigned error = 0;
        const unsigned ok = O::SlotFree(s.slot_mask, a[0], error);
        if (error)
            last_error = error;
        return ok;
    }

    // ---- 105..117, 119: string helpers -------------------------------------
    // Guest strings are copied through bounded host buffers; the bodies in
    // the header work on host memory. Wide strings are limited to 32 K code
    // units and narrow strings to 64 K bytes here.
    auto read_wide = [&](unsigned address, unsigned short *buffer, unsigned capacity) -> bool {
        for (unsigned i = 0; i < capacity; ++i)
        {
            if (!Read(address + i * 2, buffer + i, 2))
                return false;
            if (!buffer[i])
                return true;
        }
        return false;
    };
    auto read_narrow = [&](unsigned address, char *buffer, unsigned capacity) -> bool {
        for (unsigned i = 0; i < capacity; ++i)
        {
            if (!Read(address + i, buffer + i, 1))
                return false;
            if (!buffer[i])
                return true;
        }
        return false;
    };
    static unsigned short wide_a[32768], wide_b[32768];
    static char narrow_a[65536], narrow_b[65536];
    switch (ordinal)
    {
    case 105:
    {
        if (!read_wide(a[0], wide_a, 32768))
            return stop("wcstol input is not a readable wide string");
        const unsigned short *end = nullptr;
        const unsigned value = O::WideToLong(wide_a, a[1] ? &end : nullptr, a[2]);
        if (a[1])
            Write32(a[1], a[0] + unsigned(end - wide_a) * 2);
        return value;
    }
    case 108:
    case 111:
    {
        if (!read_narrow(a[0], narrow_a, sizeof narrow_a))
            return stop("string helper input is not a readable string");
        const char *p = ordinal == 108 ? O::NarrowChar(narrow_a, a[1]) : O::NarrowLastChar(narrow_a, a[1]);
        return p ? a[0] + unsigned(p - narrow_a) : 0;
    }
    case 109:
    case 112:
    {
        if (!read_narrow(a[0], narrow_a, sizeof narrow_a) || !read_narrow(a[1], narrow_b, sizeof narrow_b))
            return stop("string helper input is not a readable string");
        if (ordinal == 109)
            return O::NarrowComplementSpan(narrow_a, narrow_b);
        const char *p = O::NarrowFind(narrow_a, narrow_b);
        return p ? a[0] + unsigned(p - narrow_a) : 0;
    }
    case 110:
    {
        if (a[2] > sizeof narrow_b)
            return stop("strncpy count exceeds the native bound");
        // The source may be shorter than the count; read only what exists.
        for (unsigned i = 0; i < a[2]; ++i)
        {
            if (!Read(a[1] + i, narrow_a + i, 1))
                return stop("strncpy source is not readable");
            if (!narrow_a[i])
                break;
        }
        narrow_a[a[2] < sizeof narrow_a ? a[2] : sizeof narrow_a - 1] = 0;
        O::NarrowCopyN(narrow_b, narrow_a, a[2]);
        if (a[2] && !Store(a[0], narrow_b, a[2]))
            return stop("strncpy destination is not writable");
        return a[0];
    }
    case 113:
    {
        if (!read_wide(a[0], wide_a, 32768))
            return stop("wcschr input is not a readable wide string");
        const unsigned short *p = O::WideChar(wide_a, a[1]);
        return p ? a[0] + unsigned(p - wide_a) * 2 : 0;
    }
    case 114:
    case 115:
    {
        if (!read_wide(a[0], wide_a, 32768) || !read_wide(a[1], wide_b, 32768))
            return stop("wide compare input is not a readable wide string");
        return unsigned(ordinal == 114 ? O::WideCompare(wide_a, wide_b) : O::WideCompareN(wide_a, wide_b, a[2]));
    }
    case 116:
    {
        if (a[2] > 32768)
            return stop("wcsncpy count exceeds the native bound");
        for (unsigned i = 0; i < a[2]; ++i)
        {
            if (!Read(a[1] + i * 2, wide_a + i, 2))
                return stop("wcsncpy source is not readable");
            if (!wide_a[i])
                break;
        }
        wide_a[a[2] < 32768 ? a[2] : 32767] = 0;
        O::WideCopyN(wide_b, wide_a, a[2]);
        if (a[2] && !Store(a[0], wide_b, a[2] * 2))
            return stop("wcsncpy destination is not writable");
        return a[0];
    }
    default: break;
    }
    // 106, 107, 117 and 119 round-trip through the ANSI services the original
    // calls: WideCharToMultiByte, MultiByteToWideChar, GetStringTypeA
    // and LCMapStringA with LCMAP_UPPERCASE on the system locale.
    auto wide_to_ansi = [&](unsigned wide, int count, unsigned ansi, unsigned capacity) -> unsigned {
        const unsigned args[] = {0, 0, wide, unsigned(count), ansi, capacity, 0, 0};
        return NlsApi(811, args);
    };
    auto ansi_to_wide = [&](unsigned ansi, int count, unsigned wide, unsigned capacity) -> unsigned {
        const unsigned args[] = {0, 0, ansi, unsigned(count), wide, capacity};
        return NlsApi(594, args);
    };
    auto upper = [&](unsigned ansi, unsigned count) -> unsigned {
        const unsigned locale = Api("GetSystemDefaultLCID", a);
        const unsigned args[] = {locale, 0x200, ansi, count, ansi, count};
        return LcMapStringApi(args);
    };
    switch (ordinal)
    {
    case 106: // lstrcmpiA over ANSI copies of two wide strings
    {
        const unsigned lengths[] = {Api("lstrlenW", a) + 1, Api("lstrlenW", a + 1) + 1};
        const unsigned first = Allocate(lengths[0]);
        if (!first)
            return 0xffffffffu;
        const unsigned second = Allocate(lengths[1]);
        if (!second)
        {
            Free(first);
            return 0xffffffffu;
        }
        unsigned result = 0xffffffffu;
        if (wide_to_ansi(a[0], -1, first, lengths[0]) && wide_to_ansi(a[1], -1, second, lengths[1]))
        {
            const unsigned args[] = {first, second};
            result = Api("lstrcmpiA", args);
        }
        Free(first);
        Free(second);
        return result;
    }
    case 107: // upper-case a wide string in place through its ANSI form
    {
        const unsigned length = Api("lstrlenW", a) + 1;
        const unsigned buffer = Allocate(length);
        if (!buffer)
            return 0;
        unsigned result = 0;
        if (wide_to_ansi(a[0], -1, buffer, length))
        {
            upper(buffer, length - 1);
            if (ansi_to_wide(buffer, -1, a[0], length))
                result = a[0];
        }
        Free(buffer);
        return result;
    }
    case 117: // GetStringTypeA CT_CTYPE1 of one wide character, masked
    case 119: // upper-case one wide character
    {
        const unsigned scratch = Allocate(16);
        if (!scratch)
            return 0;
        const unsigned short wide = (unsigned short)a[0];
        Store(scratch, &wide, 2);
        unsigned result = 0;
        if (wide_to_ansi(scratch, 1, scratch + 4, 1))
        {
            if (ordinal == 117)
            {
                const unsigned args[] = {0x800, 1, scratch + 4, 1, scratch + 8};
                StringTypeApi(args);
                unsigned short type = 0;
                Read(scratch + 8, &type, 2);
                result = (type & a[1]) & 0xffff;
            }
            else
            {
                upper(scratch + 4, 1);
                if (ansi_to_wide(scratch + 4, 1, scratch + 12, 1))
                {
                    unsigned short out = 0;
                    Read(scratch + 12, &out, 2);
                    result = out;
                }
            }
        }
        Free(scratch);
        return result;
    }
    default: break;
    }

    // ---- Threads and stacks --------------------------------------------------
    switch (ordinal)
    {
    case 71: // IsThreadId(DWORD id): the object behind the id is a thread
    {
        for (unsigned i = 0; i < ThreadCapacity; ++i)
            if (threads[i].id == a[0] && threads[i].fiber && !threads[i].done)
                return 1;
        return 0;
    }
    case 87: // SSOnBigStack: a Win32 thread (TIB flag 1) answers zero
        return 0;
    case 88: // SSCall(DWORD bytes, DWORD flags, FARPROC fn,...), cdecl
    {
        // a Win32 thread copies the argument bytes onto its own
        // stack and calls the procedure there.
        if (a[0] & 3 || a[0] > 64)
            return stop("SSCall argument block is not a small dword multiple");
        unsigned args[16] = {};
        const unsigned sp = Register(Reg::Esp);
        if (a[0] && !Read(sp + 16, args, a[0]))
            return stop("SSCall arguments are not readable");
        return Call(a[2], args, a[0] / 4);
    }
    case 100: // TerminateThread's body with a third flag
    {
        // The public TerminateThread passes zero as its flag; the shell uses
        // one. The flag has no additional native effect. ExitThread ends the
        // current thread; native TerminateThread ends another.
        if (a[2])
            note("terminate flag set; its effect is not modelled");
        unsigned index = current_thread;
        if (a[0] != 0xfffffffeu)
        {
            auto *h = FindKernelHandle(a[0]);
            if (!h || h->kind != 2)
                return 0; // no object behind the handle
            index = h->object;
        }
        if (index == current_thread)
        {
            ThreadApi("ExitThread", a + 1);
            return 1;
        }
        return KernelAsyncApi("TerminateThread", a);
    }
    case 92: // GetPK16SysVar: the KRNL386 sysvar pointer
        note("GetPK16SysVar without KRNL386");
        return 0;
    case 120: // a constant
        return 0xc0887b5bu;
    case 122: // mode 0 registers a notification window
        if (a[0] == 0)
        {
            // IsWindow's answer stays in EAX, so a registered window
            // returns one and a cleared registration returns the zero mode.
            if (a[1] && !User::IsWindow(a[1]))
                return 0;
            s.notify_window = a[1];
            note(a[1] ? "notification window registered" : "notification window cleared");
            return a[1] ? 1 : 0;
        }
        if (a[0] == 1)
        {
            // Shared APPLOG index. The native file provider loads the existing
            // index lazily; process-start collection is a separate service.
            struct Cache {
                Win98AppLog::Index index;
                unsigned char *storage = nullptr;
                bool loaded = false;
                ~Cache() { delete[] storage; }
            };
            static Cache cache;
            constexpr const char *path = "C:\\WINDOWS\\APPLOG\\APPLOG.ind";
            if (!cache.loaded)
            {
                cache.loaded = true;
                unsigned node = 0;
                if (!files.store.Lookup(path, node))
                {
                    const auto *file = files.store.Get(node);
                    const unsigned char *bytes = file ? files.store.Bytes(node) : nullptr;
                    if (file && file->size >= 408 && bytes) cache.storage = new unsigned char[file->size];
                    if (cache.storage)
                    {
                        memcpy(cache.storage, bytes, file->size);
                        if (Win98AppLog::Load(cache.index, cache.storage, file->size))
                            for (unsigned i = 0; i < cache.index.count; ++i) cache.index.records[i * 32 + 30] = 0;
                    }
                }
            }
            if (!cache.index.count || !cache.index.records) return 1;
            struct Environment {
                Application &app; const char *path; unsigned handle = 0;
                unsigned NowHigh() { return app.CurrentFileTime() >> 32; }
                bool Exists(const char *name) { unsigned node = 0, error = app.files.store.Lookup(name, node); if (error) app.last_error = error; return !error; }
                bool Open() { unsigned status = 0, error = app.files.Open(path, 2, 0, 2, 0x80, handle, status); app.last_error = error ? error : status; return !error; }
                unsigned Write(const void *data, unsigned bytes) { unsigned done = 0, error = app.files.Write(handle, data, bytes, done); if (error) app.last_error = error; return done; }
                void Close() { app.files.Close(handle); }
            } environment{*this, path};
            Win98AppLog::Prune(cache.index, environment);
            Win98AppLog::Save(cache.index, environment);
            return 1;
        }
        return a[0];
    case 101: // argument zero enters VMM Win32 service 0x28
        if (a[0])
            return Register(Reg::Eax);
        Files::RequestCachePages();
        return Register(Reg::Eax); // The original POPAD restores the caller's EAX.
    default: break;
    }

    // ---- Flat-thunk priming and 16-bit side entries -------------------------
    switch (ordinal)
    {
    case 89: // FT_PrologPrime: [ESP] offset, [ESP+4] code area, then caller return
    case 90: // QT_ThunkPrime: EAX code area, EDX offset; tail jumps to code area
    {
        const bool quick=ordinal==90;
        const unsigned sp=Register(Reg::Esp);
        const unsigned area=quick?Register(Reg::Eax):Read32(sp+4);
        const unsigned offset=quick?Register(Reg::Edx):Read32(sp);
        const unsigned table=Read32(area+offset);
        if(state==Failed)return 0;
        unsigned gate=0;
        if(!NativeProcedure("KERNEL32.DLL",quick?"QT_Thunk":"FT_Prolog",0,&gate,false))return 0;
        unsigned char code[32];Win98ThunkPrime::Template(quick,table,gate,code);
        const unsigned page=area&~0xfffu;
        if(!Protect(page,((area+31)&~0xfffu)-page+0x1000,Prot::Read|Prot::Write|Prot::Exec))return 0;
        ++loader_writes;const bool written=Store(area,code,sizeof code);--loader_writes;
        if(!written)return 0;
        Register(Reg::Eflags,Register(Reg::Eflags)&~0x400u);
        if(!quick)Register(Reg::Esp,sp+8);
        Register(Reg::Eip,area);pending=0;
        return Register(Reg::Eax);
    }
    case 104: // Invoke the callback, then restore the caller's stack frame.
    {
        const unsigned record=Register(Reg::Ebx),frame=Register(Reg::Ebp);
        const unsigned args[]={Read32(record+0x22),Read32(record+0x1e),Read32(record+0x1a),Read32(record+0x16)};
        const unsigned procedure=Read32(record+0x26);
        if(state==Failed)return 0;
        // The original released the Win16 mutex around the callback. Without
        // that mutex only the counter at fs:[0x1e] moves.
        ThunkLockCount(0xffff);
        const unsigned result=Call(procedure,args,4);
        if(state==Failed||state==Exited)return result;
        ThunkLockCount((ThunkLockCount()+1)&65535);
        Register(Reg::Eax,ThreadId());
        Register(Reg::Ecx,(Register(Reg::Ecx)&~255u)|0x14);
        Register(Reg::Esp,frame+4);Register(Reg::Eip,Read32(frame));pending=0;
        return ThreadId();
    }
    case 121: // the 16-bit side of the flat-thunk connector
        return FlatThunkApi("ThunkConnect16", a);
    default: break;
    }
    return stop("unhandled ordinal");
}
} // namespace X86
