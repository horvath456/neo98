/* SPDX-License-Identifier: GPL-2.0-or-later */
/* the system mouse of DINPUT.DLL 4.06 without exclusive access.
   Win98 serves it through DINPUT.VXD; without the driver the mouse emulation
   of the DLL takes only DISCL_EXCLUSIVE | DISCL_FOREGROUND and Acquire fails,
   which left Smileyshoot-Windowed without a mouse.

   The fixture opens GUID_SysMouse with the layout of c_dfDIMouse, asks for
   DISCL_NONEXCLUSIVE | DISCL_BACKGROUND and a buffer of 32 entries, acquires,
   and then reads the buffer while the probe moves the mouse by 100 pixels to
   the right and presses and releases the left button. Exit codes name the
   failing step. */
typedef unsigned U;
typedef struct { U a; unsigned short b, c; unsigned char d[8]; } GUID;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U DirectInputCreateA(U, U, void **, void *);

static const GUID sys_mouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID x_axis = {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID y_axis = {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
static const GUID z_axis = {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
/* c_dfDIMouse: DIDFT_AXIS | DIDFT_ANYINSTANCE, DIDFT_BUTTON | DIDFT_ANYINSTANCE,
   DIDFT_OPTIONAL for Z and the last two buttons. */
static struct { const GUID *guid; U offset, type, flags; } objects[7] = {
    {&x_axis, 0, 0x00ffff03, 0}, {&y_axis, 4, 0x00ffff03, 0}, {&z_axis, 8, 0x80ffff03, 0},
    {0, 12, 0x00ffff0c, 0}, {0, 13, 0x00ffff0c, 0}, {0, 14, 0x80ffff0c, 0}, {0, 15, 0x80ffff0c, 0}};
static struct { U size, object_size, flags, data_size, count; void *objects; } format = {24, 16, 2, 16, 7, objects};

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
typedef U(__stdcall *Method)();
#define CALL(object, index, ...) (((U(__stdcall *)())((*(void ***)(object))[index]))(object, ##__VA_ARGS__))

void mainCRTStartup(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"DinputVxd98"};
    void *dinput = 0, *mouse = 0;
    U window, message[7];
    struct { U size, header, object, how, data; } buffer = {20, 16, 0, 0, 32};
    struct { U offset, data, time, sequence; } entries[32];
    int dx = 0, down = 0, up = 0, state_dx = 0;
    unsigned round;
    check(RegisterClassA(cls) != 0, 10);
    window = CreateWindowExA(0, "DinputVxd98", "dinput", 0x10cf0000u, 0, 0, 320, 240, 0, 0, 0, 0);
    check(window != 0, 11);
    check(DirectInputCreateA(GetModuleHandleA(0), 0x0500, &dinput, 0) == 0, 12);
    check(CALL(dinput, 3, &sys_mouse, &mouse, 0) == 0, 13);               /* CreateDevice */
    check(CALL(mouse, 11, &format) == 0, 14);                               /* SetDataFormat */
    check(CALL(mouse, 13, window, 2 | 8) == 0, 15);                         /* NONEXCLUSIVE | BACKGROUND */
    check(CALL(mouse, 6, (U)1, &buffer) == 0, 16);                          /* DIPROP_BUFFERSIZE */
    check(CALL(mouse, 7) == 0, 17);                                         /* Acquire */
    for (round = 0; round < 300 && !(dx >= 100 && down && up); ++round)
    {
        U count = 32, i;
        int state[4] = {0};
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        check(CALL(mouse, 10, 16, entries, &count, 0) == 0, 18);           /* GetDeviceData */
        for (i = 0; i < count; ++i)
        {
            if (entries[i].offset == 0) dx += (int)entries[i].data;
            if (entries[i].offset == 12 && (entries[i].data & 0x80)) down = 1;
            if (entries[i].offset == 12 && !(entries[i].data & 0x80) && down) up = 1;
        }
        check(CALL(mouse, 9, 16, state) == 0, 19);                         /* GetDeviceState */
        state_dx += state[0];
        Sleep(30);
    }
    check(dx == 100, 20);      /* relative data: the 100 pixels to the right */
    check(down && up, 21);     /* the left button pressed and released */
    check(state_dx == 100, 22);
    CALL(mouse, 8);                                                         /* Unacquire */
    CALL(mouse, 2);
    CALL(dinput, 2);
    ExitProcess(0);
}
