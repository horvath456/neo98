#include "x86_cpu_state.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// The machine in a saved state: the parts of the stream, and what a state
// needs besides the fields of the port (src/x86_state_fields.cpp). The
// processes, their processors with the guest memory, the threads, and the
// objects of the host they reach.
// New infrastructure of this port.
// A thread is not saved with its host stack. It is saved where the host
// stack holds nothing that matters: at the end of a time slice, between two
// instructions of the guest, or inside a call of the system that can be made
// again from its first instruction (a wait). Its fiber is made anew on a
// load and goes on from the registers (Application::Run, state_resumed).
// CanSave says whether every thread stands like that.
#include "x86_app.h"
#include "x86_state_fields.h"
#include "x86_state_dos_ui.h"
#include "x86_state_parts.h"
#include "win16_machine.h"
#include "truetype_machine.h"
#include "sound.h"
#if defined(__APPLE__) || defined(__linux__)
#include <new>
#else
#include <circle/new.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <map>
#include <memory>
#include <string>
#include <vector>
#endif
// Of the process, not of the machine (src/state_rules/state.rules): a state
// saved while it was set would stop its threads at every call for ever.
bool X86::Application::state_point_wanted = false;
namespace MachineState
{
namespace
{
constexpr unsigned long long StreamMagic = 0x4554415453383941ull; // "A98STATE"
constexpr unsigned ProcessCapacity = 64;
// The calls a thread may stand in. Each of them only waits: it has changed
// nothing by the time it suspends the thread, and made again it waits again
// (a Sleep for its whole time, a wait with its whole timeout).
const char *const restartable[] = {
    "GetMessageA", "GetMessageW", "PeekMessageA", "PeekMessageW", "WaitMessage",
    "MsgWaitForMultipleObjects", "MsgWaitForMultipleObjectsEx",
    "WaitForSingleObject", "WaitForSingleObjectEx", "WaitForMultipleObjects", "WaitForMultipleObjectsEx",
    "Sleep", "SleepEx", "EnterCriticalSection",
    // Of the DirectDraw driver: the wait for the retrace, and a Lock of the
    // screen that waits for those who draw on it (it changes nothing before
    // it waits, so that Dispatch can enter it again).
    "WaitForVerticalBlank", "Lock",
};
// The same of the 16-bit USER, by ordinal: GetMessage, PeekMessage,
// WaitMessage. A gate that waits is entered again by design
// (Application::Win16Transition).
const unsigned restartable_user16[] = {108, 109, 112};
// Every register of the interface, in the order a new processor takes them.
using Registers = X86::CpuPortableRegisters;
void ReadRegisters(X86::Cpu &cpu, Registers &r) { X86::ReadCpuPortableRegisters(cpu, r); }
void WriteRegisters(X86::Cpu &cpu, const Registers &r) { X86::WriteCpuPortableRegisters(cpu, r); }
void Text(char *out, size_t bytes, const char *text)
{
    if (out && bytes)
        snprintf(out, bytes, "%s", text);
}
} // namespace

struct Machine
{
    using Application = X86::Application;
    // Every process of the session, the first one first.
    static unsigned Processes(Application &first, Application **out, unsigned capacity)
    {
        unsigned count = 0;
        out[count++] = &first;
        for (auto *process : Application::session_processes)
            if (process && process != &first && count < capacity)
                out[count++] = process;
        return count;
    }
    static void WantSavePoint(bool wanted) { Application::state_point_wanted = wanted; }
    static const char *ImportName(Application &a, unsigned gate)
    {
        if (gate < Application::GateBase || ((gate - Application::GateBase) & 15))
            return nullptr;
        const unsigned slot = (gate - Application::GateBase) / 16;
        return slot < a.import_count ? a.imports[slot].name : nullptr;
    }

