// SPDX-License-Identifier: GPL-2.0-or-later
// Structured exception handling of an ARM64 process: the functions Windows on
// ARM64 exports from KERNEL32 for it (RaiseException, RtlCaptureContext,
// RtlLookupFunctionEntry, RtlVirtualUnwind, RtlUnwindEx, RtlUnwind,
// RtlRestoreContext, RtlPcToFileHeader, SetUnhandledExceptionFilter) and the
// delivery of a processor fault as an exception. Part of class Cpu of
// src/arm64_cpu.cpp; the arithmetic of one frame is src/arm64_unwind.cpp.
//
// Under Windows this is code of NTDLL that runs in the program: it walks the
// frames by.pdata and calls the language handler of each. Here the walk runs
// in the processor of the process, and it never calls into the program on the
// host stack: to call a handler it writes its own state below the stack of the
// program (an Activation), points the registers at the handler with a return
// address in the area, and lets the program run. The `svc` at that address
// brings the walk back. A handler that never returns (it unwinds, as the
// personality of C++ does for every catch) leaves nothing behind but bytes
// below the stack pointer, and a later walk that passes such a frame reads the
// Activation there, as the unwinder of Windows meets the frames of an earlier
// dispatch (nested exception, collided unwind). docs/ARM64-APPS.md,
// "Exceptions".

// --- what lies in the program's memory ------------------------------------
enum ActivationKind : unsigned { ActivationDispatch = 1, ActivationUnwind = 2, ActivationFilter = 3 };
static constexpr unsigned ActivationMagic = 0x48455341; // "ASEH"
struct Activation
{
    unsigned magic;
    unsigned kind;
    // The EXCEPTION_RECORD the handlers get.
    U64 record;
    // Dispatch: the CONTEXT of the exception, which a handler may change and
    // the program continues with. Unwind: the scratch CONTEXT of the caller,
    // which holds the frame whose handler runs.
    U64 context;
    U64 target_frame, target_pc, return_value;
    U64 nested_frame;
    // A dispatch that began with a fault of the processor: what the run ended
    // with, for the path an unhandled fault takes.
    unsigned run_error, trap, fault_kind, syndrome;
    Unwind::DispatcherContext dispatcher;
    unsigned char nonvolatile[Unwind::NonvolatileBytes];
    // The frame above the one whose handler runs.
    Unwind::Context next;
    // Dispatch: the frame whose handler runs (the dispatcher context names it).
    // RtlUnwind: the scratch CONTEXT the caller did not bring.
    Unwind::Context frame;
    // An unwind without a record gets this one.
    Unwind::ExceptionRecord own_record;
    // EXCEPTION_POINTERS for the filter of the process.
    U64 pointers[2];
};

struct GuestMemory final : Unwind::Memory
{
    Cpu &cpu;
    explicit GuestMemory(Cpu &c) : cpu(c) {}
    bool Read(U64 address, void *out, unsigned size) override
    {
        return !(address >> 32) && cpu.Peek((unsigned)address, out, size);
    }
};

// The state of one walk while it runs on the host.
struct Walk
{
    unsigned at = 0;       // where its Activation lies; zero: nothing written yet
    bool placed = false;
    Activation a = {};
    Unwind::Context current = {};           // the frame to examine next
    Unwind::ExceptionRecord record = {};    // dispatch, before it is written
    Unwind::Context exception = {};         // dispatch: the context of the exception
    U64 stack = 0;                          // the stack pointer the walk began below
};

