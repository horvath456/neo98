// SPDX-License-Identifier: GPL-2.0-or-later
// Actual Win32 calls around the mixed PE page exception.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API char *GetCommandLineA(void);
API U VirtualProtect(U, U, U, U *);
API U VirtualQuery(U, void *, U);
API U VirtualAlloc(U, U, U, U);
__declspec(allocate(".rdata$aaa")) const unsigned char padding[0x2800] = {1};
unsigned char data[0x8000] = {1};
static void check(U value, U code) { if (!value) ExitProcess(code); }
static void query(U address, U expected)
{
    U info[7];
    check(VirtualQuery(address, info, sizeof info) == sizeof info, 6021);
    check(info[5] == expected, 6022);
}
void mainCRTStartup(void)
{
    U base = GetModuleHandleA(0), nt = *(U *)(base + 60);
    U count = *(unsigned short *)(base + nt + 6);
    U table = base + nt + 24 + *(unsigned short *)(base + nt + 20);
    U rw = 0, ro = 0, text = 0;
    for (U i = 0; i < count; ++i)
    {
        U s = table + i * 40, name = *(U *)s, address = base + *(U *)(s + 12);
        if (name == 0x7461642e) rw = address; //.data
        if (name == 0x6164722e) ro = address + ((*(U *)(s + 8) + 4095) & ~4095u) - 4096;
        if (name == 0x7865742e) text = address;
    }
    check(rw && ro && text && (rw & ~16383u) == (ro & ~16383u), 6023);
    query(ro, 2); query(rw, 4); query(text, 2);
    for (U i = 0; i < 256; ++i) *(volatile U *)(rw + 16) = i;
    U mode = (U)(*GetCommandLineA() - '0'), old = 0;
    if (mode == 0)
    {
        /* Win98 protects .rdata initially; grant access before writing it. */
        check(VirtualProtect(ro, 4096, 4, &old) && old == 2, 6025);
        query(ro, 4); query(rw, 4);
        *(volatile U *)(ro + 16) = 0x12345678;
        check(*(volatile U *)(ro + 16) == 0x12345678, 6024);
        check(VirtualProtect(ro, 4096, 2, &old) && old == 4, 6025);
        for (U i = 0; i < 256; ++i) *(volatile U *)(rw + 16) = i;
        query(ro, 2); query(rw, 4);
        ExitProcess(0);
    }
    if (mode == 1) *(volatile U *)(text + 16) = 1;
    if (mode == 2)
    {
        check(VirtualProtect(ro, 4096, 2, &old) && old == 2, 6026);
        *(volatile U *)(ro + 16) = 1;
    }
    if (mode == 3)
    {
        check(VirtualProtect(rw, 4096, 2, &old) && old == 4, 6027);
        *(volatile U *)(rw + 16) = 1;
    }
    if (mode == 4)
    {
        check(VirtualProtect(rw, 4096, 1, &old) && old == 4, 6028);
        query(rw, 1);
        *(volatile U *)(rw + 16) = 1;
    }
    if (mode == 5)
    {
        U allocation = VirtualAlloc(0, 16384, 0x1000, 4);
        check(allocation != 0, 6029);
        check(VirtualProtect(allocation, 4096, 2, &old) && old == 4, 6030);
        *(volatile U *)(allocation + 4096) = 1;
        *(volatile U *)allocation = 1;
    }
    if (mode == 6) VirtualQuery(rw, (void *)(ro + 16), 28);
    if (mode == 7) VirtualProtect(text, 4096, 2, (U *)(ro + 16));
    ExitProcess(6031); // every negative case must raise an access violation
}
