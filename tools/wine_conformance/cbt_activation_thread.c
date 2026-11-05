/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

static HANDLE ready, stop;
static HWND target, caller_window;
static DWORD target_thread;
static volatile LONG target_calls, wrong_thread, caller_calls, veto;
static HHOOK caller_hook, target_hook;
static unsigned phase;
static unsigned window_kind(HWND window)
{
    return !window ? 0 : window == caller_window ? 1 : window == target ? 2 : 3;
}

static LRESULT CALLBACK target_observer(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_SETFOCUS)
        trace("CBT_FOCUS phase=%u hook=target new=%u old=%u owner=%u\n", phase,
              window_kind((HWND)wp), window_kind((HWND)lp), GetCurrentThreadId() == target_thread);
    if (code == HCBT_ACTIVATE && (HWND)wp == target) {
        InterlockedIncrement(&target_calls);
        if (GetCurrentThreadId() != target_thread) InterlockedIncrement(&wrong_thread);
        if (veto) return 1;
    }
    return CallNextHookEx(target_hook, code, wp, lp);
}

static LRESULT CALLBACK caller_observer(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_SETFOCUS)
        trace("CBT_FOCUS phase=%u hook=caller new=%u old=%u owner=%u\n", phase,
              window_kind((HWND)wp), window_kind((HWND)lp), GetCurrentThreadId() != target_thread);
    if (code == HCBT_ACTIVATE && (HWND)wp == target) InterlockedIncrement(&caller_calls);
    return CallNextHookEx(caller_hook, code, wp, lp);
}

static DWORD WINAPI worker(void *unused)
{
    MSG msg;
    target_thread = GetCurrentThreadId();
    target = CreateWindowA("STATIC", "CBT target", WS_OVERLAPPEDWINDOW,
                          340, 120, 200, 140, NULL, NULL, GetModuleHandleA(NULL), NULL);
    target_hook = SetWindowsHookExA(WH_CBT, target_observer, NULL, target_thread);
    ShowWindow(target, SW_SHOWNOACTIVATE);
    SetEvent(ready);
    while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(1);
    }
    UnhookWindowsHookEx(target_hook);
    DestroyWindow(target);
    return 0;
}

START_TEST(cbt_activation_thread)
{
    HWND own;
    HANDLE thread;
    DWORD deadline, thread_id;
    MSG msg;
    own = CreateWindowA("STATIC", "CBT caller", WS_OVERLAPPEDWINDOW,
                       100, 120, 200, 140, NULL, NULL, GetModuleHandleA(NULL), NULL);
    caller_window = own;
    ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    ok(own && ready && stop, "caller and synchronization objects\n");
    if (!own || !ready || !stop) return;
    thread = CreateThread(NULL, 0, worker, NULL, 0, &thread_id);
    ok(thread != NULL, "worker error=%lu\n", GetLastError());
    if (!thread) return;
    ok(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0, "worker ready\n");
    ok(target && target_hook, "target and local hook\n");
    ShowWindow(own, SW_SHOWNORMAL);
    SetForegroundWindow(own);
    ok(GetForegroundWindow() == own, "caller foreground\n");
    caller_hook = SetWindowsHookExA(WH_CBT, caller_observer, NULL, GetCurrentThreadId());
    ok(caller_hook != NULL, "caller local hook\n");
    target_calls = wrong_thread = caller_calls = 0;
    phase = 1;
    SetForegroundWindow(target);
    deadline = GetTickCount() + 5000;
    while (GetForegroundWindow() != target && (LONG)(GetTickCount() - deadline) < 0) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(1);
    }
    ok(GetForegroundWindow() == target, "target foreground\n");
    ok(target_calls != 0, "target hook delivered\n");
    ok(wrong_thread == 0, "target hook ran on owner thread\n");
    ok(caller_calls == 0, "caller local hook did not receive target activation\n");
    phase = 2;
    SetForegroundWindow(own);
    ok(GetForegroundWindow() == own, "caller foreground before veto\n");
    target_calls = 0; veto = 1;
    phase = 3;
    SetForegroundWindow(target);
    deadline = GetTickCount() + 5000;
    while (!target_calls && (LONG)(GetTickCount() - deadline) < 0) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(1);
    }
    ok(target_calls != 0, "target hook delivered veto request\n");
    /* Let the owner finish the activation after returning from its hook. */
    deadline = GetTickCount() + 50;
    while ((LONG)(GetTickCount() - deadline) < 0) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(1);
    }
    ok(GetForegroundWindow() != target, "owner hook veto prevented activation\n");
    ok(wrong_thread == 0 && caller_calls == 0, "veto stayed on owner thread\n");
    veto = 0;
    phase = 4;
    UnhookWindowsHookEx(caller_hook);
    SetEvent(stop);
    deadline = GetTickCount() + 5000;
    while (WaitForSingleObject(thread, 0) != WAIT_OBJECT_0 && (LONG)(GetTickCount() - deadline) < 0) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(1);
    }
    ok(WaitForSingleObject(thread, 0) == WAIT_OBJECT_0, "worker finished\n");
    CloseHandle(thread); CloseHandle(ready); CloseHandle(stop);
    DestroyWindow(own);
}
