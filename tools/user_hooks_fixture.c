/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual x86 callbacks through native USER hook chains and message retrieval. */
typedef unsigned U;
typedef struct
{
    U hwnd, message, wp, lp, time;
    int x, y;
} Message;
#define API __declspec(dllimport) __stdcall
API U SetWindowsHookExA(int, U, U, U);
API U UnhookWindowsHookEx(U);
API U CallNextHookEx(U, int, U, U);
API U CallMsgFilterA(Message *, int);
API U GetCurrentThreadId(void);
API U CreateThread(U, U, U, U, U, U);
API U CreateEventA(U, U, U, U);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U PostMessageA(U, U, U, U);
API U PeekMessageA(Message *, U, U, U, U);
API U GetMessageA(Message *, U, U, U);
API U CreateDialogParamA(U, U, U, U, U);
API U IsDialogMessageA(U, Message *);
API U GetDlgItem(U, U);
API U DestroyWindow(U);
API U VirtualAlloc(U, U, U, U);
API U VirtualProtect(U, U, U, U *);
API U VirtualFree(U, U, U);
API void Sleep(U);
API U SendMessageA(U, U, U, U);
API U DispatchMessageA(const Message *);
API void ExitProcess(U);
static U first, second, mode, calls, tails, reentered, retrieved, retrieval_mode;
static U ready, go, worker_hook, worker_calls;
static void hook_owner_lifecycle(void);
static void executable_hook(void);
static void scalar_hook_abi(void);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
/* WH_KEYBOARD: HC_ACTION (0) for a removed key message, HC_NOREMOVE (3) for a
 * peeked one; wParam is the virtual key, lParam the key data. A non-zero
 * answer discards the message. */
static U key_calls, key_codes[4], key_hook;
static U __stdcall keyboard(int code, U wp, U lp)
{
    if (key_calls < 4) key_codes[key_calls] = (U)code | wp << 8 | (lp >> 16 & 0xff) << 16;
    ++key_calls;
    if (wp == 0x42) return 1;
    return CallNextHookEx(key_hook, code, wp, lp);
}
/* WH_CALLWNDPROC: a CWPSTRUCT (lParam, wParam, message, hwnd) before a sent
 * message reaches the window procedure; a dispatched message stays out. */
