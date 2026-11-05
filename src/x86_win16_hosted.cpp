// SPDX-License-Identifier: GPL-2.0-or-later
// Original 16-bit code called from native code of a 32-bit process.
// Windows 98 loads a printer driver into the process that creates the printer
// DC: GDI.EXE calls LoadLibrary on PSCRIPT.DRV and then calls
// its DDI entries with far calls on the 16-bit stack of the thread, which
// KERNEL32 keeps for every Win32 thread for exactly these transitions. A
// WordPad that prints therefore runs 16-bit driver code in its own process.
// This port has the Win16 loader of PLAN.md step 9 in every process: an NE
// image a 32-bit process loads gets its segments in the LDT of that process
// (src/x86_modules.cpp). What was missing is the call itself. A 32-bit
// process runs flat code at privilege zero; 16-bit code runs at privilege
// three on a 16-bit stack. The way in is a far return through a stub, which
// changes the privilege level and the stack in one step, exactly like the
// level change of a Win16 task (Win16EnterRingThree). The way out is the
// return gate of the gate region: the 16-bit code returns to CallbackReturn,
// the gate hook stops the machine there, and the saved flat context comes
// back as a whole.
// While the 16-bit code runs it reaches KERNEL, USER and GDI through the same
// gates a Win16 task uses. A native gate that has to call 32-bit code, a
// window procedure or a hook, finds a 16-bit CPU state under it; Call then
// goes back to the flat context the hosted call started from, and the 16-bit
// state returns with Call's own restore.
#include "user.h"
#include "win16_machine.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned CallbackReturn = 0x7000fff0;
// The 16-bit stack of a thread. KERNEL32 gives every Win32 thread one of
// 4000h bytes for thunks; a printer driver draws a whole page through it, so
// the stack here is 8000h bytes.
constexpr unsigned HostedStackBytes = 0x8000;
} // namespace

bool Application::Win16Process() const { return win16.state && !win16.state->hosted; }
// FEXCore alone decodes every code segment as a flat 32-bit one, so the far
// return into a 16-bit segment of Win16HostedCall would land on the offset
// as a linear address; a process on FEXCore runs on the pair of
// src/x86_cpu_dual.cpp, whose second engine takes the 16-bit code.
bool Application::Win16HostedEngine() const { return !cpu || cpu->Runs16Bit(); }
bool Application::Win16Code() { return win16.state && (Register(Reg::Cs) & 4); }
bool Application::Win16InstructionSlice()
{
    if (!win16.state) return false;
    if (Register(Reg::Cs) & 4) return true;
    // The entry stubs start in flat code and RETF into a 16-bit segment in
    // this same Run. They need the quota before making that transition.
    const unsigned ip = Register(Reg::Eip);
    return ip == win16.state->hosted_stub || (!win16.state->hosted && ip == 0x0fe00000);
}

bool Application::Win16HostedReady()
{
    if (win16.state)
        return true;
    win16.state = new Win16Machine;
    if (!win16.state)
        return false;
    win16.state->hosted = true;
    // A hosted process enters every library explicitly (Win16HostedLibrary);
    // the first gate must not look for a task stack to enter them on.
    win16.state->libraries_entered = true;
    return true;
}

