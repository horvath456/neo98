// SPDX-License-Identifier: GPL-2.0-or-later
// MMSYSTEM's threads of the 16-bit side, ordinals 1120 to 1125:
// mmThreadCreate, mmThreadSignal, mmThreadBlock, mmThreadIsCurrent,
// mmThreadIsValid and mmThreadGetTask. MCIAVI.DRV plays on such a thread
// (MCIAVI creates it, is its body). MMSYSTEM is ours by the DLL
// rule; the thread runs the caller's 16-bit procedure hosted, on a guest thread
// of the process.
// The places in MMSYSTEM.DLL of the tree, segment 5:
// 0CD8 mmThreadCreate(fpThreadAddr, lpHandle, dwInstance, dwFlags): the
// handle is a global block of 2Eh bytes (GMEM_SHARE | GMEM_ZEROINIT)
// with "LISA" at +0, the pending signals at +4, the event at +8, the
// thread at +0Ch, the procedure at +10h, dwInstance at +14h, the state
// at +24h, dwFlags at +28h and the task at +2Ch. It starts the thread
// and waits until the state is 10h; the answer is zero or 2
// (MMSYSERR_NOMEM... the allocation failed).
// 0C30 the thread: the task at +2Ch, state 10h, mmThreadBlock, state 20h,
// the procedure with dwInstance, state 30h, a wait until no signal is
// pending, the signature DEADDEADh, the event closed, the block freed.
// 0E48 mmThreadSignal: the pending count up, SetEvent, the count down.
// 0E0A mmThreadBlock: a wait on the event.
// 0EA0 mmThreadIsValid: a writable block with "LISA" whose thread still
// runs.
// The older tasks of MMSYSTEM, ordinals 900 to 905 (mmTaskCreate, mmTaskBlock,
// mmTaskSignal, mmGetCurrentTask, mmTaskYield), are the same thing with a
// 16-bit task behind them: Windows starts MMTASK.TSK, which calls the
// procedure with dwInstance and ends; mmTaskBlock waits until the task is
// signalled, mmTaskSignal posts it the signal. MCIVISCA.DRV keeps its
// background task this way. Here a task of mmTaskCreate is a thread of
// mmThreadCreate that does not wait for a first signal; the handle is the
// same block, and GetCurrentTask on the thread answers it.
#include "win16_machine.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned MmThreadEntry = 0x71000f30, Signature = 0x4153494c, Dead = 0xdeaddead;
// dwFlags of a block that came from mmTaskCreate: the procedure runs at once.
constexpr unsigned TaskBlock = 0x80000000u;
constexpr unsigned AtPending = 4, AtEvent = 8, AtThread = 0xc, AtProcedure = 0x10, AtInstance = 0x14,
                   AtState = 0x24, AtFlags = 0x28, AtTask = 0x2c;
} // namespace

unsigned Application::Win16MmThreadBlock(unsigned handle)
{
    if (!handle || !Win16GlobalBlock(handle))
        return 0;
    const unsigned block = Win16Linear(handle, 0);
    return block && Read32(block) == Signature ? block : 0;
}