// --- contexts --------------------------------------------------------------
// at_call: the thread stands behind the `svc` of an entry of the area; its
// context is the one its caller goes on with.
void Capture(Unwind::Context &c, bool at_call)
{
    machine->SyncRegisters(registers);
    memset(&c, 0, sizeof c);
    c.flags = Unwind::ContextFull;
    c.cpsr = (unsigned)registers.pstate;
    for (unsigned i = 0; i < 31; ++i) c.x[i] = registers.x[i];
    c.sp = registers.sp;
    c.pc = at_call ? registers.x[30] : registers.pc;
    for (unsigned i = 0; i < 32; ++i)
    {
        c.v[i].low = registers.v[i].low;
        c.v[i].high = registers.v[i].high;
    }
    c.fpcr = (unsigned)registers.fpcr;
    c.fpsr = (unsigned)registers.fpsr;
}
// The program goes on with this context. x18 stays: it is the TEB.
void Continue(const Unwind::Context &c)
{
    machine->SyncRegisters(registers);
    for (unsigned i = 0; i < 31; ++i)
        if (i != 18) registers.x[i] = c.x[i];
    registers.sp = c.sp;
    registers.pc = c.pc;
    registers.pstate = (registers.pstate & ~0xf0000000ull) | (c.cpsr & 0xf0000000u);
    for (unsigned i = 0; i < 32; ++i)
    {
        registers.v[i].low = c.v[i].low;
        registers.v[i].high = c.v[i].high;
    }
    registers.fpcr = c.fpcr;
    registers.fpsr = c.fpsr;
    machine->VectorsChanged(registers);
    Standing();
}
// The registers were set from here: the thread stands between two
// instructions, and the transitions its new stack pointer left behind are gone.
void Standing()
{
    Record record;
    unsigned at = 0;
    while (TopRecord(record, at) && at < registers.sp)
    {
        // Return to Application::Call before executing the continuation:
        // its native frames must leave without restoring their old context.
        if (record.callback) stop = true;
        SetTopRecord(record.previous);
    }
    if (!(registers.sp >> 32) && registers.sp > StackMargin) slots[Reg::Esp] = ((unsigned)registers.sp - StackMargin) & ~15u;
    eip_slot = ContinueAddress(layout);
    entry = false;
    live = true;
    refused = {};
}

