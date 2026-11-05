/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The x86 side of tools/arm64_mixed_fixture.c: an original-style 32-bit
   program that finds the window of the ARM64 process, sends it messages and
   posts back what it saw. Bit n of the posted mask is check n. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U FindWindowA(const char *, const char *);
API U SendMessageA(U, U, U, U);
API U PostMessageA(U, U, U, U);
API int GetWindowTextA(U, char *, int);
API int lstrcmpA(const char *, const char *);
API int SetPropA(U, const char *, U);
API U GetPropA(U, const char *);

struct CopyData { U data, bytes; const void *pointer; };

void mainCRTStartup(void)
{
    U window = 0, mask = 0;
    char text[64];
    for (int i = 0; i < 200 && !window; ++i)
    {
        window = FindWindowA("Arm64Mixed", 0);
        if (!window) Sleep(20);
    }
    if (!window) ExitProcess(2);
    /* A message with numbers: the ARM64 procedure answers -3. */
    if (SendMessageA(window, 0x401, 0x1234, (U)-9) == (U)-3) mask |= 1;
    /* A message with a string: the text crosses into the other process. */
    SendMessageA(window, 0x000c, 0, (U)"set by x86");
    if (GetWindowTextA(window, text, sizeof text) == 10 && !lstrcmpA(text, "set by x86")) mask |= 2;
    /* WM_COPYDATA: a struct with a pointer, in the 32-bit layout of this side. */
    struct CopyData copy = {4711, 9, "x86 data"};
    if (SendMessageA(window, 0x004a, 0, (U)&copy) == 1) mask |= 4;
    /* The ARM64 procedure changes its title on request; this side reads it. */
    text[0] = 0;
    if (SendMessageA(window, 0x402, 0, 0) == 77 && GetWindowTextA(window, text, sizeof text) == 12 &&
        !lstrcmpA(text, "set by arm64")) mask |= 8;
    /* Properties of the ARM64 window: read its own, leave one. */
    if (GetPropA(window, "arm64 prop") == 0x66 && SetPropA(window, "x86 prop", 0x55)) mask |= 16;
    PostMessageA(window, 0x409, mask, 0);
    ExitProcess(0);
}