    // ---- Can the machine be saved now? ----------------------------------------
    // Null when the thread can be taken up again from its registers.
    // Whether a call a thread stands in lasts until the person does
    // something: a modal dialog, a menu. By the name of the import, and for
    // a 16-bit program by its module and ordinal.
    static Verdict::Why Lasting(Application &a, unsigned gate)
    {
        const char *name = ImportName(a, gate);
        if (!name)
            return Verdict::Busy;
        static const char *const dialogs[] = {"MessageBox", "DialogBox", "GetOpenFileName", "GetSaveFileName", "ChooseColor",
                                              "ChooseFont", "PrintDlg", "PageSetupDlg", "PropertySheet", "ShellAbout",
                                              "SHBrowseForFolder", "FindText", "ReplaceText"};
        for (const char *dialog : dialogs)
            if (!strncmp(name, dialog, strlen(dialog)))
                return Verdict::Dialog;
        if (!strncmp(name, "TrackPopupMenu", 14))
            return Verdict::Menu;
        if (a.Win16Process())
        {
            const auto &item = a.imports[(gate - Application::GateBase) / 16];
            // MessageBox, DialogBox, DialogBoxIndirect, DialogBoxParam, DialogBoxIndirectParam; the common dialogs.
            if (!strcmp(item.dll, "COMMDLG") ||
                (!strcmp(item.dll, "USER") && (item.id == 1 || item.id == 87 || item.id == 218 || item.id == 239 || item.id == 240)))
                return Verdict::Dialog;
            if (!strcmp(item.dll, "USER") && item.id == 416)
                return Verdict::Menu;
        }
        return Verdict::Busy;
    }
#if defined(__APPLE__) || defined(__linux__)
    // The host functions each waiting thread of a process stands in, read
    // off the frame pointers of its fiber.
    static void Chain(Application &a, FILE *out, FrameVisitor visitor = nullptr, void *context = nullptr)
    {
        for (unsigned i = 0; i < 16; ++i)
        {
            auto &t = a.threads[i];
            if (!t.fiber || t.fiber->Finished() || !t.fiber->Started())
                continue;
            if (visitor)
            {
                char reason[256];
                Verdict::Why kind;
                if (ThreadStands(a, i, reason, sizeof reason, kind)) continue;
            }
            unsigned long long frame = 0, address = 0;
            t.fiber->SavedFrame(frame, address);
            const unsigned long long low = reinterpret_cast<unsigned long long>(t.fiber->StackBase()),
                                     high = low + t.fiber->StackBytes();
            const char *pending = ImportName(a, t.pending);
            if (out) fprintf(out, "NEO98 STATE chain %s thread %u depth %u pending %s:", a.program_name, i, t.depth, pending ? pending : "-");
            const char *last = nullptr;
            auto walk = [&](unsigned long long frame, unsigned long long address) {
            for (unsigned steps = 0; steps < 96 && address; ++steps)
            {
                // Mangled as it is: the reader takes the names apart. Code
                // without a symbol is the engine's translation of the guest.
                const char *name = SymbolOf(address);
                if (!name)
                    name = "(guest)";
                if (out && name != last)
                    fprintf(out, " %s", name);
                if (visitor && name != last) visitor(context, a.program_name, i, name);
                last = name;
                if (frame < low || frame + 16 > high || (frame & 7))
                    break;
                const unsigned long long next = reinterpret_cast<const unsigned long long *>(frame)[0];
                address = reinterpret_cast<const unsigned long long *>(frame)[1];
                if (next <= frame)
                    break;
                frame = next;
            }
            };
            walk(frame, address);
            // FEX's translated guest does not form a host frame-pointer
            // chain back into Call. Each callback kept that bridge before
            // entering the engine, so also visit the frames below it.
            for (unsigned level = t.depth; level; --level)
            {
                const auto anchor = t.call_marks[level].host_frame;
                if (anchor >= low && anchor + 16 <= high && !(anchor & 7))
                    walk(reinterpret_cast<const unsigned long long *>(anchor)[0],
                         reinterpret_cast<const unsigned long long *>(anchor)[1]);
            }
            if (out) fprintf(out, "\n");
        }
    }
#endif
    // Whether the frames of the host a thread has can be built again
    // (src/x86_state_resume.cpp): every callback is the last thing its
    // caller does or the caller is a record, every modal loop has a caller
    // that keeps its work in a record, and the innermost frame waits in its
    // loop or is no frame at all. The level whose caller fails comes back
    // in `level` (zero: the call the innermost level stands in).
    static bool HostFramesResumable(Application &a, const Application::Thread &t, unsigned &level)
    {
        for (unsigned i = 0; i < t.host_frame_count; ++i)
        {
            const auto &f = t.host_frames[i];
            const unsigned thread=unsigned(&t-a.threads);
            bool property=true;
            switch(f.kind){
            case Application::HostWin16HostedLoad:
            case Application::HostWin16HostedLibrary:
            case Application::HostWin16HostedEnter:
                property=a.Win16HostedWorkRecorded(f.a,f.kind,f.b,f.c);break;
            case Application::HostWin16HostedCall:
                property=f.b<=1&&f.c<=1&&f.data[0]<=32767&&i&&
                    t.host_frames[i-1].kind==Application::HostWin16HostedEnter;break;
            case Application::HostPropertySheet:
                property=(f.d==1&&i+1<t.host_frame_count&&
                    (t.host_frames[i+1].kind==Application::HostModal||t.host_frames[i+1].kind==Application::HostPropertyModalFinish)&&
                    t.host_frames[i+1].a==f.b)||((f.d==2||f.d==3||f.d==4)&&a.PropertyWorkRecorded(thread,i,f.kind,f.d));break;
            case Application::HostPropertyNotify:case Application::HostPropertyRelease:case Application::HostPropertyQuery:
                property=f.d==1&&a.PropertyWorkRecorded(thread,i,f.kind,f.d);break;
            case Application::HostPropertyProcedure:
                property=(f.d==1||f.d==2)&&a.PropertyWorkRecorded(thread,i,f.kind,f.d);break;
            case Application::HostPropertyReleaseInput:
                property=f.d==1&&i&&f.b<=4096&&t.host_frames[i-1].kind==Application::HostPropertyApi&&t.host_frames[i-1].d==3&&t.host_frames[i-1].data[0]==f.a;break;
            case Application::HostWin16FileMessage:case Application::HostWin16FileAccept:case Application::HostWin16FileOutput:
            case Application::HostWin16FileSelect:case Application::HostWin16FileDrive:
                property=a.Win16FileWorkRecorded(thread,i,f.kind,f.d);break;
            case Application::HostFileDialog:
                property=!f.b||!f.d||(f.d==1&&i+1<t.host_frame_count&&t.host_frames[i+1].kind==Application::HostDialogInit);break;
            case Application::HostPropertyApi:
                property=f.d==1||f.d==2||(f.d==3&&i+1<t.host_frame_count&&t.host_frames[i+1].kind==Application::HostPropertyReleaseInput&&t.host_frames[i+1].a==f.data[0]);break;
            case Application::HostPropertyPageWindow:
            case Application::HostPropertyAdd:
                property=(f.d==1||f.d==2)&&a.PropertyWorkRecorded(thread,i,f.kind,f.d);break;
            case Application::HostPropertyActivate:
            case Application::HostPropertyCommand:case Application::HostPropertyApply:
            case Application::HostPropertyRemoveRequest:
                property=f.d>=1&&f.d<=3&&a.PropertyWorkRecorded(thread,i,f.kind,f.d);break;
            case Application::HostPropertyInit:
                property=f.d>=1&&f.d<=4&&a.PropertyWorkRecorded(thread,i,f.kind,f.d);break;
            case Application::HostPropertyRemove:
                property=(f.d==2||(f.d==1&&i+1<t.host_frame_count&&t.host_frames[i+1].kind==Application::HostPropertyPageNonClient&&t.host_frames[i+1].a==f.b))&&a.PropertyWorkRecorded(thread,i,f.kind,f.d);break;
            case Application::HostPropertyPageNonClient:
                property=f.d==1&&i&&t.host_frames[i-1].kind==Application::HostPropertyRemove&&t.host_frames[i-1].d==1&&t.host_frames[i-1].b==f.a&&t.host_frames[i-1].a==f.b;break;
            case Application::HostPropertyModalFinish:
                property=f.d==1;break;
            case Application::HostPropertyNonClient:
                property=f.d==1&&i&&t.host_frames[i-1].kind==Application::HostPropertyModalFinish&&t.host_frames[i-1].a==f.a;break;
            }
            // A caller of the loop that keeps its work on the stack.
            if ((f.kind == Application::HostModal && !(f.d & 2)) || f.kind == Application::HostNone ||
                ((f.kind == Application::HostSerialAdvanced ||
                  f.kind == Application::HostCloseProgram || f.kind == Application::HostPifSheet ||
                  f.kind == Application::HostPifAdvanced) && f.d != 1) || !property)
            {
                level = 0;
                for (unsigned k = 1; k <= t.depth; ++k)
                    if (t.call_marks[k].frames <= i)
                        level = k;
                level = level < t.depth ? level + 1 : 0;
                return false;
            }
        }
        for (unsigned k = 1; k <= t.depth && k < 33; ++k)
        {
            const auto &mark = t.call_marks[k];
            const unsigned below = k > 1 ? t.call_marks[k - 1].frames : 0;
            const bool records = mark.frames > below;
            level = k;
            const bool crt_entry = k == 1 && records && t.host_frames[0].kind == Application::HostCrtThread;
            const bool dos_entry = k == 1 && records && t.app && t.app->SavedDosUiThread(0) &&
                                   &t == &t.app->threads[0];
            const bool close_entry = k == 1 && records && t.host_frames[0].kind == Application::HostCloseProgram &&
                                     t.host_frames[0].d == 1;
            const bool hosted = (mark.win16 & 2) && mark.frames && mark.frames <= t.host_frame_count &&
                t.host_frames[mark.frames-1].kind == Application::HostWin16HostedCall;
            if ((mark.win16 & 2) && !hosted) return false;
            if (!mark.context || (!mark.gate && !crt_entry && !close_entry && !dos_entry))
                return false;
            const bool sent_again = records && mark.tail == Application::TailAgain &&
                (mark.frames == below + 1 || (mark.frames == below + 2 &&
                  t.host_frames[below + 1].kind == Application::HostWin16Delivery)) &&
                t.host_frames[below].kind == Application::HostSentDelivery &&
                a.SentWaitGate(mark.gate);
            if (records ? mark.tail != Application::TailRecord && !sent_again
                        : mark.tail != Application::TailResult && mark.tail != Application::TailZero)
                return false;
        }
        level = 0;
        const unsigned below = t.depth ? t.call_marks[t.depth].frames : 0;
        if (t.host_frame_count > below)
            return t.host_waiting && (t.host_frames[t.host_frame_count - 1].kind == Application::HostModal ||
                                     t.host_frames[t.host_frame_count - 1].kind == Application::HostMenu ||
                                     t.host_frames[t.host_frame_count - 1].kind == Application::HostFrameTrack ||
                                     t.host_frames[t.host_frame_count - 1].kind == Application::HostSentWait);
        return !t.host_waiting;
    }
    static bool ThreadStands(Application &a, unsigned index, char *why, size_t bytes, Verdict::Why &kind)
    {
        auto &t = a.threads[index];
        kind = Verdict::Busy;
        if (!a.Win16Process() && a.win16.state && a.win16.state->hosted)
        {
            auto flat = [&](const X86::CpuContext *context) {
                if (!context) return true;
                unsigned cs = 0;
                return !a.cpu->ContextStateRefusal(context) &&
                       a.cpu->ContextGet(context, X86::Reg::Cs, cs) == X86::CpuError::Ok &&
                       (!(cs & 4) || a.cpu->ContextStateBytes(context));
            };
            if (!flat(t.context))
            {
                snprintf(why, bytes, "%s thread %u still has a 16-bit context in the second engine", a.program_name, index);
                return false;
            }
            for (unsigned level = 1; level <= t.depth && level < 33; ++level)
                if (!flat(t.call_marks[level].context))
                {
                    snprintf(why, bytes, "%s thread %u callback level %u still has a context in the second engine",
                             a.program_name, index, level);
                    return false;
                }
        }
        if (!t.fiber || t.fiber->Finished() || !t.fiber->Started())
            return true;
        if (a.SavedFaultBoxThread(index)) return true;
        const unsigned eip = a.ThreadEip(index);
        unsigned level = 0;
        const bool frames = HostFramesResumable(a, t, level);
        if (a.SavedDosUiThread(index) && !t.depth && !t.pending && t.host_frame_count == 1 && t.block == 7) return true;
        if (t.depth && level)
        {
            // A callback whose caller has work left that no mark and no
            // record holds.
            const char *outer = ImportName(a, t.call_marks[level].gate), *name = ImportName(a, t.pending);
            kind = Lasting(a, t.call_marks[1].gate);
            for (unsigned k = 2; k <= t.depth && kind == Verdict::Busy; ++k)
                kind = Lasting(a, t.call_marks[k].gate);
            if (kind == Verdict::Busy && t.pending)
                kind = Lasting(a, t.pending);
            snprintf(why, bytes, "%s thread %u is inside a callback of %s (level %u of %u%s%s)", a.program_name, index,
                     outer ? outer : "the system", level, t.depth, name ? ", in " : "", name ? name : "");
            return false;
        }
        if (eip == 0x71000f20)
        {
            // The multimedia thread of the port between two turns of its loop.
            if (t.block == 3 && !t.depth)
                return true;
            snprintf(why, bytes, "%s thread %u, the multimedia thread, is in the middle of a turn", a.program_name, index);
            return false;
        }
        if (eip == 0x71000f10 || eip == 0x71000f30 || eip == Application::CloseProgramThread)
        {
            if (eip == Application::CloseProgramThread && frames && a.SavedCloseProgramThread(index)) return true;
            snprintf(why, bytes, "%s thread %u is a thread of the port that keeps its state on the host stack (%08x)",
                     a.program_name, index, eip);
            return false;
        }
        if (t.pulse_handle)
        {
            snprintf(why, bytes, "%s thread %u was woken by a PulseEvent it has not seen yet", a.program_name, index);
            return false;
        }
        if (!t.pending && frames)
            return true; // the end of a time slice, between two guest instructions
        const char *name = ImportName(a, t.pending);
        kind = Lasting(a, t.pending);
        // A call whose frames on the host stack the thread carries as
        // records, waiting in the loop of the innermost one (a modal
        // dialog): taken up again after a load (Application::ResumeHostCall).
        if (name && frames && t.host_waiting)
            return true;
        // The registers stand at the gate: in a flat process its address,
        // in a 16-bit one an offset in the segment of the thunks.
        if (name && a.Win16Process())
        {
            const auto &item = a.imports[(t.pending - Application::GateBase) / 16];
            if (!strcmp(item.dll, "USER"))
                for (unsigned ordinal : restartable_user16)
                    if (item.id == ordinal)
                        return true;
            snprintf(why, bytes, "%s thread %u is inside %s.%u", a.program_name, index, item.dll, item.id);
            return false;
        }
        if (name && eip >= Application::GateBase && eip < Application::GateBase + Application::GateRegionBytes)
        {
            for (const char *known : restartable)
                if (!strcmp(known, name))
                    return true;
            // A thread that suspended itself and waits to be resumed: the
            // rest of the call is taken up on a load (Application::StateResume).
            if (!strcmp(name, "SuspendThread") && t.block == 6 && t.suspended)
                return true;
        }
        snprintf(why, bytes, "%s thread %u is inside %s", a.program_name, index, name ? name : "a call of the system");
        return false;
    }
    static Verdict CanSave(Application &first, bool dos_ui = false)
    {
        Verdict verdict;
        const char *why = nullptr;
        if (!Available(&why))
        {
            Text(verdict.reason, sizeof verdict.reason, why);
            verdict.why = Verdict::NoTable;
            return verdict;
        }
        Application *processes[ProcessCapacity];
        const unsigned count = Processes(first, processes, ProcessCapacity);
        if (dos_ui && (count != 1 || !first.DosUiStateReady(verdict.reason, sizeof verdict.reason)))
        { verdict.why = Verdict::DosProgram; if (!verdict.reason[0]) Text(verdict.reason, sizeof verdict.reason, "the combined provider requires exactly one DOS process"); return verdict; }
        // What running on does not change.
        for (unsigned p = 0; p < count; ++p)
        {
            auto &a = *processes[p];
            const auto engine = a.cpu ? a.cpu->Backend() : X86::CpuBackend::None;
            const char *refusal = !a.cpu ? "it has no processor"
                                  : a.dos_vm && !dos_ui ? "it is a DOS program (the DOS machine has its own states)"
                                  : a.Arm64Process() ? "it is an ARM64 program (its machine is not saved)"
                                  : a.Win16Process() && engine != X86::CpuBackend::Unicorn && engine != X86::CpuBackend::Native ? "a 16-bit program on an unknown engine"
                                  : !a.Win16Process() && !(dos_ui && a.dos_vm) && engine != X86::CpuBackend::Fex && engine != X86::CpuBackend::Native ? "a 32-bit program on an unsupported engine"
                                  : a.cpu->StateRefusal();
            if (refusal)
            {
                snprintf(verdict.reason, sizeof verdict.reason, "%s: %s", a.program_name, refusal);
                verdict.why = !a.cpu ? Verdict::Ended : a.dos_vm ? Verdict::DosProgram : a.Arm64Process() ? Verdict::Arm64Program
                              : Verdict::Engine;
                if (a.cpu && a.win16.state && a.win16.state->hosted &&
                    !strcmp(refusal, "16-bit code is active in the process (second engine)"))
                    verdict.transient = true;
                return verdict;
            }
            if (!a.Win16Process() && a.win16_flat_template)
            {
                unsigned cs = 0;
                if (a.cpu->ContextStateRefusal(a.win16_flat_template) ||
                    a.cpu->ContextGet(a.win16_flat_template, X86::Reg::Cs, cs) != X86::CpuError::Ok || (cs & 4))
                {
                    snprintf(verdict.reason, sizeof verdict.reason, "%s still has a flat template in the second engine", a.program_name);
                    verdict.why = Verdict::Engine;
                    return verdict;
                }
            }
        }
        verdict.transient = true;
        verdict.why = Verdict::Busy;
        if (User::ScreenLockActive())
        {
            Text(verdict.reason, sizeof verdict.reason, "a program holds the lock of the screen");
            return verdict;
        }
        for (unsigned p = 0; p < count; ++p)
        {
            auto &a = *processes[p];
            if (a.state == Application::Failed)
            {
                verdict.transient = false;
                verdict.why = Verdict::Ended;
                snprintf(verdict.reason, sizeof verdict.reason, "%s has failed", a.program_name);
                return verdict;
            }
            bool active_handler = false;
            for (unsigned i = 0; i < Application::ThreadCapacity; ++i)
                active_handler |= a.threads[i].exceptions.handler_frames && !a.SavedFaultBoxThread(i);
            if (active_handler)
            {
                snprintf(verdict.reason, sizeof verdict.reason, "%s handles an exception", a.program_name);
                return verdict;
            }
            for (unsigned i = 0; i < 16; ++i)
                if (!ThreadStands(a, i, verdict.reason, sizeof verdict.reason, verdict.why))
                    return verdict;
            // The state format carries sixteen thread records.
            for (unsigned i = 16; i < Application::ThreadCapacity; ++i)
                if (a.threads[i].fiber)
                {
                    snprintf(verdict.reason, sizeof verdict.reason, "%s runs more than sixteen threads", a.program_name);
                    return verdict;
                }
        }
        verdict.possible = true;
        verdict.transient = false;
        verdict.why = Verdict::Nothing;
        return verdict;
    }

#if defined(__APPLE__) || defined(__linux__)
    // ---- The parts of the stream ---------------------------------------------------
    // A state is a head and parts. Each part has a name of eight characters
    // and its length, so a reader passes over a part it does not know, and a
    // version of its own, so a part whose making has changed is converted or
    // refused by name.
    struct Buffer final : Sink
    {
        std::vector<unsigned char> data;
        bool Write(const void *bytes, size_t count) override
        {
            const auto *b = static_cast<const unsigned char *>(bytes);
            data.insert(data.end(), b, b + count);
            return true;
        }
    };
    struct Memory final : Source
    {
        const unsigned char *data = nullptr;
        size_t size = 0, at = 0;
        bool Read(void *bytes, size_t count) override
        {
            if (count > size - at)
                return false;
            memcpy(bytes, data + at, count);
            at += count;
            return true;
        }
    };
    static bool PutPart(Sink &sink, const char *name, const Buffer &part)
    {
        char tag[8] = {};
        memcpy(tag, name, strlen(name) < sizeof tag ? strlen(name) : sizeof tag);
        return Put(sink, tag, sizeof tag) && Put64(sink, part.data.size()) && Put(sink, part.data.data(), part.data.size());
    }
    static constexpr unsigned long long ProcessPart = 3, LegacyWindowsProcessPart = 1, TrueTypePart = 1;