struct Application::Win16HostedLoadFrame
{
    static Win16HostedLoadFrame *head;
    static unsigned next_key;
    Application *app;
    Win16HostedLoadFrame *next;
    unsigned key, first = 0, handle = 0, cursor = 0, error = 0;
    bool recorded = false, owns = false, target = false;
    char path[260] = {};
    Win16HostedLoadFrame(Application &a, const char *name) : app(&a), next(head), key(++next_key)
    { snprintf(path, sizeof path, "%s", name); head = this; }
    ~Win16HostedLoadFrame()
    {
        auto **at = &head;
        while (*at && *at != this) at = &(*at)->next;
        if (*at) *at = next;
    }
};
Application::Win16HostedLoadFrame *Application::Win16HostedLoadFrame::head = nullptr;
unsigned Application::Win16HostedLoadFrame::next_key = 0;
namespace {
Application::Win16HostedLoadFrame *HostedLoadOf(const Application *app, unsigned key)
{
    for (auto *f = Application::Win16HostedLoadFrame::head; f; f = f->next)
        if (f->app == app && f->key == key) return f;
    return nullptr;
}
}
struct Application::HostedRecord
{
    Application &app;
    bool kept;
    ~HostedRecord() { if (kept) { app.TakeTail(); app.PopHostFrame(); } }
};
bool Application::Win16HostedWorkRecorded(unsigned key, unsigned kind, unsigned module, unsigned phase) const
{
    const auto *frame = HostedLoadOf(this, key);
    if (!frame || !frame->recorded || !win16.state || !win16.state->hosted) return false;
    if (kind == HostWin16HostedLoad) return !frame->owns;
    if (kind == HostWin16HostedLibrary) return frame->cursor <= win16.state->module_count;
    return kind == HostWin16HostedEnter && module < win16.state->module_count && (phase == 1 || phase == 2);
}
void Application::DiscardSavedWin16HostedLoads()
{
    for (auto *f = Win16HostedLoadFrame::head, *next = f; f; f = next)
    { next = f->next; if (f->app == this) delete f; }
}

// The record of the current thread, made on first use.
template <class Machine> static auto *HostedThreadOf(Machine &machine, unsigned thread, bool make)
{
    decltype(&machine.hosted_threads[0]) none = nullptr;
    for (auto &record : machine.hosted_threads)
        if (record.thread == thread)
            return &record;
    if (!make)
        return none;
    for (auto &record : machine.hosted_threads)
        if (!record.thread)
        {
            record = {};
            record.thread = thread;
            return &record;
        }
    return none;
}

unsigned Application::Win16HostedStack()
{
    auto *record = HostedThreadOf(*win16.state, ThreadId(), true);
    if (!record)
        return 0;
    if (!record->stack)
    {
        // GMEM_FIXED | GMEM_ZEROINIT: a fixed block's handle is its selector.
        const unsigned handle = Win16GlobalAlloc(0x40, HostedStackBytes);
        Win16Block *block = handle ? Win16GlobalBlock(handle) : nullptr;
        if (!block)
            return 0;
        record->stack = block->selector;
        record->low = HostedStackBytes - 16;
        if (log)
        {
            char text[120];
            snprintf(text, sizeof text, "WIN16 hosted stack thread=%u selector=%04x", ThreadId(), record->stack);
            log(text);
        }
    }
    return record->stack;
}

// Call found 16-bit code on the CPU: switch to the flat context of the
// innermost hosted call of this thread and remember how far down the 16-bit
// stack is in use, so a hosted call made from the callback starts below it.
bool Application::Win16HostedFlat(unsigned &low)
{
    if (!win16.state || !win16.state->hosted || !(Register(Reg::Cs) & 4))
        return false;
    auto *record = HostedThreadOf(*win16.state, ThreadId(), false);
    if (!record || !record->depth || !record->flat[record->depth - 1])
        return false;
    low = record->low;
    record->low = (Register(Reg::Esp) & 0xffff) & ~1u;
    if (record->low > 64)
        record->low -= 64;
    Check(cpu->Restore(record->flat[record->depth - 1]));
    return true;
}
// A 16-bit window procedure reached from the flat code of a process that
// hosts it: the stack selector of the thread and the lowest word in use,
// under which the callback frame goes.
bool Application::Win16HostedArea(unsigned &stack, unsigned &sp, unsigned &low)
{
    if (!win16.state || !win16.state->hosted || (Register(Reg::Cs) & 4))
        return false;
    stack = Win16HostedStack();
    auto *record = HostedThreadOf(*win16.state, ThreadId(), false);
    if (!stack || !record)
        return false;
    low = record->low;
    sp = low & ~1u;
    return true;
}
void Application::Win16HostedSetLow(unsigned low)
{
    if (auto *record = win16.state ? HostedThreadOf(*win16.state, ThreadId(), false) : nullptr)
        record->low = low;
}
void Application::Win16HostedResume(unsigned low)
{
    if (!win16.state)
        return;
    if (auto *record = HostedThreadOf(*win16.state, ThreadId(), false))
        record->low = low;
}

