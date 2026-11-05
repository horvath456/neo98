// SPDX-License-Identifier: GPL-2.0-or-later
// A processor fault of the guest becomes a Win32 exception record and goes
// down the FS:[0] chain of the running thread, the way the Win98 VMM does it.
// The dispatcher itself is src/x86_exceptions.inc; this file builds the record
// that a page fault, a breakpoint or a single step produces and resumes the
// guest when a handler says it may continue.
namespace X86 {
namespace {
// The engine memory errors the VMM turns into an access violation, with the
// access kind the first parameter of the record carries.
bool AccessViolation(unsigned type, unsigned &access)
{
    switch (type)
    {
    case CpuFault::ReadUnmapped:
    case CpuFault::ReadProtected: access = 0; return true;
    case CpuFault::WriteUnmapped:
    case CpuFault::WriteProtected: access = 1; return true;
    case CpuFault::FetchUnmapped:
    case CpuFault::FetchProtected: access = 8; return true;
    default: return false;
    }
}
bool MemoryError(unsigned code)
{
    return code == CpuError::ReadUnmapped || code == CpuError::WriteUnmapped || code == CpuError::FetchUnmapped ||
           code == CpuError::ReadProtected || code == CpuError::WriteProtected || code == CpuError::FetchProtected;
}
}
// Builds the record and the context of the running thread, hands them to the
// original dispatcher and restores the context a handler asked to continue
// Return whether the guest may continue.
bool Application::DispatchGuestException(unsigned code, unsigned trap, unsigned parameters,
                                         unsigned first, unsigned second, const char *described)
{
    const unsigned lease = Allocate(288);
    if (!lease)
        return false;
    const unsigned record = lease, context = lease + 80;
    ExceptionEnvironment e{*this};
    GuestExceptionContext captured;
    captured.d[0] = ContextFlags;
    for (unsigned i = 0; i < 16; ++i)
        captured.d[35 + i] = Register(ContextRegisters[i]);
    Store(context, &captured, sizeof captured);
    if (log)
    {
        char text[200];
        // The two parameters of an access violation are the access kind and
        // the address; they say which operand of the instruction failed.
        snprintf(text, sizeof text, "EXCEPTION %08x trap %u at %08x eflags %08x depth=%u info=%u,%08x,%08x", code,
                 trap, Register(Reg::Eip), Register(Reg::Eflags), depth, parameters, first, second);
        log(text);
    }
    Win98Exceptions::Record(e, record, code, 0, 0, 0);
    Write32(record + 12, Register(Reg::Eip));
    Write32(record + 16, parameters);
    if (parameters > 0) Write32(record + 20, first);
    if (parameters > 1) Write32(record + 24, second);
    const unsigned before_abandoned = ExceptionState().abandoned_callbacks;
    const auto result = Win98Exceptions::Dispatch(e, context, record, trap);
    if (result != Win98Exceptions::Continue || !e.Alive())
    {
        Free(lease);
        if (ExceptionState().abandoned_callbacks != before_abandoned)
        {
            // A handler left through a non-local jump. The guest already
            // stands where it wants to go on, so nothing is restored, and the
            // run did not come back to the faulting instruction, so this is
            // not the repeat the count below guards against.
            fault_repeats = {};
            return state != Failed && state != Exited;
        }
        // An exception nobody handles ends the run, and then the address the
        // guest stumbled over is what the matrix groups by. The dispatcher's
        // own message names the code but not the address.
        if (state == Failed && described && *described)
        {
            const unsigned used = (unsigned)strlen(error);
            if (used + strlen(described) + 2 < sizeof error)
                snprintf(error + used, sizeof error - used, ",%s", described);
        }
        return state != Failed && state != Exited;
    }
    GuestExceptionContext continued;
    if (Read(context, &continued, sizeof continued))
    {
        if (log)
        {
            char text[128];
            snprintf(text, sizeof text, "EXCEPTION handled, continues at %08x esp %08x", continued.d[46],
                     continued.d[49]);
            log(text);
        }
        if ((continued.d[0] & ~ContextFlags) || !(continued.d[0] & 0x10000))
            Fail("SEH context requests unported register groups");
        else if (!virtual_memory.Access(continued.d[46], 1, false) ||
                 !virtual_memory.Access(continued.d[49], 1, false))
            Fail("SEH continuation targets inaccessible guest memory");
        else
            for (unsigned i = 0; i < 16; ++i)
            {
                const unsigned group = i < 4 ? 4 : (i < 10 ? 2 : 1);
                if (continued.d[0] & group)
                    Register(ContextRegisters[i], continued.d[35 + i]);
            }
    }
    Free(lease);
    return state != Failed && state != Exited;
}
// Called by Application::Step with the result of uc_emu_start. Answers true
// when the stop was a guest exception that has been delivered, so the thread
// keeps running; false leaves the stop to Check.
bool Application::GuestFault(unsigned code)
{
    if (state == Failed || state == Exited || !cpu)
        return false;
    if (Arm64Process())
        return Arm64GuestFault(code);
    // Delivering an exception calls guest handlers, and that needs a running
    // fiber, exactly as the RaiseException entry does.
    if (!threads[current_thread].fiber || !threads[current_thread].fiber->Active())
        return false;
    if (!code && pending_exception.valid)
    {
        const unsigned raised = pending_exception.code;
        pending_exception = {};
        return DispatchGuestException(raised, raised == 0x80000004u ? 1u : 3u, 0, 0, 0, nullptr);
    }
    pending_exception = {};
    // An opcode the processor does not know is trap 6, the "invalid
    // instruction" of Win98's error box (EXCEPTION_ILLEGAL_INSTRUCTION). Only
    // where a runner provides that box (src/x86_fault_box.inc); elsewhere the
    // stop keeps the message the tests read. A handler that continues at the
    // same instruction faults again, bounded like the memory fault below.
    if (code == CpuError::InvalidInstruction && fault_box && !Win16Process())
    {
        const unsigned at = Register(Reg::Eip);
        if (fault_repeats.address == at && fault_repeats.fault == at)
        {
            if (++fault_repeats.count > 8)
            {
                fault_repeats = {};
                return false;
            }
        }
        else
            fault_repeats = {at, at, 1};
        return DispatchGuestException(0xc000001du, 6, 0, 0, 0, nullptr);
    }
    // CLI and STI of a Win32 program. Its threads run at IOPL 0, so the
    // processor stops with a general protection fault, and the VMM answers it
    // for the protected-mode application by virtualizing the interrupt flag:
    // the instruction is passed over and the program goes on. MSO.DLL of
    // Office XP brackets its own InterlockedCompareExchange for Windows 95
    // with CLI and STI (30B25194), and Word ended there.
    if (code == CpuError::Exception && !Win16Process())
    {
        const unsigned at = Register(Reg::Eip);
        unsigned char opcode = 0;
        if (virtual_memory.Access(at, 1, false) && Read(at, &opcode, 1) && (opcode == 0xfa || opcode == 0xfb))
        {
            Register(Reg::Eip, at + 1);
            return true;
        }
    }
    unsigned access = 0;
    if (!code || !memory_fault.valid || !MemoryError(code) || !AccessViolation(memory_fault.type, access))
        return false;
    const unsigned faulting = Register(Reg::Eip);
    // A handler that continues at the same instruction without repairing the
    // access faults again. Win98 loops there too; this port stops after a few
    // rounds so a run ends with a report instead of spinning.
    if (fault_repeats.address == faulting && fault_repeats.fault == memory_fault.address)
    {
        if (++fault_repeats.count > 8)
        {
            fault_repeats = {};
            return false;
        }
    }
    else
        fault_repeats = {faulting, memory_fault.address, 1};
    char described[200] = {};
    DescribeMemoryFault(described, sizeof described);
    // The record stays valid: a handler that repairs nothing leaves the run to
    // Check, and that report names the address.
    const unsigned address = memory_fault.address;
    const unsigned size = memory_fault.size ? memory_fault.size : 1;
    const bool continued = DispatchGuestException(0xc0000005u, 0xe, 2, access, address, described);
    // A successful repair is not a repeated failure. A stream may release
    // and reserve the same buffer between reads, committing its pages in SEH.
    // Those independent faults must not exhaust the retry count.
    // Win98's dispatcher continues each handled access violation;
    // the port's loop bound is only for retries that still cannot succeed.
    // Capture the operand before dispatch, since a handler may fault itself.
    if (continued && (Register(Reg::Eip) != faulting ||
                      (access != 8 && virtual_memory.Access(address, size, access == 1))))
        fault_repeats = {};
    return continued;
}
// The same for an ARM64 process. Its frames have no FS:[0] chain; their
// handlers stand in.pdata, and the processor of the process walks them
// (src/arm64_seh.inc). First the fault goes there. When no frame has a
// handler, or every handler and the filter of the process declined, it ends
// the way an unhandled fault of any process ends: through the dispatcher
// above, whose chain is empty here, to the error box or the failure message.
bool Application::Arm64GuestFault(unsigned code)
{
    if (!threads[current_thread].fiber || !threads[current_thread].fiber->Active())
        return false;
    pending_exception = {};
    Arm64::UnhandledException final;
    const bool second = Arm64::TakeUnhandled(*cpu, final);
    unsigned exception = 0, trap = 0, count = 0, kind = 0;
    unsigned long long information[2] = {};
    if (second)
    {
        exception = final.code;
        trap = final.trap;
        kind = final.fault_kind;
        count = final.parameters > 2 ? 2 : final.parameters;
        information[0] = final.information[0];
        information[1] = final.information[1];
        if (trap == 0xe)
        {
            // The report names the access the exception began with.
            memory_fault.valid = true;
            memory_fault.type = kind;
            memory_fault.address = (unsigned)information[1];
        }
        if (final.quiet)
        {
            // The filter of the program answered EXCEPTION_EXECUTE_HANDLER.
            char message[128];
            snprintf(message, sizeof message, "Unhandled guest exception %08x at %08x", exception, (unsigned)final.address);
            if (fault_box && user_initialized)
            {
                if (log) log(message);
                TerminateSelf(exception);
            }
            else
                Fail(message);
            return false;
        }
    }
    else if (code == CpuError::InvalidInstruction)
    {
        // `brk` with the numbers Windows on ARM64 gives a meaning: a
        // breakpoint, a failed assertion, a fast fail, a division by zero
        // the compiler checks for. Anything else is an instruction the
        // processor does not know.
        unsigned long long pc = 0, sp = 0, lr = 0;
        unsigned syndrome = 0;
        Arm64::FaultState(*cpu, pc, sp, lr, syndrome);
        exception = 0xc000001du;
        trap = 6;
        if (syndrome >> 26 == 0x3c)
            switch (syndrome & 0xffff)
            {
            case 0xf000: exception = 0x80000003u; trap = 3; break;
            case 0xf001: exception = 0xc0000420u; break;
            case 0xf003: exception = 0xc0000409u; break;
            case 0xf004: exception = 0xc0000094u; trap = 0; break;
            default: break;
            }
    }
    else
    {
        unsigned access = 0;
        if (!code || !memory_fault.valid || !MemoryError(code) || !AccessViolation(memory_fault.type, access))
            return false;
        exception = 0xc0000005u;
        trap = 0xe;
        kind = memory_fault.type;
        count = 2;
        information[0] = access;
        information[1] = memory_fault.address;
    }
    if (!second && exception != 0xc0000409u)
    {
        // A handler that continues at the same instruction without repairing
        // anything faults again; a few rounds, then the fault is final.
        const unsigned at = Register(Reg::Eip), where = trap == 0xe ? (unsigned)information[1] : at;
        bool again = true;
        if (fault_repeats.address == at && fault_repeats.fault == where)
        {
            if (++fault_repeats.count > 8)
            {
                fault_repeats = {};
                again = false;
            }
        }
        else
            fault_repeats = {at, where, 1};
        if (again && Arm64::DeliverFault(*cpu, exception, count, information, code, trap, kind))
        {
            if (log)
            {
                char text[160];
                snprintf(text, sizeof text, "EXCEPTION %08x trap %u at %08x goes to the handlers of the ARM64 program", exception, trap, at);
                log(text);
            }
            return true;
        }
    }
    // Nobody took it.
    if (code == CpuError::InvalidInstruction)
    {
        // The stop keeps the message the tests read where no runner provides
        // the error box, as for an x86 process.
        if (!fault_box) return false;
        return DispatchGuestException(exception, trap, 0, 0, 0, nullptr);
    }
    if (trap == 0xe)
    {
        char described[200] = {};
        DescribeMemoryFault(described, sizeof described);
        return DispatchGuestException(exception, trap, 2, (unsigned)information[0], (unsigned)information[1], described);
    }
    // An exception the program raised itself.
    return DispatchGuestException(exception, 0, count, (unsigned)information[0], (unsigned)information[1], nullptr);
}
}