// NEO98_ARM64_SEH_TRACE in the environment of the host: every step of a walk
// on stderr.
static bool Tracing()
{
    static const bool on = getenv("NEO98_ARM64_SEH_TRACE") != nullptr;
    return on;
}
// --- one frame ---------------------------------------------------------------
enum StepKind { StepFrame, StepEnd, StepSystem };
struct StepInfo
{
    U64 handler = 0, handler_data = 0, frame = 0, control_pc = 0, image_base = 0, entry_address = 0;
    bool unwound = false;
    Unwind::Context next = {};
};
bool ImageAt(U64 pc, Unwind::Image &image)
{
    unsigned base = 0;
    if (pc >> 32 || !layout.image_of || !layout.image_of(layout.context, (unsigned)pc, base)) return false;
    GuestMemory memory(*this);
    const bool found = Unwind::ReadImage(memory, base, image);
    refused = {};
    return found;
}
StepKind Step(const Unwind::Context &current, StepInfo &info, Activation &hit, unsigned &hit_at)
{
    GuestMemory memory(*this);
    info = {};
    info.unwound = current.flags & Unwind::ContextUnwoundToCall;
    // The frame of a walk that called a handler: its state lies where the
    // stack pointer of that call stood.
    if (current.pc == handler_return && !(current.sp >> 32))
    {
        if (Peek((unsigned)current.sp, &hit, sizeof hit) && hit.magic == ActivationMagic)
        {
            hit_at = (unsigned)current.sp;
            return StepSystem;
        }
        refused = {};
        return StepEnd;
    }
    if (!current.pc || current.pc >> 32 || current.sp >> 32) return StepEnd;
    if (info.unwound && (InGates(current.pc) || InArea(current.pc)))
    {
        // Search is read-only. A matching callback record bridges the native
        // frames to the guest caller of SendMessage, an enumeration, etc.
        // Process/thread roots have no caller and still end the walk.
        Record record;
        unsigned at = 0;
        if (!TopRecord(record, at)) return StepEnd;
        for (unsigned n = 0; at && n < 256; ++n)
        {
            if (record.callback && record.sp == current.sp && record.return_gate == current.pc && record.saved)
            {
                Registers saved;
                if (!Peek(record.saved, &saved, sizeof saved)) break;
                if (!record.interrupted)
                {
                    Record caller;
                    if (!record.previous || !Peek(record.previous, &caller, sizeof caller) ||
                        caller.magic != RecordMagic || caller.callback) break;
                    saved.pc = saved.x[30];
                    saved.sp = caller.sp;
                }
                info.next = {};
                info.next.flags = Unwind::ContextFull | (record.interrupted ? 0 : Unwind::ContextUnwoundToCall);
                info.next.cpsr = (unsigned)saved.pstate;
                for (unsigned i = 0; i < 31; ++i) info.next.x[i] = saved.x[i];
                for (unsigned i = 0; i < 32; ++i)
                {
                    info.next.v[i].low = saved.v[i].low;
                    info.next.v[i].high = saved.v[i].high;
                }
                info.next.pc = saved.pc;
                info.next.sp = saved.sp;
                info.next.fpcr = (unsigned)saved.fpcr;
                info.next.fpsr = (unsigned)saved.fpsr;
                info.frame = current.sp;
                return info.next.sp > current.sp ? StepFrame : StepEnd;
            }
            if (!record.previous || record.previous <= at) break;
            at = record.previous;
            if (!Peek(at, &record, sizeof record) || record.magic != RecordMagic) break;
        }
        refused = {};
        return StepEnd;
    }
    info.control_pc = current.pc - (info.unwound ? 4 : 0);
    Unwind::Image image;
    Unwind::RuntimeFunction function = {};
    bool has_function = false;
    if (ImageAt(info.control_pc, image))
    {
        info.image_base = image.base;
        has_function = Unwind::Lookup(memory, image, info.control_pc, function, info.entry_address);
    }
    info.next = current;
    const Unwind::Result result = Unwind::VirtualUnwind(memory, info.image_base, info.control_pc, has_function ? &function : nullptr,
                                                        info.next, info.handler, info.handler_data, info.frame);
    refused = {};
    if (Tracing())
        fprintf(stderr, "ARM64 SEH step pc=%llx control=%llx base=%llx entry=%llx result=%u handler=%llx frame=%llx next pc=%llx sp=%llx\n",
                current.pc, info.control_pc, info.image_base, info.entry_address, (unsigned)result, info.handler, info.frame,
                info.next.pc, info.next.sp);
    if (result != Unwind::Ok) return StepEnd;
    // A stack grows down: a frame above never has a lower stack pointer, and
    // a step that changes nothing would not end.
    if (info.next.sp < current.sp || (info.next.sp == current.sp && info.next.pc == current.pc)) return StepEnd;
    return StepFrame;
}

