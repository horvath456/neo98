// SPDX-License-Identifier: GPL-2.0-or-later
// Program the PC speaker through control port 43h, divisor port 42h and
// gate/speaker bits of port 61h. Then call MessageBeep(0xFFFFFFFF) and verify
// the same audio path. Failure returns the check identifier.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
API void S ExitProcess(U);
API void S Sleep(U);
API U S MessageBeep(U);

static void out8(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static unsigned char in8(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}

// 1,193,182 Hz divided by 1193 is 1000.2 Hz.
#define DIVISOR 1193

void mainCRTStartup(void)
{
    // Set the counter divisor before opening the gate. There must be no sound
    // while the gate is closed.
    out8(0x43, 0xb6);
    out8(0x42, DIVISOR & 0xff);
    out8(0x42, DIVISOR >> 8);
    Sleep(100);
    // Read-modify-write the gate and speaker bits, then verify both remain
    // set.
    unsigned char before = in8(0x61);
    out8(0x61, (unsigned char)(before | 3));
    check((in8(0x61) & 3) == 3, 28901);
    Sleep(300);
    out8(0x61, (unsigned char)(in8(0x61) & 0xfc));
    check((in8(0x61) & 3) == 0, 28902);
    Sleep(200);
    // The same speaker through the API.
    check(MessageBeep(0xffffffffu) != 0, 28903);
    Sleep(300);
    ExitProcess(0);
}
