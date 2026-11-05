// SPDX-License-Identifier: GPL-2.0-or-later
// TOOLHELP.DLL and VER.DLL for 16-bit callers. Both modules are ours
// (DllRoutes::Win16System).
//
// VER.DLL: the routines src/x86_version16.cpp already serves for the flat
// thunks of VERSION.DLL. The 32-bit stubs push exactly the Pascal frame a
// 16-bit caller pushes (far pointers as dwords, UINT as a word), so a 16-bit
// call hands its argument block to the same provider. In the tree MSVIDEO,
// DDRAW16, WINFILE and MAINCP16 import VER.6, 7, 8, 10 and 11; SETUPX takes
// all eight named routines, 2 and 3 among them, through GetProcAddress.
//
// TOOLHELP.DLL: the four registrations the tree imports, NotifyRegister and
// NotifyUnregister (WSASRV.EXE), InterruptRegister and InterruptUnregister
// (DISPDIB.DLL). TOOLHELP and Windows 98 keep one record per task in a
// list: a second registration of the same task answers FALSE, a task of zero
// is the current one, and Unregister answers whether it found one. The
// records are kept the same way here. What TOOLHELP then does with them,
// calling the notification procedure on task and module events and the
// interrupt procedure on a fault, is not ported: no event reaches a
// registered procedure yet.
#include "win16_machine.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
bool Application::Win16VersionCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    // The retf of each routine in VER.DLL: 2 eight words, 3
    // twelve, 6 and 11 four and eight, 7 eight,
    // 8 and 9 fifteen, 10 four, 20
    // eight. Ordinals 1, 21 and 22 are the DLL entry and the two thunk
    // blocks, nothing a caller calls.
    switch (ordinal)
    {
    case 2: words = 8; break;
    case 3: words = 12; break;
    case 20: words = 8; break;
    case 6: words = 4; break;
    case 7: words = 8; break;
    case 8: words = 15; break;
    case 9: words = 15; break;
    case 10: words = 4; break;
    case 11: words = 8; break;
    default:
    {
        char text[160];
        snprintf(text, sizeof text, "Win16 VER ordinal %u is not ported yet", ordinal);
        Fail(text);
        return false;
    }
    }
    const unsigned block = frame.stack + 4;
    result = VersionInfo16(ordinal, block, true);
    return state != Failed;
}

namespace
{
// Two gates of this port under the module name TOOLHELP, outside the range of
// its ordinals: the end of a task after its NFY_EXITTASK callbacks, and the
// return of an interrupt callback that passes the interrupt on.
constexpr unsigned ExitGate = 0xf001, PassGate = 0xf002;
} // namespace

// The callbacks of NotifyRegister, in the order of TOOLHELP's list: TOOLHELP
// Windows 98 calls each one with (wID, dwData) and stops at the first that answers
// nonzero. A task switch (NFY_TASKIN, NFY_TASKOUT) goes only to a callback
// registered with NF_TASKSWITCH; this port has one task per
// process and raises neither. The callbacks run with the registers of the
// event, on the stack of the task, as TOOLHELP calls them from KERNEL's hook.
void Application::Win16ToolhelpNotify(unsigned id, unsigned data)
{
    if (!win16.state)
        return;
    auto &machine = *win16.state;
    Win16Machine::ToolhelpRecord order[Win16Machine::ToolhelpRecords];
    unsigned count = 0;
    for (const auto &record : machine.toolhelp_notify)
        if (record.task && record.procedure && ((id != 8 && id != 9) || (record.flags & 1)))
            order[count++] = record;
    for (unsigned i = 1; i < count; ++i)
        for (unsigned j = i; j > 0 && order[j - 1].order > order[j].order; --j)
        {
            const auto swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned push[] = {id, data >> 16, data & 0xffff};
        bool returned = false;
        const unsigned answer = Win16HostedCall(order[i].procedure, push, 3, 0, nullptr, &returned) & 0xffff;
        if (!returned || state == Failed || state == Exited || answer)
            return;
    }
}