// --- placing a walk in the program -------------------------------------------
bool WriteActivation(Walk &w)
{
    w.a.magic = ActivationMagic;
    return Poke(w.at, &w.a, sizeof w.a);
}
// A dispatch writes its record and the context of the exception below the
// stack of the program when the first handler needs them.
bool Place(Walk &w)
{
    if (w.placed) return true;
    if (w.stack >> 32 || w.stack < 0x10000) return false;
    unsigned cursor = ((unsigned)w.stack - StackMargin) & ~15u;
    if (w.a.kind == ActivationDispatch)
    {
        cursor = (cursor - (unsigned)sizeof(Unwind::ExceptionRecord)) & ~15u;
        w.a.record = cursor;
        cursor = (cursor - (unsigned)sizeof(Unwind::Context)) & ~15u;
        w.a.context = cursor;
        if (!Poke((unsigned)w.a.record, &w.record, sizeof w.record) || !Poke((unsigned)w.a.context, &w.exception, sizeof w.exception))
        {
            refused = {};
            return false;
        }
    }
    cursor = (cursor - (unsigned)sizeof(Activation)) & ~15u;
    w.at = cursor;
    if (w.a.kind == ActivationUnwind)
    {
        if (!w.a.record)
        {
            w.a.record = w.at + offsetof(Activation, own_record);
        }
        if (!w.a.context) w.a.context = w.at + offsetof(Activation, frame);
    }
    if (!WriteActivation(w))
    {
        refused = {};
        return false;
    }
    w.placed = true;
    return true;
}
bool RecordFlags(Walk &w, unsigned set, unsigned clear)
{
    if (!w.placed)
    {
        w.record.flags = (w.record.flags | set) & ~clear;
        return true;
    }
    unsigned flags = 0;
    if (!Peek32((unsigned)w.a.record + 4, flags)) return false;
    return Poke32((unsigned)w.a.record + 4, (flags | set) & ~clear);
}
void FillNonvolatile(Walk &w, const Unwind::Context &frame)
{
    U64 values[19];
    for (unsigned i = 0; i < 11; ++i) values[i] = frame.x[19 + i];
    for (unsigned i = 0; i < 8; ++i) values[11 + i] = frame.v[8 + i].low;
    memcpy(w.a.nonvolatile, values, sizeof values);
}
// The program runs the handler; its return is the `svc` of handler_return.
unsigned Enter(Walk &w, U64 procedure, U64 x0, U64 x1, U64 x2, U64 x3)
{
    if (!handler_return) handler_return = SystemEntry(AreaEntry::ReturnEntry, 0);
    if (!handler_return)
    {
        snprintf(detail, sizeof detail, "ARM64 area has no room for the return of an exception handler");
        return CpuError::Exception;
    }
    if (!WriteActivation(w)) return RaiseRefused();
    registers.x[0] = x0;
    registers.x[1] = x1;
    registers.x[2] = x2;
    registers.x[3] = x3;
    registers.x[30] = handler_return;
    registers.sp = w.at;
    registers.pc = procedure;
    Standing();
    return CpuError::Ok;
}

// --- the search for a handler ------------------------------------------------
enum WalkResult { WalkRunning, WalkUnhandled, WalkError };
WalkResult Dispatch(Walk &w, unsigned &error)
{
    for (unsigned rounds = 0; rounds < 8192; ++rounds)
    {
        StepInfo step;
        Activation hit;
        unsigned hit_at = 0;
        const StepKind kind = Step(w.current, step, hit, hit_at);
        if (kind == StepEnd) break;
        if (kind == StepSystem)
        {
            // The exception came up inside a handler. Behind an unwind the
            // search goes on at the frame that handler ran for; behind a
            // dispatch at the context of that exception, and the handlers on
            // the way are told that they are called again.
            Unwind::Context below;
            if (hit.kind == ActivationUnwind)
            {
                if (!Peek((unsigned)hit.dispatcher.context_record, &below, sizeof below)) break;
            }
            else
            {
                if (!Peek((unsigned)hit.context, &below, sizeof below)) break;
                if (hit.kind == ActivationDispatch)
                {
                    RecordFlags(w, Unwind::ExceptionNestedCall, 0);
                    if (w.a.nested_frame < hit.dispatcher.establisher_frame) w.a.nested_frame = hit.dispatcher.establisher_frame;
                }
            }
            if (below.sp < w.current.sp) break;
            w.current = below;
            continue;
        }
        if (step.handler)
        {
            if (!Place(w)) break;
            auto &d = w.a.dispatcher;
            d = {};
            d.control_pc = w.current.pc;
            d.image_base = step.image_base;
            d.function_entry = step.entry_address;
            d.establisher_frame = step.frame;
            d.context_record = w.at + offsetof(Activation, frame);
            d.language_handler = step.handler;
            d.handler_data = step.handler_data;
            d.control_pc_is_unwound = step.unwound;
            d.nonvolatile_registers = w.at + offsetof(Activation, nonvolatile);
            w.a.frame = w.current;
            w.a.next = step.next;
            FillNonvolatile(w, w.current);
            error = Enter(w, step.handler, w.a.record, step.frame, w.a.context, w.at + offsetof(Activation, dispatcher));
            return error ? WalkError : WalkRunning;
        }
        if (w.a.nested_frame && step.frame == w.a.nested_frame)
        {
            RecordFlags(w, 0, Unwind::ExceptionNestedCall);
            w.a.nested_frame = 0;
        }
        w.current = step.next;
    }
    refused = {};
    // No frame took it: the filter of the process decides, once.
    if (unhandled_filter && w.a.kind == ActivationDispatch && Place(w))
    {
        w.a.kind = ActivationFilter;
        w.a.pointers[0] = w.a.record;
        w.a.pointers[1] = w.a.context;
        error = Enter(w, unhandled_filter, w.at + offsetof(Activation, pointers), 0, 0, 0);
        return error ? WalkError : WalkRunning;
    }
    return WalkUnhandled;
}