    // ---- Memory that is not the port's own -------------------------------------------
    // A pointer of the port into the guest memory of a processor is written
    // as the processor and the guest address. The processors are numbered as
    // Processes lists them; the one of the TrueType driver is the last number.
    static constexpr unsigned TrueTypeProcessor = 0xffff;
    struct Guests
    {
        Application *processes[ProcessCapacity];
        unsigned count = 0;
    };
    static bool GuestOf(void *context, const void *address, unsigned &processor, unsigned &guest)
    {
        auto &g = *static_cast<Guests *>(context);
        const auto *p = static_cast<const unsigned char *>(address);
        for (unsigned i = 0; i < g.count; ++i)
            if (const auto *cpu = g.processes[i]->cpu; cpu && cpu->memory && p >= cpu->memory && p - cpu->memory < (1ll << 32))
            {
                processor = i;
                guest = (unsigned)(p - cpu->memory);
                return true;
            }
        if (const auto *cpu = TrueTypeMachine::StateCpu(); cpu && cpu->memory && p >= cpu->memory && p - cpu->memory < (1ll << 32))
        {
            processor = TrueTypeProcessor;
            guest = (unsigned)(p - cpu->memory);
            return true;
        }
        return false;
    }
    static void *HostOf(void *context, unsigned processor, unsigned guest)
    {
        auto &g = *static_cast<Guests *>(context);
        const X86::Cpu *cpu = processor == TrueTypeProcessor ? TrueTypeMachine::StateCpu()
                              : processor < g.count ? g.processes[processor]->cpu : nullptr;
        return cpu && cpu->memory ? cpu->memory + guest : nullptr;
    }