static U proc_calls, proc_seen[4], proc_hook, proc_watch;
static U __stdcall procedure_hook(int code, U wp, U lp)
{
    const U *c = (const U *)lp;
    if (!code && c[2] == 0x8002 && c[3] == proc_watch)
    {
        proc_seen[0] = c[0]; proc_seen[1] = c[1]; proc_seen[2] = wp;
        ++proc_calls;
    }
    return CallNextHookEx(proc_hook, code, wp, lp);
}
static U __stdcall tail(int code, U wp, U lp)
{
    Message *m = (Message *)lp;
    check(code == 7 || code == 8, 3200);
    check(!wp && m->wp == 17, 3201);
    ++tails;
    return 0x1234;
}
static U __stdcall head(int code, U wp, U lp)
{
    Message *m = (Message *)lp;
    ++calls;
    if (!code)
    {
        m->wp = 88;
        return 1;
    }
    check(!wp, 3202);
    m->wp = 17;
    Sleep(0);
    if (mode == 1)
        check(UnhookWindowsHookEx(second), 3203);
    if (mode == 2)
        check(UnhookWindowsHookEx(first), 3204);
    if (mode == 3 && !reentered)
    {
        reentered = 1;
        check(CallMsgFilterA(m, 8) == 0x1235, 3205);
        check(UnhookWindowsHookEx(second), 3206);
    }
    return CallNextHookEx(second, code, wp, lp) + 1;
}
static U __stdcall gets(int code, U wp, U lp)
{
    Message *m = (Message *)lp;
    check(!code && wp == retrieval_mode && m->wp == 100, 3207);
    ++retrieved;
    m->wp = 999;
    if (retrieved == 3)
        check(UnhookWindowsHookEx(first), 3208);
    return 71; /* WH_GETMESSAGE return values do not consume a message. */
}
static U __stdcall worker_filter(int code, U wp, U lp)
{
    check(code == 7 && !wp && ((Message *)lp)->wp == 41, 3209);
    ++worker_calls;
    return 98;
}
static U __stdcall worker(U unused)
{
    (void)unused;
    worker_hook = SetWindowsHookExA(-1, (U)worker_filter, 0, GetCurrentThreadId());
    check(worker_hook && SetEvent(ready), 3210);
    check(!WaitForSingleObject(go, 10000), 3211);
    Message m = {0, 0x8001, 41};
    check(CallMsgFilterA(&m, 7) == 98, 3212);
    return 0; /* Thread cleanup must remove its remaining hook. */
}
static U __stdcall dialog(U hwnd, U msg, U wp, U lp)
{
    (void)hwnd;
    (void)wp;
    (void)lp;
    return msg == 0x110;
}
void run_user_hooks_test(U instance)
{
    U thread = GetCurrentThreadId();
    Message m = {0, 0x8001, 0};
    check(!SetWindowsHookExA(100, (U)head, 0, thread), 3213);
    check(!SetWindowsHookExA(-1, 0, 0, thread), 3214);
    check(!SetWindowsHookExA(-1, (U)head, 0, 0x7fffffff), 3215);
    /* Journal hooks are global; a thread-local one is refused. */
    check(!SetWindowsHookExA(0, (U)head, 0, thread), 3216);
    key_hook = SetWindowsHookExA(2, (U)keyboard, 0, thread);
    check(key_hook && PostMessageA(0, 0x100, 0x41, 0x001e0001), 3241);
    check(PeekMessageA(&m, 0, 0x100, 0x100, 0) && key_calls == 1 && key_codes[0] == (3 | 0x41 << 8 | 0x1e << 16), 3242);
    check(GetMessageA(&m, 0, 0x100, 0x100) && m.wp == 0x41 && key_calls == 2 && key_codes[1] == (0 | 0x41 << 8 | 0x1e << 16), 3243);
    check(PostMessageA(0, 0x100, 0x42, 0x00300001) && PostMessageA(0, 0x100, 0x43, 0x002e0001), 3244);
    check(GetMessageA(&m, 0, 0x100, 0x100) && m.wp == 0x43 && key_calls == 4, 3245);
    check(UnhookWindowsHookEx(key_hook), 3246);
    m.hwnd = 0; m.message = 0x8001; m.wp = 0; m.lp = 0;
    check(!CallNextHookEx(0, 7, 0, (U)&m), 3217); /* Original Win98 rejects zero. */
    for (mode = 0; mode < 4; ++mode)
    {
        calls = tails = reentered = 0;
        first = SetWindowsHookExA(-1, (U)tail, 0, thread);
        second = SetWindowsHookExA(-1, (U)head, 0, thread);
        check(first && second && first != second, 3218);
        check(CallMsgFilterA(&m, 7) == (mode == 2 ? 1 : 0x1235), 3219);
        check(calls == (mode == 3 ? 2 : 1), 3220);
        check(tails == (mode == 2 ? 0 : mode == 3 ? 2 : 1), 3221);
        check(UnhookWindowsHookEx(second) == (mode == 0 || mode == 2), 3222);
        check(UnhookWindowsHookEx(first) == (mode != 2), 3223);
        check(!CallMsgFilterA(&m, 7) && !UnhookWindowsHookEx(second), 3224);
    }
    ready = CreateEventA(0, 0, 0, 0);
    go = CreateEventA(0, 0, 0, 0);
    U handle = CreateThread(0, 0, (U)worker, 0, 0, 0);
    check(ready && go && handle && !WaitForSingleObject(ready, 10000), 3225);
    check(!CallMsgFilterA(&m, 7) && !worker_calls, 3226);
    check(SetEvent(go) && !WaitForSingleObject(handle, 10000), 3227);
    check(worker_calls == 1 && !UnhookWindowsHookEx(worker_hook), 3228);
    check(CloseHandle(handle) && CloseHandle(ready) && CloseHandle(go), 3229);
    first = SetWindowsHookExA(3, (U)gets, 0, thread);
    check(first && PostMessageA(0, 0x8001, 100, 0), 3230);
    retrieval_mode = 0;
    check(PeekMessageA(&m, 0, 0x8001, 0x8001, 0) && m.wp == 999, 3231);
    retrieval_mode = 1;
    check(PeekMessageA(&m, 0, 0x8001, 0x8001, 1) && m.wp == 999, 3232);
    check(PostMessageA(0, 0x8001, 100, 0), 3233);
    check(GetMessageA(&m, 0, 0x8001, 0x8001) && m.wp == 999, 3234);
    check(retrieved == 3 && !UnhookWindowsHookEx(first), 3235);
    check(PostMessageA(0, 0x8001, 100, 0), 3236);
    check(GetMessageA(&m, 0, 0x8001, 0x8001) && m.wp == 100 && retrieved == 3, 3237);
    second = SetWindowsHookExA(-1, (U)head, 0, thread);
    U hwnd = CreateDialogParamA(instance, 24, 0, (U)dialog, 0);
    check(hwnd && second, 3238);
    m.hwnd = GetDlgItem(hwnd, 201);
    m.message = 0x100;
    m.wp = 9;
    calls = 0;
    check(IsDialogMessageA(hwnd, &m) && calls == 1 && m.wp == 88, 3239);
    proc_hook = SetWindowsHookExA(4, (U)procedure_hook, 0, thread);
    proc_watch = hwnd;
    check(proc_hook, 3247);
    SendMessageA(hwnd, 0x8002, 5, 6);
    check(proc_calls == 1 && proc_seen[0] == 6 && proc_seen[1] == 5 && proc_seen[2], 3248);
    check(PostMessageA(hwnd, 0x8002, 7, 8) && GetMessageA(&m, hwnd, 0x8002, 0x8002), 3249);
    DispatchMessageA(&m);
    check(proc_calls == 1 && UnhookWindowsHookEx(proc_hook), 3250);
    check(UnhookWindowsHookEx(second) && DestroyWindow(hwnd), 3240);
    hook_owner_lifecycle();
    executable_hook();
    scalar_hook_abi();
}