unsigned Application::Win16HostedCall(unsigned far_pointer, const unsigned *push, unsigned count, unsigned ds,
                                      const Win16HostedRegisters *registers, bool *returned_out,
                                      unsigned *removed_out)
{
    const bool recorded = TakeTail() == TailRecord;
    if (returned_out)
        *returned_out = false;
    if (removed_out)
        *removed_out = count;
    if (!far_pointer || state == Failed || state == Exited || !Win16HostedReady())
        return 0;
    if (!Win16HostedEngine())
    {
        Fail("16-bit code in a 32-bit process needs a processor with 16-bit segments; FEXCore runs "
             "flat 32-bit code only, NEO98_CPU=unicorn runs it");
        return 0;
    }
    const bool from_flat = !(Register(Reg::Cs) & 3);
    Win16Machine::HostedThread *record = nullptr;
    unsigned stack = 0, sp = 0;
    if (from_flat)
    {
        stack = Win16HostedStack();
        record = HostedThreadOf(*win16.state, ThreadId(), false);
        if (!stack || !record || record->depth >= 16)
        {
            Fail("Win16 hosted call has no 16-bit stack");
            return 0;
        }
        sp = record->low;
    }
    else
    {
        stack = Register(Reg::Ss);
        sp = (Register(Reg::Esp) & 0xffff) & ~1u;
    }
    if (sp < count * 2 + 256)
    {
        Fail("Win16 hosted call ran out of 16-bit stack");
        return 0;
    }
    if (++depth > 32)
    {
        Fail("Win16 hosted call nesting exceeded");
        --depth;
        return 0;
    }
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
    {
        --depth;
        return 0;
    }
    Check(cpu->Save(context));
    const unsigned original_sp = Register(Reg::Esp);
    const unsigned old_pending = pending;
    const bool old_waiting = waiting;
    waiting = false;
    const unsigned base = Win16Linear(stack, 0);
    sp -= count * 2 + 4;
    for (unsigned i = 0; i < count; ++i)
        Win16Word(base + sp + 4 + (count - 1 - i) * 2, push[i] & 0xffff);
    Win16Word(base + sp, CallbackReturn & 0xffff);
    Win16Word(base + sp + 2, Win16GateSelector());
    const Win16HostedRegisters none;
    const Win16HostedRegisters &r = registers ? *registers : none;
    Register(Reg::Eax, r.ax);
    Register(Reg::Ebx, r.bx);
    Register(Reg::Ecx, r.cx);
    Register(Reg::Edx, r.dx);
    Register(Reg::Esi, r.si);
    Register(Reg::Edi, r.di);
    Register(Reg::Ebp, 0);
    Register(Reg::Ds, ds);
    Register(Reg::Es, r.es);
    if (from_flat)
    {
        // The far return of the stub pops EIP, CS, ESP and SS as dwords,
        // because the stub runs in the flat 32-bit code segment, and it goes
        // to privilege three because the RPL of the new CS says so.
        auto &machine = *win16.state;
        if (!machine.hosted_stub)
        {
            const unsigned request[] = {0, 4096, 0x3000, 0x40};
            machine.hosted_stub = Api("VirtualAlloc", request);
            const unsigned char retf = 0xcb;
            if (!machine.hosted_stub || !Store(machine.hosted_stub, &retf, 1))
            {
                machine.hosted_stub = 0;
                Check(cpu->Restore(context));
                cpu->FreeContext(context);
                --depth;
                Fail("Win16 hosted call stub could not be placed");
                return 0;
            }
        }
        const unsigned frame[] = {far_pointer & 0xffff, far_pointer >> 16, sp, stack};
        Store(machine.hosted_stub + 0x100, frame, sizeof frame);
        record->flat[record->depth++] = context;
        Register(Reg::Esp, machine.hosted_stub + 0x100);
        Register(Reg::Eip, machine.hosted_stub);
    }
    else
    {
        Register(Reg::Ss, stack);
        Register(Reg::Esp, sp);
        Register(Reg::Cs, far_pointer >> 16);
        Register(Reg::Eip, far_pointer & 0xffff);
    }
    const unsigned low = record ? record->low : 0;
    if (record)
        record->low = sp > 64 ? sp - 64 : 0;
    const bool kept = recorded && PushHostFrame(HostWin16HostedCall, low, from_flat, old_waiting, sp);
    const unsigned frame_at = kept ? threads[current_thread].host_frame_count - 1 : 0;
    if (kept) threads[current_thread].host_frames[frame_at].data[0] = count;
    HostedRecord holder{*this, kept};
    auto &thread = threads[current_thread];
    thread.call_marks[depth] = {sp, count * 2, old_pending, context, kept ? TailRecord : 0u,
                               thread.host_frame_count, 3, original_sp,
                               reinterpret_cast<unsigned long long>(__builtin_frame_address(0))};
    ++callbacks;
    // IOPL three, as a Win16 task has it (StartWin16): the hosted code is
    // 16-bit Windows code and uses CLI and STI the same way (SETUPX
    // around its INT 12h vector, under the Add Printer wizard). The flat
    // flags come back with the context on the way out.
    Register(Reg::Eflags, Register(Reg::Eflags) | 0x3000);
    bool returned = false;
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        if (!Step())
            break;
        if (pending == CallbackReturn)
        {
            returned = true;
            break;
        }
        if (pending && !Service())
            break;
    }
    const unsigned result = (Register(Reg::Eax) & 0xffff) | ((Register(Reg::Edx) & 0xffff) << 16);
    // The words the routine took off the stack with its far return: a
    // Pascal routine all of its arguments, a C routine none.
    if (returned && removed_out)
    {
        const unsigned words = (((Register(Reg::Esp) & 0xffff) - sp - 4) & 0xffff) / 2;
        if (words <= count)
            *removed_out = words;
    }
    if (!returned && state != Failed && state != Exited && !threads[current_thread].done)
    {
        char reason[160];
        snprintf(reason, sizeof reason, "Win16 hosted call %04x:%04x did not return", far_pointer >> 16,
                 far_pointer & 0xffff);
        Fail(reason);
    }
    if (kept) FinishWin16HostedCall(frame_at, returned);
    else
    {
        if (record) record->low = low;
        DropWin16HostedContext(context);
    }
    thread.call_marks[depth] = {};
    Check(cpu->Restore(context));
    cpu->FreeContext(context);
    pending = old_pending;
    waiting = old_waiting;
    --depth;
    if (returned_out)
        *returned_out = returned;
    return result;
}

