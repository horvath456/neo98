// SPDX-License-Identifier: GPL-2.0-or-later
// The gate for software interrupts of the 32-bit guest. The 16-bit side has
// had one since the NE loader arrived (`Win16Interrupt`, src/ne_loader.cpp);
// without one here every `int` of a 32-bit program became an unhandled CPU
// exception, which is how RUNDLL32 died on the `int 2Eh` inside the original
// NTDLL.
#include "win98_nt_services.h"
#include "win98_power_policy.h"
#include "x86_app.h"
#include "host_devices.h"
#include <stdio.h>
namespace X86
{
// int 2Eh: EAX is the service number, EDX points at the arguments. The stub in
// NTDLL removes them itself, so nothing here touches the stack. The answer
// goes into EAX as an NTSTATUS.
unsigned Application::NtServiceCall(unsigned service, unsigned arguments)
{
    const auto *entry = Win98NtServices::Find(service);
    if (!entry)
    {
        char text[128];
        snprintf(text, sizeof text, "NT service %xh through int 2Eh is not ported yet", service);
        Fail(text);
        return Win98NtServices::NotImplemented;
    }
    if (service == 0x108)
    {
        // NtPowerInformation(level, input, input bytes, output, output bytes).
        unsigned a[5] = {};
        if (!Read(arguments, a, sizeof a))
            return Win98NtServices::StatusAccessViolation;
        if (a[0] == Win98NtServices::SystemPowerCapabilities)
            return PowerCapabilities(a[1], a[2], a[3], a[4]);
        const unsigned status = PowerPolicy(a[0], a[1], a[2], a[3], a[4]);
        if (status != Win98NtServices::NotImplemented)
            return status;
    }
    // What the kernel of Win98 does behind the other numbers lives in a VxD
    // and is not read yet. The honest answer is the failure the callers
    // already handle, with a line that names the call.
    if (log)
    {
        char text[192];
        unsigned first = 0;
        Read(arguments, &first, 4);
        snprintf(text, sizeof text, "NTSERVICE %s (%xh) args=%08x first=%u answered STATUS_NOT_IMPLEMENTED",
                 entry->name, service, arguments, first);
        log(text);
    }
    return Win98NtServices::NotImplemented;
}
// The power state NTKERN keeps belongs to the machine, not to a process
// (win98_power_policy.h).
static Win98PowerPolicy::Machine &PowerMachine()
{
    static Win98PowerPolicy::Machine machine;
    Win98PowerPolicy::Start(machine);
    return machine;
}
// NtPowerInformation level 4 the way NTKERN answers it (win98_nt_services.h).
unsigned Application::PowerCapabilities(unsigned input, unsigned input_bytes, unsigned output,
                                        unsigned output_bytes)
{
    auto &machine = PowerMachine();
    auto &record = machine.capabilities;
    static_assert(sizeof record == Win98NtServices::PowerCapabilitiesBytes, "one record");
    if (input)
    {
        if (input_bytes != Win98NtServices::PowerCapabilitiesBytes)
            return Win98NtServices::StatusInvalidParameter;
        if (!Read(input, record, sizeof record))
            return Win98NtServices::StatusAccessViolation;
        // 01104C1E: new capabilities check the stored policies again.
        Win98PowerPolicy::Reload(machine);
    }
    record[Win98NtServices::FullWakeByte] = 1;
    if(HostDevices::current)record[30]=HostDevices::HasBattery()?1:0;
    if (log)
        log("NTSERVICE NtPowerInformation (108h) SystemPowerCapabilities answered");
    if (!output)
        return 0;
    if (output_bytes < sizeof record)
        return Win98NtServices::StatusBufferTooSmall;
    return Store(output, record, sizeof record) ? 0u : Win98NtServices::StatusAccessViolation;
}
// NtPowerInformation levels 0 to 3, 8 and 9 (01104B3F to 01104BF8 and
// 01104B78): the policies for mains and battery, their check without taking
// them, the active one and the administrator limits (win98_power_policy.h).
// Every other level still answers STATUS_NOT_IMPLEMENTED in the caller.
unsigned Application::PowerPolicy(unsigned level, unsigned input, unsigned input_bytes, unsigned output,
                                  unsigned output_bytes)
{
    namespace P = Win98PowerPolicy;
    auto &machine = PowerMachine();
    unsigned char buffer[P::PolicyBytes], checked[P::PolicyBytes];
    const unsigned char *answer = nullptr;
    unsigned bytes = P::PolicyBytes, status = 0;
    switch (level)
    {
    case P::PolicyAc:
    case P::PolicyDc:
    {
        const bool ac = level == P::PolicyAc;
        if (input)
        {
            // The input is read as long as the caller says; NTKERN copies
            // exactly E8h bytes after the length check.
            if (input_bytes == P::PolicyBytes && !Read(input, buffer, sizeof buffer))
                return Win98NtServices::StatusAccessViolation;
            status = P::Take(machine, true, ac, buffer, input_bytes);
        }
        answer = ac ? machine.ac : machine.dc;
        break;
    }
    case P::VerifyPolicyAc:
    case P::VerifyPolicyDc:
        if (!input || !output)
            return P::StatusInvalidParameter;
        if (!Read(input, buffer, sizeof buffer))
            return Win98NtServices::StatusAccessViolation;
        status = P::Check(machine, level == P::VerifyPolicyAc, buffer, checked);
        answer = checked;
        break;
    case P::PolicyCurrent:
        if (input)
            return P::StatusInvalidParameter;
        answer = machine.ac; // no battery, so mains is the active policy
        break;
    case P::AdministratorPolicy:
        if (input)
        {
            unsigned char limits[P::AdminBytes];
            if (input_bytes == P::AdminBytes && !Read(input, limits, sizeof limits))
                return Win98NtServices::StatusAccessViolation;
            status = P::TakeAdmin(machine, true, limits, input_bytes);
            if (!status)
                P::Reload(machine);
        }
        answer = machine.admin;
        bytes = P::AdminBytes;
        break;
    default:
        return Win98NtServices::NotImplemented;
    }
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text, "NTSERVICE NtPowerInformation (108h) level %u input=%08x output=%08x answered %08x",
                 level, input, output, status);
        log(text);
    }
    // The shared tail (01104E34): the answer goes out only after a success
    // and only into a buffer that holds all of it.
    if (int(status) < 0 || !output)
        return status;
    if (output_bytes < bytes)
        return P::StatusBufferTooSmall;
    return Store(output, answer, bytes) ? status : Win98NtServices::StatusAccessViolation;
}
void Application::GuestInterrupt(Cpu &cpu, unsigned number, void *context)
{
    auto &app = *static_cast<Application *>(context);
    // A 16-bit library this process hosts (a printer driver) raises the
    // interrupts of a Win16 task: DOS, the BIOS clock, DPMI.
    if (app.Win16Code())
    {
        Win16Interrupt(cpu, number, context);
        return;
    }
    // Win98 without a debugger returns from the INT 68h / AX=4300h
    // installation probe with EAX unchanged and without an SEH exception.
    // SetupUbi uses it after its Win9x process-pointer check.
    // Windows 98 behavior; other functions remain
    // unsupported rather than claiming a debugger service exists.
    if (number == 0x68 && (app.Register(Reg::Eax) & 0xffff) == 0x4300)
        return;
    switch (number)
    {
    case 0x2e:
    {
        const unsigned service = app.Register(Reg::Eax), arguments = app.Register(Reg::Edx);
        const unsigned status = app.NtServiceCall(service, arguments);
        if (app.state == Failed)
            break;
        app.Register(Reg::Eax, status);
        return;
    }
    case 0x21:
    {
        // A 32-bit program reaches the same DOS services as a 16-bit one; the
        // function is in AH and the answer in AX with the carry flag. The DOS
        // side keeps its handles and errors in the Win16 machine, which a
        // 32-bit process only has once it hosts 16-bit code; the gate makes it
        // the way a hosted library does.
        unsigned result = 0;
        if (!app.win16.state && !app.Win16HostedReady())
            break;
        if (app.Win16DosCall(result))
        {
            app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | (result & 0xffff));
            return;
        }
        break;
    }
    case 0:
        // The processor leaves EIP on DIV/IDIV for #DE. Win98 reports the
        // fault as c0000094 through the thread's SEH chain, with no operands
        // in the exception record (KERNEL-EXCEPTIONS.md).
        app.pending_exception = {0xc0000094u, true};
        break;
    case 1:
    case 3:
        // A single step is STATUS_SINGLE_STEP and a breakpoint is
        // STATUS_BREAKPOINT. Win98 offers both to the debugger of the process
        // first; without one the VMM raises them on the faulting thread, so
        // they go down its FS:[0] chain like any other exception. Delivery
        // runs guest handlers, so it happens after the emulator has stopped:
        // Application::GuestFault picks the record up in the next Step.
        // The processor clears the trap flag when it takes the single-step
        // trap, so the handler does not run one instruction at a time; the
        // dispatcher clears it in the reported context as well (trap 1).
        if (number == 1)
            app.Register(Reg::Eflags, app.Register(Reg::Eflags) & ~0x100u);
        app.pending_exception = {number == 1 ? 0x80000004u : 0x80000003u, true};
        break;
    default:
    {
        char text[128];
        snprintf(text, sizeof text, "Guest interrupt %02xh from 32-bit code is not ported yet", number);
        app.Fail(text);
        break;
    }
    }
    cpu.Stop();
}
} // namespace X86