    // ---- A processor in the stream ----------------------------------------------
    struct RegionWriter
    {
        Sink &sink;
        X86::Cpu &cpu;
        bool okay = true;
        char error[256] = {};
        unsigned long long private_bytes = 0, shared_bytes = 0;
    };
    // A saved register state: the bytes of the engine where it has such
    // (Cpu::ContextBytes), else register by register. The second way puts
    // the state on the processor to read it.
    static constexpr unsigned ContextCapacity = 65536;
    static bool SaveContext(X86::Cpu &cpu, const X86::CpuContext *context, Sink &sink)
    {
        const unsigned bytes = cpu.ContextStateBytes(context);
        if (!Put64(sink, bytes))
            return false;
        if (bytes)
        {
            std::vector<unsigned char> buffer(ContextCapacity);
            return bytes <= ContextCapacity && cpu.ContextExport(context, buffer.data()) && Put(sink, buffer.data(), bytes);
        }
        Registers registers;
        cpu.Restore(context);
        ReadRegisters(cpu, registers);
        return Put(sink, &registers, sizeof registers);
    }
    static bool LoadContext(X86::Cpu &cpu, X86::CpuContext *&context, Source &source)
    {
        unsigned long long bytes = 0;
        if (!Get64(source, bytes))
            return Trouble("the stream ends before a register state");
        context = nullptr;
        if (const unsigned error = cpu.NewContext(context))
            return Trouble("no room for a register state", error);
        if (bytes)
        {
            std::vector<unsigned char> buffer(ContextCapacity);
            if (bytes != cpu.ContextBytes() || bytes > ContextCapacity)
                return Trouble("a register state of another size", (unsigned)bytes);
            return (Get(source, buffer.data(), bytes) && cpu.ContextImport(context, buffer.data())) ||
                   Trouble("the engine does not take a register state");
        }
        Registers registers;
        if (!Get(source, &registers, sizeof registers))
            return Trouble("the stream ends inside a register state");
        WriteRegisters(cpu, registers);
        if (const unsigned error = cpu.Save(context))
            return Trouble("the engine does not keep a register state", error);
        return true;
    }
    // What went wrong in a load, for the log: the caller's text says less.
    static bool Trouble(const char *what, unsigned number = 0)
    {
        fprintf(stderr, "NEO98 STATE load: %s (%u)\n", what, number);
        return false;
    }
    static bool SaveProcessor(X86::Cpu &cpu, Sink &sink, char *error, size_t error_bytes)
    {
        RegionWriter writer{sink, cpu};
        const bool walked = cpu.SavedRegions(
            [](void *context, unsigned address, unsigned bytes, unsigned prot, void *host) {
                auto &w = *static_cast<RegionWriter *>(context);
                if (!w.okay)
                    return;
                if (host)
                {
                    // Bytes someone else owns: the fields or a region carry
                    // them, and the pages are bound to them again. One run
                    // per block: neighbours here need not be neighbours after a load.
                    Buffer reference;
                    for (unsigned done = 0; w.okay && done < bytes;)
                    {
                        reference.data.clear();
                        unsigned long long mapped = 0;
                        w.okay = SavePointer(reference, static_cast<const unsigned char *>(host) + done,
                                             bytes - done, mapped, w.error, sizeof w.error);
                        if (!w.okay)
                            break;
                        const unsigned long long record[4] = {(unsigned long long)address + done, mapped, prot, 1};
                        w.okay = Put(w.sink, record, sizeof record) && Put(w.sink, reference.data.data(), reference.data.size());
                        done += (unsigned)mapped;
                        w.shared_bytes += mapped;
                    }
                    return;
                }
                const unsigned long long record[4] = {address, bytes, prot, 0};
                w.okay = Put(w.sink, record, sizeof record);
                w.private_bytes += bytes;
                // In pieces: a run can be large.
                static constexpr unsigned Piece = 1u << 20;
                std::vector<unsigned char> buffer(Piece);
                for (unsigned done = 0; w.okay && done < bytes; done += Piece)
                {
                    const unsigned n = bytes - done < Piece ? bytes - done : Piece;
                    w.okay = w.cpu.Read(address + done, buffer.data(), n) == X86::CpuError::Ok && PutPages(w.sink, buffer.data(), n);
                }
            },
            &writer);
        if (!walked || !writer.okay)
        {
            Text(error, error_bytes, writer.error[0] ? writer.error : "the guest memory of a processor could not be read");
            return false;
        }
        const unsigned long long end[4] = {0, 0, 0, 0};
        return Put(sink, end, sizeof end);
    }
    // The registers the processor stands with, around what a save does to it.
    struct Live
    {
        X86::Cpu &cpu;
        X86::CpuContext *context = nullptr;
        explicit Live(X86::Cpu &c) : cpu(c)
        {
            if (cpu.NewContext(context) != X86::CpuError::Ok || cpu.Save(context) != X86::CpuError::Ok)
                context = nullptr;
        }
        ~Live()
        {
            if (!context)
                return;
            cpu.Restore(context);
            cpu.FreeContext(context);
        }
    };
    static bool LoadMemory(X86::Cpu &cpu, Source &source, FieldReader *fields, const char *&error)
    {
        static constexpr unsigned Piece = 1u << 20;
        std::vector<unsigned char> buffer(Piece);
        for (;;)
        {
            unsigned long long record[4];
            error = "the stream ends inside the guest memory";
            if (!Get(source, record, sizeof record))
                return false;
            const unsigned address = (unsigned)record[0], bytes = (unsigned)record[1], prot = (unsigned)record[2];
            if (!bytes)
                break;
            if (record[3])
            {
                void *host = nullptr;
                char text[256] = {};
                const bool pointer = LoadPointer(fields, source, host, text, sizeof text);
                const unsigned mapped = pointer && host ? cpu.MapHost(address, bytes, prot, host) : X86::CpuError::Argument;
                if (!pointer || !host || mapped != X86::CpuError::Ok)
                {
                    fprintf(stderr,"NEO98 STATE MapHost address=%08x bytes=%u host=%p error=%u\n",address,bytes,host,mapped);
                    Trouble(text[0] ? text : "MapHost", address);
                    error = "shared guest pages could not be mapped again";
                    return false;
                }
                continue;
            }
            if (cpu.MapSaved(address, bytes, prot) != X86::CpuError::Ok)
            {
                error = "guest pages could not be mapped again";
                return false;
            }
            for (unsigned done = 0; done < bytes; done += Piece)
            {
                const unsigned n = bytes - done < Piece ? bytes - done : Piece;
                bool zero = false;
                if (!GetPages(source, buffer.data(), n, zero))
                    return false;
                if (!zero && cpu.Write(address + done, buffer.data(), n) != X86::CpuError::Ok)
                {
                    error = "guest pages could not be written";
                    return false;
                }
            }
        }
        error = nullptr;
        return true;
    }

    static bool HostedAliases(Application &a, Sink &sink)
    {
        unsigned count = 0;
        if (a.win16.state && a.win16.state->hosted)
            for (const auto &record : a.win16.state->hosted_threads) if (record.depth) ++count;
        if (!Put64(sink, count)) return false;
        if (!count) return true;
        for (unsigned index = 0; index < 16; ++index)
        {
            const auto &record = a.win16.state->hosted_threads[index];
            if (!record.depth) continue;
            if (record.depth > 16 || !Put64(sink, index) || !Put64(sink, record.depth)) return false;
            unsigned thread = 16;
            for (unsigned i = 0; i < 16; ++i) if (a.threads[i].id == record.thread) thread = i;
            if (thread == 16 || !Put64(sink, thread)) return false;
            for (unsigned at = 0; at < record.depth; ++at)
            {
                unsigned level = 0;
                for (unsigned i = 1; i <= a.threads[thread].depth && i < 33; ++i)
                    if (a.threads[thread].call_marks[i].context == record.flat[at] &&
                        (a.threads[thread].call_marks[i].win16 & 2)) level = i;
                if (!record.flat[at] || !level || !Put64(sink, level)) return false;
            }
        }
        return true;
    }
    static bool HostedAliases(Application &a, Source &source)
    {
        unsigned long long count = 0;
        if (!Get64(source, count) || count > 16 || (count && (!a.win16.state || !a.win16.state->hosted))) return false;
        bool seen[16] = {};
        if (a.win16.state)
            for (auto &record : a.win16.state->hosted_threads) for (auto &flat : record.flat) flat = nullptr;
        for (unsigned n = 0; n < count; ++n)
        {
            unsigned long long index = 0, depth = 0, thread = 0;
            if (!Get64(source, index) || index >= 16 || seen[index] || !Get64(source, depth) || !depth || depth > 16 ||
                !Get64(source, thread) || thread >= 16) return false;
            seen[index] = true;
            auto &record = a.win16.state->hosted_threads[index];
            if (record.depth != depth || record.thread != a.threads[thread].id) return false;
            unsigned previous = 0;
            for (unsigned at = 0; at < depth; ++at)
            {
                unsigned long long level = 0;
                if (!Get64(source, level) || !level || level > a.threads[thread].depth || level >= 33) return false;
                const auto &mark = a.threads[thread].call_marks[level];
                if (!mark.context || !(mark.win16 & 2) || level <= previous || !mark.frames ||
                    mark.frames > a.threads[thread].host_frame_count ||
                    a.threads[thread].host_frames[mark.frames-1].kind != Application::HostWin16HostedCall ||
                    !a.threads[thread].host_frames[mark.frames-1].b) return false;
                previous = unsigned(level);
                record.flat[at] = mark.context;
            }
        }
        if (a.win16.state && a.win16.state->hosted)
            for (unsigned i = 0; i < 16; ++i) if (a.win16.state->hosted_threads[i].depth && !seen[i]) return false;
        return true;
    }