void Application::DropWin16HostedContext(CpuContext *context)
{
    if (!context || !win16.state) return;
    for (auto &record : win16.state->hosted_threads)
    {
        for (auto &flat : record.flat) if (flat == context) flat = nullptr;
        while (record.depth && !record.flat[record.depth - 1]) --record.depth;
    }
}
void Application::FinishWin16HostedCall(unsigned at, bool returned)
{
    auto &t = threads[current_thread];
    const auto frame = t.host_frames[at];
    t.hosted_returned = returned;
    t.hosted_return_words = frame.data[0];
    if (returned)
    {
        const unsigned words = (((Register(Reg::Esp) & 0xffff) - frame.d - 4) & 0xffff) / 2;
        if (words <= frame.data[0]) t.hosted_return_words = words;
    }
    if (auto *record = HostedThreadOf(*win16.state, ThreadId(), false)) record->low = frame.a;
    DropWin16HostedContext(t.call_marks[depth].context);
}
unsigned Application::ResumeWin16HostedCall(unsigned at)
{
    const auto frame = threads[current_thread].host_frames[at];
    HostedRecord record{*this, true};
    const unsigned answer = ResumeInner();
    waiting = frame.c != 0;
    return answer;
}

// The initialization of a library the process hosts: LibMain with the
// registers KRNL386 gives it, DI the instance, CX the local heap size, DS the
// data segment, and then, for a library marked for Windows 4.0 or later, its
// DLLENTRYPOINT export. KRNL386 looks that export up by name and
// calls it with the Pascal arguments dwReason (1, DLL_PROCESS_ATTACH), the
// instance, the data segment, the heap size of the header and three zero
// words for dwReserved1 and wReserved2; a zero answer fails the load.
// The flat thunk DLLs of Win98 (MSVIDEO, DCIMAN and their kin) connect their
// thunk blocks in DLLENTRYPOINT, not in LibMain.
bool Application::Win16HostedEnter(Win16Module &module, unsigned &error)
{
    if (module.entered) return true;
    auto *f = new Win16HostedLoadFrame(*this, module.path);
    unsigned index = unsigned(&module - win16.state->modules);
    const bool answer = ContinueWin16HostedEnter(*f, index, false);
    error = f->error;
    delete f;
    return answer;
}
bool Application::ContinueWin16HostedEnter(Win16HostedLoadFrame &frame, unsigned index, bool resumed)
{
    if (index >= win16.state->module_count) { Fail("A saved library initializer lost its module"); return false; }
    auto &module = win16.state->modules[index];
    auto &t = threads[current_thread];
    unsigned phase = 0, at = 0;
    bool kept = false;
    if (resumed)
    { at = t.host_resume_at++; phase = t.host_frames[at].c; kept = true; }
    else if (frame.recorded)
    { kept = PushHostFrame(HostWin16HostedEnter, frame.key, index); if (kept) at = t.host_frame_count - 1; }
    HostedRecord record{*this, kept};
    auto call = [&](unsigned part, unsigned target, const unsigned *push, unsigned count,
                    const Win16HostedRegisters *registers) {
        if (kept) t.host_frames[at].c = part;
        if (kept) GiveTail(TailRecord);
        bool returned = false;
        const unsigned result = Win16HostedCall(target, push, count, module.instance, registers, &returned) & 0xffff;
        return returned && result;
    };
    if (resumed)
    {
        const unsigned answer = ResumeInner() & 0xffff;
        if (state == Failed || state == Exited || t.done) return false;
        if (!answer) { frame.error = 20; return false; }
    }
    else
    {
        module.entered = true;
        const unsigned segment = module.image.EntrySegment();
        if (segment && segment <= module.segments && module.selector[segment])
        {
            Win16ToolhelpStartDll(module, module.selector[segment] << 16 | module.image.EntryOffset());
            if (state == Failed) return false;
            Win16HostedRegisters registers;
            registers.di = module.instance; registers.cx = module.image.HeapSize(); registers.bx = 1;
            if (!call(1, module.selector[segment] << 16 | module.image.EntryOffset(), nullptr, 0, &registers))
            { if (state != Failed && state != Exited && !t.done) frame.error = 20; return false; }
        }
    }
    if (phase < 2 && module.image.ExpectedVersion() >= 0x400)
    {
        const unsigned number = Win16Ordinal(module, "DLLENTRYPOINT");
        Win98Ne::Entry entry;
        if (number && module.image.EntryAt(number, entry) && entry.segment &&
            entry.segment <= module.segments && module.selector[entry.segment])
        {
            const unsigned push[] = {0, 1, module.instance, module.instance, module.image.HeapSize(), 0, 0, 0};
            if (!call(2, module.selector[entry.segment] << 16 | entry.offset, push, 8, nullptr))
            { if (state != Failed && state != Exited && !t.done) frame.error = 20; return false; }
        }
    }
    if (log)
    {
        char text[320];
        snprintf(text, sizeof text, "WIN16 hosted library %s handle=%04x data=%04x", module.path, module.handle, module.instance);
        log(text);
    }
    return true;
}
unsigned Application::ResumeWin16HostedEnter(unsigned key, unsigned module)
{
    auto *f = HostedLoadOf(this, key);
    if (!f) { Fail("A saved initializer lost its load controller"); return 0; }
    return ContinueWin16HostedEnter(*f, module, true);
}

