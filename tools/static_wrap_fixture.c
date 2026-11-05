/* SPDX-License-Identifier: GPL-2.0-or-later
 * A STATIC control with a paragraph: USER lays it out with DrawText, word
 * break and all, from the top of the control. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U UpdateWindow(U);
API U GetMessageA(void *, U, U, U);
API U DispatchMessageA(const void *);
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    U klass[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 16, 0, (U)"Neo98StaticWrap"};
    if (!RegisterClassA(klass)) ExitProcess(6101);
    U frame = CreateWindowExA(0, "Neo98StaticWrap", "wrap", 0x10cf0000, 0, 0, 400, 300, 0, 0, instance, 0);
    if (!frame) ExitProcess(6102);
    /* SS_LEFT, 160 wide: the sentence needs four lines of the system font. */
    if (!CreateWindowExA(0, "STATIC", "one two three four five six seven eight nine ten eleven twelve thirteen fourteen",
                         0x50000000, 20, 20, 160, 120, frame, 100, instance, 0)) ExitProcess(6103);
    /* SS_LEFTNOWORDWRAP keeps one line and cuts it at the control. */
    if (!CreateWindowExA(0, "STATIC", "one two three four five six seven eight nine ten eleven twelve thirteen fourteen",
                         0x5000000c, 20, 160, 160, 60, frame, 101, instance, 0)) ExitProcess(6104);
    ShowWindow(frame, 5);
    UpdateWindow(frame);
    U message[7];
    while (GetMessageA(message, 0, 0, 0)) DispatchMessageA(message);
    ExitProcess(0);
}
