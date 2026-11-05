// SPDX-License-Identifier: GPL-2.0-or-later
// TranslateMessage after USER: the character comes from the
// virtual key, the scan code and the thread's key state through KEYBOARD.DRV's
// ToAscii with the installed layout, whoever put the key into the queue.
#include "../src/user.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"TRANSLATE check failed at %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned window;
static void state(std::initializer_list<unsigned> keys)
{
    unsigned char s[256] = {};
    for (unsigned k : keys) s[k] = 0x80;
    User::KeyboardState(s, true);
}
// Translates one key message and collects what TranslateMessage posted.
static unsigned translate(unsigned message, unsigned vk, unsigned lparam, User::Message *out, unsigned capacity)
{
    User::Message m{};
    m.hwnd = window; m.message = message; m.wparam = vk; m.lparam = lparam;
    if (!User::TranslateMessage(&m)) return ~0u;
    unsigned n = 0;
    User::Message got;
    while (User::PeekMessage(&got, 0, 0, 0, true)) if (n < capacity) out[n++] = got;
    return n;
}
int main()
{
    CHECK(User::Initialize());
    CHECK(User::RegisterClass("TranslateOwner", 0, 0, 0));
    window = User::CreateWindow("TranslateOwner", "Keys", 0x10cf0000, 20, 20, 200, 100, 0, 0, 0, 1);
    CHECK(window);
    User::Message m[4], drain;
    while (User::PeekMessage(&drain, 0, 0, 0, true)) {}
    User::SetKeyboardLanguage(0x0407);
    // A key a program posted translates like one from the keyboard.
    state({});
    CHECK(translate(0x100, 'A', 0x001e0001, m, 4) == 1 && m[0].message == 0x102 && m[0].wparam == 'a' &&
          m[0].lparam == 0x001e0001 && m[0].hwnd == window);
    state({0x10});
    CHECK(translate(0x100, 'A', 0x001e0001, m, 4) == 1 && m[0].wparam == 'A');
    // German layout: Z and Y by virtual key, the umlauts, sharp s.
    state({});
    CHECK(translate(0x100, 0xba, 0x001a0001, m, 4) == 1 && m[0].wparam == 0xfc);
    CHECK(translate(0x100, 0xdb, 0x000c0001, m, 4) == 1 && m[0].wparam == 0xdf);
    state({0x10});
    CHECK(translate(0x100, 0xde, 0x00280001, m, 4) == 1 && m[0].wparam == 0xc4);
    CHECK(translate(0x100, '7', 0x00080001, m, 4) == 1 && m[0].wparam == '/');
    // Caps lock shifts the letters and the umlauts of KBDGR (caps bitmap).
    {
        unsigned char s[256] = {};
        s[0x14] = 1;
        User::KeyboardState(s, true);
        CHECK(translate(0x100, 0xc0, 0x00270001, m, 4) == 1 && m[0].wparam == 0xd6);
        CHECK(translate(0x100, 'B', 0x00300001, m, 4) == 1 && m[0].wparam == 'B');
    }
    // A dead key gives WM_DEADCHAR, the next key the composed character.
    state({});
    CHECK(translate(0x100, 0xdc, 0x00290001, m, 4) == 1 && m[0].message == 0x103 && m[0].wparam == '^');
    CHECK(translate(0x100, 'E', 0x00120001, m, 4) == 1 && m[0].message == 0x102 && m[0].wparam == 0xea);
    // The release of the dead key in between translates to nothing.
    CHECK(translate(0x100, 0xdd, 0x000d0001, m, 4) == 1 && m[0].message == 0x103 && m[0].wparam == 0xb4);
    CHECK(translate(0x101, 0xdd, 0xc00d0001, m, 4) == 0);
    CHECK(translate(0x100, 'A', 0x001e0001, m, 4) == 1 && m[0].wparam == 0xe1);
    // Space releases the dead key itself; a key without composition gives both.
    CHECK(translate(0x100, 0xdd, 0x000d0001, m, 4) == 1 && m[0].message == 0x103);
    CHECK(translate(0x100, 0x20, 0x00390001, m, 4) == 1 && m[0].message == 0x102 && m[0].wparam == 0xb4);
    CHECK(translate(0x100, 0xdc, 0x00290001, m, 4) == 1 && m[0].message == 0x103);
    CHECK(translate(0x100, 'X', 0x002d0001, m, 4) == 2 && m[0].message == 0x102 && m[0].wparam == '^' &&
          m[1].message == 0x102 && m[1].wparam == 'x');
    // AltGr is CONTROL+ALT.
    state({0x11, 0x12});
    CHECK(translate(0x100, 'Q', 0x00100001, m, 4) == 1 && m[0].message == 0x102 && m[0].wparam == '@');
    CHECK(translate(0x100, 'E', 0x00120001, m, 4) == 1 && m[0].wparam == 0x80);
    CHECK(translate(0x100, 0xdb, 0x000c0001, m, 4) == 1 && m[0].wparam == '\\');
    // ALT alone translates as if it were not held and gives WM_SYSCHAR, a dead
    // key WM_SYSDEADCHAR.
    state({0x12});
    CHECK(translate(0x104, 'A', 0x201e0001, m, 4) == 1 && m[0].message == 0x106 && m[0].wparam == 'a');
    CHECK(translate(0x104, 0xdc, 0x20290001, m, 4) == 1 && m[0].message == 0x107 && m[0].wparam == '^');
    state({});
    CHECK(translate(0x100, 0x20, 0x00390001, m, 4) == 1 && m[0].wparam == '^');
    // ALT and the numeric pad: the digits collect, the release of ALT gives
    // the character, as WM_CHAR although the key message is WM_SYSKEYUP.
    state({0x12});
    CHECK(translate(0x104, 0x12, 0x20380001, m, 4) == 0);
    CHECK(translate(0x104, 0x66, 0x204d0001, m, 4) == 0);
    CHECK(translate(0x105, 0x66, 0xe04d0001, m, 4) == 0);
    CHECK(translate(0x104, 0x65, 0x204c0001, m, 4) == 0);
    CHECK(translate(0x105, 0x65, 0xe04c0001, m, 4) == 0);
    state({});
    CHECK(translate(0x105, 0x12, 0xc0380001, m, 4) == 1 && m[0].message == 0x102 && m[0].wparam == 'A');
    // Keys without a character, key releases and other messages.
    CHECK(translate(0x100, 0x25, 0x014b0001, m, 4) == 0);
    CHECK(translate(0x101, 'A', 0xc01e0001, m, 4) == 0);
    CHECK(translate(0x102, 'A', 0x001e0001, m, 4) == ~0u);
    // The US layout: the same virtual key is a backslash there.
    User::SetKeyboardLanguage(0x0409);
    CHECK(translate(0x100, 0xdc, 0x002b0001, m, 4) == 1 && m[0].message == 0x102 && m[0].wparam == '\\');
    // Keyboard input: CONTROL+ALT is WM_KEYDOWN, not WM_SYSKEYDOWN, and the
    // key state of the retrieved key decides the character.
    User::SetKeyboardLanguage(0x0407);
    User::SetFocus(window);
    while (User::PeekMessage(&drain, 0, 0, 0, true)) {}
    User::KeyboardRaw(0xa2, 0, true, 0x1d);
    User::KeyboardRaw(0xa5, 0, true, 0x38, true);
    User::KeyboardRaw('Q', 0, true, 0x10);
    User::Message key{};
    bool found = false;
    while (User::PeekMessage(&key, 0, 0, 0, true))
        if (key.wparam == 'Q') { found = true; break; }
    CHECK(found && key.message == 0x100 && (key.lparam & 0x20000000));
    CHECK(User::TranslateMessage(&key));
    CHECK(User::PeekMessage(&key, 0, 0x102, 0x102, true) && key.wparam == '@');
    User::KeyboardRaw('Q', 0, false, 0x10);
    User::KeyboardRaw(0xa5, 0, false, 0x38, true);
    User::KeyboardRaw(0xa2, 0, false, 0x1d);
    User::KeyboardRaw(0xa4, 0, true, 0x38);
    User::KeyboardRaw('F', 0, true, 0x21);
    found = false;
    while (User::PeekMessage(&key, 0, 0, 0, true))
        if (key.wparam == 'F') { found = true; break; }
    CHECK(found && key.message == 0x104);
    CHECK(User::TranslateMessage(&key));
    CHECK(User::PeekMessage(&key, 0, 0x106, 0x106, true) && key.wparam == 'f');
    User::KeyboardRaw('F', 0, false, 0x21);
    User::KeyboardRaw(0xa4, 0, false, 0x38);
    while (User::PeekMessage(&drain, 0, 0, 0, true)) {}
    User::DestroyWindow(window);
    User::Shutdown();
    CHECK(!User::LiveWindows() && !Gdi::LiveObjects());
    std::puts("TRANSLATE MESSAGE PASS: posted keys, KBDGR umlauts, caps lock, dead keys, AltGr, WM_SYSCHAR, "
              "WM_SYSDEADCHAR, Alt+numpad, US layout, CONTROL+ALT is WM_KEYDOWN");
    return 0;
}