    // ---- Save ---------------------------------------------------------------------
    // Counts what passes, so a save can say what its bytes are.
    struct Counting final : Sink
    {
        Sink &to;
        unsigned long long bytes = 0;
        explicit Counting(Sink &s) : to(s) {}
        bool Write(const void *data, size_t count) override
        {
            bytes += count;
            return to.Write(data, count);
        }
    };
    // What a thread is besides its fields: whether it has a fiber and how far
    // that one came, and which registers are kept for it.
    enum ThreadBits : unsigned long long { HasFiber = 1, Started = 2, Finished = 4, HasContext = 8, Nested = 16, Deep = 32 };
    static bool Save(Application &first, Sink &target, char *error, size_t error_bytes, bool dos_ui = false)
    {
        Counting sink(target);
        last_save = {};
        const Verdict verdict = CanSave(first, dos_ui);
        if (!verdict.possible || !Application::MachinePaused())
        {
            Text(error, error_bytes, verdict.possible ? "the machine is not paused" : verdict.reason);
            return false;
        }
        Guests guests;
        guests.count = Processes(first, guests.processes, ProcessCapacity);
        Application **processes = guests.processes;
        const unsigned count = guests.count;
        // What the providers of the drives and the journal of the registry
        // still hold in memory goes to the files: the copy of the write
        // layers and of the journal that belongs to this state is taken
        // after this call.
        Files::System().Flush();
        Registry::Flush();
        // And what the rasterizer of the host holds of the surfaces goes
        // into their bytes: the state carries the bytes.
        for (unsigned p = 0; p < count; ++p)
        {
            processes[p]->HalSyncAll();
            processes[p]->GlideStateSync();
            processes[p]->OpenGLStateSync();
        }
        Outside outside;
        outside.context = &guests;
        outside.guest = GuestOf;
        outside.host = HostOf;
        Buffer part;
        const unsigned long long began = Application::HostNow();
        if (!SaveFields(part, &first, outside, error, error_bytes))
            return false;
        const unsigned long long fields_done = Application::HostNow();
        Buffer regions, clock;
        Put64(clock, Application::Now());
        Put64(clock, count);
        const bool head = SaveRegions(regions) && Put64(sink, StreamMagic) && Put64(sink, FormatVersion) &&
                          PutPart(sink, "clock", clock) && PutPart(sink, "regions", regions) && PutPart(sink, "fields", part);
        Text(error, error_bytes, "the stream cannot be written");
        if (!head)
            return false;
        last_save.field_bytes = sink.bytes;
        last_save.processes = count;
        for (unsigned p = 0; p < count; ++p)
        {
            auto &a = *processes[p];
            part.data.clear();
            const unsigned long long kind = a.dos_vm ? (a.cpu->Backend() == X86::CpuBackend::None ? 2 : 3) : a.Win16Process() ? 1 : a.cpu->Backend() == X86::CpuBackend::Fex && a.cpu->Runs16Bit() ? 4 : 0;
            Put64(part, a.dos_vm ? 2 : ProcessPart);
            Put64(part, p);
            Put64(part, kind);
            if (!SaveProcessor(*a.cpu, part, error, error_bytes))
                return false;
            Text(error, error_bytes, "the registers of a thread could not be read");
            Live live(*a.cpu);
            if (!live.context || !SaveContext(*a.cpu, live.context, part))
                return false;
            for (unsigned i = 0; i < 16; ++i)
            {
                const auto &t = a.threads[i];
                // The registers Call kept for every
                // callback a thread stands in: of one in the form of the
                // first states, of more with their number.
                const bool open = t.fiber && t.context && t.depth >= 1 && t.depth < 33;
                const bool nested = open && t.depth == 1 && t.call_marks[1].context, deep = open && t.depth > 1;
                const unsigned long long bits = (t.fiber ? HasFiber : 0) | (t.fiber && (t.fiber->Started() || t.state_resumed) ? Started : 0) |
                                                (t.fiber && t.fiber->Finished() ? Finished : 0) | (t.context ? HasContext : 0) |
                                                (nested ? Nested : 0) | (deep ? Deep : 0);
                Put64(part, bits);
                if ((t.context && !SaveContext(*a.cpu, t.context, part)) ||
                    (nested && !SaveContext(*a.cpu, t.call_marks[1].context, part)))
                    return false;
                if (deep)
                {
                    Put64(part, t.depth);
                    for (unsigned k = 1; k <= t.depth; ++k)
                        if (!t.call_marks[k].context || !SaveContext(*a.cpu, t.call_marks[k].context, part))
                            return false;
                }
            }
            // The flat registers a 16-bit task makes its calls into 32-bit
            // code from (Application::Win16EnterRingThree).
            Put64(part, a.win16_flat_template ? 1 : 0);
            if (a.win16_flat_template && !SaveContext(*a.cpu, a.win16_flat_template, part))
                return false;
            if (!a.dos_vm && !HostedAliases(a, part))
            { Text(error, error_bytes, "a hosted flat context has no captured owner"); return false; }
            if (!PutPart(sink, "process", part))
                return false;
        }
        // The TrueType driver runs on a processor of its own.
        if (X86::Cpu *truetype = TrueTypeMachine::StateCpu())
        {
            part.data.clear();
            Put64(part, TrueTypePart);
            if (truetype->StateRefusal())
            {
                Text(error, error_bytes, "the processor of the TrueType driver cannot be saved");
                return false;
            }
            if (!SaveProcessor(*truetype, part, error, error_bytes))
                return false;
            Live live(*truetype);
            if (!live.context || !SaveContext(*truetype, live.context, part) || !PutPart(sink, "truetype", part))
                return false;
        }
        part.data.clear();
        if (!PutPart(sink, "end", part))
            return false;
        last_save.guest_bytes = sink.bytes - last_save.field_bytes;
        // NEO98_STATE_TIMES=1: where the time of a save goes.
        if (getenv("NEO98_STATE_TIMES"))
            fprintf(stderr, "NEO98 STATE save times: fields %.1f ms, the rest (guest memory, registers, the stream) %.1f ms\n",
                    (fields_done - began) / 1000.0, (Application::HostNow() - fields_done) / 1000.0);
        for (unsigned p = 0; p < count; ++p)
            for (const auto &t : processes[p]->threads)
                last_save.threads += t.fiber && t.context ? 1 : 0;
        Text(error, error_bytes, "");
        return true;
    }
    static inline SaveNumbers last_save;

