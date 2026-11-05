/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* The transitions between an ARM64 program and the providers, one check per
   class of parameter: integers, pointers, handles, structs in both
   directions, more than eight arguments, floating point, variadic calls,
   callbacks of every kind, window messages with struct parameters, threads.
   Prints one line per failed check and a summary; the exit code is the
   number of failures. tools/test_arm64_apps_native.py, docs/ARM64-APPS.md. */
#include <windows.h>

static int checks, failures;
static HANDLE output;

static void Print(const char *text)
{
    DWORD written;
    WriteFile(output, text, (DWORD)lstrlenA(text), &written, NULL);
}
static void Check(int id, int condition)
{
    char line[64];
    ++checks;
    if (condition) return;
    ++failures;
    wsprintfA(line, "FAIL %d\r\n", id);
    Print(line);
}

/* --- callbacks --------------------------------------------------------- */
struct EnumContext
{
    int calls;
    HWND first;
    unsigned long long marker;
};
static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam)
{
    struct EnumContext *context = (struct EnumContext *)lParam;
    if (!context->calls++) context->first = hwnd;
    return context->marker == 0x1122334455667788ull && context->calls < 2;
}
static DWORD WINAPI ThreadProc(LPVOID parameter)
{
    int *value = (int *)parameter;
    *value += 41;
    Sleep(1);
    return 0x80000007u;
}
static int timer_calls;
static UINT_PTR timer_id_seen;
static void CALLBACK TimerProc(HWND hwnd, UINT message, UINT_PTR id, DWORD time)
{
    (void)hwnd; (void)time;
    if (message == WM_TIMER) { ++timer_calls; timer_id_seen = id; }
}

/* --- window procedure --------------------------------------------------- */
static int create_ok, nccreate_ok, user_ok, poschanging_seen, copydata_ok, other_calls;
static void *create_parameter;
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lParam;
        nccreate_ok = cs->lpCreateParams == create_parameter && !lstrcmpA(cs->lpszClass, "Arm64Transitions");
        break;
    }
    case WM_CREATE:
    {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lParam;
        create_ok = cs->lpCreateParams == create_parameter && cs->cx == 320 && cs->cy == 200 &&
                    !lstrcmpA(cs->lpszName, "transitions") && cs->hwndParent == NULL &&
                    cs->hInstance == GetModuleHandleA(NULL);
        return 0;
    }
    case WM_USER + 1:
        user_ok = wParam == 0x12345678u && lParam == -5;
        return -2;
    case WM_USER + 2:
        /* A pointer of the program as an opaque value, there and back. */
        return lParam;
    case WM_WINDOWPOSCHANGING:
    {
        WINDOWPOS *pos = (WINDOWPOS *)lParam;
        if (pos->hwnd == hwnd && !(pos->flags & SWP_NOSIZE) && pos->cx == 333)
        {
            ++poschanging_seen;
            pos->cx = 300; /* an inout struct: the system takes the change */
        }
        break;
    }
    case WM_COPYDATA:
    {
        COPYDATASTRUCT *data = (COPYDATASTRUCT *)lParam;
        copydata_ok = data->dwData == 77 && data->cbData == 6 && !lstrcmpA((const char *)data->lpData, "hello") &&
                      (HWND)wParam == hwnd;
        return 1;
    }
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}
static LRESULT CALLBACK OtherProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    ++other_calls;
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static int Deep(int n)
{
    /* More than a page of locals in one frame: the compiler calls __chkstk. */
    volatile char block[20000];
    block[0] = (char)n;
    block[sizeof block - 1] = (char)(n + 1);
    return n ? Deep(n - 1) + block[0] : block[sizeof block - 1];
}

void *ReadX18(void);
__asm__(".text\n.globl ReadX18\nReadX18:\n mov x0, x18\n ret\n");

