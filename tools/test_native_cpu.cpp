// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_cpu.h"
#include "../src/linux_shared_memory.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
#include <execinfo.h>
#include <spawn.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace X86;
constexpr unsigned Code = 0x10000, Data = 0x20000, Stack = 0x30000, Gates = 0x50000, Gdt = 0x60000;
static void okay(unsigned error)
{
    if (error) {
        fprintf(stderr, "CPU error %u: %s\n", error, CpuErrorText(error));
        void *frames[8]; const int count = backtrace(frames, 8); backtrace_symbols_fd(frames, count, 2);
        assert(!error);
    }
}
struct Seen { unsigned kind = 0, address = 0, faults = 0, gates = 0, interrupts = 0; bool repair = false; };
static bool Fault(Cpu &cpu, unsigned kind, unsigned address, unsigned, void *context)
{
    auto &seen = *static_cast<Seen *>(context);
    seen.kind = kind; seen.address = address; ++seen.faults;
    if (seen.repair) { okay(cpu.Protect(address & ~4095u, 4096, Prot::All)); return true; }
    return false;
}
static void Gate(Cpu &cpu, unsigned address, void *context)
{
    if (address != Gates) fprintf(stderr, "Unexpected gate %08x, wanted %08x\n", address, Gates);
    assert(address == Gates);
    auto &seen = *static_cast<Seen *>(context); ++seen.gates;
    unsigned sp, ret; okay(cpu.Get(Reg::Esp, sp)); okay(cpu.Read(sp, &ret, 4));
    okay(cpu.Set(Reg::Eax, 98)); okay(cpu.Set(Reg::Esp, sp + 4)); okay(cpu.Set(Reg::Eip, ret));
}
static void Interrupt(Cpu &cpu, unsigned number, void *context)
{
    auto &seen = *static_cast<Seen *>(context); ++seen.interrupts;
    assert(number == 0x2e || number == 0x80);
    cpu.Stop();
}
static void TimeSlice(Cpu &cpu, unsigned, void *context)
{
    ++*static_cast<unsigned *>(context); cpu.Stop();
}
static unsigned PortIn(Cpu &, unsigned port, unsigned size, void *context)
{ assert(port == 0x12 && size == 2); ++*static_cast<unsigned *>(context); return 0xbeef; }
static void PortOut(Cpu &, unsigned port, unsigned size, unsigned value, void *context)
{ assert(port == 0x34 && size == 2 && value == 0xbeef); ++*static_cast<unsigned *>(context); }
static void Gate16(Cpu &cpu, unsigned address, void *context)
{
    assert(address == Gates); ++*static_cast<unsigned *>(context); cpu.Stop();
}
static unsigned Access(Cpu &cpu, unsigned opcode, unsigned address)
{
    unsigned char code[] = {static_cast<unsigned char>(opcode), 0, 0, 0, 0};
    memcpy(code + 1, &address, 4); okay(cpu.Write(Code, code, sizeof code));
    return cpu.Run(Code, Code + sizeof code, 20);
}
static void ProcessMappings()
{
    Cpu *a = nullptr, *b = nullptr;
    okay(CreateCpu(CpuBackend::Native, a)); okay(CreateCpu(CpuBackend::Native, b));
    std::unique_ptr<Cpu> first(a), second(b);
    Seen seen[2];
    for (unsigned process = 0; process < 2; ++process)
    {
        Cpu &cpu = process ? *b : *a;
        okay(cpu.Map(Code, 4096, Prot::All));
        okay(cpu.Map(Stack, 4096, Prot::Read | Prot::Write));
        okay(cpu.Set(Reg::Esp, Stack + 4000));
        okay(cpu.SetFaultHook(Fault, &seen[process]));
        // Allocate adjacent pages out of order. Activation can combine
        // mappings, but must still honor holes and per-page permissions.
        for (unsigned odd = 0; odd < 2; ++odd)
            for (unsigned page = odd; page < 96; page += 2)
            {
                const unsigned address = 0x100000 + page * 4096;
                okay(cpu.Map(address, 4096, Prot::Read | Prot::Write));
                const unsigned value = process * 1000 + page;
                okay(cpu.Write(address, &value, sizeof value));
            }
        okay(cpu.Protect(0x102000, 4096, Prot::Read));
    }
    for (unsigned turn = 0; turn < 4; ++turn)
        for (unsigned process = 0; process < 2; ++process)
        {
            Cpu &cpu = process ? *b : *a;
            for (unsigned page : {0u, 2u, 47u, 95u})
            {
                okay(Access(cpu, 0xa1, 0x100000 + page * 4096));
                unsigned value = 0; okay(cpu.Get(Reg::Eax, value));
                assert(value == process * 1000 + page);
            }
            assert(Access(cpu, 0xa3, 0x102000) == CpuError::WriteProtected);
        }
    okay(a->Unmap(0x101000, 4096));
    okay(Access(*b, 0xa1, 0x101000));
    assert(Access(*a, 0xa1, 0x101000) == CpuError::ReadUnmapped);
    okay(a->Map(0x101000, 4096, Prot::Read | Prot::Write));
    okay(Access(*b, 0xa1, 0x101000));
    okay(Access(*a, 0xa1, 0x101000));
    unsigned value = 1; okay(a->Get(Reg::Eax, value)); assert(value == 0);
    okay(a->Protect(0x102000, 4096, Prot::Read | Prot::Write));
    okay(Access(*b, 0xa1, 0x102000));
    okay(Access(*a, 0xa3, 0x102000));
    assert(Access(*b, 0xa3, 0x102000) == CpuError::WriteProtected);
}
static void GateMappings()
{
    constexpr unsigned last = Gates + 63 * 4096;
    struct Expected { unsigned address = last; } expected[2];
    std::unique_ptr<Cpu> cpus[2];
    for (unsigned i = 0; i < 2; ++i)
    {
        Cpu *made = nullptr; okay(CreateCpu(CpuBackend::Native, made)); cpus[i].reset(made);
        auto &cpu = *made;
        okay(cpu.Map(Code, 4096, Prot::All));
        okay(cpu.Map(Stack, 4096, Prot::Read | Prot::Write));
        okay(cpu.Map(Gates, 64 * 4096, Prot::All));
        okay(cpu.SetGateHook(Gates, Gates + 64 * 4096 - 1,
            [](Cpu &target, unsigned address, void *context) {
                const auto &want = *static_cast<Expected *>(context);
                assert(address == want.address);
                if (address == Gates) { unsigned value = 0; okay(target.Get(Reg::Eax, value)); assert(value == 77); }
                target.Stop();
            }, &expected[i]));
        okay(cpu.SetGateCodeExecution(true));
        const unsigned char halt = 0xf4;
        okay(cpu.Write(Gates, &halt, 1)); okay(cpu.Write(last, &halt, 1));
        unsigned char caller[] = {0xb8,0,0,0,0,0xff,0xd0,0xba,0,0,0,0,0xff,0xd2};
        memcpy(caller + 1, &last, 4); memcpy(caller + 8, &Gates, 4);
        okay(cpu.Write(Code, caller, sizeof caller));
    }
    auto run = [&](unsigned i) { okay(cpus[i]->Set(Reg::Esp, Stack + 4000)); okay(cpus[i]->Run(Code, 0)); };
    for (unsigned turn = 0; turn < 3; ++turn) { run(0); run(1); }
    // The first process is inactive. Replace a prepared gate by real code;
    // it must execute after activation, while the other process keeps its gate.
    const unsigned char code[] = {0xb8,77,0,0,0,0xc3};
    okay(cpus[0]->Write(last, code, sizeof code)); expected[0].address = Gates;
    run(0); run(1); run(0);
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--host-clock"))
    {
        int policy = 0; assert(!prctl(PR_GET_TSC, &policy) && policy == PR_TSC_ENABLE);
        unsigned low, high; asm volatile("rdtsc" : "=a"(low), "=d"(high));
        assert(low || high);
        timespec time; assert(!clock_gettime(CLOCK_MONOTONIC, &time));
        return 0;
    }
    Cpu *made = nullptr; okay(CreateCpu(ProcessCpuBackend(), made)); std::unique_ptr<Cpu> cpu(made);
    assert(cpu->Backend() == CpuBackend::Native);
    assert(cpu->Runs16Bit());
    okay(cpu->Map(Code, 4096, Prot::All));
    okay(cpu->Map(Data, 4 * 4096, Prot::Read | Prot::Write));
    okay(cpu->Map(Stack, 4096, Prot::Read | Prot::Write));
    okay(cpu->Map(Gates, 4096, Prot::All));
    okay(cpu->Map(Gdt, 4096, Prot::Read | Prot::Write));
    assert(cpu->memory && cpu->page_read[Data / 4096] && cpu->page_write[Data / 4096]);
    assert(cpu->Map(Data, 4096, Prot::All) == CpuError::Map);
    assert(cpu->Map(0xfffff000, 8192, Prot::All) == CpuError::Argument);
    okay(cpu->Set(Reg::Esp, Stack + 4000));
    Seen seen; okay(cpu->SetFaultHook(Fault, &seen));
    unsigned value = 0x98abcdef; okay(cpu->Write(Data, &value, 4));
    okay(Access(*cpu, 0xa1, Data)); unsigned got; okay(cpu->Get(Reg::Eax, got)); assert(got == value);
    okay(cpu->Protect(Data, 4096, Prot::Read));
    assert(cpu->page_read[Data / 4096] && !cpu->page_write[Data / 4096]);
    assert(Access(*cpu, 0xa3, Data) == CpuError::WriteProtected);
    assert(seen.kind == CpuFault::WriteProtected && seen.address == Data);
    okay(cpu->Protect(Data + 4096, 4096, Prot::None));
    assert(Access(*cpu, 0xa1, Data + 4096) == CpuError::ReadProtected);
    assert(seen.kind == CpuFault::ReadProtected && seen.address == Data + 4096);
    assert(Access(*cpu, 0xa3, Data + 4096) == CpuError::WriteProtected);
    assert(seen.kind == CpuFault::WriteProtected);
    assert(Access(*cpu, 0xa1, 0x90000) == CpuError::ReadUnmapped);
    assert(seen.kind == CpuFault::ReadUnmapped);
    assert(cpu->Run(Data, 0, 1) == CpuError::FetchProtected);
    assert(seen.kind == CpuFault::FetchProtected);
    seen.repair = true;
    okay(cpu->Set(Reg::Eax, 123)); okay(Access(*cpu, 0xa3, Data));
    okay(cpu->Read(Data, &got, 4)); assert(got == 123); seen.repair = false;
    // Real FS addressing through a Linux LDT descriptor with the Win98 TEB base.
    unsigned long long descriptors[] = {0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull,
        0xfffull | (static_cast<unsigned long long>(Data) << 16) | (0x93ull << 40) | (0x40ull << 48)};
    okay(cpu->Write(Gdt, descriptors, sizeof descriptors));
    DescriptorTable gdtr; gdtr.base = Gdt; gdtr.limit = sizeof descriptors - 1;
    okay(cpu->SetWide(Reg::Gdtr, &gdtr)); okay(cpu->Set(Reg::Fs, 24));
    const unsigned char fs_code[] = {0x64, 0xa1, 0, 0, 0, 0, 0x8c, 0xe1}; // mov eax,fs:[0]; mov ecx,fs
    okay(cpu->Write(Code, fs_code, sizeof fs_code)); okay(cpu->Run(Code, Code + sizeof fs_code, 20));
    okay(cpu->Get(Reg::Eax, got)); assert(got == 123); okay(cpu->Get(Reg::Ecx, got)); assert(got == 24);
    // A gate changes registers and returns to untouched guest code.
    const unsigned char call[] = {0xe8, 0xfb, 0xff, 0x03, 0, 0xa3, 4, 0, 2, 0, 0xc3};
    const unsigned back = Gates + 0xff0, sp = Stack + 4000;
    const unsigned char halt = 0xf4;
    okay(cpu->Write(back, &halt, 1));
    okay(cpu->Write(sp, &back, 4)); okay(cpu->Set(Reg::Esp, sp));
    okay(cpu->Write(Code, call, sizeof call)); okay(cpu->SetGateHook(Gates, Gates + 4095, Gate, &seen));
    okay(cpu->Run(Code, back)); assert(seen.gates == 1);
    okay(cpu->Read(Data + 4, &got, 4)); assert(got == 98);
    // Executable gate bodies bypass the hook, while HLT entries still dispatch.
    const unsigned char body[] = {0xb8, 9, 3, 0, 0, 0xc3}; // mov eax,777; ret
    okay(cpu->Write(Gates, &halt, 1)); okay(cpu->Write(Gates + 64, body, sizeof body));
    okay(cpu->SetGateCodeExecution(true));
    unsigned char direct_call[sizeof call]; memcpy(direct_call, call, sizeof call);
    const unsigned displacement = Gates + 64 - (Code + 5);
    memcpy(direct_call + 1, &displacement, 4);
    okay(cpu->Write(Code, direct_call, sizeof direct_call)); okay(cpu->Write(sp, &back, 4));
    okay(cpu->Set(Reg::Esp, sp)); okay(cpu->Run(Code, back));
    okay(cpu->Read(Data + 4, &got, 4)); assert(got == 777 && seen.gates == 1);
    okay(cpu->Write(Code, call, sizeof call)); okay(cpu->Write(sp, &back, 4));
    okay(cpu->Set(Reg::Esp, sp)); okay(cpu->Run(Code, back));
    okay(cpu->Read(Data + 4, &got, 4)); assert(got == 98 && seen.gates == 2);
    okay(cpu->SetGateCodeExecution(false));
    // Stopping halfway through an observed gate must restore its execution
    // protection. The next guest call must still enter the hook.
    unsigned observed_entries = 0;
    okay(cpu->SetGateHook(Gates, Gates + 4095,
        [](Cpu &, unsigned address, void *context) {
            if (address == Gates + 64) ++*static_cast<unsigned *>(context);
        }, &observed_entries));
    okay(cpu->Write(sp, &back, 4)); okay(cpu->Set(Reg::Esp, sp));
    okay(cpu->Run(Gates + 64, Gates + 69));
    assert(observed_entries == 1);
    okay(cpu->Write(Code, direct_call, sizeof direct_call)); okay(cpu->Write(sp, &back, 4));
    okay(cpu->Set(Reg::Esp, sp));
    okay(cpu->Run(Code, back)); assert(observed_entries == 2);
    okay(cpu->Write(Code, call, sizeof call));
    okay(cpu->SetGateHook(Gates, Gates + 4095, Gate, &seen));
    // A RET 8 import stub serves a host callback, pops both arguments and
    // preserves the caller's flags. The direct TrueType adapter uses these.
    if (cpu->SetReturnGateExecution(true) == CpuError::Ok)
    {
        unsigned calls = 0;
        okay(cpu->SetGateHook(Gates, Gates + 4095,
            [](Cpu &target, unsigned address, void *context) {
                assert(address == Gates); ++*static_cast<unsigned *>(context);
                unsigned long host_flags;
                asm volatile("pushfq; popq %0" : "=r"(host_flags));
                assert(!(host_flags & ((1u << 10) | (1u << 18))));
                // A host tool launched inside a guest API must run with its
                // own normal TSC policy, as Ghostscript does for PDF jobs.
                extern char **environ;
                char executable[] = "/proc/self/exe", option[] = "--host-clock";
                char *host_args[] = {executable, option, nullptr};
                std::thread([] {
                    int policy = 0;
                    assert(!prctl(PR_GET_TSC, &policy) && policy == PR_TSC_ENABLE);
                    timespec now = {}; assert(!clock_gettime(CLOCK_MONOTONIC, &now));
                    unsigned low = 0, high = 0;
                    asm volatile("rdtsc" : "=a"(low), "=d"(high));
                    assert(low || high);
                }).join();
                for (bool search : {false, true})
                {
                    pid_t child = 0;
                    const auto error = search ? posix_spawnp(&child, executable, nullptr, nullptr, host_args, environ)
                                              : posix_spawn(&child, executable, nullptr, nullptr, host_args, environ);
                    assert(!error);
                    int status = 0; assert(waitpid(child, &status, 0) == child);
                    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
                }
                unsigned esp = 0, args[3]; okay(target.Get(Reg::Esp, esp));
                okay(target.Read(esp, args, sizeof args));
                assert(args[1] == 22 && args[2] == 11);
                okay(target.Set(Reg::Eax, 812));
            }, &calls));
        const unsigned char ret_args[] = {0xc2, 8, 0};
        const unsigned char caller[] = {0xf9,0x6a,11,0x6a,22,0xe8,0xf6,0xff,3,0,0xa3,4,0,2,0,0xc3};
        okay(cpu->Write(Gates, ret_args, sizeof ret_args)); okay(cpu->Write(Code, caller, sizeof caller));
        okay(cpu->Set(Reg::Eflags, 0x40602)); // Guest DF and alignment check must stay out of host C++.
        okay(cpu->Write(sp, &back, 4)); okay(cpu->Set(Reg::Esp, sp)); okay(cpu->Run(Code, back));
        okay(cpu->Get(Reg::Esp, got)); assert(got == sp + 4 && calls == 1);
        okay(cpu->Get(Reg::Eflags, got)); assert((got & 0x40401) == 0x40401);
        okay(cpu->Set(Reg::Eflags, 0x202));
        okay(cpu->Read(Data + 4, &got, 4)); assert(got == 812);
        // Reject invalid guest entry state without crashing the host's
        // IRET or FXRSTOR transition.
        okay(cpu->Set(Reg::Cs, 16)); assert(cpu->Run(Code, 0, 1) == CpuError::Exception);
        okay(cpu->Set(Reg::Cs, 8)); okay(cpu->Set(Reg::Ss, 8));
        assert(cpu->Run(Code, 0, 1) == CpuError::Exception);
        okay(cpu->Set(Reg::Ss, 16)); okay(cpu->Set(Reg::Mxcsr, 0xffffffff));
        assert(cpu->Run(Code, 0, 1) == CpuError::Exception);
        okay(cpu->Set(Reg::Mxcsr, 0x1f80));
        okay(cpu->SetReturnGateExecution(false));
        okay(cpu->SetGateHook(Gates, Gates + 4095, Gate, &seen));
        okay(cpu->Write(Gates, &halt, 1));
    }
    // Direct3D's SMSW probe must work even when Linux enables UMIP.
    const unsigned char smsw[] = {0x0f, 0x01, 0x60, 0x04, 0xc3}; // smsw [eax+4]; ret
    okay(cpu->Write(Code, smsw, sizeof smsw)); okay(cpu->Set(Reg::Eax, Data));
    okay(cpu->Write(sp, &back, 4)); okay(cpu->Set(Reg::Esp, sp)); okay(cpu->Run(Code, back));
    unsigned short msw = 0; okay(cpu->Read(Data + 4, &msw, 2)); unsigned cr0 = 0; okay(cpu->Get(Reg::Cr0, cr0));
    assert(msw == (cr0 & 0xffff));
    okay(cpu->Set(Reg::Eax, 98));
    // An instruction watch uses hardware breakpoints while the loop runs natively.
    const unsigned watched = Code + 0x300;
    const unsigned char watched_loop[] = {0xb9,3,0,0,0,0x31,0xc0,0x40,0x49,0x75,0xfc,0xc3};
    okay(cpu->Write(watched, watched_loop, sizeof watched_loop));
    unsigned hits = 0;
    okay(cpu->AddTrace(TraceKind::Instruction, watched + 7, watched + 7,
        [](Cpu &, unsigned, unsigned, void *context) { ++*static_cast<unsigned *>(context); }, &hits));
    assert(cpu->AddTrace(TraceKind::Instruction, watched, watched + 1, nullptr, nullptr) == CpuError::Hook);
    okay(cpu->Write(sp, &back, 4));okay(cpu->Set(Reg::Esp, sp));okay(cpu->Run(watched, back));
    okay(cpu->Get(Reg::Eax, got));assert(got == 3 && hits == 3);
    okay(cpu->Set(Reg::Eax, 98));
    // Host contexts retain both integer and x87 controls.
    CpuContext *context = nullptr; okay(cpu->NewContext(context));
    okay(cpu->Set(Reg::Fpcw, 0x67f)); okay(cpu->Save(context));
    okay(cpu->Set(Reg::Eax, 2)); okay(cpu->Set(Reg::Fpcw, 0xa7f)); okay(cpu->Restore(context));
    okay(cpu->Get(Reg::Eax, got)); assert(got == 98); okay(cpu->Get(Reg::Fpcw, got)); assert(got == 0x67f);
    assert(!cpu->StateRefusal() && !cpu->ContextStateRefusal(context));
    std::vector<unsigned char> saved(cpu->ContextBytes());
    assert(!saved.empty() && cpu->ContextExport(context, saved.data()));
    cpu->FreeContext(context);
    okay(cpu->NewContext(context));
    assert(cpu->ContextImport(context, saved.data()));
    okay(cpu->Set(Reg::Eax, 3)); okay(cpu->Set(Reg::Fpcw, 0xa7f)); okay(cpu->Restore(context));
    okay(cpu->Get(Reg::Eax, got)); assert(got == 98);
    okay(cpu->Get(Reg::Fpcw, got)); assert(got == 0x67f);
    cpu->FreeContext(context);
    // A guest INT 80h must stop before Linux executes the requested exit.
    okay(cpu->SetInterruptHook(Interrupt, &seen));
    const unsigned char sys_code[] = {0xb8, 1, 0, 0, 0, 0xcd, 0x80};
    okay(cpu->Write(Code, sys_code, sizeof sys_code)); okay(cpu->Run(Code, back)); assert(seen.interrupts == 1);
    const unsigned char nt_code[] = {0xcd, 0x2e};
    okay(cpu->Write(Code, nt_code, sizeof nt_code)); okay(cpu->Run(Code, back)); assert(seen.interrupts == 2);
    // CLI and STI of a Win32 program go on, as under the VMM of Win98.
    const unsigned char flag_code[] = {0xfa, 0xb8, 7, 0, 0, 0, 0xfb, 0xcd, 0x2e};
    okay(cpu->Write(Code, flag_code, sizeof flag_code)); okay(cpu->Run(Code, back)); assert(seen.interrupts == 3);
    okay(cpu->Get(Reg::Eax, got)); assert(got == 7);
    // Cross-thread stop must interrupt an uninterrupted native infinite loop.
    const unsigned char loop[] = {0xeb, 0xfe}; okay(cpu->Write(Code, loop, sizeof loop));
    std::thread stopper([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); cpu->RequestStop(); });
    unsigned result = cpu->Run(Code, back); stopper.join(); okay(result);
    unsigned slices = 0;
    okay(cpu->SetTimeSliceHook(TimeSlice, &slices));
    okay(cpu->Run(Code, back)); assert(slices == 1);
    okay(cpu->SetTimeSliceHook(nullptr, nullptr));
    okay(cpu->Write(Code, fs_code, sizeof fs_code)); okay(cpu->Run(Code, Code + sizeof fs_code, 20));
    okay(cpu->Get(Reg::Eax, got)); assert(got == 123);
    // An uninterrupted RDTSC traps to the guest clock and excludes pauses.
    const unsigned char clock_code[] = {0x90, 0x0f, 0x31, 0xc3};
    okay(cpu->Write(Code, clock_code, sizeof clock_code));
    auto clock = [&]() {
        okay(cpu->Write(sp, &back, 4)); okay(cpu->Set(Reg::Esp, sp)); okay(cpu->Run(Code, back));
        unsigned low, high; okay(cpu->Get(Reg::Eax, low)); okay(cpu->Get(Reg::Edx, high));
        return (static_cast<unsigned long long>(high) << 32) | low;
    };
    const auto before_pause = clock(); SetCounterPause(1000000);
    const auto after_pause = clock(); SetCounterPause(0);
    assert(before_pause > after_pause && before_pause - after_pause > 900000000);
    unsigned ports = 0; okay(cpu->SetPortHooks(PortIn, PortOut, &ports));
    const unsigned char port_code[] = {0x90, 0x66, 0xe5, 0x12, 0x66, 0xef, 0xc3};
    okay(cpu->Write(Code, port_code, sizeof port_code)); okay(cpu->Write(sp, &back, 4));
    okay(cpu->Set(Reg::Esp, sp)); okay(cpu->Set(Reg::Eax, 0x98000000)); okay(cpu->Set(Reg::Edx, 0x34));
    okay(cpu->Run(Code, back)); okay(cpu->Get(Reg::Eax, got)); assert(ports == 2 && got == 0x9800beef);
    // Shared host bytes propagate to both directions and survive partial unmap.
    unsigned char shared[8192] = {};
    okay(cpu->MapHost(0xa0000, sizeof shared, Prot::All, shared));
    assert(!cpu->page_read[0xa0000 / 4096] && !cpu->page_write[0xa0000 / 4096]);
    value = 456; memcpy(shared + 4, &value, 4);
    okay(Access(*cpu, 0xa1, 0xa0004)); okay(cpu->Get(Reg::Eax, got)); assert(got == value);
    okay(cpu->Set(Reg::Eax, 789)); okay(Access(*cpu, 0xa3, 0xa1004)); memcpy(&got, shared + 4100, 4); assert(got == 789);
    struct Mapping { unsigned address, bytes, prot; void *host; };
    std::vector<Mapping> regions;
    auto record = +[](void *ctx,unsigned a,unsigned n,unsigned p,void *host) {
        static_cast<std::vector<Mapping> *>(ctx)->push_back({a,n,p,host});
    };
    assert(cpu->Regions(record,&regions));
    auto mapping = [&](unsigned a) -> Mapping {
        for (auto r : regions) if (r.address == a) return r;
        assert(false); return {};
    };
    assert(mapping(0xa0000).bytes == 8192 && mapping(0xa0000).host == shared);
    okay(cpu->Unmap(0xa0000, 4096));
    regions.clear(); assert(cpu->Regions(record,&regions));
    assert(mapping(0xa1000).bytes == 4096 && mapping(0xa1000).host == shared+4096);
    okay(cpu->Protect(0xa1000,4096,Prot::Read));
    regions.clear(); assert(cpu->Regions(record,&regions));
    assert(mapping(0xa1000).prot == Prot::Read && mapping(0xa1000).host == shared+4096);
    okay(cpu->Unmap(0xa1000, 4096));
    // Files allocated after both forks are shared without copying at stops.
    Cpu *other_made = nullptr; okay(CreateCpu(CpuBackend::Native, other_made)); std::unique_ptr<Cpu> other(other_made);
    auto *physical = static_cast<unsigned *>(LinuxSharedMemory::Allocate(4096)); assert(physical);
    okay(cpu->MapHost(0xa0000, 4096, Prot::Read | Prot::Write, physical));
    okay(other->MapHost(0xa0000, 4096, Prot::Read | Prot::Write, physical));
    assert(cpu->page_read[0xa0000 / 4096] && cpu->page_write[0xa0000 / 4096]);
    *reinterpret_cast<unsigned *>(cpu->memory + 0xa0000) = 990; assert(physical[0] == 990);
    physical[0] = 991; okay(cpu->Read(0xa0000, &got, 4)); assert(got == 991);
    okay(cpu->Set(Reg::Eax, 992)); okay(Access(*cpu, 0xa3, 0xa0000));
    assert(physical[0] == 992); okay(other->Read(0xa0000, &got, 4)); assert(got == 992);
    okay(cpu->Unmap(0xa0000, 4096)); okay(other->Unmap(0xa0000, 4096)); assert(LinuxSharedMemory::Free(physical));
    // Reusing a former shared address gives private storage, initially zero.
    okay(cpu->Map(0xa0000, 4096, Prot::Read | Prot::Write)); okay(cpu->Read(0xa0000, &got, 4)); assert(!got);
    okay(cpu->Unmap(0xa0000, 4096));
    // A device range (the VGA aperture of Application::EnsureVga) stays
    // unmapped in the guest; moves and string moves there reach the device.
    static unsigned char device[0x10000];
    auto device_access = +[](void *context, unsigned address, unsigned char *data, unsigned size, bool write) {
        auto *store = static_cast<unsigned char *>(context) + (address - 0xd0000);
        if (write) memcpy(store, data, size); else memcpy(data, store, size);
    };
    okay(cpu->MapDevice(0xd0000, sizeof device, device_access, device));
    assert(cpu->MapDevice(0xd0000, sizeof device, device_access, device) == CpuError::Argument);
    value = 0x12345678; memcpy(device + 4, &value, 4);
    okay(Access(*cpu, 0xa1, 0xd0004)); okay(cpu->Get(Reg::Eax, got)); assert(got == value);
    okay(cpu->Set(Reg::Eax, 0xcafef00d)); okay(Access(*cpu, 0xa3, 0xd0010));
    memcpy(&got, device + 0x10, 4); assert(got == 0xcafef00d);
    okay(cpu->Read(0xd0004, &got, 4)); assert(got == value);
    value = 0x55aa55aa; okay(cpu->Write(0xd0020, &value, 4)); memcpy(&got, device + 0x20, 4); assert(got == value);
    // mov edi, 0xd0100; mov ecx, 4; rep stosd
    const unsigned char fill[] = {0xbf, 0, 1, 0x0d, 0, 0xb9, 4, 0, 0, 0, 0xf3, 0xab};
    okay(cpu->Set(Reg::Eax, 0x01020304)); okay(cpu->Write(Code, fill, sizeof fill));
    okay(cpu->Run(Code, Code + sizeof fill, 20));
    for (unsigned i = 0; i < 4; ++i) { memcpy(&got, device + 0x100 + 4 * i, 4); assert(got == 0x01020304); }
    okay(cpu->Get(Reg::Ecx, got)); assert(got == 0); okay(cpu->Get(Reg::Edi, got)); assert(got == 0xd0110);
    // movzx ecx, byte [eax + 1]; mov byte [eax + 0x10], cl
    const unsigned char bytes[] = {0x0f, 0xb6, 0x48, 0x01, 0x88, 0x48, 0x10};
    okay(cpu->Set(Reg::Eax, 0xd0102)); okay(cpu->Write(Code, bytes, sizeof bytes));
    okay(cpu->Run(Code, Code + sizeof bytes, 20)); okay(cpu->Get(Reg::Ecx, got)); assert(got == 1);
    assert(device[0x112] == 1);
    // Enter an actual 16-bit code/stack pair and return through a far API gate.
    okay(cpu->Map(0x80000, 4096, Prot::All)); okay(cpu->Map(0x90000, 4096, Prot::All));
    okay(cpu->Map(0xb0000, 4096, Prot::Read | Prot::Write));
    auto descriptor = [](unsigned base, unsigned access) {
        return 0xfffull | (static_cast<unsigned long long>(base & 0xffffff) << 16) |
               (static_cast<unsigned long long>(access) << 40) | (static_cast<unsigned long long>(base >> 24) << 56);
    };
    const unsigned long long ldt[] = {0, descriptor(0x80000, 0xfa), descriptor(0x90000, 0xf2),
        descriptor(0xb0000, 0xf2), descriptor(Gates, 0xfa)};
    okay(cpu->Write(Gdt + 0x100, ldt, sizeof ldt));
    DescriptorTable table; table.base = Gdt + 0x100; table.limit = sizeof ldt - 1;
    okay(cpu->SetWide(Reg::Ldtr, &table));
    const unsigned char code16[] = {0xb8, 0x34, 0x12, 0xa3, 0, 0, 0xcb};
    okay(cpu->Write(0x80000, code16, sizeof code16));
    const unsigned short back16[] = {0, 0x27}; okay(cpu->Write(0xb0ff0, back16, sizeof back16));
    const unsigned transition[] = {0, 0x0f, 0xff0, 0x1f}; okay(cpu->Write(Stack, transition, sizeof transition));
    const unsigned char retf = 0xcb; okay(cpu->Write(Code, &retf, 1));
    okay(cpu->Set(Reg::Cs, 8)); okay(cpu->Set(Reg::Ss, 16)); okay(cpu->Set(Reg::Esp, Stack)); okay(cpu->Set(Reg::Ds, 0x17));
    unsigned gates16 = 0; okay(cpu->SetGateHook(Gates, Gates + 4095, Gate16, &gates16));
    okay(cpu->Run(Code, Gates + 0xff0)); assert(gates16 == 1);
    unsigned short word; okay(cpu->Read(0x90000, &word, 2)); assert(word == 0x1234);
    // Win16 uses an unmapped stop sentinel. A long native loop must reach
    // its API gate without a trace signal for every guest instruction.
    // Use a separate CPU: the instruction watch above deliberately remains
    // installed and forces instrumentation in the direct backend.
    Cpu *fast = nullptr; okay(CreateCpu(CpuBackend::Native, fast));
    for (unsigned address : {Gdt, Gates, 0x80000u, 0xb0000u})
    {
        okay(fast->Map(address, 4096, Prot::All));
        unsigned char page[4096]; okay(cpu->Read(address, page, sizeof page));
        okay(fast->Write(address, page, sizeof page));
    }
    CpuContext *fast_context = nullptr; okay(cpu->NewContext(fast_context));
    okay(cpu->Save(fast_context)); okay(fast->Restore(fast_context)); cpu->FreeContext(fast_context);
    const unsigned char loop16[] = {0x66, 0xb9, 0, 0, 0x40, 0, 0x66, 0x49, 0x75, 0xfc, 0xcb};
    okay(fast->Write(0x80000, loop16, sizeof loop16));
    okay(fast->Write(0xb0ff0, back16, sizeof back16));
    okay(fast->Set(Reg::Cs, 0x0f)); okay(fast->Set(Reg::Ss, 0x1f)); okay(fast->Set(Reg::Esp, 0xff0));
    okay(fast->SetGateHook(Gates, Gates + 4095, Gate16, &gates16));
    okay(fast->Run(0, 0xfffffffeu)); assert(gates16 == 2);
    okay(fast->Get(Reg::Ecx, got)); assert(got == 0); delete fast;
    ProcessMappings();
    GateMappings();
    puts("NATIVE CPU PASS: hardware execution, TEB, page faults, gates, contexts, interrupts, stop, shared pages");
}