// LoadLibrary of KRNL386 for a module the process hosts: place the image and
// the libraries it imports, then initialize them, the imported ones first.
unsigned Application::Win16HostedLibrary(const char *path, unsigned &error, Win16HostedLoadFrame *owner)
{
    const bool recorded = TakeTail() == TailRecord;
    auto *f = owner ? owner : new Win16HostedLoadFrame(*this, path);
    f->owns = !owner; f->recorded = recorded; error = f->error = 0;
    auto finish = [&](unsigned result) { error = f->error; if (!owner) delete f; return result; };
    if (!Win16HostedReady()) { f->error = 8; return finish(0); }
    if (auto *found = Win16Find(path)) { ++found->references; return finish(found->handle); }
    unsigned char *bytes = nullptr;
    unsigned length = 0;
    if (unsigned result = Win16ReadFile(path, bytes, length)) { f->error = result; return finish(0); }
    f->first = win16.state->module_count;
    auto *module = Win16Place(path, bytes, length, true, f->error);
    if (!module) { Win16HostedUnwind(f->first); return finish(0); }
    f->handle = module->handle;
    const unsigned answer = ContinueWin16HostedLibrary(*f, false);
    return finish(answer);
}
unsigned Application::ContinueWin16HostedLibrary(Win16HostedLoadFrame &f, bool resumed)
{
    auto &t = threads[current_thread];
    const bool kept = resumed || (f.recorded && PushHostFrame(HostWin16HostedLibrary, f.key));
    if (resumed) ++t.host_resume_at;
    HostedRecord record{*this, kept};
    struct Owner { Win16HostedLoadFrame *frame; bool resumed; ~Owner() { if (resumed && frame->owns) delete frame; } } owner{&f, resumed};
    auto fail = [&] { Win16HostedUnwind(f.first); return 0u; };
    if (resumed)
    {
        const bool answer = ResumeInner() != 0;
        if (!answer) return fail();
        if (f.target) return f.handle;
        ++f.cursor;
    }
    while (f.cursor < win16.state->module_count)
    {
        const unsigned index = f.cursor;
        auto &module = win16.state->modules[index];
        if (module.library && !module.entered && module.handle != f.handle)
            if (!ContinueWin16HostedEnter(f, index, false)) return fail();
        ++f.cursor;
    }
    f.target = true;
    for (unsigned index = 0; index < win16.state->module_count; ++index)
        if (win16.state->modules[index].handle == f.handle)
        {
            if (win16.state->modules[index].entered || ContinueWin16HostedEnter(f, index, false)) return f.handle;
            break;
        }
    return fail();
}
unsigned Application::ResumeWin16HostedLibrary(unsigned key)
{
    auto *f = HostedLoadOf(this, key);
    if (!f) { Fail("A saved library load lost its controller"); return 0; }
    return ContinueWin16HostedLibrary(*f, true);
}