int main(int argc, char **argv)
{
    char text[512], path[MAX_PATH];
    DWORD count = 0;
    (void)argc; (void)argv;
    output = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Integers and strings. */
    Check(1, MulDiv(1000, 3, 7) == 429);
    Check(2, lstrlenA("twelve chars") == 12);
    Check(3, lstrcpyA(text, "copied") == text && !lstrcmpA(text, "copied"));
    Check(4, lstrcmpiA("ABC", "abc") == 0 && lstrcmpA("a", "b") < 0);

    /* A handle is sign-extended: INVALID_HANDLE_VALUE is -1 in 64 bits. */
    HANDLE missing = CreateFileA("C:\\NOSUCH\\FILE.TXT", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    Check(10, missing == INVALID_HANDLE_VALUE);
    Check(11, GetLastError() == ERROR_PATH_NOT_FOUND || GetLastError() == ERROR_FILE_NOT_FOUND);
    SetLastError(1234);
    Check(12, GetLastError() == 1234);

    /* Files. */
    Check(20, GetTempPathA(sizeof path, path) > 0);
    lstrcatA(path, "ARM64T.TMP");
    HANDLE file = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    Check(21, file != INVALID_HANDLE_VALUE && file != NULL);
    Check(22, WriteFile(file, "0123456789", 10, &count, NULL) && count == 10);
    Check(23, GetFileSize(file, NULL) == 10);
    Check(24, SetFilePointer(file, 4, NULL, FILE_BEGIN) == 4);
    memset(text, 0, sizeof text);
    Check(25, ReadFile(file, text, 3, &count, NULL) && count == 3 && !lstrcmpA(text, "456"));
    Check(26, CloseHandle(file));
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA(path, &found);
    Check(27, find != INVALID_HANDLE_VALUE && found.nFileSizeLow == 10 && !lstrcmpiA(found.cFileName, "ARM64T.TMP"));
    Check(28, FindClose(find));
    char *part = NULL;
    Check(29, GetFullPathNameA(path, sizeof text, text, &part) > 0 && part > text && part < text + sizeof text &&
                  !lstrcmpiA(part, "ARM64T.TMP"));
    Check(30, DeleteFileA(path) && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES);

    /* Memory, and structs the system fills. */
    HANDLE heap = GetProcessHeap();
    char *block = (char *)HeapAlloc(heap, HEAP_ZERO_MEMORY, 1000);
    Check(40, block != NULL && block[999] == 0 && HeapSize(heap, 0, block) >= 1000);
    block[999] = 9;
    block = (char *)HeapReAlloc(heap, 0, block, 5000);
    Check(41, block != NULL && block[999] == 9);
    Check(42, HeapFree(heap, 0, block));
    char *pages = (char *)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(43, pages != NULL && ((ULONG_PTR)pages >> 32) == 0);
    pages[0x8000] = 5;
    MEMORY_BASIC_INFORMATION info;
    memset(&info, 0xee, sizeof info);
    Check(44, VirtualQuery(pages + 0x8000, &info, sizeof info) == sizeof info);
    Check(45, info.AllocationBase == pages && info.BaseAddress == pages + 0x8000 && info.State == MEM_COMMIT &&
                  info.Protect == PAGE_READWRITE && info.RegionSize != 0 && (info.RegionSize >> 32) == 0);
    Check(46, VirtualFree(pages, 0, MEM_RELEASE));
    SYSTEM_INFO system;
    memset(&system, 0xee, sizeof system);
    GetSystemInfo(&system);
    Check(47, system.dwPageSize == 4096 && system.dwNumberOfProcessors >= 1 &&
                  ((ULONG_PTR)system.lpMaximumApplicationAddress >> 32) == 0 &&
                  system.lpMaximumApplicationAddress > system.lpMinimumApplicationAddress);
    MEMORYSTATUS memory;
    memset(&memory, 0, sizeof memory);
    memory.dwLength = sizeof memory;
    GlobalMemoryStatus(&memory);
    Check(48, memory.dwLength == sizeof memory && memory.dwTotalPhys != 0 && (memory.dwTotalPhys >> 32) == 0);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, 64);
    char *locked = (char *)GlobalLock(global);
    Check(49, global != NULL && locked != NULL && GlobalSize(global) >= 64);
    GlobalUnlock(global);
    Check(50, GlobalFree(global) == NULL);

    /* More than eight arguments, floating point, a struct by value. */
    HFONT font = CreateFontA(-12, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "MS Sans Serif");
    Check(60, font != NULL);
    HDC dc = CreateCompatibleDC(NULL);
    Check(61, dc != NULL);
    HGDIOBJ old_font = SelectObject(dc, font);
    TEXTMETRICA metrics;
    Check(62, GetTextMetricsA(dc, &metrics) && metrics.tmAveCharWidth > 0 && metrics.tmHeight > 0);
    SIZE extent = {0, 0};
    Check(63, GetTextExtentPoint32A(dc, "wide text", 9, &extent) && extent.cx > 9 && extent.cy == metrics.tmHeight);
    SelectObject(dc, old_font);
    FLOAT before = 0, now = 0;
    Check(64, SetMiterLimit(dc, 7.5f, &before) && GetMiterLimit(dc, &now) && now == 7.5f && before == 10.0f);
    Check(65, DeleteDC(dc) && DeleteObject(font));
    RECT rect;
    POINT inside = {15, 25}, outside = {50, 25};
    SetRect(&rect, 10, 20, 30, 40);
    Check(66, PtInRect(&rect, inside) && !PtInRect(&rect, outside));
    wsprintfA(text, "%d %s %x %c %d %d %d %d %d %u", -3, "str", 0xbeef, 'Z', 5, 6, 7, 8, 9, 4000000000u);
    Check(67, !lstrcmpA(text, "-3 str beef Z 5 6 7 8 9 4000000000"));
    LARGE_INTEGER counter, frequency;
    counter.QuadPart = 0;
    Check(68, QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&counter) && counter.QuadPart != 0);

    /* Kernel objects, a handle array, TLS, a critical section. */
    HANDLE events[2];
    events[0] = CreateEventA(NULL, TRUE, FALSE, NULL);
    events[1] = CreateEventA(NULL, TRUE, FALSE, NULL);
    Check(70, events[0] && events[1] && WaitForSingleObject(events[0], 0) == WAIT_TIMEOUT);
    SetEvent(events[1]);
    Check(71, WaitForMultipleObjects(2, events, FALSE, 0) == WAIT_OBJECT_0 + 1);
    Check(72, CloseHandle(events[0]) && CloseHandle(events[1]));
    DWORD tls = TlsAlloc();
    Check(73, tls != TLS_OUT_OF_INDEXES && TlsSetValue(tls, &counter) && TlsGetValue(tls) == &counter && TlsFree(tls));
    CRITICAL_SECTION section;
    InitializeCriticalSection(&section);
    EnterCriticalSection(&section);
    EnterCriticalSection(&section);
    LeaveCriticalSection(&section);
    LeaveCriticalSection(&section);
    DeleteCriticalSection(&section);
    Check(74, 1);

    /* A thread: its routine is a callback, its parameter a pointer. */
    int shared = 1;
    DWORD thread_id = 0, exit_code = 0;
    HANDLE thread = CreateThread(NULL, 0, ThreadProc, &shared, 0, &thread_id);
    Check(80, thread != NULL && thread_id != 0 && thread_id != GetCurrentThreadId());
    Check(81, WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    Check(82, GetExitCodeThread(thread, &exit_code) && exit_code == 0x80000007u && shared == 42);
    CloseHandle(thread);

    /* GetProcAddress hands out the same gate the import table holds. */
    DWORD (WINAPI *tick)(void) = (DWORD (WINAPI *)(void))GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "GetTickCount");
    Check(85, tick != NULL && tick() != 0 && (void *)tick == (void *)GetTickCount);
    Check(86, LoadLibraryA("COMCTL32.DLL") == NULL && GetLastError() == ERROR_BAD_EXE_FORMAT);
    Check(87, Deep(6) == 22);
    void **teb = (void **)ReadX18();
    Check(88, teb != NULL && teb[6] == (void *)teb);

    /* Windows: a class with a procedure, creation parameters, messages. */
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "Arm64Transitions";
    wc.cbWndExtra = 16;
    Check(100, RegisterClassA(&wc) != 0);
    WNDCLASSA queried;
    memset(&queried, 0xee, sizeof queried);
    Check(101, GetClassInfoA(wc.hInstance, "Arm64Transitions", &queried) && queried.lpfnWndProc == WindowProc &&
                   queried.cbWndExtra == 16 && queried.hbrBackground == wc.hbrBackground);
    create_parameter = &shared;
    HWND hwnd = CreateWindowExA(0, "Arm64Transitions", "transitions", WS_OVERLAPPEDWINDOW, 40, 50, 320, 200, NULL, NULL,
                                wc.hInstance, create_parameter);
    Check(102, hwnd != NULL && IsWindow(hwnd) && ((ULONG_PTR)hwnd >> 32) == 0);
    Check(103, nccreate_ok && create_ok);
    Check(104, SendMessageA(hwnd, WM_USER + 1, 0x12345678u, -5) == -2 && user_ok);
    Check(105, SendMessageA(hwnd, WM_USER + 2, 0, (LPARAM)&shared) == (LRESULT)&shared);
    Check(106, GetWindowTextA(hwnd, text, sizeof text) == 11 && !lstrcmpA(text, "transitions"));
    Check(107, SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)&shared) == 0 &&
                   GetWindowLongPtrA(hwnd, GWLP_USERDATA) == (LONG_PTR)&shared);
    Check(108, SetWindowLongPtrA(hwnd, 8, (LONG_PTR)text) == 0 && GetWindowLongPtrA(hwnd, 8) == (LONG_PTR)text);
    Check(109, (WNDPROC)GetWindowLongPtrA(hwnd, GWLP_WNDPROC) == WindowProc);
    WNDPROC previous = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)OtherProc);
    Check(110, previous == WindowProc);
    user_ok = 0;
    Check(111, CallWindowProcA(previous, hwnd, WM_USER + 1, 0x12345678u, -5) == -2 && user_ok);
    other_calls = 0;
    SendMessageA(hwnd, WM_USER + 1, 1, 1);
    Check(112, other_calls == 1);
    SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)previous);
    Check(113, SetWindowPos(hwnd, NULL, 0, 0, 333, 200, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) && poschanging_seen == 1);
    Check(114, GetWindowRect(hwnd, &rect) && rect.right - rect.left == 300);
    COPYDATASTRUCT copy;
    copy.dwData = 77;
    copy.cbData = 6;
    copy.lpData = "hello";
    Check(115, SendMessageA(hwnd, WM_COPYDATA, (WPARAM)hwnd, (LPARAM)&copy) == 1 && copydata_ok);
    struct EnumContext context = {0, NULL, 0x1122334455667788ull};
    EnumWindows(EnumProc, (LPARAM)&context);
    Check(116, context.calls >= 1 && context.first != NULL && IsWindow(context.first));

    /* The queue: a posted message, a timer with a procedure. */
    MSG message;
    Check(120, PostMessageA(hwnd, WM_USER + 7, 0xabcdef01u, -7));
    memset(&message, 0xee, sizeof message);
    Check(121, PeekMessageA(&message, hwnd, WM_USER + 7, WM_USER + 7, PM_REMOVE) && message.hwnd == hwnd &&
                   message.message == WM_USER + 7 && message.wParam == 0xabcdef01u && message.lParam == -7);
    Check(122, SetTimer(hwnd, 5, 10, TimerProc) == 5);
    DWORD started = GetTickCount();
    while (!timer_calls && GetTickCount() - started < 3000 && GetMessageA(&message, NULL, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    Check(123, timer_calls >= 1 && timer_id_seen == 5 && KillTimer(hwnd, 5));
    Check(124, DestroyWindow(hwnd) && !IsWindow(hwnd) && UnregisterClassA("Arm64Transitions", wc.hInstance));

    wsprintfA(text, "TRANSITIONS checks=%d failures=%d\r\n", checks, failures);
    Print(text);
    return failures;
}
