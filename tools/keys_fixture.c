/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct
{
    U hwnd, message, wp, lp, time;
    int x, y;
} Message;
API U GetKeyState(U);
API U GetAsyncKeyState(U);
API U GetKeyboardState(unsigned char *);
API U SetKeyboardState(const unsigned char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U SetFocus(U);
API U SetTimer(U, U, U, U);
API U KillTimer(U, U);
API void Sleep(U);
API U DestroyWindow(U);
API U PostMessageA(U, U, U, U);
API U PeekMessageA(Message *, U, U, U, U);
API U GetMessageA(Message *, U, U, U);
API U TranslateMessage(const Message *);
API void ExitProcess(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static Message next(U h, U lo, U hi)
{
    Message m;
    check(PeekMessageA(&m, h, lo, hi, 1), 2200);
    return m;
}
void run_keys_test(U instance)
{
    unsigned char state[256], output[288];
    for (U i = 0; i < 256; ++i)
        state[i] = (unsigned char)(i * 37);
    for (U i = 0; i < 288; ++i)
        output[i] = 0xa5;
    check(SetKeyboardState(state) && GetKeyboardState(output + 16), 2201);
    for (U i = 0; i < 288; ++i)
        check(output[i] == (i >= 16 && i < 272 ? state[i - 16] : 0xa5), 2202);
    for (U i = 0; i < 256; ++i)
    {
        const U value = ((state[i] & 128) ? 0xffffff80 : 0) | (state[i] & 1);
        check(GetKeyState(i) == value && GetKeyState(i | 0xffff0000) == value && !GetAsyncKeyState(i), 2203);
        state[i] = 0;
    }
    check(SetKeyboardState(state), 2204);
    U cls[] = {0, (U)procedure, 0, 0, instance, 0, 0, 16, 0, (U) "NEO98InputContract"};
    check(RegisterClassA(cls), 2205);
    U h = CreateWindowExA(0, "NEO98InputContract", "Keyboard queue contract", 0x10cf0000, 100, 100, 300, 200,
                          0, 0, instance, 0);
    check(h != 0, 2206);
    SetFocus(h);
    check(PostMessageA(h, 0x100, 'A', 1), 2207);
    Message m = next(h, 0x100, 0x101);
    // USER answers 1 for every WM_KEYDOWN/KEYUP/SYSKEYDOWN/SYSKEYUP,
    // also for one a program posted; only other messages give 0.
    check(m.wp == 'A' && !GetKeyState('A') && !GetAsyncKeyState('A') && TranslateMessage(&m) == 1, 2208);
    {
        // The character comes from the key and the key state through the
        // layout, also for a key the program posted itself.
        Message c;
        check(PeekMessageA(&c, h, 0x102, 0x103, 1) && c.message == 0x102 && c.wp == 'a' && c.lp == 1, 22126);
    }
    {
        Message other = m;
        other.message = 0x200;
        check(TranslateMessage(&other) == 0 && TranslateMessage(0) == 0, 22123);
    }
    // A due unrelated timer and pending paint must not spin a filtered GetMessage.
    U timer = SetTimer(h, 98, 10, 0);
    check(timer != 0, 2229);
    Sleep(70); // a USER timer comes at most every 55 ms
    // Host/Pi driver enqueues all hardware transitions before this wake-up.
    check(GetMessageA(&m, h, 0x8001, 0x8001), 2209);
    check(KillTimer(h, timer), 2230);
    check(!GetKeyState('A') && !GetKeyState(0x10) && GetAsyncKeyState('A') == 1 && !GetAsyncKeyState('A'),
          2210);
    check(GetAsyncKeyState(2) == 1 && !GetAsyncKeyState(2), 2211);
    check(PeekMessageA(&m, h, 0x100, 0x101, 0), 22120);
    check(m.wp == 'A', 22121);
    check(!GetKeyState('A'), 22122);
    m = next(h, 0x100, 0x101);
    check(m.message == 0x100 && m.wp == 'A' && m.lp == 1 && GetKeyState('A') == 0xffffff80 &&
              GetKeyState(0x10) == 0xffffff80,
          2213);
    check(TranslateMessage(&m), 2214);
    m = next(h, 0x100, 0x101);
    check(m.message == 0x101 && m.wp == 'A' && m.lp == 0xc0000001 && !GetKeyState('A') && !GetKeyState(0x10),
          2215);
    check(TranslateMessage(&m) == 1, 22124);
    m = next(h, 0x100, 0x101);
    check(m.wp == 0x14 && GetKeyState(0x14) == 0xffffff81 && !(m.lp & 0x40000000), 2216);
    check(TranslateMessage(&m) == 1, 22125); // Caps Lock has no character.
    m = next(h, 0x100, 0x101);
    check(m.wp == 0x14 && GetKeyState(0x14) == 0xffffff81 && (m.lp & 0x40000000), 2217);
    m = next(h, 0x100, 0x101);
    check(m.message == 0x101 && GetKeyState(0x14) == 1, 2218);
    m = next(h, 0x100, 0x101);
    check(m.message == 0x100 && GetKeyState(0x14) == 0xffffff80, 2219);
    m = next(h, 0x100, 0x101);
    check(m.message == 0x101 && !GetKeyState(0x14), 2220);
    m = next(h, 0x204, 0x205);
    check(m.message == 0x204 && m.wp == 2 && m.lp == (23u << 16 | 46) && GetKeyState(2) == 0xffffff80, 2221);
    m = next(h, 0x204, 0x205);
    check(m.message == 0x205 && !m.wp && !GetKeyState(2), 2222);
    m = next(h, 0x100, 0x101);
    check(m.wp == 0x10 && GetKeyState(0xa0) == 0xffffff80 && !GetKeyState(0xa1), 2223);
    m = next(h, 0x100, 0x101);
    check(m.wp == 0x10 && GetKeyState(0xa0) == 0xffffff80 && GetKeyState(0xa1) == 0xffffff80, 2224);
    m = next(h, 0x100, 0x101);
    check(m.message == 0x101 && !GetKeyState(0xa0) && GetKeyState(0xa1) == 0xffffff80 &&
              GetKeyState(0x10) == 0xffffff80,
          2225);
    m = next(h, 0x100, 0x101);
    check(m.message == 0x101 && !GetKeyState(0xa1) && !GetKeyState(0x10), 2226);
    m = next(h, 0x102, 0x102);
    check(m.wp == 'A' && !GetKeyState('A'), 2227);
    while (PeekMessageA(&m, h, 0, 0, 1))
    {
        if (m.message == 0xf)
            DefWindowProcA(h, m.message, m.wp, m.lp);
    }
    check(DestroyWindow(h), 2228);
}
