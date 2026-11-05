// SPDX-License-Identifier: GPL-2.0-or-later
#include "../build/generated/notepad_program.h"
#include "../build/generated/thread_contract.h"
#include "../build/generated/common_contract.h"
#include "../build/generated/virtual_contract.h"
#include "x86_app.h"
#include "jit_memory.h"
#include "../tools/input_contract_driver.h"
#include "../tools/crt_exit_contract.h"
#include "../tools/fixture_native_modules.h"
#include <circle/string.h>
#include <string.h>
extern "C" void neo98_diagnostic(const char *);
extern "C" unsigned long long neo98_clock_us();
unsigned RuntimeThreadSelfTest()
{
    auto *app = new X86::Application(nullptr);
    if (!app->Start(ThreadContractProgram, sizeof ThreadContractProgram, OriginalFixedsys,
                    sizeof OriginalFixedsys, "THREADS.EXE"))
    {
        delete app;
        return 1000;
    }
    for (unsigned i = 0;
         i < 100000 && app->Status() != X86::Application::Exited && app->Status() != X86::Application::Failed;
         ++i)
        app->Pump();
    unsigned result = app->Status() == X86::Application::Exited ? app->ExitCode() : 1001;
    if (!result && app->ThreadsCreated() != 3)
        result = 1002;
    delete app;
    if (!result && (User::LiveWindows() || Gdi::LiveObjects()))
        result = 1003;
    return result;
}
// Two engines at once: the virtual-memory contract program runs twice side by
// side in separate code-cache slots, and both slots come back afterwards.
// THREADS.EXE cannot be the partner here because it asserts thread id 1, which
// only the first process of a boot receives.
unsigned RuntimeMultiEngineSelfTest(unsigned &peak_slots)
{
    peak_slots = 0;
    // GDI's original TrueType driver retains an engine after COMMON.EXE.
    // The two application engines must return their slots to this baseline.
    const unsigned free_slots = neo98_jit_free_slots();
    if (free_slots < 2) return 999;
    X86::Application *apps[2] = {new X86::Application(nullptr), new X86::Application(nullptr)};
    unsigned result = 0;
    for (unsigned i = 0; i < 2 && !result; ++i)
        if (!apps[i]->Start(VirtualContractProgram, sizeof VirtualContractProgram, OriginalFixedsys,
                            sizeof OriginalFixedsys, "VMEM.EXE", "0"))
            result = 1000 + i;
    if (!result && neo98_jit_free_slots() != free_slots - 2) result = 1010;
    if (!result && apps[0]->ProcessId() == apps[1]->ProcessId()) result = 1011;
    for (unsigned i = 0; i < 200000 && !result; ++i)
    {
        unsigned live = 0;
        neo98_jit_statistics(&live, nullptr, nullptr, nullptr);
        const unsigned application_slots = live - (NEO98_JIT_SLOTS - free_slots);
        if (application_slots > peak_slots) peak_slots = application_slots;
        bool busy = false;
        for (auto *app : apps)
            if (app->Status() != X86::Application::Exited && app->Status() != X86::Application::Failed)
            { app->Pump(); busy = true; }
        if (!busy) break;
    }
    for (unsigned i = 0; i < 2 && !result; ++i)
    {
        if (apps[i]->Status() != X86::Application::Exited)
        {
            CString detail;
            detail.Format("multi-engine app %u status=%u error=%s", i, (unsigned)apps[i]->Status(), apps[i]->Error());
            neo98_diagnostic(detail);
            result = 1020 + i;
        }
        else if (apps[i]->ExitCode()) result = 1030 + i;
    }
    delete apps[0];
    if (!result && neo98_jit_free_slots() != free_slots - 1) result = 1040;
    delete apps[1];
    if (!result && neo98_jit_free_slots() != free_slots) result = 1041;
    if (!result && peak_slots < 2) result = 1042;
    if (!result && (User::LiveWindows() || Gdi::LiveObjects())) result = 1043;
    return result;
}
static bool CommonWindowDrawn(unsigned window)
{
    Gdi::RECT update;
    if (!window || User::GetUpdateRect(window, &update)) return false;
    for (unsigned child = User::GetWindow(window, 5); child; child = User::GetWindow(child, 2))
        if (!CommonWindowDrawn(child)) return false;
    return true;
}