// --- unwinding ---------------------------------------------------------------
WalkResult UnwindFrames(Walk &w, unsigned &error)
{
    for (unsigned rounds = 0; rounds < 8192; ++rounds)
    {
        StepInfo step;
        Activation hit;
        unsigned hit_at = 0;
        const StepKind kind = Step(w.current, step, hit, hit_at);
        if (kind == StepEnd) break;
        Unwind::DispatcherContext d = {};
        if (kind == StepSystem)
        {
            if (hit.kind != ActivationUnwind)
            {
                // Through a dispatch: on at the context of its exception.
                Unwind::Context below;
                if (!Peek((unsigned)hit.context, &below, sizeof below) || below.sp < w.current.sp) break;
                w.current = below;
                continue;
            }
            // A collided unwind: the handler of an earlier unwind started
            // this one. It goes on where that one stood, at the same frame,
            // and the handler of that frame is called once more.
            if (!Peek((unsigned)hit.dispatcher.context_record, &w.current, sizeof w.current)) break;
            d = hit.dispatcher;
            step.handler = d.language_handler;
            step.handler_data = d.handler_data;
            step.frame = d.establisher_frame;
            step.next = hit.next;
            RecordFlags(w, Unwind::ExceptionCollidedUnwind, 0);
        }
        else
        {
            d.control_pc = w.current.pc;
            d.image_base = step.image_base;
            d.function_entry = step.entry_address;
            d.establisher_frame = step.frame;
            d.language_handler = step.handler;
            d.handler_data = step.handler_data;
            d.control_pc_is_unwound = step.unwound;
        }
        if (w.a.target_frame && step.frame > w.a.target_frame) break;
        if (step.handler)
        {
            if (!Place(w)) break;
            if (step.frame == w.a.target_frame) RecordFlags(w, Unwind::ExceptionTargetUnwind, 0);
            d.target_pc = w.a.target_pc;
            d.context_record = w.a.context;
            d.history_table = 0;
            d.nonvolatile_registers = w.at + offsetof(Activation, nonvolatile);
            w.a.dispatcher = d;
            w.a.next = step.next;
            FillNonvolatile(w, w.current);
            if (!Poke((unsigned)w.a.context, &w.current, sizeof w.current)) break;
            error = Enter(w, step.handler, w.a.record, step.frame, w.a.context, w.at + offsetof(Activation, dispatcher));
            return error ? WalkError : WalkRunning;
        }
        if (step.frame == w.a.target_frame && w.a.target_frame)
        {
            Arrive(w);
            return WalkRunning;
        }
        w.current = step.next;
    }
    refused = {};
    return WalkUnhandled;
}
// The target frame is reached: the program goes on in it at the target
// address, with the return value in x0.
void Arrive(Walk &w)
{
    Unwind::Context c = w.current;
    c.pc = w.a.target_pc;
    c.x[0] = w.a.return_value;
    Continue(c);
}