// A LoadLibrary that failed gives back what it placed, the way KRNL386 frees
// the module and the libraries it loaded for it: without this a library
// whose initialization failed stayed in the table, half entered, and the next
// LoadLibrary ran its LibMain after all (MODEM.CPL behind UMDM16).
void Application::Win16HostedUnwind(unsigned first)
{
    auto &machine = *win16.state;
    while (machine.module_count > first && state != Failed)
    {
        Win16Module &last = machine.modules[machine.module_count - 1];
        if (!last.handle || !last.library)
            break;
        last.references = 1;
        const unsigned count = machine.module_count;
        Win16FreeLibrary(last.handle);
        if (machine.module_count == count)
            break;
    }
}
// KERNEL32 ordinal 35 for an NE file that no native catalogue entry carries:
// KRNL386 LoadLibrary answers the module or an error below 32.
unsigned Application::Win16HostedLoad(const char *path)
{
    const bool gate = TakeFresh();
    auto *f = new Win16HostedLoadFrame(*this, path);
    const bool kept = gate && PushHostFrame(HostWin16HostedLoad, f->key);
    HostedRecord record{*this, kept};
    if (kept) GiveTail(TailRecord);
    const unsigned handle = Win16HostedLibrary(f->path, f->error, f);
    unsigned answer = handle ? (Win16HostedInstance(handle) ? Win16HostedInstance(handle) : handle) :
                              f->error && f->error < 32 ? f->error : 2;
    if (log)
    {
        char text[320];
        snprintf(text, sizeof text, "WIN16 LoadLibrary16 %s handle=%04x error=%u", f->path, handle, f->error);
        log(text);
    }
    delete f;
    return answer;
}
unsigned Application::ResumeWin16HostedLoad(unsigned key)
{
    auto *f = HostedLoadOf(this, key);
    if (!f) { Fail("A saved LoadLibrary16 lost its controller"); return 0; }
    HostedRecord record{*this, true};
    const unsigned handle = ResumeInner();
    if (log)
    {
        char text[320];
        snprintf(text, sizeof text, "WIN16 LoadLibrary16 %s handle=%04x error=%u", f->path, handle, f->error);
        log(text);
    }
    const unsigned answer = handle ? (Win16HostedInstance(handle) ? Win16HostedInstance(handle) : handle) :
                                    f->error && f->error < 32 ? f->error : 2;
    delete f;
    return answer;
}
// The ordinal of the export `name` of the hosted module `handle`, zero when
// it has none.
unsigned Application::Win16HostedOrdinal(unsigned handle, const char *name)
{
    char copy[128];
    snprintf(copy, sizeof copy, "%s", name ? name : "");
    if (!win16.state)
        return 0;
    for (unsigned i = 0; i < win16.state->module_count; ++i)
        if (win16.state->modules[i].handle == handle || win16.state->modules[i].instance == handle)
            return Win16Ordinal(win16.state->modules[i], copy);
    return 0;
}
// Whether `handle` names a 16-bit module this 32-bit process hosts.
bool Application::Win16HostedModule(unsigned handle)
{
    if (!handle || !win16.state || !win16.state->hosted)
        return false;
    for (unsigned i = 0; i < win16.state->module_count; ++i)
        if (win16.state->modules[i].handle == handle || win16.state->modules[i].instance == handle)
            return true;
    return false;
}
unsigned Application::Win16HostedProcedure(unsigned handle, unsigned ordinal)
{
    if (!win16.state)
        return 0;
    for (unsigned i = 0; i < win16.state->module_count; ++i)
    {
        Win16Module &module = win16.state->modules[i];
        if (module.handle != handle && module.instance != handle)
            continue;
        Win98Ne::Entry entry;
        if (!module.image.EntryAt(ordinal, entry) || !entry.segment || entry.segment > module.segments ||
            !module.selector[entry.segment])
            return 0;
        return module.selector[entry.segment] << 16 | (entry.offset & 0xffff);
    }
    return 0;
}
unsigned Application::Win16HostedInstance(unsigned handle)
{
    if (!win16.state)
        return 0;
    for (unsigned i = 0; i < win16.state->module_count; ++i)
        if (win16.state->modules[i].handle == handle || win16.state->modules[i].instance == handle)
            return win16.state->modules[i].instance;
    return 0;
}
// The exit procedures of a hosted library whose last reference went
// (Win16FreeLibrary, src/ne_loader.cpp). DLLENTRYPOINT hears
// DLL_PROCESS_DETACH (reason 0) for a module marked for Windows 4.0, the
// counterpart of the attach call above, and WEP gets WEP_FREE_DLL (0).
void Application::Win16HostedExit(Win16Module &module)
{
    if (!module.entered)
        return;
    module.entered = false;
    auto call = [&](const char *name, const unsigned *push, unsigned count) {
        const unsigned number = Win16Ordinal(module, name);
        Win98Ne::Entry entry;
        if (!number || !module.image.EntryAt(number, entry) || !entry.segment || entry.segment > module.segments ||
            !module.selector[entry.segment])
            return;
        bool returned = false;
        Win16HostedCall(module.selector[entry.segment] << 16 | entry.offset, push, count, module.instance, nullptr,
                        &returned);
    };
    if (module.image.ExpectedVersion() >= 0x400)
    {
        const unsigned push[] = {0, 0, module.instance, module.instance, module.image.HeapSize(), 0, 0, 0};
        call("DLLENTRYPOINT", push, 8);
    }
    const unsigned push[] = {0};
    call("WEP", push, 1);
}
} // namespace X86