    // ---- Load ---------------------------------------------------------------------
    static X86::Cpu *NewProcessor(const char *purpose, X86::CpuBackend engine, bool pair)
    {
        X86::Cpu *cpu = nullptr;
#ifdef __linux__
        if (engine == X86::CpuBackend::Fex || engine == X86::CpuBackend::Unicorn) engine = X86::CpuBackend::Native;
#endif
        if (X86::CreateCpu(engine, cpu, purpose) != X86::CpuError::Ok || !cpu)
            return nullptr;
        if (pair && !cpu->Runs16Bit())
            cpu = X86::CreateDualCpu(cpu);
        return cpu;
    }
    // The registers a new processor stands with.
    static bool LoadLive(X86::Cpu &cpu, Source &source)
    {
        X86::CpuContext *context = nullptr;
        const bool okay = LoadContext(cpu, context, source) && cpu.Restore(context) == X86::CpuError::Ok;
        if (context)
            cpu.FreeContext(context);
        return okay;
    }
    static bool LoadProcess(Application &a, Source &source, FieldReader *fields, const char *&error, bool dos_ui,
                            unsigned long long process_version)
    {
        unsigned long long kind = 0;
        error = "the stream ends inside a process";
        if (!Get64(source, kind) || (dos_ui ? kind != 2 && kind != 3 : kind > 1 && !(process_version == ProcessPart && kind == 4)) || (dos_ui && !a.dos_vm))
            return false;
        // The fields do not carry the processor (src/state_rules/): a new
        // one, with the hooks of Application::Start and of
        // Application::Win16Machinery.
        a.cpu = kind == 2 ? NewProcessor("dos-ui-address-space (state)", X86::CpuBackend::None, false)
                        : kind == 3 ? NewProcessor("dos-ui-unicorn (state)", X86::CpuBackend::Unicorn, false)
                        : kind == 1 ? NewProcessor("win16-process (state)", X86::CpuBackend::Unicorn, false)
                          : NewProcessor("win32-process (state)", X86::CpuBackend::Fex, true);
        if (!a.cpu)
        {
            error = "a processor could not be made";
            return false;
        }
        bool hooks = kind == 2 || (a.cpu->SetGateHook(Application::GateBase, Application::GateBase + Application::GateRegionBytes - 1,
                                        Application::Gate, &a) == X86::CpuError::Ok &&
                     a.cpu->SetFaultHook(Application::MemoryFaultHook, &a) == X86::CpuError::Ok);
        hooks = hooks && (kind == 2 || a.AddSliceHook());
        if (kind == 1)
            hooks = hooks && a.cpu->SetInterruptHook(Application::Win16Interrupt, &a) == X86::CpuError::Ok;
        else if (kind != 2)
            hooks = hooks && a.cpu->SetInterruptHook(Application::GuestInterrupt, &a) == X86::CpuError::Ok &&
                    a.cpu->SetPortHooks(Application::GuestPortInput, Application::GuestPortOutput, &a) == X86::CpuError::Ok;
        if (!hooks)
        {
            error = "the hooks of a processor could not be installed";
            return false;
        }
        if (!LoadMemory(*a.cpu, source, fields, error))
            return false;
        if (a.vga && !a.BindVga())
        { error = "the VGA device aperture could not be restored"; return false; }
        error = "the registers of a thread could not be set";
        // The registers the processor stood with come first and are set last.
        X86::CpuContext *live = nullptr;
        if (!LoadContext(*a.cpu, live, source))
            return false;
        for (unsigned i = 0; i < 16; ++i)
        {
            auto &t = a.threads[i];
            unsigned long long bits = 0;
            if (!Get64(source, bits))
                return false;
            t.fiber = nullptr;
            t.context = nullptr;
            for (auto &mark : t.call_marks)
                mark.context = nullptr;
            if ((bits & HasContext) && !LoadContext(*a.cpu, t.context, source))
                return false;
            if ((bits & Nested) && !LoadContext(*a.cpu, t.call_marks[1].context, source))
                return false;
            if (bits & Deep)
            {
                unsigned long long levels = 0;
                if (!Get64(source, levels) || levels < 2 || levels > 32)
                    return false;
                for (unsigned k = 1; k <= levels; ++k)
                    if (!LoadContext(*a.cpu, t.call_marks[k].context, source))
                        return false;
            }
            if (!(bits & HasFiber))
                continue;
            // A new fiber: the stack of the old one belonged to the process
            // that saved, and a thread stands where its registers say.
            t.fiber = Application::NewThreadFiber();
            const bool started = bits & Started, finished = bits & Finished;
            const bool first_thread = i == 0;
            if (!t.fiber || !(first_thread ? t.fiber->Start([](void *p) { static_cast<Application *>(p)->Run(); }, &a)
                                           : t.fiber->Start([](void *p) { static_cast<Application::Thread *>(p)->app->Run(); }, &t)))
            {
                error = "a fiber could not be made";
                return false;
            }
            if (finished)
                t.fiber->Retire();
            t.state_resumed = started && !finished;
            // A thread that slept between two instructions (the pace of the
            // frames) sleeps the rest of its time.
            if (t.state_resumed && !t.pending && t.block == 3 && t.deadline)
                t.sleep_after_call = t.deadline;
        }
        unsigned long long flat = 0;
        a.win16_flat_template = nullptr;
        if (!Get64(source, flat) || (flat && !LoadContext(*a.cpu, a.win16_flat_template, source)))
            return false;
        if (!dos_ui && process_version == ProcessPart && !HostedAliases(a, source))
        { error = "hosted flat-context aliases are invalid"; a.cpu->FreeContext(live); return false; }
        const bool stood = a.cpu->Restore(live) == X86::CpuError::Ok;
        a.cpu->FreeContext(live);
        error = stood ? nullptr : "the registers of a processor could not be set";
        return stood;
    }
    // Parse all parts without constructing USER/GDI objects or assigning a
    // machine global. Context imports use detached, short-lived processors.
    static bool ValidateDosUi(Source &source, char *error, size_t error_bytes, const DosVm::Launch *launch = nullptr)
    {
        auto fail = [&](const char *why) { Text(error, error_bytes, why); return false; };
        unsigned long long magic = 0, version = 0;
        if (!Get64(source, magic) || magic != StreamMagic || !Get64(source, version) || version != FormatVersion)
            return fail("the combined UI stream has an invalid header");
        bool clock = false, fields = false, process = false, regions = false, truetype = false;
        unsigned native_depth = 0;
        auto context = [&](X86::Cpu &cpu, Source &part) {
            X86::CpuContext *saved = nullptr;
            const bool okay = LoadContext(cpu, saved, part);
            if (saved) cpu.FreeContext(saved);
            return okay;
        };
        std::vector<unsigned char> buffer(1u << 20);
        for (;;) {
            char tag[9] = {}; unsigned long long size = 0;
            if (!Get(source, tag, 8) || !Get64(source, size) || size > (1ull << 29)) return fail("a combined UI part exceeds its input limit");
            std::vector<unsigned char> payload((size_t)size);
            if (size && !Get(source, payload.data(), payload.size())) return fail("a combined UI part is truncated");
            Memory part; part.data = payload.data(); part.size = payload.size();
            if (!strcmp(tag, "end")) return size == 0 && clock && fields && process && regions;
            if (!strcmp(tag, "clock")) {
                unsigned long long time = 0, count = 0;
                if (clock || !Get64(part,time) || !Get64(part,count) || count != 1) return fail("the combined UI requires exactly one process");
                clock = true;
            } else if (!strcmp(tag, "fields")) {
                if (!clock || !regions || fields || !(launch ? ValidateDosUiFields(part,*launch,error,error_bytes,&native_depth) : ValidateFields(part,error,error_bytes,&native_depth))) return false;
                fields = true;
            } else if (!strcmp(tag, "regions")) {
                if (regions) return fail("the combined UI regions are duplicated");
                unsigned long long count = 0, total = 0;
                if (!Get64(part,count) || count > 65536) return fail("the combined UI region count is invalid");
                std::vector<std::string> names;
                for (unsigned long long i=0;i<count;++i) {
                    unsigned long long length=0,bytes=0;char name[128]={};
                    if(!Get64(part,length)||!length||length>=sizeof name||!Get(part,name,length)||memchr(name,0,length)||
                       !Get64(part,bytes)||!bytes||bytes>(1ull<<34)) return fail("a region declaration is invalid");
                    const bool imported = !strncmp(name, "windows-import/", 15);
                    if (imported && !HaveImportedRegion(name, bytes))
                        return fail("the state requires different imported Windows data from C:");
                    if (!imported && (bytes & 4095)) return fail("a region declaration is invalid");
                    for(const auto &prior:names) if(prior==name) return fail("a region name is duplicated");
                    names.emplace_back(name);
                    for(;;) {
                        unsigned long long kind=0,offset=0,n=0;
                        if(!Get64(part,kind)) return fail("a region record is truncated");
                        if(!kind) break;
                        if(imported||kind>2||!Get64(part,offset)||!Get64(part,n)||!n||(offset&4095)||(n&4095)||offset>bytes||n>bytes-offset)
                            return fail("a region range is invalid");
                        if(kind==2) {
                            if(total+n>(1ull<<30)) return fail("region data exceeds its limit");total+=n;
                            for(unsigned long long done=0;done<n;done+=buffer.size())
                                if(!Get(part,buffer.data(),n-done<buffer.size()?n-done:buffer.size())) return fail("region data is truncated");
                        }
                    }
                }
                regions = true;
            } else if (!strcmp(tag, "process") || !strcmp(tag, "truetype")) {
                const bool tt = !strcmp(tag, "truetype");
                if (!fields) return fail("a combined processor stands before its fields");
                unsigned long long part_version = 0, index = 0, kind = 0;
                if (!Get64(part,part_version) || part_version != (tt ? TrueTypePart : 2)) return fail("a combined processor part has an invalid version");
                if (tt ? truetype : process) return fail("a combined processor part is duplicated");
                if (!tt && (!Get64(part,index) || index || !Get64(part,kind) || (kind != 2 && kind != 3))) return fail("a combined processor has an invalid native kind");
                std::unique_ptr<X86::Cpu> cpu(NewProcessor("dos-ui-validation", tt ? X86::CpuBackend::Fex : kind == 2 ? X86::CpuBackend::None : X86::CpuBackend::Unicorn, false));
                if (!cpu) return fail("a validation processor could not be made");
                unsigned long long previous = 0, total = 0;
                for (;;) {
                    unsigned long long record[4]; if (!Get(part,record,sizeof record)) return fail("a processor memory record is truncated");
                    if (!record[1]) { if (record[0] || record[2] || record[3]) return fail("an invalid memory terminator"); break; }
                    const auto address = record[0], bytes = record[1];
                    if (address > 0xffffffffull || bytes > 0xffffffffull || (address & 4095) || (bytes & 4095) ||
                        address < previous || address + bytes > (1ull << 32) || (record[2] & ~7ull) || record[3] > 1 ||
                        total + bytes > (1ull << 30)) return fail("a processor memory range is invalid");
                    previous = address + bytes; total += bytes;
                    if (record[3]) {
                        unsigned pointer_bytes = 0;
                        if (!Get(part,&pointer_bytes,4) || pointer_bytes > 1024) return fail("an invalid shared pointer record");
                        unsigned char pointer[1024]; if (!Get(part,pointer,pointer_bytes)) return fail("a shared pointer is truncated");
                    } else for (unsigned long long done = 0; done < bytes; done += buffer.size()) {
                        const auto n = bytes-done < buffer.size() ? bytes-done : buffer.size(); bool zero = false;
                        if (!GetPages(part,buffer.data(),n,zero)) return fail("a processor page payload is invalid");
                    }
                }
                if (!context(*cpu,part)) return fail("a processor live context is invalid");
                if (!tt) {
                    for (unsigned i=0;i<16;++i) {
                        unsigned long long bits=0;
                        if (!Get64(part,bits) || (bits & ~63ull) ||
                            (!i && ((bits & (HasFiber|Started)) != (HasFiber|Started) || (bits & Finished))) ||
                            (i && (bits & HasFiber) && !(bits & Finished)) ||
                            ((bits&Nested) && (bits&Deep)) ||
                            ((bits&(Nested|Deep)) && !(bits&HasContext))) return fail("an unsupported native DOS thread");
                        unsigned callback_depth = bits&Nested ? 1 : 0;
                        if ((bits&HasContext) && !context(*cpu,part)) return fail("a thread context is invalid");
                        if ((bits&Nested) && !context(*cpu,part)) return fail("a callback context is invalid");
                        if (bits&Deep) {
                            unsigned long long depth=0;if (!Get64(part,depth)||depth<2||depth>32) return fail("a callback depth is invalid");
                            callback_depth = (unsigned)depth;
                            for(unsigned k=0;k<depth;++k) if(!context(*cpu,part)) return fail("a callback context is invalid");
                        }
                        if (!i && callback_depth != native_depth) return fail("the native DOS callback depth differs between fields and contexts");
                    }
                    unsigned long long flat=0;if(!Get64(part,flat)||flat>1||(flat&&!context(*cpu,part))) return fail("a flat context is invalid");
                    process = true;
                } else truetype = true;
            } else return fail("an unknown combined UI part");
            if (part.at != part.size) return fail("a combined UI part has trailing bytes");
        }
    }
    static Application *Load(Source &source, const Host &host, char *error, size_t error_bytes, bool dos_ui = false)
    {
        const char *why = nullptr;
        if (!Available(&why))
        {
            Text(error, error_bytes, why);
            return nullptr;
        }
        if (!host.first)
        {
            Text(error, error_bytes, "the host has made no first process");
            return nullptr;
        }
        unsigned long long magic = 0, version = 0;
        if (!Get64(source, magic) || magic != StreamMagic || !Get64(source, version))
        {
            Text(error, error_bytes, "this is not a state of a Windows machine");
            return nullptr;
        }
        if (version != FormatVersion)
        {
            snprintf(error, error_bytes, "the state has format %llu, this NEO98 reads format %u", version, FormatVersion);
            return nullptr;
        }
        Guests guests;
        Outside outside;
        outside.context = &guests;
        outside.guest = GuestOf;
        outside.host = HostOf;
        FieldReader *fields = nullptr;
        unsigned long long now = 0, expected = 0, loaded = 0;
        bool ended = false;
        std::vector<unsigned char> payload;
        while (!ended)
        {
            char tag[9] = {};
            unsigned long long bytes = 0;
            if (!Get(source, tag, 8) || !Get64(source, bytes) || bytes > (1ull << 34))
            {
                Text(error, error_bytes, "the stream does not end where a state ends");
                return nullptr;
            }
            payload.resize(bytes);
            if (bytes && !Get(source, payload.data(), bytes))
            {
                snprintf(error, error_bytes, "the stream ends inside its part %s", tag);
                return nullptr;
            }
            Memory part;
            part.data = payload.data();
            part.size = payload.size();
            const char *reason = nullptr;
            unsigned long long part_version = 0;
            if (!strcmp(tag, "clock"))
            {
                if (!Get64(part, now) || !Get64(part, expected) || expected > ProcessCapacity)
                    reason = "the clock of the state cannot be read";
            }
            else if (!strcmp(tag, "regions"))
            {
                Text(error, error_bytes, "the regions of the state cannot be read");
                if (!LoadRegions(part, error, error_bytes))
                    return nullptr;
            }
            else if (!strcmp(tag, "fields"))
            {
                const unsigned long long began = Application::HostNow();
                host.first->BindUserProviders();
                fields = LoadFields(part, host.first, outside, error, error_bytes);
                if (!fields)
                    return nullptr;
                if (getenv("NEO98_STATE_TIMES"))
                    fprintf(stderr, "NEO98 STATE load times: fields %.1f ms\n", (Application::HostNow() - began) / 1000.0);
                // From here on the variables of the port are the saved ones.
                guests.count = Processes(*host.first, guests.processes, ProcessCapacity);
                if (guests.count != expected)
                    reason = "the state names other processes than its fields hold";
                Application::SetNow(now);
                if (host.wall_clock)
                    Application::StartGuestClock(host.wall_clock);
            }
            else if (!strcmp(tag, "process"))
            {
                unsigned long long index = 0;
                if (!fields || !Get64(part, part_version) || !Get64(part, index) || index >= guests.count)
                    reason = "a process of the state stands before its fields";
                else if (dos_ui ? part_version != 2 : part_version != ProcessPart && part_version != LegacyWindowsProcessPart)
                    reason = "the processes of the state are of a version this NEO98 does not read";
                else if (LoadProcess(*guests.processes[index], part, fields, reason, dos_ui, part_version))
                    ++loaded;
                else if (!reason)
                    reason = "a process of the state could not be made";
            }
            else if (!strcmp(tag, "truetype"))
            {
                X86::Cpu *&cpu = TrueTypeMachine::StateCpu();
                if (!fields || !Get64(part, part_version) || part_version != TrueTypePart)
                    reason = "the TrueType driver of the state is of a version this NEO98 does not read";
                else if (!(cpu = NewProcessor("truetype-machine (state)", X86::CpuBackend::Fex, false)) ||
                         !TrueTypeMachine::StateBind() || !LoadMemory(*cpu, part, fields, reason) || !LoadLive(*cpu, part))
                    reason = reason ? reason : "the processor of the TrueType driver could not be made again";
            }
            else if (!strcmp(tag, "end"))
                ended = true;
            // A part of another name is one a later NEO98 added: passed over.
            if (reason)
            {
                Text(error, error_bytes, reason);
                return nullptr;
            }
        }
        if (!fields || loaded != expected)
        {
            Text(error, error_bytes, "the state lacks a part");
            return nullptr;
        }
        // The pointers into guest memory, now that the processors exist.
        Text(error, error_bytes, "");
        if (!FinishFields(fields, error, error_bytes))
            return nullptr;
        // The objects of the host that the state reaches by what it holds
        // and not by a pointer: each part binds its own.
        if (dos_ui && (guests.count != 1 || !host.first->DosUiStateReady(error, error_bytes, false))) return nullptr;
        Sound::Shared().StateBind();
        Sound::Microphone::StateBind();
        Files::System().StateBind();
        DropSockets();
        Text(error, error_bytes, "");
        return host.first;
    }
#endif
};

void WantSavePoint(bool wanted)
{
    Machine::WantSavePoint(wanted);
}
void Chains(X86::Application &first, void *file)
{
#if defined(__APPLE__) || defined(__linux__)
    FILE *out = static_cast<FILE *>(file);
    X86::Application *processes[ProcessCapacity];
    const unsigned count = Machine::Processes(first, processes, ProcessCapacity);
    for (unsigned p = 0; p < count; ++p)
        Machine::Chain(*processes[p], out);
#else
    (void)first;
    (void)file;
#endif
}
void BlockedFrames(X86::Application &first, FrameVisitor visitor, void *context)
{
#if defined(__APPLE__) || defined(__linux__)
    X86::Application *processes[ProcessCapacity];
    const unsigned count = Machine::Processes(first, processes, ProcessCapacity);
    for (unsigned p = 0; p < count; ++p)
        Machine::Chain(*processes[p], nullptr, visitor, context);
#else
    (void)first; (void)visitor; (void)context;
#endif
}
#if defined(__APPLE__) || defined(__linux__)
namespace
{
// What the test keeps in containers: numbers, structures that point at each
// other inside a vector, objects behind unique and shared pointers (one made
// by make_shared, which lies inside the block of its count), two maps and a
// string.
struct TestItem
{
    unsigned id = 0;
    TestItem *next = nullptr;
    char name[12] = {};
};
struct TestBoundaryItem
{
    unsigned long long id = 0;
    TestBoundaryItem *next = nullptr;
};
struct TestBox
{
    // Encounter the end before its owner. The following allocation has a
    // different type and must never be discovered through this end field.
    TestBoundaryItem *boundary_end = nullptr, *boundary_buffer = nullptr;
    unsigned *boundary_neighbour = nullptr, *boundary_spare[64] = {};
    std::vector<unsigned> numbers;
    std::vector<TestItem> items;
    std::map<unsigned, std::unique_ptr<TestItem>> by_id;
    std::map<unsigned, unsigned> squares;
    std::shared_ptr<TestItem> first, again, emplaced, emplaced_again;
    std::unique_ptr<TestItem> own;
    std::string text, small;
    TestItem *into = nullptr;
    // A buffer that fills its block to the last byte, with a pointer to its
    // end, and the block the allocator put right behind it: the end and
    // the start of the neighbour are the same address (the rule `end`).
    unsigned *buffer = nullptr, *buffer_end = nullptr, *neighbour = nullptr;
    unsigned *spare[64] = {};
    const unsigned char *binary = nullptr;
    const char *literal = nullptr;
} test_box;
// Starts with zero, as required for built-in FNT resources.
const unsigned char test_binary[] = {0, 0x31, 0, 0xff, 0x7e, 0x19};
} // namespace
int TestContainers()
{
    auto &b = test_box;
    if (b.numbers.empty())
    {
        for (unsigned i = 0; i < 1000; ++i)
            b.numbers.push_back(i * i);
        b.items.resize(5);
        for (unsigned i = 0; i < 5; ++i)
        {
            b.items[i].id = 100 + i;
            b.items[i].next = i + 1 < 5 ? &b.items[i + 1] : nullptr;
            snprintf(b.items[i].name, sizeof b.items[i].name, "item %u", i);
        }
        b.into = &b.items[3];
        for (unsigned i = 0; i < 200; i += 3)
        {
            b.by_id[i] = std::make_unique<TestItem>();
            b.by_id[i]->id = i;
        }
        for (unsigned i = 0; i < 300; ++i)
            b.squares[i * 7] = i * i;
        b.first.reset(new TestItem);
        b.first->id = 4711;
        b.again = b.first;
        b.emplaced = std::make_shared<TestItem>();
        b.emplaced->id = 4712;
        b.emplaced_again = b.emplaced;
        b.own = std::make_unique<TestItem>();
        b.own->id = 99;
        b.own->next = b.first.get();
        b.text = "a text that is longer than the room a string has inside itself";
        b.small = "short";
        b.binary = test_binary;
        b.literal = "the text survives a load";
        static_assert(sizeof(TestBoundaryItem) == 16);
        for (unsigned i = 0; i < 64 && !b.boundary_neighbour; ++i)
        {
            b.boundary_spare[i] = static_cast<unsigned *>(malloc(64));
            for (unsigned k = 0; k < 16; ++k)
                b.boundary_spare[i][k] = 20000 + k;
            for (unsigned j = 0; j < i && !b.boundary_neighbour; ++j)
                if (b.boundary_spare[j] + 16 == b.boundary_spare[i] ||
                    b.boundary_spare[i] + 16 == b.boundary_spare[j])
                {
                    auto *first = b.boundary_spare[j] + 16 == b.boundary_spare[i]
                                      ? b.boundary_spare[j] : b.boundary_spare[i];
                    b.boundary_buffer = reinterpret_cast<TestBoundaryItem *>(first);
                    b.boundary_neighbour = first + 16;
                }
        }
#if __has_feature(address_sanitizer) || defined(__linux__)
        // Linux malloc keeps metadata between blocks, so user buffers are
        // not adjacent. Verify their end pointers with separate allocations.
        if (!b.boundary_neighbour)
        {
            b.boundary_buffer = reinterpret_cast<TestBoundaryItem *>(b.boundary_spare[0]);
            b.boundary_neighbour = b.boundary_spare[1];
        }
#endif
        if (b.boundary_buffer)
        {
            b.boundary_end = b.boundary_buffer + 4;
            for (unsigned i = 0; i < 4; ++i)
                new (&b.boundary_buffer[i]) TestBoundaryItem{300 + i,
                    i < 3 ? b.boundary_buffer + i + 1 : nullptr};
        }
        // Blocks of 64 bytes until two lie one behind the other.
        for (unsigned i = 0; i < 64 && !b.neighbour; ++i)
        {
            b.spare[i] = static_cast<unsigned *>(malloc(64));
            for (unsigned k = 0; k < 16; ++k)
                b.spare[i][k] = i * 100 + k;
            for (unsigned j = 0; j < i && !b.neighbour; ++j)
                if (b.spare[j] + 16 == b.spare[i] || b.spare[i] + 16 == b.spare[j])
                {
                    b.buffer = b.spare[j] + 16 == b.spare[i] ? b.spare[j] : b.spare[i];
                    b.neighbour = b.buffer + 16;
                    b.buffer_end = b.buffer + 16;
                }
        }
#if __has_feature(address_sanitizer) || defined(__linux__)
        // ASan red zones and Linux malloc metadata separate allocations.
        // Exercise the end rule there too; other allocators require adjacency.
        if (!b.neighbour)
        {
            b.buffer = b.spare[0];
            b.buffer_end = b.buffer + 16;
            b.neighbour = b.spare[1];
        }
#endif
        return 0;
    }
    if (b.binary != test_binary || memcmp(b.binary, test_binary, sizeof test_binary) ||
        !b.literal || strcmp(b.literal, "the text survives a load"))
        return -1;
    // The end is the end of its buffer, and the neighbour a block of its
    // own (which may lie behind the buffer again).
    if (b.neighbour && (b.buffer_end != b.buffer + 16 || b.buffer[15] % 100 != 15 || b.neighbour[5] % 100 != 5 ||
                        b.buffer[5] / 100 == b.neighbour[5] / 100))
        return -1;
    if (!b.neighbour)
        return -1;
    if (!b.boundary_neighbour || b.boundary_end != b.boundary_buffer + 4)
        return -1;
    for (unsigned i = 0; i < 4; ++i)
        if (b.boundary_buffer[i].id != 300 + i ||
            b.boundary_buffer[i].next != (i < 3 ? b.boundary_buffer + i + 1 : nullptr))
            return -1;
    for (unsigned i = 0; i < 16; ++i)
        if (b.boundary_neighbour[i] != 20000 + i)
            return -1;
    bool same = b.numbers.size() == 1000 && b.items.size() == 5 && b.by_id.size() == 67 && b.squares.size() == 300;
    for (unsigned i = 0; same && i < 1000; ++i)
        same = b.numbers[i] == i * i;
    for (unsigned i = 0; same && i < 5; ++i)
        same = b.items[i].id == 100 + i && b.items[i].next == (i + 1 < 5 ? &b.items[i + 1] : nullptr);
    same = same && b.into == &b.items[3] && !strcmp(b.items[3].name, "item 3");
    unsigned seen = 0, last = 0;
    for (const auto &[id, item] : b.by_id)
    {
        same = same && item && item->id == id && id == seen * 3 && (!seen || id > last);
        last = id;
        ++seen;
    }
    seen = 0;
    for (const auto &[key, value] : b.squares)
    {
        same = same && key == seen * 7 && value == seen * seen;
        ++seen;
    }
    same = same && seen == 300 && b.squares.find(7 * 123) != b.squares.end() && b.squares.find(5) == b.squares.end();
    same = same && b.first && b.first.get() == b.again.get() && b.first->id == 4711 && b.first.use_count() == 2;
    same = same && b.emplaced && b.emplaced.get() == b.emplaced_again.get() && b.emplaced->id == 4712;
    same = same && b.own && b.own->id == 99 && b.own->next == b.first.get();
    same = same && b.text == "a text that is longer than the room a string has inside itself" && b.small == "short";
    if (!same)
        return -1;
    // And they are containers like any other: they grow, shrink and find.
    for (unsigned i = 0; i < 2000; ++i)
        b.squares[100000 + i * 3] = i;
    for (unsigned i = 0; i < 300; i += 2)
        b.squares.erase(i * 7);
    bool usable = b.squares.size() == 2150;
    last = 0;
    seen = 0;
    for (const auto &[key, value] : b.squares)
    {
        usable = usable && (!seen || key > last);
        last = key;
        ++seen;
    }
    for (unsigned i = 0; i < 5000; ++i)
        b.numbers.push_back(i);
    b.by_id.erase(30);
    b.by_id[1000] = std::make_unique<TestItem>();
    b.text += " and more";
    b.again.reset();
    usable = usable && seen == 2150 && b.numbers.size() == 6000 && b.numbers[999] == 999 * 999 && b.numbers[5999] == 4999 &&
             b.by_id.size() == 67 && b.first.use_count() == 1 && b.text.size() == 71;
    return usable ? 1 : -1;
}
#else
int TestContainers()
{
    return -1;
}
#endif
bool Available(const char **why)
{
    if (why)
        *why = HaveLayout() ? "" : "this NEO98 has no table of its fields beside the program";
    return HaveLayout();
}
unsigned ScanPointers(X86::Application &first, void *file, bool verbose)
{
#if defined(__APPLE__) || defined(__linux__)
    // NEO98_STATE_TEST_LEAK: a variable of the port that holds an address of
    // the host, which the guards have to find (tools/test_state_host.py).
    // "pointer": as a pointer, which refuses the save. "number": as a
    // number, which the scan of the written bytes finds.
    static void *test_leak = nullptr;
    static unsigned long long test_number = 0;
    if (const char *leak = getenv("NEO98_STATE_TEST_LEAK"); leak && !test_leak && !test_number)
    {
        void *block = malloc(64);
        if (!strcmp(leak, "number"))
            test_number = reinterpret_cast<unsigned long long>(block);
        else
            test_leak = block;
    }
    FILE *out = static_cast<FILE *>(file);
    Machine::Guests guests;
    guests.count = Machine::Processes(first, guests.processes, ProcessCapacity);
    Outside outside;
    outside.context = &guests;
    outside.guest = Machine::GuestOf;
    outside.host = Machine::HostOf;
    Machine::Buffer part;
    char error[512] = {};
    CollectRefusals(true);
    const bool saved = SaveFields(part, &first, outside, error, sizeof error);
    CollectRefusals(false);
    if (!saved)
    {
        const unsigned refusals =
            Refusals([](void *context, const char *text) { fprintf(static_cast<FILE *>(context), "NEO98 STATE refused: %s\n", text); }, out);
        if (!refusals)
            fprintf(out, "NEO98 STATE refused: %s\n", error);
        return refusals ? refusals : 1;
    }
    const FieldNumbers n = LastFields();
    fprintf(out, "NEO98 STATE fields bytes=%zu variables=%u objects=%u object-bytes=%llu types=%u pointers=%llu "
                 "to-objects=%llu to-variables=%llu to-symbols=%llu to-guest=%llu strings=%llu\n",
            part.data.size(), n.variables, n.objects, n.object_bytes, n.types, n.pointers, n.to_objects, n.to_variables,
            n.to_symbols, n.to_guest, n.strings);
    fprintf(out, "NEO98 STATE scan suspects=%llu weak=%llu\n", n.suspects, n.weak);
    return Suspects(
        [](void *context, const char *place, bool counts) {
            fprintf(static_cast<FILE *>(context), "NEO98 STATE %s %s\n", counts ? "suspect" : "weak", place);
        },
        out, verbose);
#else
    (void)first;
    (void)file;
    (void)verbose;
    return 0;
#endif
}
Verdict CanSave(X86::Application &first)
{
    return Machine::CanSave(first);
}
SaveNumbers LastSave()
{
#if defined(__APPLE__) || defined(__linux__)
    return Machine::last_save;
#else
    return {};
#endif
}
bool Save(X86::Application &first, Sink &sink, char *error, size_t error_bytes)
{
#if defined(__APPLE__) || defined(__linux__)
    return Machine::Save(first, sink, error, error_bytes);
#else
    (void)first;
    (void)sink;
    Text(error, error_bytes, "this build keeps no states");
    return false;
#endif
}
X86::Application *Load(Source &source, const Host &host, char *error, size_t error_bytes)
{
#if defined(__APPLE__) || defined(__linux__)
    return Machine::Load(source, host, error, error_bytes);
#else
    (void)source;
    (void)host;
    Text(error, error_bytes, "this build keeps no states");
    return nullptr;
#endif
}
Verdict CanSaveDosUi(X86::Application &first)
{
    return Machine::CanSave(first, true);
}
bool SaveDosUi(X86::Application &first, Sink &sink, char *error, size_t bytes)
{
#if defined(__APPLE__) || defined(__linux__)
    return Machine::Save(first, sink, error, bytes, true);
#else
    (void)first; (void)sink; Text(error, bytes, "this build keeps no combined states"); return false;
#endif
}
X86::Application *LoadDosUi(Source &source, const Host &host, char *error, size_t bytes)
{
#if defined(__APPLE__) || defined(__linux__)
    return Machine::Load(source, host, error, bytes, true);
#else
    (void)source; (void)host; Text(error, bytes, "this build keeps no combined states"); return nullptr;
#endif
}

bool ValidateDosUi(Source &source, char *error, size_t bytes)
{
#if defined(__APPLE__) || defined(__linux__)
    return Machine::ValidateDosUi(source, error, bytes);
#else
    (void)source; Text(error, bytes, "this build keeps no combined states"); return false;
#endif
}

bool ValidateDosUiLaunch(Source &source, const DosVm::Launch &launch, char *error, size_t bytes)
{
#if defined(__APPLE__) || defined(__linux__)
    return Machine::ValidateDosUi(source, error, bytes, &launch);
#else
    (void)source; (void)launch; Text(error, bytes, "this build keeps no combined states"); return false;
#endif
}
} // namespace MachineState