// --- the end of a walk nobody answered ---------------------------------------
// The exception goes to the process: its registers are the ones of the
// exception again, and Run ends with what the fault ended it with, or with an
// exception of its own.
unsigned Final(Walk &w, unsigned code, bool quiet)
{
    Unwind::ExceptionRecord record = w.record;
    Unwind::Context context = w.exception;
    if (w.placed && w.a.kind != ActivationUnwind)
    {
        Peek((unsigned)w.a.record, &record, sizeof record);
        Peek((unsigned)w.a.context, &context, sizeof context);
        refused = {};
    }
    else if (w.a.kind == ActivationUnwind)
    {
        if (w.a.record) Peek((unsigned)w.a.record, &record, sizeof record);
        context = w.current;
        refused = {};
    }
    unhandled = {};
    unhandled.valid = true;
    unhandled.code = code ? code : record.code;
    unhandled.flags = record.flags;
    unhandled.address = record.address;
    unhandled.parameters = record.parameters > 15 ? 15 : record.parameters;
    for (unsigned i = 0; i < unhandled.parameters; ++i) unhandled.information[i] = record.information[i];
    unhandled.run_error = w.a.run_error;
    unhandled.trap = w.a.trap;
    unhandled.fault_kind = w.a.fault_kind;
    unhandled.quiet = quiet;
    Continue(context);
    fault_valid = true;
    fault_pc = context.pc;
    fault_sp = context.sp;
    fault_lr = context.x[30];
    fault_syndrome = w.a.syndrome;
    eip_slot = (unsigned)context.pc;
    return w.a.run_error ? w.a.run_error : (unsigned)CpuError::Exception;
}
unsigned Finish(Walk &w, WalkResult result, unsigned error, unsigned code)
{
    if (result == WalkRunning) return CpuError::Ok;
    if (result == WalkError) return error;
    return Final(w, code, false);
}

// --- the handler returned ----------------------------------------------------
unsigned HandlerReturned()
{
    Walk w;
    if (registers.sp >> 32 || !Peek((unsigned)registers.sp, &w.a, sizeof w.a) || w.a.magic != ActivationMagic)
    {
        refused = {};
        snprintf(detail, sizeof detail, "ARM64 exception handler returned with a stack pointer %016llx that holds no walk", registers.sp);
        return CpuError::Exception;
    }
    w.at = (unsigned)registers.sp;
    w.placed = true;
    const int answer = (int)registers.x[0];
    unsigned error = 0;
    if (w.a.kind == ActivationFilter)
    {
        // EXCEPTION_CONTINUE_EXECUTION, or the process ends: with the error
        // box (CONTINUE_SEARCH) or without (EXECUTE_HANDLER).
        if (answer == -1)
        {
            Unwind::Context c;
            if (!Peek((unsigned)w.a.context, &c, sizeof c)) return RaiseRefused();
            Continue(c);
            return CpuError::Ok;
        }
        return Final(w, 0, answer == 1);
    }
    if (w.a.kind == ActivationDispatch)
    {
        if (answer == 0)
        {
            // ExceptionContinueExecution.
            unsigned flags = 0;
            Unwind::Context c;
            if (!Peek32((unsigned)w.a.record + 4, flags) || !Peek((unsigned)w.a.context, &c, sizeof c)) return RaiseRefused();
            if (flags & Unwind::ExceptionNoncontinuable) return Final(w, 0xc0000025u, false);
            Continue(c);
            return CpuError::Ok;
        }
        if (answer != 1) return Final(w, 0xc0000026u, false); // an invalid disposition
        if (w.a.nested_frame && w.a.dispatcher.establisher_frame == w.a.nested_frame)
        {
            RecordFlags(w, 0, Unwind::ExceptionNestedCall);
            w.a.nested_frame = 0;
        }
        w.current = w.a.next;
        const WalkResult result = Dispatch(w, error);
        return Finish(w, result, error, 0);
    }
    if (answer != 1) return Final(w, 0xc0000026u, false);
    // What the handler left in the context of its frame is what the frame
    // goes on with (the personality of C++ sets x1 there).
    if (!Peek((unsigned)w.a.context, &w.current, sizeof w.current)) return RaiseRefused();
    RecordFlags(w, 0, Unwind::ExceptionCollidedUnwind);
    if (w.a.target_frame && w.a.dispatcher.establisher_frame == w.a.target_frame)
    {
        Arrive(w);
        return CpuError::Ok;
    }
    w.current = w.a.next;
    const WalkResult result = UnwindFrames(w, error);
    // An unwind that finds its target no more: STATUS_INVALID_UNWIND_TARGET.
    return Finish(w, result, error, 0xc0000029u);
}