unsigned RuntimeCommonSelfTest()
{
    auto *app = new X86::Application(nullptr);
    // COMMON.EXE tests our implementations, as in the host fixture runner.
    if (!FixtureModules::KeepNative(*app) ||
        !app->Start(CommonContractProgram, sizeof CommonContractProgram, OriginalFixedsys,
                    sizeof OriginalFixedsys, "COMMON.EXE"))
    {
        delete app;
        return 1000;
    }
    // The fixture exercises ExitWindowsEx without ending the boot session.
    struct SessionEnd { unsigned calls = 0, flags = 0, code = 0; } session;
    X86::Application::SetSessionEndHook(&session,
        [](void *context, X86::Application &, unsigned flags, unsigned code) {
            auto &end = *static_cast<SessionEnd *>(context);
            ++end.calls; end.flags = flags; end.code = code;
        });
    const auto start = neo98_clock_us();
    auto progress = start;
    unsigned calls = app->ApiCalls(), callbacks = app->Callbacks();
    bool clicked = false;
    unsigned presented = 0;
    // The contract grows with the port. Detect a stalled guest separately from
    // a slow but progressing run, and retain a hard bound for looping API calls.
    // The contract includes class/menu/toolbar/CBT, scrollbar, DC and EDIT
    // lifetime tests. It has since grown past 65,000 API calls, which the
    // emulated Pi needs three to five minutes for while the host is busy. Allow 600 seconds.
    while (neo98_clock_us() - start < 600000000 && neo98_clock_us() - progress < 10000000 &&
           app->Status() != X86::Application::Exited && app->Status() != X86::Application::Failed)
    {
        app->Pump();
        if (calls != app->ApiCalls() || callbacks != app->Callbacks())
        {
            calls = app->ApiCalls();
            callbacks = app->Callbacks();
            progress = neo98_clock_us();
        }
        InputContractDriver(*app);
        MouseFocusContractDriver(*app);
        // These fixtures wait for the host screenshot runner's acknowledgement.
        // On the Pi, compose their finished frame before releasing that wait.
        if (app->WaitingForInput())
        {
            const char *classes[] = {"NEO98ListBoxContract", "NEO98PaintContract",
                                     "NEO98TreeContract", "NEO98ListContract"};
            bool settled = false;
            for (unsigned i = 0; i < 64 && !(settled = !User::Render()); ++i) {}
            for (unsigned i = 0; settled && i < 4; ++i)
            {
                const unsigned window = User::FindWindow(classes[i], nullptr);
                if (!(presented & (1u << i)) && CommonWindowDrawn(window) &&
                    User::PostMessage(window, 0x8f00, 0, 0))
                    presented |= 1u << i;
            }
        }
        if (!clicked && app->WaitingForInput() &&
            User::FindWindow("NEO98WindowContract", "Window ordering and desktop"))
        {
            Gdi::POINT button, background;
            if (WindowContractPoints(button, background))
            {
                app->Mouse(button.x, button.y, true, false);
                app->Mouse(button.x, button.y, false, true);
                app->Mouse(background.x, background.y, true, false);
                app->Mouse(background.x, background.y, false, true);
                clicked = true;
            }
        }
    }
    unsigned result = app->Status() == X86::Application::Exited ? app->ExitCode() : 1001;
    if (result)
    {
        CString detail;
        detail.Format("COMMON failure state=%u code=%u calls=%u callbacks=%u input=%u windows=%u "
                      "elapsed_ms=%u stalled_ms=%u error=%s",
                      app->Status(), app->ExitCode(), app->ApiCalls(), app->Callbacks(),
                      User::FindWindow("NEO98InputContract", nullptr), User::LiveWindows(),
                      unsigned((neo98_clock_us() - start) / 1000),
                      unsigned((neo98_clock_us() - progress) / 1000), app->Error());
        neo98_diagnostic(detail);
    }
    if (!result && !CrtExitContractResult()) result = 2980;
    if (!result && (session.calls != 1 || session.flags != 1 || session.code != 0)) result = 2981;
    if (!result && presented != 15) result = 2982;
    X86::Application::SetSessionEndHook(nullptr, nullptr);
    delete app;
    if (!result && (User::LiveWindows() || Gdi::LiveObjects()))
        result = 1003;
    return result;
}