// NFY_LOADSEG: NFYLOADSEG is dwSize 10h, wSelector, wSegNum (counted from
// one), wType (0 code, 1 data), wcInstance and lpstrModuleName.
void Application::Win16ToolhelpLoadSegment(const Win16Module &module, unsigned number)
{
    if (!win16.state)
        return;
    auto &machine = *win16.state;
    bool any = false;
    for (const auto &record : machine.toolhelp_notify)
        any |= record.task != 0;
    if (!any)
        return;
    if (!machine.toolhelp_block)
        machine.toolhelp_block = Win16GlobalAlloc(0x40, 64); // GMEM_ZEROINIT, fixed
    auto *block = machine.toolhelp_block ? Win16GlobalBlock(machine.toolhelp_block) : nullptr;
    if (!block || !block->memory)
        return;
    Win98Ne::Segment segment;
    const bool data = module.image.SegmentAt(number, segment) && (segment.flags & 1);
    const unsigned at = block->memory;
    Write32(at, 0x10);
    Win16Word(at + 4, module.selector[number]);
    Win16Word(at + 6, number);
    Win16Word(at + 8, data ? 1 : 0);
    Win16Word(at + 10, data ? 1 : 0);
    Win16Word(at + 12, 0x20);
    Win16Word(at + 14, block->selector);
    char name[16] = {};
    memcpy(name, module.name, sizeof name - 1);
    Store(at + 0x20, name, sizeof name);
    Win16ToolhelpNotify(1, block->selector << 16);
}

// NFY_STARTDLL before the initialization entry of a library: NFYSTARTDLL is
// dwSize 0Ah, hModule, wCS and wIP.
void Application::Win16ToolhelpStartDll(const Win16Module &module, unsigned entry)
{
    if (!win16.state)
        return;
    auto &machine = *win16.state;
    bool any = false;
    for (const auto &record : machine.toolhelp_notify)
        any |= record.task != 0;
    if (!any)
        return;
    if (!machine.toolhelp_block)
        machine.toolhelp_block = Win16GlobalAlloc(0x40, 64);
    auto *block = machine.toolhelp_block ? Win16GlobalBlock(machine.toolhelp_block) : nullptr;
    if (!block || !block->memory)
        return;
    const unsigned at = block->memory + 0x30;
    Write32(at, 0x0a);
    Win16Word(at + 4, module.handle);
    Win16Word(at + 6, entry >> 16);
    Win16Word(at + 8, entry & 0xffff);
    Win16ToolhelpNotify(3, block->selector << 16 | 0x30);
}

// The end of a task with NotifyRegister callbacks: INT 21h function 4Ch runs
// inside the engine, where no 16-bit callback can run, so the task goes on
// at a gate that calls them and then ends (ExitGate above). The return
// address of the interrupt stays on the stack as the gate's caller.
bool Application::Win16ToolhelpExit(unsigned code)
{
    (void)code;
    if (!win16.state)
        return false;
    bool any = false;
    for (const auto &record : win16.state->toolhelp_notify)
        any |= record.task != 0;
    if (!any)
        return false;
    const unsigned gate = Win16GatePointer(Win16Gate("TOOLHELP", nullptr, ExitGate));
    if (!gate)
        return false;
    const unsigned ss = Register(Reg::Ss), sp = (Register(Reg::Esp) - 4) & 0xffff, base = Win16Linear(ss, 0);
    Win16Word(base + sp, Register(Reg::Eip) & 0xffff);
    Win16Word(base + ((sp + 2) & 0xffff), Register(Reg::Cs));
    Register(Reg::Esp, (Register(Reg::Esp) & 0xffff0000u) | sp);
    Register(Reg::Cs, gate >> 16);
    Register(Reg::Eip, gate & 0xffff);
    return true;
}

