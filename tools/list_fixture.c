/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 report ListView, display callback and status-bar contract. */
#include "capture_handshake.h"
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct
{
    U hwnd, message, wp, lp, time;
    int x, y;
} CaptureMessage;
API U GetMessageA(CaptureMessage *, U, U, U);
API U TranslateMessage(const CaptureMessage *);
API U DispatchMessageA(const CaptureMessage *);
API void ExitProcess(U);
API void Sleep(U);
API U RegisterClassA(U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U SendMessageA(U, U, U, U);
API U GetClientRect(U, int *);
API U ShowWindow(U, int);
API U BeginDeferWindowPos(U);
API U DeferWindowPos(U, U, U, int, int, int, int, U);
API U EndDeferWindowPos(U);
API U CreateStatusWindowA(U, const char *, U, U);
API void GetEffectiveClientRect(U, int *, U *);
static U second_position, position_checks, check_positions;
static U sort_calls;
static int __stdcall compare_rows(U a, U b, U direction)
{
    ++sort_calls;
    Sleep(0);
    return ((int)a - (int)b) * (int)direction;
}
static U list, display_calls, inserted, deleted, vetoes, delete_all, suppress_delete;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static U __stdcall procedure(U hwnd, U message, U wp, U lp)
{
    if (message == 5 && check_positions)
    {
        int rect[4];
        check(GetClientRect(second_position, rect) && rect[2] == 80 && rect[3] == 30, 230);
        ++position_checks;
    }
    if (message == 0x4e)
    {
        U *data = (U *)lp;
        check(data[0] == list && data[1] == 99, 201);
        if (data[2] == (U)-150)
        {
            ++display_calls;
            data[8] = (U) "Deferred data";
            data[3] |= 0x1000;
        }
        if (data[2] == (U)-102)
            ++inserted;
        if (data[2] == (U)-103)
            ++deleted;
        if (data[2] == (U)-104)
        {
            ++delete_all;
            return suppress_delete;
        }
        if (data[2] == (U)-100 && (data[5] & 2) && data[10] == 2)
        {
            ++vetoes;
            return 1;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}
void run_list_test(U instance)
{
    U klass[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 0, 0, (U) "NEO98ListContract"};
    check(RegisterClassA(klass), 202);
    U parent = CreateWindowExA(0, "NEO98ListContract", "Native ListView", 0x10cf0000, 50, 50, 460, 330, 0, 0,
                               instance, 0);
    check(parent, 203);
    list = CreateWindowExA(0, "SysListView32", "", 0x50000041, 5, 5, 430, 240, parent, 99, instance, 0);
    check(list, 204);
    for (U i = 0; i < 2; ++i)
    {
        U column[6] = {2 | 4, 0, 200, (U)(i ? "Data" : "Name"), 0, i};
        check(SendMessageA(list, 0x101b, i, (U)column) == i, 205);
    }
    U column[6] = {2 | 8};
    check(SendMessageA(list, 0x1019, 1, (U)column) && column[2] == 200 && column[5] == 1, 206);
    U item[10] = {1 | 4, 0, 0, 0, 0, (U) "Windows 98", 0, 0, 1};
    check(SendMessageA(list, 0x1007, 0, (U)item) == 0, 207);
    item[2] = 1;
    item[5] = (U) "ARM services";
    check(SendMessageA(list, 0x102e, 0, (U)item), 208);
    item[1] = 1;
    item[2] = 0;
    item[5] = (U) "Callback row";
    item[8] = 2;
    check(SendMessageA(list, 0x1007, 0, (U)item) == 1, 209);
    item[2] = 1;
    item[5] = ~0u;
    check(SendMessageA(list, 0x102e, 1, (U)item), 210);
    char text[64];
    item[5] = (U)text;
    item[6] = 64;
    check(SendMessageA(list, 0x102d, 1, (U)item) == 13, 241);
    check(equal(text, "Deferred data"), 242);
    check(display_calls >= 1, 243);
    U cached_calls = display_calls;
    check(SendMessageA(list, 0x102d, 1, (U)item) == 13 && display_calls == cached_calls, 212);
    U state[5] = {0, 0, 0, 3, 3};
    check(SendMessageA(list, 0x102b, 0, (U)state), 213);
    check(!SendMessageA(list, 0x102b, 1, (U)state) && vetoes == 1, 214);
    check(SendMessageA(list, 0x100c, ~0u, 3) == 0 && SendMessageA(list, 0x1032, 0, 0) == 1, 215);
    check(SendMessageA(list, 0x1030, ~0u, (U)compare_rows) && sort_calls &&
              SendMessageA(list, 0x100c, ~0u, 3) == 1,
          238);
    check(SendMessageA(list, 0x1030, 1, (U)compare_rows) && SendMessageA(list, 0x100c, ~0u, 3) == 0, 239);
    U status = CreateStatusWindowA(0x50000110, "Ready", parent, 100);
    check(status, 216);
    int full[4], effective[4];
    U info[] = {0, 0, 1, 100, 0, 0};
    check(GetClientRect(parent, full), 217);
    GetEffectiveClientRect(parent, effective, info);
    check(effective[3] == full[3] - 20, 218);
    ShowWindow(status, 0);
    GetEffectiveClientRect(parent, effective, info);
    check(effective[3] == full[3], 219);
    ShowWindow(status, 1);
    int parts[] = {200, -1};
    check(SendMessageA(status, 0x404, 2, (U)parts), 220);
    check(SendMessageA(status, 0x401, 0, (U) "Normal pane") &&
              SendMessageA(status, 0x401, 255, (U) "Simple pane"),
          221);
    check(SendMessageA(status, 0x402, 0, (U)text) == 11 && equal(text, "Normal pane"), 222);
    check(SendMessageA(status, 0x402, 255, (U)text) == 11 && equal(text, "Simple pane"), 223);
    SendMessageA(status, 0x409, 1, 0);
    check(SendMessageA(status, 0x40e, 0, 0), 224);
    U first_position =
        CreateWindowExA(0, "NEO98ListContract", "", 0x40000000, 0, 0, 10, 10, parent, 110, instance, 0);
    second_position =
        CreateWindowExA(0, "NEO98ListContract", "", 0x40000000, 0, 0, 10, 10, parent, 111, instance, 0);
    check(first_position && second_position, 231);
    U batch = BeginDeferWindowPos(2);
    check(batch, 232);
    batch = DeferWindowPos(batch, first_position, 0, 10, 20, 40, 30, 0x14);
    check(batch, 233);
    batch = DeferWindowPos(batch, second_position, 0, 60, 20, 80, 30, 0x14);
    check(batch, 234);
    int before[4];
    check(GetClientRect(second_position, before) && before[2] == 10, 235);
    check_positions = 1;
    check(EndDeferWindowPos(batch) && position_checks == 2, 236);
    check_positions = 0;
    check(!EndDeferWindowPos(batch) && DestroyWindow(first_position) && DestroyWindow(second_position), 237);
    NEO98_WAIT_FOR_CAPTURE(CaptureMessage); // The host takes its picture of the finished list here.
    check(inserted == 2 && SendMessageA(list, 0x1009, 0, 0) && deleted == 2 &&
              SendMessageA(list, 0x1004, 0, 0) == 0,
          225);
    check(delete_all == 1, 227);
    item[0] = 1;
    item[1] = 0;
    item[2] = 0;
    item[5] = (U) "Suppressed deletion";
    check(SendMessageA(list, 0x1007, 0, (U)item) == 0, 228);
    suppress_delete = 1;
    check(SendMessageA(list, 0x1009, 0, 0) && delete_all == 2 && deleted == 2, 229);
    check(DestroyWindow(parent), 226);
}