static U life_ready, life_exit, life_thread, life_head, life_tail, life_main_id, life_owner_id;
static U life_replacement, life_new_ready, life_new_go, life_new_id, life_new_hook;
static U life_calls, life_new_calls;
static U __stdcall replacement_filter(int code, U wp, U lp)
{
    check(GetCurrentThreadId() == life_new_id && code == 9 && !wp && ((Message *)lp)->wp == 123, 3341);
    ++life_new_calls;
    return 0x2468;
}
static U __stdcall replacement_worker(U unused)
{
    (void)unused;
    Message m = {0, 0x8001, 123};
    check(!CallMsgFilterA(&m, 9), 3342);
    life_new_hook = SetWindowsHookExA(-1, (U)replacement_filter, 0, GetCurrentThreadId());
    check(life_new_hook && CallMsgFilterA(&m, 9) == 0x2468 && life_new_calls == 1, 3343);
    check(SetEvent(life_new_ready) && !WaitForSingleObject(life_new_go, 10000), 3344);
    check(CallMsgFilterA(&m, 9) == 0x2468 && life_new_calls == 2, 3345);
    check(UnhookWindowsHookEx(life_new_hook), 3346);
    return 0;
}
static U __stdcall detached_filter(int code, U wp, U lp)
{
    check(GetCurrentThreadId() == life_main_id && code == 9 && !wp && ((Message *)lp)->wp == 123, 3347);
    ++life_calls;
    /* This callback runs on main while its owning worker exits. */
    check(SetEvent(life_exit) && !WaitForSingleObject(life_thread, 10000), 3348);
    check(CloseHandle(life_thread) && !UnhookWindowsHookEx(life_head), 3349);
    /* Closing the final thread handle allows CreateThread to reuse its native slot. */
    life_replacement = CreateThread(0, 0, (U)replacement_worker, 0, 0, (U)&life_new_id);
    check(life_replacement && life_new_id != life_owner_id && !WaitForSingleObject(life_new_ready, 10000),
          3350);
    check(!CallMsgFilterA((Message *)lp, 9) && life_new_calls == 1, 3351);
    check(UnhookWindowsHookEx(life_tail), 3352); /* Still active: removal must remain deferred. */
    return 0x5678;
}
static U __stdcall owner_head(int code, U wp, U lp)
{
    (void)code;
    (void)wp;
    (void)lp;
    check(0, 3353); /* CallNext skips this record. */
    return 0;
}
static U __stdcall owner_worker(U unused)
{
    (void)unused;
    life_owner_id = GetCurrentThreadId();
    life_tail = SetWindowsHookExA(-1, (U)detached_filter, 0, life_owner_id);
    life_head = SetWindowsHookExA(-1, (U)owner_head, 0, life_owner_id);
    check(life_tail && life_head && SetEvent(life_ready), 3354);
    check(!WaitForSingleObject(life_exit, 10000), 3355);
    return 0;
}
static void hook_owner_lifecycle(void)
{
    Message m = {0, 0x8001, 123};
    U handles[128];
    life_main_id = GetCurrentThreadId();
    life_ready = CreateEventA(0, 0, 0, 0);
    life_exit = CreateEventA(0, 0, 0, 0);
    life_new_ready = CreateEventA(0, 0, 0, 0);
    life_new_go = CreateEventA(0, 0, 0, 0);
    check(life_ready && life_exit && life_new_ready && life_new_go, 3356);
    life_thread = CreateThread(0, 0, (U)owner_worker, 0, 0, 0);
    check(life_thread && !WaitForSingleObject(life_ready, 10000), 3357);
    check(CallNextHookEx(life_head, 9, 0, (U)&m) == 0x5678 && life_calls == 1, 3358);
    check(!UnhookWindowsHookEx(life_tail) && !CallMsgFilterA(&m, 9), 3359);
    check(SetEvent(life_new_go) && !WaitForSingleObject(life_replacement, 10000), 3360);
    check(life_new_calls == 2 && !UnhookWindowsHookEx(life_new_hook), 3361);
    check(CloseHandle(life_replacement) && CloseHandle(life_ready) && CloseHandle(life_exit) &&
              CloseHandle(life_new_ready) && CloseHandle(life_new_go),
          3362);
    /* A dead deferred record must not silently consume one of the 128 hook slots. */
    for (U i = 0; i < 128; ++i)
    {
        handles[i] = SetWindowsHookExA(-1, (U)owner_head, 0, life_main_id);
        check(handles[i] != 0, 3363);
    }
    check(!SetWindowsHookExA(-1, (U)owner_head, 0, life_main_id), 3364);
    for (U i = 0; i < 128; ++i)
        check(UnhookWindowsHookEx(handles[i]), 3365);
}
static void executable_hook(void)
{
    Message m = {0, 0x8001, 0};
    U previous = 0;
    U memory = VirtualAlloc(0, 4096, 0x3000, 4);
    check(memory != 0, 3366);
    /* mov eax, 98; ret 12 -- no data read from the callback's code page. */
    const unsigned char code[] = {0xb8, 98, 0, 0, 0, 0xc2, 12, 0};
    for (U i = 0; i < sizeof code; ++i)
        ((unsigned char *)memory)[i] = code[i];
    check(VirtualProtect(memory, 4096, 0x10, &previous), 3367);
    U hook = SetWindowsHookExA(-1, memory, 0, GetCurrentThreadId());
    check(hook && CallMsgFilterA(&m, 9) == 98, 3368);
    check(UnhookWindowsHookEx(hook) && VirtualFree(memory, 0, 0x8000), 3369);
}

