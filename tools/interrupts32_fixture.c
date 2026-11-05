// SPDX-License-Identifier: GPL-2.0-or-later
// Software interrupts from 32-bit guest code. The instructions are assembled
// as bytes into an executable block, because the Win98 NTDLL reaches the
// kernel exactly this way.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetProcessHeap(void);
API U HeapAlloc(U, U, U);
API U HeapFree(U, U, U);
API void ExitProcess(U);
static void check(U v, U code)
{
    if (!v)
        ExitProcess(code);
}
static U dword(const unsigned char *p, U at)
{
    return p[at] | (U)p[at + 1] << 8 | (U)p[at + 2] << 16 | (U)p[at + 3] << 24;
}
static void put(unsigned char *p, U at, U v)
{
    p[at] = (unsigned char)v;
    p[at + 1] = (unsigned char)(v >> 8);
    p[at + 2] = (unsigned char)(v >> 16);
    p[at + 3] = (unsigned char)(v >> 24);
}
typedef U(__stdcall *Entry)(void);
// Issue an NT service interrupt with its service number and argument pointer,
// then return to the caller.
static Entry build_service(U block, U service)
{
    unsigned char *p = (unsigned char *)block;
    p[0] = 0xb8;
    p[1] = (unsigned char)service;
    p[2] = (unsigned char)(service >> 8);
    p[3] = (unsigned char)(service >> 16);
    p[4] = (unsigned char)(service >> 24);
    p[5] = 0x8d; p[6] = 0x54; p[7] = 0x24; p[8] = 0x04; // lea edx,[esp+4]
    p[9] = 0xcd; p[10] = 0x2e;                          // int 2Eh
    p[11] = 0xc3;                                       // ret
    return (Entry)block;
}
// The stub of NtPowerInformation with its five arguments and the `ret 14h`
// of the NTDLL stub.
typedef U(__stdcall *Power)(U level, U input, U input_bytes, U output, U output_bytes);
static Power build_power(U block)
{
    build_service(block, 0x108);
    unsigned char *p = (unsigned char *)block;
    p[11] = 0xc2; p[12] = 0x14; p[13] = 0x00;           // ret 14h
    return (Power)block;
}
// Query the DOS version; the Win16 path reports 0A07h.
static Entry build_dos(U block)
{
    unsigned char *p = (unsigned char *)block;
    p[0] = 0xb8; p[1] = 0x00; p[2] = 0x30; p[3] = 0x00; p[4] = 0x00;
    p[5] = 0xcd; p[6] = 0x21;
    p[7] = 0x0f; p[8] = 0xb7; p[9] = 0xc0;
    p[10] = 0xc3;
    return (Entry)block;
}
void mainCRTStartup(void)
{
    U heap = GetProcessHeap();
    check(heap, 17700);
    U block = HeapAlloc(heap, 8, 64);
    check(block, 17701);
    // Every service number the NTDLL of the tree uses answers
    // STATUS_NOT_IMPLEMENTED instead of ending the run.
    static const U services[] = {0x107, 0x109, 0x10b, 0x194, 0x195, 0x196};
    for (U i = 0; i < 6; ++i)
        check(build_service(block, services[i])() == 0xc0000002u, 17710 + i);
    // NtPowerInformation: level 4, SystemPowerCapabilities, answers what
    // NTKERN answers on a machine without APM or ACPI: 4Ch bytes, all zero
    // but FullWake (byte 9). A short output buffer is STATUS_BUFFER_TOO_SMALL,
    // an input of another length STATUS_INVALID_PARAMETER.
    U record = HeapAlloc(heap, 0, 0x4c);
    check(record, 17702);
    unsigned char *bytes = (unsigned char *)record;
    for (U i = 0; i < 0x4c; ++i)
        bytes[i] = 0x5a;
    check(build_power(block)(4, 0, 0, record, 0x4c) == 0, 17730);
    for (U i = 0; i < 0x4c; ++i)
        check(bytes[i] == (i == 9 ? 1 : 0), 17731);
    check(build_power(block)(4, 0, 0, record, 0x4b) == 0xc0000023u, 17732);
    check(build_power(block)(4, record, 8, record, 0x4c) == 0xc000000du, 17733);
    check(build_power(block)(4, 0, 0, 0, 0) == 0, 17734);
    check(HeapFree(heap, 0, record), 17736);
    // Levels 0 to 3, 8 and 9: the policies NTKERN keeps. On this
    // machine without sleep states the default policy folds every sleep
    // action to none and every sleep state to working.
    U policy = HeapAlloc(heap, 0, 0xe8), other = HeapAlloc(heap, 0, 0xe8);
    check(policy && other, 17737);
    unsigned char *p = (unsigned char *)policy, *q = (unsigned char *)other;
    Power power = build_power(block);
    check(power(0, 0, 0, policy, 0xe8) == 0, 17740);
    check(dword(p, 0) == 1 && dword(p, 4) == 4 && dword(p, 0x10) == 0 && dword(p, 0x28) == 1, 17741);
    check(dword(p, 0x44) == 1 && dword(p, 0x48) == 1 && dword(p, 0x4c) == 1, 17742);
    check(dword(p, 0x5c) == 0x19 && p[0xd8] == 0 && p[0xd9] == 100 && p[0xda] == 100 && p[0xdb] == 20, 17743);
    check(power(0, 0, 0, policy, 0xe7) == 0xc0000023u, 17744);
    check(power(1, 0, 0, other, 0xe8) == 0 && dword(q, 0x44) == 1, 17745);
    // A policy of POWERCFG's kind: sleep after 30 minutes, screen off after
    // 15. Verify answers the checked copy and keeps nothing.
    put(p, 0x30, 2);
    put(p, 0x3c, 1800);
    put(p, 0xc0, 900);
    put(p, 0xd4, 30);
    check(power(2, policy, 0xe8, other, 0xe8) == 0, 17746);
    check(dword(q, 0x30) == 0 && dword(q, 0x3c) == 0 && dword(q, 0xc0) == 900, 17747);
    check(power(8, 0, 0, other, 0xe8) == 0 && dword(q, 0xc0) == 0, 17748);
    // Level 0 with an input takes it.
    check(power(0, policy, 0xe8, other, 0xe8) == 0 && dword(q, 0xc0) == 900 && dword(q, 0xd4) == 30, 17749);
    check(power(8, 0, 0, other, 0xe8) == 0 && dword(q, 0xc0) == 900, 17750);
    check(power(8, policy, 0xe8, other, 0xe8) == 0xc000000du, 17751);
    check(power(0, policy, 0x40, 0, 0) == 0xc0000023u, 17752);
    check(power(0, policy, 0xf0, 0, 0) == 0x80000005u, 17753);
    put(p, 0, 2);
    check(power(3, policy, 0xe8, other, 0xe8) == 0xc000000du, 17754);
    check(power(3, 0, 0, other, 0xe8) == 0xc000000du, 17755);
    // The administrator limits of a fresh NTKERN.
    check(power(9, 0, 0, other, 0x18) == 0, 17756);
    check(dword(q, 0) == 2 && dword(q, 4) == 5 && dword(q, 0xc) == 0xffffffffu, 17757);
    check(power(5, 0, 0, other, 0x20) == 0xc0000002u, 17735);
    check(HeapFree(heap, 0, policy) && HeapFree(heap, 0, other), 17758);
    // The DOS gate answers a 32-bit int 21h the way it answers a 16-bit one.
    check(build_dos(block)() == 0x0a07, 17720);
    check(HeapFree(heap, 0, block), 17721);
    ExitProcess(0);
}
