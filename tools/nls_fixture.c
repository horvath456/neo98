/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 test of implemented Win98 NLS services and original codepage tables. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetACP(void);
API U GetOEMCP(void);
API U GetCPInfo(U, void *);
API U IsDBCSLeadByte(U);
API U IsDBCSLeadByteEx(U, U);
API U MultiByteToWideChar(U, U, const char *, int, unsigned short *, int);
API U WideCharToMultiByte(U, U, const unsigned short *, int, char *, int, const char *, U *);
API U GetLastError(void);
API void SetLastError(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void composite_test(void)
{
    const unsigned short registry[] = {'S', 'O', 'F', 'T', 'W', 'A', 'R',  'E', '\\', 'M', 'i', 'c',
                                       'r', 'o', 's', 'o', 'f', 't', '\\', 'O', 'L',  'E', 0};
    const char expected[] = "SOFTWARE\\Microsoft\\OLE";
    char bytes[46];
    U used = 55;
    SetLastError(98);
    check(WideCharToMultiByte(0, 0x200, registry, -1, bytes, sizeof bytes, 0, 0) == sizeof expected, 4300);
    for (U i = 0; i < sizeof expected; ++i)
        check(bytes[i] == expected[i], 4301);
    check(GetLastError() == 98, 4302);
    const unsigned short acute[] = {'e', 0x301, 0};
    check(WideCharToMultiByte(1252, 0x200, acute, -1, 0, 0, "!", &used) == 2 && !used, 4303);
    check(WideCharToMultiByte(1252, 0x200, acute, -1, bytes, sizeof bytes, "!", &used) == 2, 4304);
    check((unsigned char)bytes[0] == 0xe9 && !bytes[1] && !used, 4305);
    for (U i = 0; i < sizeof bytes; ++i)
        bytes[i] = 0x55;
    check(!WideCharToMultiByte(0, 0x200, acute, -1, bytes, 1, "!", &used) && GetLastError() == 122, 4306);
    check(bytes[0] == 'e' && bytes[1] == 0x55 && !used, 4307);
    const unsigned short unknown[] = {'x', 0x301, 0};
    check(WideCharToMultiByte(0, 0x210, unknown, -1, bytes, sizeof bytes, "!", &used) == 2, 4308);
    check(bytes[0] == 'x' && !bytes[1] && !used, 4309);
    check(WideCharToMultiByte(0, 0x240, unknown, -1, bytes, sizeof bytes, "!", &used) == 2, 4310);
    check(bytes[0] == '!' && !bytes[1] && used == 1, 4311);
    const unsigned short chain[] = {'e', 0x301, 0x308, 0};
    check(WideCharToMultiByte(0, 0x200, chain, -1, bytes, sizeof bytes, 0, &used) == 3, 4312);
    check((unsigned char)bytes[0] == 0xe9 && (unsigned char)bytes[1] == 0xa8 && !bytes[2] && !used, 4313);
    check(WideCharToMultiByte(0, 0x600, chain, -1, bytes, sizeof bytes, "!", &used) == 3, 4314);
    check((unsigned char)bytes[0] == 0xe9 && bytes[1] == '!' && !bytes[2] && used == 1, 4315);
    const unsigned short leading[] = {0x301, 0};
    check(WideCharToMultiByte(0, 0x210, leading, -1, bytes, sizeof bytes, 0, &used) == 1 && !bytes[0] &&
              !used,
          4316);
    check(!WideCharToMultiByte(65001, 0x200, acute, -1, bytes, sizeof bytes, 0, 0) && GetLastError() == 1004,
          4317);
    unsigned short wide[8];
    for (U i = 0; i < 8; ++i)
        wide[i] = 0x5555;
    SetLastError(98);
    check(MultiByteToWideChar(0, 2, "\xe9", -1, 0, 0) == 3 && GetLastError() == 98, 4320);
    check(MultiByteToWideChar(0, 2, "\xe9", -1, wide, 8) == 3, 4321);
    check(wide[0] == 'e' && wide[1] == 0x301 && !wide[2] && wide[3] == 0x5555, 4322);
    // Original Win98 expands only as far as capacity permits.
    check(MultiByteToWideChar(0, 2, "\xe9", 1, wide, 1) == 1 && wide[0] == 0xe9, 4323);
    check(!MultiByteToWideChar(0, 2, "\xe9", -1, wide, 2) && GetLastError() == 122, 4324);
    check(wide[0] == 'e' && wide[1] == 0x301, 4325);
    check(MultiByteToWideChar(437, 2, "\x82", -1, wide, 8) == 3 && wide[0] == 'e' && wide[1] == 0x301 &&
              !wide[2],
          4326);
    check(MultiByteToWideChar(850, 2, "\x82", -1, wide, 8) == 3 && wide[0] == 'e' && wide[1] == 0x301 &&
              !wide[2],
          4327);
    check(WideCharToMultiByte(850, 0x200, wide, -1, bytes, sizeof bytes, 0, 0) == 2 &&
              (unsigned char)bytes[0] == 0x82 && !bytes[1],
          4328);
}
void run_nls_test(void)
{
    unsigned short overlap[2] = {0x3f55, 0x5555};
    SetLastError(98);
    check(!MultiByteToWideChar(1252, 8, (const char *)overlap + 1, 1, overlap, 1) && GetLastError() == 1113 &&
              overlap[0] == 0x3f && overlap[1] == 0x5555,
          4329);
    composite_test();
    /* OEMCP comes from HKLM\\System\\CurrentControlSet\\Control\\Nls\\CodePage
       (original ). The ISO installation (US English) stores 437 there;
       the German reference system installation stored 850. */
    check(GetACP() == 1252 && GetOEMCP() == 437, 750);
    SetLastError(98);
    for (U i = 0; i < 512; ++i)
        check(!IsDBCSLeadByte(i) && !IsDBCSLeadByteEx(437, i) && GetLastError() == 98, 769);
    check(!IsDBCSLeadByteEx(932, 0x81) && GetLastError() == 87, 770);
    unsigned char info[24];
    for (U i = 0; i < 24; ++i)
        info[i] = 0xa5;
    check(GetCPInfo(0, info), 751);
    check(*(U *)info == 1 && info[4] == '?' && !info[5] && info[18] == 0xa5 && info[19] == 0xa5, 752);
    check(GetCPInfo(65001, info) && *(U *)info == 3, 753);
    check(!GetCPInfo(99999, info) && GetLastError() == 87, 754);
    unsigned short wide[32];
    SetLastError(98);
    check(MultiByteToWideChar(0, 0, "a\x80\xe9", -1, wide, 32) == 4, 755);
    check(wide[0] == 'a' && wide[1] == 0x20ac && wide[2] == 0xe9 && !wide[3] && GetLastError() == 98, 756);
    check(MultiByteToWideChar(1, 0, "\x82", 1, wide, 32) == 1 && wide[0] == 0xe9, 757);
    const unsigned short text[] = {'a', 0x20ac, 0x2603, 0};
    char bytes[32];
    U used = 55;
    check(WideCharToMultiByte(0, 0, text, -1, bytes, 32, "!", &used) == 4, 758);
    check(bytes[0] == 'a' && (unsigned char)bytes[1] == 0x80 && bytes[2] == '!' && !bytes[3] && used == 1,
          759);
    for (U i = 0; i < 32; ++i)
        bytes[i] = 0x55;
    used = 55;
    check(!WideCharToMultiByte(0, 0, text, 4, bytes, 2, "!", &used) && GetLastError() == 122, 760);
    check(bytes[0] == 'a' && (unsigned char)bytes[1] == 0x80 && bytes[2] == 0x55 && !used, 761);
    check(WideCharToMultiByte(65001, 0, text, 2, bytes, 32, 0, 0) == 4 && bytes[0] == 'a' &&
              (unsigned char)bytes[1] == 0xe2 && (unsigned char)bytes[2] == 0x82 &&
              (unsigned char)bytes[3] == 0xac,
          762);
    check(MultiByteToWideChar(65001, 0, bytes, 4, wide, 32) == 2 && wide[0] == 'a' && wide[1] == 0x20ac, 763);
    SetLastError(98);
    wide[0] = 0xabcd;
    check(!MultiByteToWideChar(65001, 0, "\xe2\x82", 2, wide, 32) && wide[0] == 0x82 && GetLastError() == 98,
          764);
    const unsigned short pair[] = {0xd83d, 0xdca9};
    check(WideCharToMultiByte(65001, 0, pair, 2, bytes, 32, 0, 0) == 6, 765);
    const unsigned char historical[] = {0xed, 0xa0, 0xbd, 0xed, 0xb2, 0xa9};
    for (U i = 0; i < 6; ++i)
        check((unsigned char)bytes[i] == historical[i], 766);
    check(!MultiByteToWideChar(0, 3, "x", 1, wide, 32) && GetLastError() == 1004, 767);
    check(!WideCharToMultiByte(65001, 0, text, 1, bytes, 32, 0, &used) && GetLastError() == 87, 768);
}