#include "../build/generated/module_contract.h"
unsigned RuntimeModuleSelfTest()
{
    unsigned result = 0;
    for (unsigned i = 1; i < sizeof ModuleContractFiles / sizeof ModuleContractFiles[0]; ++i)
    {
        const auto &file = ModuleContractFiles[i];
        char path[260] = "C:\\WINDOWS\\";
        unsigned n = 11;
        for (const char *p = file.name; *p;) path[n++] = *p++;
        path[n] = 0;
        if (Files::System().Seed(path, file.data, file.size)) result = 1004;
    }
    auto *app = new X86::Application(nullptr);
    if (!result && !app->Start(ModuleContractFiles[0].data, ModuleContractFiles[0].size,
                               OriginalFixedsys, sizeof OriginalFixedsys, "MODULE.EXE")) result = 1000;
    const auto start = neo98_clock_us();
    while (!result && neo98_clock_us() - start < 10000000 && app->Status() != X86::Application::Exited &&
           app->Status() != X86::Application::Failed) app->Pump();
    if (!result) result = app->Status() == X86::Application::Exited ? app->ExitCode() : 1001;
    delete app;
    for (unsigned i = 1; i < sizeof ModuleContractFiles / sizeof ModuleContractFiles[0]; ++i)
    {
        char path[260] = "C:\\WINDOWS\\";
        unsigned n = 11;
        for (const char *p = ModuleContractFiles[i].name; *p;) path[n++] = *p++;
        path[n] = 0;
        if (Files::System().Remove(path, false) && !result) result = 1005;
    }
    if (!result && (User::LiveWindows() || Gdi::LiveObjects())) result = 1003;
    return result;
}

unsigned RuntimeVirtualSelfTest()
{
    for (unsigned mode = 1; mode <= 4; ++mode)
    {
        auto *app = new X86::Application(nullptr);
        const char command[] = {char('0' + mode), 0};
        unsigned result = 0;
        if (!app->Start(VirtualContractProgram, sizeof VirtualContractProgram, OriginalFixedsys,
                        sizeof OriginalFixedsys, "VMEM.EXE", command)) result = 3100;
        for (unsigned i = 0; !result && i < 10000 && app->Status() != X86::Application::Exited &&
             app->Status() != X86::Application::Failed; ++i) app->Pump();
        // CPU faults now travel through Win98's exception dispatcher and carry
        // the address and page state. Check the access kind, not the old bare
        // Unicorn error text. API writes retain their own detailed diagnostic.
        const char *expected = mode == 1 ? "write to read-only" :
                               mode == 2 ? "Guest write violates page protection" :
                               mode == 3 ? "read from unmapped" :
                                           "fetch from non-executable";
        if (!result && (app->Status() != X86::Application::Failed || !strstr(app->Error(), expected) ||
                        (mode != 2 && !strstr(app->Error(), "Unhandled exception c0000005")))) result = 3100 + mode;
        if (result)
        {
            CString detail;
            detail.Format("VMEM failure mode=%u state=%u code=%u error=%s", mode,
                          app->Status(), app->ExitCode(), app->Error());
            neo98_diagnostic(detail);
        }
        delete app;
        if (result) return result;
        if (User::LiveWindows() || Gdi::LiveObjects()) return 3105;
    }
    return 0;
}
