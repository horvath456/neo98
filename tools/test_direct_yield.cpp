// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/fiber.h"
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <sys/prctl.h>
#include <thread>
using namespace X86;
constexpr unsigned Code = 0x10000, Stack = 0x30000, Gate = 0x50000, Return = Gate + 0xff0;
static void okay(unsigned error) { assert(error == CpuError::Ok); }
static void NativeHost()
{
    int policy = 0;
    assert(!prctl(PR_GET_TSC, &policy) && policy == PR_TSC_ENABLE);
}
static std::unique_ptr<Cpu> Make()
{
    Cpu *raw = nullptr;
    okay(CreateCpu(CpuBackend::Native, raw));
    std::unique_ptr<Cpu> cpu(raw);
    okay(cpu->Map(Code, 4096, Prot::All));
    okay(cpu->Map(Stack, 4096, Prot::All));
    okay(cpu->Map(Gate, 4096, Prot::All));
    const unsigned char halt = 0xf4;
    okay(cpu->Write(Gate, &halt, 1));
    okay(cpu->Write(Return, &halt, 1));
    okay(cpu->Write(Stack + 4000, &Return, 4));
    okay(cpu->Set(Reg::Esp, Stack + 4000));
    okay(cpu->SetGateCodeExecution(true));
    return cpu;
}
struct Job
{
    std::unique_ptr<Cpu> cpu = Make();
    Fiber fiber;
    unsigned calls = 0;
    bool completed = false;
    CpuContext *context = nullptr;
    ~Job()
    {
        if (context)
            cpu->FreeContext(context);
    }
    void Start()
    {
        const unsigned char code[] = {0xe8, 0xfb, 0xff, 3, 0, 0xc3};
        okay(cpu->Write(Code, code, sizeof code));
        const unsigned char nested[] = {0xe8, 0xcb, 0xff, 3, 0, 0xc3}; // call Gate+16 from Code+64
        okay(cpu->Write(Code + 64, nested, sizeof nested));
        const unsigned char halt = 0xf4;
        const unsigned back = Gate + 0xfe0;
        okay(cpu->Write(Gate + 16, &halt, 1));
        okay(cpu->Write(back, &halt, 1));
        okay(cpu->Write(Stack + 2000, &back, 4));
        okay(cpu->NewContext(context));
        okay(cpu->SetGateHook(
            Gate, Gate + 4095,
            [](Cpu &cpu, unsigned address, void *context)
            {
                auto &job = *static_cast<Job *>(context);
                if (address == Gate)
                {
                    okay(cpu.Save(job.context));
                    okay(cpu.Set(Reg::Esp, Stack + 2000));
                    okay(cpu.Run(Code + 64, Gate + 0xfe0));
                    okay(cpu.Restore(job.context));
                }
                else
                {
                    assert(address == Gate + 16);
                    ++job.calls;
                    void *saved = cpu.DetachFrame();
                    job.fiber.Yield();
                    cpu.AttachFrame(saved);
                }
                unsigned sp, ret;
                okay(cpu.Get(Reg::Esp, sp));
                okay(cpu.Read(sp, &ret, 4));
                okay(cpu.Set(Reg::Eax, 98));
                okay(cpu.Set(Reg::Esp, sp + 4));
                okay(cpu.Set(Reg::Eip, ret));
            },
            this));
        assert(fiber.Start(
            [](void *context)
            {
                auto &job = *static_cast<Job *>(context);
                okay(job.cpu->Run(Code, Return));
                job.completed = true;
            },
            this));
    }
};
int main()
{
    auto other = Make();
    const unsigned char code[] = {0xb8, 7, 0, 0, 0, 0xc3};
    okay(other->Write(Code, code, sizeof code));
    okay(other->SetGateHook(Gate, Gate + 4095, [](Cpu &cpu, unsigned, void *) { cpu.Stop(); }, nullptr));
    auto run_other = [&]
    {
        std::thread worker(
            [&]
            {
                okay(other->Write(Stack + 4000, &Return, 4));
                okay(other->Set(Reg::Esp, Stack + 4000));
                okay(other->Run(Code, Return));
                unsigned value;
                okay(other->Get(Reg::Eax, value));
                assert(value == 7);
                NativeHost();
            });
        worker.join();
    };
    Job resumed;
    resumed.Start();
    resumed.fiber.Resume();
    assert(!resumed.completed && resumed.calls == 1);
    NativeHost();
    run_other();
    resumed.fiber.Resume();
    assert(resumed.completed);
    NativeHost();
    unsigned value;
    okay(resumed.cpu->Get(Reg::Eax, value));
    assert(value == 98);
    {
        Job discarded;
        discarded.Start();
        discarded.fiber.Resume();
        assert(!discarded.completed && discarded.calls == 1);
        NativeHost();
        // Destroy its stack while Run is suspended inside the gate hook.
    }
    run_other();
    NativeHost();
    puts("DIRECT YIELD PASS: resumed and discarded fibers release the native thread policies and guest lock");
}