// A fault or trap of 16-bit code with InterruptRegister callbacks: the first
// one in TOOLHELP's list gets the frame of the SDK (return address, AX, the
// interrupt number, a handle, IP, CS and the flags of the interrupt). It
// either handles the interrupt (restores AX, drops the first five words and
// IRETs) or returns with RETF to PassGate, which hands it to the next one.
bool Application::Win16ToolhelpFault(unsigned number)
{
    if (!win16.state)
        return false;
    auto &machine = *win16.state;
    const Win16Machine::ToolhelpRecord *first = nullptr;
    for (const auto &record : machine.toolhelp_interrupt)
        if (record.task && (!first || record.order < first->order))
            first = &record;
    if (!first)
        return false;
    const unsigned pass = Win16GatePointer(Win16Gate("TOOLHELP", nullptr, PassGate));
    if (!pass)
        return false;
    const unsigned ss = Register(Reg::Ss), sp = (Register(Reg::Esp) - 16) & 0xffff, base = Win16Linear(ss, 0);
    const unsigned words[] = {pass & 0xffff,          pass >> 16,     Register(Reg::Eax) & 0xffff, number,
                              first->order,           Register(Reg::Eip) & 0xffff, Register(Reg::Cs),
                              Register(Reg::Eflags) & 0xffff};
    for (unsigned i = 0; i < 8; ++i)
        Win16Word(base + ((sp + 2 * i) & 0xffff), words[i]);
    Register(Reg::Esp, (Register(Reg::Esp) & 0xffff0000u) | sp);
    Register(Reg::Cs, first->procedure >> 16);
    Register(Reg::Eip, first->procedure & 0xffff);
    return true;
}