// --- the exports -------------------------------------------------------------
enum CoreFunction : int
{
    CoreRaiseException,
    CoreRtlCaptureContext,
    CoreRtlLookupFunctionEntry,
    CoreRtlVirtualUnwind,
    CoreRtlUnwindEx,
    CoreRtlUnwind,
    CoreRtlRestoreContext,
    CoreRtlPcToFileHeader,
    CoreSetUnhandledExceptionFilter,
    CoreCount
};
static int CoreIndex(const char *name)
{
    static const char *const names[CoreCount] = {"RaiseException", "RtlCaptureContext", "RtlLookupFunctionEntry",
                                                 "RtlVirtualUnwind", "RtlUnwindEx", "RtlUnwind", "RtlRestoreContext",
                                                 "RtlPcToFileHeader", "SetUnhandledExceptionFilter"};
    for (int i = 0; i < CoreCount; ++i)
        if (!strcmp(names[i], name)) return i;
    return -1;
}
// A dispatch of this exception from the registers as they stand.
unsigned Raise(const Unwind::ExceptionRecord &record, bool at_call, unsigned run_error, unsigned trap, unsigned fault_kind,
               unsigned syndrome, bool &taken)
{
    Walk w;
    w.a.kind = ActivationDispatch;
    w.a.run_error = run_error;
    w.a.trap = trap;
    w.a.fault_kind = fault_kind;
    w.a.syndrome = syndrome;
    w.record = record;
    Capture(w.exception, at_call);
    if (!w.record.address) w.record.address = w.exception.pc;
    // Behind a call the pc is a return address; the frame it belongs to is
    // the one of the instruction before. Preserve that fact in the saved
    // exception context too, since unwinding resumes there after dispatch.
    if (at_call) w.exception.flags |= Unwind::ContextUnwoundToCall;
    w.current = w.exception;
    w.stack = registers.sp;
    unsigned error = 0;
    const WalkResult result = Dispatch(w, error);
    taken = result == WalkRunning;
    if (result == WalkUnhandled && run_error) return run_error; // the fault stays what it was
    return Finish(w, result, error, 0);
}
unsigned StartUnwind(U64 target_frame, U64 target_pc, U64 record, U64 return_value, U64 context)
{
    Walk w;
    w.a.kind = ActivationUnwind;
    w.a.target_frame = target_frame;
    w.a.target_pc = target_pc;
    w.a.return_value = return_value;
    w.a.record = record;
    w.a.context = context;
    Capture(w.current, true);
    w.current.flags |= Unwind::ContextUnwoundToCall;
    w.stack = registers.sp;
    // Without a record the unwind brings STATUS_UNWIND.
    w.a.own_record = {};
    w.a.own_record.code = 0xc0000027u;
    w.a.own_record.address = w.current.pc;
    const unsigned flags = Unwind::ExceptionUnwinding | (target_frame ? 0 : Unwind::ExceptionExitUnwind);
    if (record)
    {
        unsigned old = 0;
        if (!Peek32((unsigned)record + 4, old) || !Poke32((unsigned)record + 4, old | flags)) return RaiseRefused();
    }
    else
        w.a.own_record.flags = flags;
    unsigned error = 0;
    const WalkResult result = UnwindFrames(w, error);
    return Finish(w, result, error, target_frame ? 0xc0000029u : 0);
}
// `svc` in an entry of the area that stands for one of the exports above. The
// arguments are in x0..x7 as the program passed them; the `ret` behind the
// `svc` is the return.
unsigned CoreCall(int which)
{
    machine->SyncGeneral(registers);
    GuestMemory memory(*this);
    U64 *const x = registers.x;
    switch (which)
    {
    case CoreRaiseException:
    {
        Unwind::ExceptionRecord record = {};
        record.code = (unsigned)x[0];
        record.flags = (unsigned)x[1] & Unwind::ExceptionNoncontinuable;
        record.address = x[30];
        if (x[3])
        {
            record.parameters = (unsigned)x[2] > 15 ? 15 : (unsigned)x[2];
            if (record.parameters && !Peek((unsigned)x[3], record.information, 8 * record.parameters)) return RaiseRefused();
        }
        bool taken = false;
        return Raise(record, true, 0, 0, 0, 0, taken);
    }
    case CoreRtlCaptureContext:
    {
        Unwind::Context c;
        Capture(c, true);
        if (x[0] >> 32 || !Poke((unsigned)x[0], &c, sizeof c)) return RaiseRefused();
        return CpuError::Ok;
    }
    case CoreRtlLookupFunctionEntry:
    {
        Unwind::Image image;
        Unwind::RuntimeFunction function;
        U64 entry = 0, base = 0;
        if (ImageAt(x[0], image))
        {
            base = image.base;
            if (!Unwind::Lookup(memory, image, x[0], function, entry)) entry = 0;
            refused = {};
        }
        if (x[1] && !Poke64((unsigned)x[1], base)) return RaiseRefused();
        x[0] = entry;
        return CpuError::Ok;
    }
    case CoreRtlVirtualUnwind:
    {
        Unwind::Context c;
        Unwind::RuntimeFunction function = {};
        if (x[4] >> 32 || !Peek((unsigned)x[4], &c, sizeof c)) return RaiseRefused();
        if (x[3] && !Peek((unsigned)x[3], &function, sizeof function)) return RaiseRefused();
        U64 handler = 0, data = 0, frame = 0;
        if (Unwind::VirtualUnwind(memory, x[1], x[2], x[3] ? &function : nullptr, c, handler, data, frame) != Unwind::Ok)
        {
            // As Windows answers a walk it cannot continue: no pc left.
            c.pc = 0;
            handler = 0;
        }
        refused = {};
        if (!Poke((unsigned)x[4], &c, sizeof c) || (x[5] && !Poke64((unsigned)x[5], data)) || (x[6] && !Poke64((unsigned)x[6], frame)))
            return RaiseRefused();
        x[0] = handler;
        return CpuError::Ok;
    }
    case CoreRtlUnwindEx: return StartUnwind(x[0], x[1], x[2], x[3], x[4]);
    case CoreRtlUnwind: return StartUnwind(x[0], x[1], x[2], x[3], 0);
    case CoreRtlRestoreContext:
    {
        Unwind::Context c;
        if (x[0] >> 32 || !Peek((unsigned)x[0], &c, sizeof c)) return RaiseRefused();
        Continue(c);
        return CpuError::Ok;
    }
    case CoreRtlPcToFileHeader:
    {
        Unwind::Image image;
        const U64 base = ImageAt(x[0], image) ? image.base : 0;
        if (x[1] && !Poke64((unsigned)x[1], base)) return RaiseRefused();
        x[0] = base;
        return CpuError::Ok;
    }
    case CoreSetUnhandledExceptionFilter:
    {
        const U64 previous = unhandled_filter;
        unhandled_filter = x[0];
        x[0] = previous;
        return CpuError::Ok;
    }
    default:
        snprintf(detail, sizeof detail, "ARM64 core function %d is not provided", which);
        return CpuError::Exception;
    }
}