static U abi_code, abi_wp, abi_result, abi_calls;
static U __stdcall scalar_callback(int code, U wp, U lp)
{
    check((U)code == abi_code && wp == abi_wp && lp != 0, 3370);
    ++abi_calls;
    return abi_result;
}
static void scalar_hook_abi(void)
{
    Message m = {0, 0x8001, 123};
    U thread = GetCurrentThreadId();
    U tail_hook = SetWindowsHookExA(-1, (U)scalar_callback, 0, thread);
    U head_hook = SetWindowsHookExA(-1, (U)owner_head, 0, thread);
    check(tail_hook && head_hook, 3371);
    abi_code = 0xffff8001;
    abi_wp = 0x8001;
    abi_result = 0x12348001;
    check(CallNextHookEx(head_hook, 0x12348001, 0x89ab8001, (U)&m) == abi_result && abi_calls == 1, 3372);
    check(UnhookWindowsHookEx(head_hook), 3373);
    abi_wp = 0;
    check(CallMsgFilterA(&m, 0x12348001) == 0xffff8001 && abi_calls == 2, 3374);
    abi_result = 0x10000;
    check(CallMsgFilterA(&m, 0x12348001) == 0 && abi_calls == 3, 3375);
    check(UnhookWindowsHookEx(tail_hook), 3376);
}