bool Application::Win16ToolhelpCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    auto &machine = *win16.state;
    auto current_task = [&] {
        unsigned task = machine.task;
        if (machine.hosted)
            for (const auto &record : machine.hosted_threads)
                if (record.thread == ThreadId() && record.task)
                    task = record.task;
        return task;
    };
    auto registered = [&](Win16Machine::ToolhelpRecord *list, unsigned task) -> Win16Machine::ToolhelpRecord * {
        for (unsigned i = 0; i < Win16Machine::ToolhelpRecords; ++i)
            if (list[i].task && list[i].task == task)
                return &list[i];
        return nullptr;
    };
    auto add = [&](Win16Machine::ToolhelpRecord *list, unsigned task, unsigned procedure, unsigned flags) {
        if (!task)
            task = current_task();
        if (registered(list, task))
            return 0u;
        for (unsigned i = 0; i < Win16Machine::ToolhelpRecords; ++i)
            if (!list[i].task)
            {
                list[i] = {task, procedure, flags, ++machine.toolhelp_order};
                return 1u;
            }
        return 0u; // TOOLHELP's LocalAlloc of the record failed
    };
    auto remove = [&](Win16Machine::ToolhelpRecord *list, unsigned task) {
        if (!task)
            task = current_task();
        auto *record = registered(list, task);
        if (!record)
            return 0u;
        *record = {};
        return 1u;
    };
    switch (ordinal)
    {
    case 73: // NotifyRegister(hTask, lpfnCallback, wFlags), retf 8
        words = 4;
        result = add(machine.toolhelp_notify, frame.Word(3), frame.Long(1), frame.Word(0));
        return true;
    case 74: // NotifyUnregister(hTask), retf 2
        words = 1;
        result = remove(machine.toolhelp_notify, frame.Word(0));
        return true;
    case 75: // InterruptRegister(hTask, lpfnIntCallback), retf 6
        words = 3;
        result = add(machine.toolhelp_interrupt, frame.Word(2), frame.Long(0), 0);
        return true;
    case 76: // InterruptUnregister(hTask), retf 2
        words = 1;
        result = remove(machine.toolhelp_interrupt, frame.Word(0));
        return true;
    // The walks of the global heap and of the module list, as MSISYS.OCX of
    // MSINFO32 takes them for "16-bit Modules Loaded". The global
    // heap of this port is not KRNL386's arena chain and a 32-bit process
    // has no module list of 16-bit tasks to show: each walk is empty, which
    // TOOLHELP answers with FALSE.
    case 51: // GlobalFirst(lpGlobal, wFlags), retf 6
    case 52: // GlobalNext(lpGlobal, wFlags), retf 6
    case 54: // GlobalEntryHandle(lpGlobal, hItem), retf 6
        words = 3;
        result = 0;
        return true;
    case 59: // ModuleFirst(lpModule), retf 4
    case 60: // ModuleNext(lpModule), retf 4
        words = 2;
        result = 0;
        return true;
    case ExitGate:
    {
        // The end of a task that INT 21h function 4Ch asked for, moved here
        // so the NFY_EXITTASK callbacks can run (Win16ToolhelpExit).
        const unsigned code = Register(Reg::Eax) & 255;
        Win16ToolhelpNotify(5, code);
        if (state == Failed)
            return false;
        result = Register(Reg::Eax) & 0xffff;
        exit_code = code;
        state = Exited;
        if (user_initialized)
            User::DropPointerLevel(process_id, 0); // USER,
        if (user_initialized)
            User::DestroyProcessWindows(process_id); // USER
        return true;
    }
    case PassGate:
    {
        // An interrupt callback that passed the interrupt on returned with
        // `retf` to here: SS:SP holds AX, the interrupt number, the handle
        // (the place of the callback in the list), IP, CS and the flags. The
        // next callback gets the same frame; after the last one the fault
        // stands where it was, and the port stops with its diagnosis.
        const unsigned ss = Register(Reg::Ss), sp = Register(Reg::Esp) & 0xffff;
        const unsigned base = Win16Linear(ss, 0);
        const unsigned ax = Win16Word(base + sp), number = Win16Word(base + ((sp + 2) & 0xffff)),
                       handle = Win16Word(base + ((sp + 4) & 0xffff));
        const unsigned ip = Win16Word(base + ((sp + 6) & 0xffff)), cs = Win16Word(base + ((sp + 8) & 0xffff)),
                       flags = Win16Word(base + ((sp + 10) & 0xffff));
        const Win16Machine::ToolhelpRecord *next = nullptr;
        for (const auto &record : machine.toolhelp_interrupt)
            if (record.task && record.order > handle && (!next || record.order < next->order))
                next = &record;
        result = ax | (Register(Reg::Edx) & 0xffff) << 16;
        if (next)
        {
            const unsigned pass = Win16GatePointer(Win16Gate("TOOLHELP", nullptr, PassGate));
            const unsigned top = (sp - 4) & 0xffff;
            Win16Word(base + ((sp + 4) & 0xffff), next->order);
            Win16Word(base + top, pass & 0xffff);
            Win16Word(base + ((top + 2) & 0xffff), pass >> 16);
            machine.throw_pending = 1;
            machine.throw_ss = ss;
            machine.throw_sp = top;
            machine.throw_cs = next->procedure >> 16;
            machine.throw_ip = next->procedure & 0xffff;
            return true;
        }
        Register(Reg::Eax, (Register(Reg::Eax) & 0xffff0000u) | ax);
        Register(Reg::Esp, (Register(Reg::Esp) & 0xffff0000u) | ((sp + 12) & 0xffff));
        Register(Reg::Cs, cs);
        Register(Reg::Eip, ip);
        Register(Reg::Eflags, (Register(Reg::Eflags) & 0xffff0000u) | flags);
        return Win16FaultStop(number);
    }
    default:
    {
        char text[160];
        snprintf(text, sizeof text, "Win16 TOOLHELP ordinal %u is not ported yet", ordinal);
        Fail(text);
        return false;
    }
    }
}
} // namespace X86