bool Application::Win16MmThreadCall(unsigned ordinal, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    result = 0;
    switch (ordinal)
    {
    case 902: // mmTaskBlock(hTask): the calling task waits for its signal
    case 903: // mmTaskSignal(hTask)
    {
        words = 1;
        // The task of the caller when it names none of ours (a task that is
        // no mmTask has no block to wait on: it yields once instead).
        unsigned handle = frame.Word(0);
        if (ordinal == 902 && win16.state)
            for (const auto &record : win16.state->hosted_threads)
                if (record.thread == ThreadId() && record.task) handle = record.task;
        const unsigned block = Win16MmThreadBlock(handle);
        if (!block)
        {
            const unsigned zero[] = {0};
            if (ordinal == 902) Api("Sleep", zero);
            result = 0;
            return state != Failed;
        }
        if (ordinal == 903)
        {
            const unsigned event[] = {Read32(block + AtEvent)};
            ThreadApi("SetEvent", event);
            result = 1;
            return true;
        }
        const unsigned wait[] = {Read32(block + AtEvent), 0xffffffffu};
        ThreadApi("WaitForSingleObject", wait);
        return state != Failed;
    }
    case 904: // mmGetCurrentTask
    {
        words = 0;
        unsigned task = win16.state ? win16.state->task : 0;
        if (win16.state)
            for (const auto &record : win16.state->hosted_threads)
                if (record.thread == ThreadId() && record.task) task = record.task;
        result = task;
        return true;
    }
    case 905: // mmTaskYield
    {
        words = 0;
        const unsigned zero[] = {0};
        Api("Sleep", zero);
        return state != Failed;
    }
    case 900:  // mmTaskCreate(lpfn, lpHandle, dwInstance)
    case 1120: // mmThreadCreate(fpThreadAddr, lpHandle, dwInstance, dwFlags)
    {
        const bool task = ordinal == 900;
        words = task ? 6 : 8;
        const unsigned shift = task ? 0 : 2;
        const unsigned procedure = frame.Long(4 + shift), out = frame.Pointer(2 + shift), instance = frame.Long(shift);
        const unsigned flags = task ? TaskBlock : frame.Long(0);
        const unsigned handle = Win16GlobalAlloc(0x2040, 0x2e);
        const unsigned block = handle ? Win16Linear(handle, 0) : 0;
        if (!block)
        {
            result = 2;
            return true;
        }
        Write32(block, Signature);
        Write32(block + AtProcedure, procedure);
        Write32(block + AtInstance, instance);
        Write32(block + AtFlags, flags);
        const unsigned create_event[] = {0, 0, 0, 0};
        Write32(block + AtEvent, ThreadApi("CreateEventA", create_event));
        const unsigned start[] = {0, 0, MmThreadEntry, handle, 0, 0};
        const unsigned thread = CreateThread(start);
        if (!thread)
        {
            const unsigned close[] = {Read32(block + AtEvent)};
            ThreadApi("CloseHandle", close);
            Win16GlobalFree(handle);
            result = 2;
            return true;
        }
        const unsigned close[] = {thread};
        if (log)
        {
            char text[160];
            snprintf(text, sizeof text, "MMTHREAD create handle=%04x procedure=%08x instance=%08x thread=%08x",
                     handle, procedure, instance, thread);
            log(text);
        }
        // The thread starts and blocks; MMSYSTEM waits for it with Yield.
        while (state != Failed && state != Exited && Read32(block + AtState) < 0x10 &&
               Read32(block) == Signature)
        {
            const unsigned zero[] = {0};
            Api("Sleep", zero);
        }
        ThreadApi("CloseHandle", close);
        if (out)
            Win16Word(out, handle);
        return state != Failed;
    }
    case 1121: // mmThreadSignal(hThread)
    {
        words = 1;
        const unsigned block = Win16MmThreadBlock(frame.Word(0));
        if (!block)
            return true;
        Write32(block + AtPending, Read32(block + AtPending) + 1);
        const unsigned event[] = {Read32(block + AtEvent)};
        ThreadApi("SetEvent", event);
        if (log && trace_calls)
        {
            char text[120];
            snprintf(text, sizeof text, "MMTHREAD signal handle=%04x thread=%u", frame.Word(0), ThreadId());
            log(text);
        }
        Write32(block + AtPending, Read32(block + AtPending) - 1);
        return true;
    }
    case 1122: // mmThreadBlock(hThread)
    {
        words = 1;
        const unsigned block = Win16MmThreadBlock(frame.Word(0));
        if (!block)
            return true;
        const unsigned wait[] = {Read32(block + AtEvent), 0xffffffffu};
        if (log && trace_calls)
        {
            char text[120];
            snprintf(text, sizeof text, "MMTHREAD block handle=%04x thread=%u", frame.Word(0), ThreadId());
            log(text);
        }
        ThreadApi("WaitForSingleObject", wait);
        if (log && trace_calls)
        {
            char text[120];
            snprintf(text, sizeof text, "MMTHREAD woke handle=%04x thread=%u", frame.Word(0), ThreadId());
            log(text);
        }
        return state != Failed;
    }
    case 1123: // mmThreadIsCurrent(hThread)
    {
        words = 1;
        const unsigned block = Win16MmThreadBlock(frame.Word(0));
        result = block && Read32(block + AtThread) == ThreadId();
        return true;
    }
    case 1124: // mmThreadIsValid(hThread)
    {
        words = 1;
        const unsigned block = Win16MmThreadBlock(frame.Word(0));
        result = block && Read32(block + AtState) < 0x30;
        return true;
    }
    case 1125: // mmThreadGetTask(hThread)
    {
        words = 1;
        const unsigned block = Win16MmThreadBlock(frame.Word(0));
        result = block ? Win16Word(block + AtTask) : 0;
        return true;
    }
    default:
        break;
    }
    return false;
}

// on the new guest thread.
void Application::RunWin16MmThread()
{
    const unsigned handle = Read32(Register(Reg::Esp) + 4) & 0xffff;
    const unsigned block = Win16MmThreadBlock(handle);
    if (!block || !Win16HostedReady())
        return;
    // The thread is a task of its own: GetCurrentTask on it answers the
    // handle, which is what mmThreadGetTask hands the creator.
    Win16HostedStack();
    for (auto &record : win16.state->hosted_threads)
        if (record.thread == ThreadId())
            record.task = handle;
    Win16Word(block + AtTask, handle);
    Write32(block + AtThread, ThreadId());
    Write32(block + AtState, 0x10);
    if (log)
    {
        char text[120];
        snprintf(text, sizeof text, "MMTHREAD started handle=%04x thread=%u", handle, ThreadId());
        log(text);
    }
    // A thread of mmThreadCreate waits for its first signal; a task of
    // mmTaskCreate runs its procedure at once.
    if (!(Read32(block + AtFlags) & TaskBlock))
    {
        const unsigned wait[] = {Read32(block + AtEvent), 0xffffffffu};
        ThreadApi("WaitForSingleObject", wait);
    }
    Write32(block + AtState, 0x20);
    const unsigned procedure = Read32(block + AtProcedure), instance = Read32(block + AtInstance);
    if (procedure && state != Failed && state != Exited)
    {
        const unsigned push[] = {instance >> 16, instance & 0xffff};
        bool returned = false;
        Win16HostedCall(procedure, push, 2, 0, nullptr, &returned);
    }
    Write32(block + AtState, 0x30);
    while (state != Failed && state != Exited && (int)Read32(block + AtPending) > 0)
    {
        const unsigned zero[] = {0};
        Api("Sleep", zero);
    }
    Write32(block, Dead);
    const unsigned close[] = {Read32(block + AtEvent)};
    ThreadApi("CloseHandle", close);
    Win16GlobalFree(handle);
    if (state != Failed && state != Exited && !threads[current_thread].done)
    {
        threads[current_thread].result = 0;
        threads[current_thread].done = true;
    }
}
} // namespace X86
