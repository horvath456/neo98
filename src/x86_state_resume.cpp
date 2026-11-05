// SPDX-License-Identifier: GPL-2.0-or-later
// A thread out of a saved state that stood inside callbacks, or inside a
// call with a loop of its own (a modal dialog): the frames of the host it
// had between the levels of the guest are entered again.
// New infrastructure of this port.
// A state holds no host stack. What it holds of a thread: the registers of
// the innermost level of the guest, for every callback the mark of Call
// (the registers of the level below, and what the host has left to do when
// the callback returns), and a record for every frame of the host that
// loops (Thread::host_frames). From those the host stack is built again
// from the outside in, with the real functions: the loop of Call for every
// level (ResumeCall), the modal loop for every record (ResumeHostCall), and
// the end of every gate (FinishGate). A frame that has more to do after its
// callback than a mark or a record says is not built again, and a state is
// not taken while a thread has one (MachineState, ThreadStands).
#include "x86_app.h"
#include <string.h>
#include <strings.h> // strcasecmp is declared separately by newlib.
namespace X86
{
namespace
{
// The gate region and the return mark of a callback, flat and 16-bit
// (src/x86_app.cpp, src/win16_user.cpp).
constexpr unsigned Gates = Application::GateBase, Callback = 0x7000fff0;
}
// The calls a state can be taken inside of, by name. Only the handler of
// such a gate is told that it is the gate's (TailFresh), so a function of
// the host that uses the same code on its own way is never taken for one.
bool Application::TailGate(const char *dll, const char *name, unsigned id, bool win16)
{
    if (win16)
    {
        if (!strcmp(dll, "COMMDLG")) return id == 1 || id == 2 || id == 5 || id == 15 || id == 20;
        if (!strcmp(dll, "COMMCTRL")) return id == 159 || id == 161;
        if (strcmp(dll, "USER"))
            return false;
        switch (id)
        {
        case 1:   // MessageBox
        case 41:  // CreateWindow
        case 87:  // DialogBox
        case 107: // DefWindowProc, only without staged control translation
        case 111: // SendMessage
        case 114: // DispatchMessage
        case 122: // CallWindowProc
        case 218: // DialogBoxIndirect
        case 239: // DialogBoxParam
        case 240: // DialogBoxIndirectParam
        case 452: // CreateWindowEx
            return true;
        default:
            return false;
        }
    }
    static const char *const names[] = {"DispatchMessageA", "SendMessageA",   "CallWindowProcA",        "MessageBoxA",
                                        "MessageBoxExA",    "DialogBoxParamA", "DialogBoxIndirectParamA",
                                        "TrackPopupMenu", "TrackPopupMenuEx", "DefWindowProcA", "CreateWindowExA", "EndTask", "GetMessageA", "PeekMessageA",
                                        "CreateDialogParamA", "CreateDialogIndirectParamA", "SendMessageTimeoutA"};
    if (!strcasecmp(dll, "COMDLG32.DLL"))
        return !strcmp(name, "GetOpenFileNameA") || !strcmp(name, "GetSaveFileNameA");
    if (!strcasecmp(dll, "COMCTL32.DLL"))
        return !strcmp(name, "PropertySheet") || !strcmp(name, "PropertySheetA") || !strcmp(name, "PropertySheetW") ||
            !strcmp(name,"CreatePropertySheetPageA") || !strcmp(name,"CreatePropertySheetPageW") ||
            !strcmp(name,"CreatePropertySheetPage") || !strcmp(name,"DestroyPropertySheetPage");
    if (!strcasecmp(dll, "KERNEL32.DLL") && (!strcmp(name, "CommConfigDialogA") || !strcmp(name, "ordinal_35"))) return true;
    if (strcasecmp(dll, "USER32.DLL"))
        return false;
    for (const char *known : names)
        if (!strcmp(name, known))
            return true;
    return false;
}
bool Application::ResumeThread()
{
    auto &t = threads[current_thread];
    if (SavedFaultBoxThread(current_thread)) return ResumeFaultBox();
    t.host_resume = t.host_frame_count != 0;
    t.host_resume_at = 0;
    t.host_resume_top = depth;
    t.host_resume_level = 0;
    t.tail = TailNone;
    const bool went = ResumeGuest(0);
    t.host_resume = false;
    return went && state != Failed && state != Exited;
}
// The guest of one level goes on where it stood. On return the loop that
// runs this level (of Run, of Call) takes over.
bool Application::ResumeGuest(unsigned level)
{
    auto &t = threads[current_thread];
    depth = level;
    if (level == t.host_resume_top)
    {
        // The innermost level: its registers are on the processor. It
        // stands between two instructions, or in a call that is made again
        // from its gate (a wait), or in one whose records are still to be
        // taken (Dispatch enters them in the place of the call).
        if (t.host_resume_at >= t.host_frame_count)
            t.host_resume = false;
        if (!pending)
            return true;
        return StateResume();
    }
    // This level made a call that called back: first everything above it.
    const auto mark = t.call_marks[level + 1];
    const unsigned outer = t.host_resume_level;
    t.host_resume_level = level;
    unsigned value = 0;
    const bool records = t.host_resume && t.host_resume_at < mark.frames;
    if (records)
        value = ResumeHostCall(); // the handler of the gate, by its record
    else if (mark.tail == TailResult || mark.tail == TailZero)
        value = ResumeCall(level + 1);
    else
    {
        Fail("A thread of a saved state stands in a callback that cannot be taken up");
        return false;
    }
    t.host_resume_level = outer;
    if (state == Failed || state == Exited || t.done)
        return false;
    // The registers stand at the gate of the call again.
    depth = level;
    if (records && mark.tail == TailAgain)
    {
        // Delivery belonged to a restartable USER wait. It has finished its
        // own work; enter that wait again, using the restored pending gate.
        if (!SentWaitGate(mark.gate)) { Fail("A resumed send has no restartable USER wait"); return false; }
        if (t.host_resume_at >= t.host_frame_count) t.host_resume = false;
        return StateResume();
    }
    return FinishGate(mark.gate, !records && mark.tail == TailZero ? 0 : value);
}
// What a frame out of a record had called when the state was taken: the
// next record, or the callback of the next level.
unsigned Application::ResumeInner()
{
    auto &t = threads[current_thread];
    const unsigned level = t.host_resume_level;
    const unsigned limit = level < t.host_resume_top ? t.call_marks[level + 1].frames : t.host_frame_count;
    if (t.host_resume && t.host_resume_at < limit)
        return ResumeHostCall();
    if (level < t.host_resume_top)
        return ResumeCall(level + 1);
    Fail("A thread of a saved state has nothing above a frame that called");
    return 0;
}
// The loop of Call and of Win16Invoke, entered with the callback already
// running: the rest of the callback, then what those two do after it.
unsigned Application::ResumeCall(unsigned level)
{
    auto &t = threads[current_thread];
    t.call_marks[level].host_frame = reinterpret_cast<unsigned long long>(__builtin_frame_address(0));
    const auto mark = t.call_marks[level];
    if (!mark.context || level > t.host_resume_top)
    {
        Fail("A thread of a saved state stands in a callback without its registers");
        return 0;
    }
    const unsigned outer = t.host_resume_level;
    t.host_resume_level = level;
    bool returned = false;
    if (ResumeGuest(level))
        while (state != Failed && state != Exited && !t.done)
        {
            if (pending == Callback) { returned = true; break; }
            if (!Step()) break;
            if (direct_break) { direct_break = false; break; }
            if (pending == Callback) { returned = true; break; }
            if (pending && !Service()) break;
        }
    t.host_resume_level = outer;
    if (!returned || state == Failed || state == Exited)
    {
        if (state != Failed && state != Exited && !t.done)
            Fail("A callback taken up from a saved state was interrupted");
        // ExitThread/_endthreadex may end the resumed callback without
        // returning through Callback. Its Call mark still belongs to this
        // frame, just as in the original Call's cleanup on thread exit.
        if (t.done)
        {
            if (mark.win16 & 2) FinishWin16HostedCall(mark.frames - 1, false);
            Check(cpu->Restore(mark.context));
            cpu->FreeContext(mark.context);
            t.call_marks[level] = {};
            pending = mark.gate;
            waiting = false;
            depth = level - 1;
            t.depth = depth;
        }
        return 0;
    }
    const unsigned result = mark.win16 ? (Register(Reg::Eax) & 0xffff) | ((Register(Reg::Edx) & 0xffff) << 16)
                                       : Register(Reg::Eax);
    if (mark.win16 & 2) FinishWin16HostedCall(mark.frames - 1, true);
    else LoadError();
    Check(cpu->Restore(mark.context));
    cpu->FreeContext(mark.context);
    t.call_marks[level] = {};
    pending = mark.gate;
    waiting = false;
    dispatching_posted = false;
    depth = level - 1;
    // A finished fiber does not yield again to copy this into Thread.
    // Its stored depth must follow the released Call mark as well.
    t.depth = depth;
    if (mark.win16)
        Register(Reg::Esp, mark.saved_sp); // Win16Deliver
    return result;
}
// The end of a gate whose handler was entered again: what Dispatch and
// Win16Transition do after the handler.
bool Application::FinishGate(unsigned gate, unsigned value)
{
    if (gate < Gates || gate >= Gates + import_count * 16 || ((gate - Gates) & 15))
    {
        Fail("A thread of a saved state stands in a callback of no gate");
        return false;
    }
    auto &item = imports[(gate - Gates) / 16];
    if (Win16Process() || Win16Code())
    {
        const unsigned words = Win16StateWords(item);
        const unsigned sp = Register(Reg::Esp) & 0xffff, stack = Win16Linear(Register(Reg::Ss), sp);
        const unsigned ip = Win16Word(stack), cs = Win16Word(stack + 2);
        Win16GateReturn(sp, cs, ip, words, value);
        return state != Failed && state != Exited;
    }
    StoreError();
    const unsigned sp = Register(Reg::Esp), count = item.argc & 255;
    Register(Reg::Eax, value);
    Register(Reg::Esp, sp + 4 + ((item.argc & 0x80000000u) ? 0 : count * 4));
    Register(Reg::Eip, Read32(sp));
    pending = 0;
    return state != Failed && state != Exited;
}
unsigned Application::Win16StateWords(const Import &item) const
{
    if (!strcmp(item.dll, "COMMDLG") && (item.id == 1 || item.id == 2)) return 2;
    if (!strcmp(item.dll, "COMMCTRL") && (item.id == 159 || item.id == 161)) return 2;
    const char *name = nullptr;
    return Win16UserWords(item.id, name);
}
} // namespace X86
