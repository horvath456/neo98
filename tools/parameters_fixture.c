/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U SystemParametersInfoA(U, U, void *, U);
API int GetSystemMetrics(int);
API U CreateFontIndirectA(const void *);
API U GetObjectA(U, U, void *);
API U DeleteObject(U);
API void SetLastError(U);
API U GetLastError(void);
API U RegOpenKeyA(U, const char *, U *);
API U RegQueryValueExA(U, const char *, U *, U *, unsigned char *, U *);
API U RegCloseKey(U);
API void ExitProcess(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void fill(unsigned char *out, U count)
{
    while (count--)
        *out++ = 0xcd;
}
static U value(const unsigned char *p) { return p[0] | (U)p[1] << 8 | (U)p[2] << 16 | (U)p[3] << 24; }
/* A decimal REG_SZ under HKCU\Control Panel\Desktop, 0 when it is missing. */
static U desktop_number(const char *name)
{
    U key = 0, type = 0, size = 16, result = 0;
    unsigned char text[16];
    if (RegOpenKeyA(0x80000001u, "Control Panel\\Desktop", &key))
        return 0;
    if (!RegQueryValueExA(key, name, 0, &type, text, &size) && type == 1)
        for (U i = 0; i < size && text[i] >= '0' && text[i] <= '9'; ++i)
            result = result * 10 + (text[i] - '0');
    RegCloseKey(key);
    return result;
}
void run_parameters_test(void)
{
    unsigned char data[360], copy[60];
    const U actions[] = {1, 5, 10, 13, 14, 16, 18, 22, 24, 25, 27, 35, 38};
    U expected[] = {1, 1, 31, 75, 0, 0, 1, 1, 75, 1, 0, 1, 0};
    /* USER reads the screen saver's timeout and switch at start from
       HKCU\Control Panel\Desktop into DS:DAh, negative while inactive
       (src/x86_user.cpp LoadUserStartSettings). The registry of every run is
       the one imported from the installed USER.DAT, volume or not; Win98 SE
       setup leaves ScreenSaveTimeOut=840 there and no ScreenSaveActive, so
       SPI_GETSCREENSAVETIMEOUT answers 840 and SPI_GETSCREENSAVEACTIVE 0. */
    expected[4] = desktop_number("ScreenSaveTimeOut");
    expected[5] = expected[4] && desktop_number("ScreenSaveActive");
    for (U i = 0; i < sizeof actions / sizeof *actions; ++i)
    {
        fill(data, sizeof data);
        SetLastError(9898);
        // SPI_GETDRAGFULLWINDOWS answers only with the Plus module USER loads
        // at start; this RAM volume has no COOL.DLL.
        check(SystemParametersInfoA(actions[i], 0, data + 4, 0) == (actions[i] != 38), 1501);
        check(value(data) == 0xcdcdcdcd && value(data + 4) == expected[i] && value(data + 8) == 0xcdcdcdcd,
              1502);
        check(GetLastError() == 9898, 1503);
    }
    /* without the Plus module USER writes a zero WORD for
       SPI_GETFONTSMOOTHING and answers zero for both font smoothing actions.
       tools/user_system_state_fixture.c covers the installation with it. */
    fill(data, sizeof data);
    check(SystemParametersInfoA(0x4a, 0, data + 4, 0) == 0, 1540);
    check(value(data + 4) == 0 && value(data + 8) == 0xcdcdcdcd, 1541);
    check(SystemParametersInfoA(0x4b, 1, 0, 0) == 0, 1542);
    fill(data, sizeof data);
    check(SystemParametersInfoA(3, 0, data + 4, 0), 1504);
    check(value(data + 4) == 6 && value(data + 8) == 10 && value(data + 12) == 1 &&
              value(data + 16) == 0xcdcdcdcd,
          1505);
    check(SystemParametersInfoA(48, 0, data + 4, 0), 1506);
    check(value(data + 4) == 0 && value(data + 8) == 0 && value(data + 12) == 800 && value(data + 16) == 600,
          1507);
    fill(data, sizeof data);
    check(SystemParametersInfoA(31, 60, data + 4, 0), 1508);
    check(value(data + 4) == (U)-11 && value(data + 20) == 400 && value(data + 64) == 0xcdcdcdcd, 1509);
    const char face[] = "MS Sans Serif";
    for (U i = 0; i < sizeof face; ++i)
        check(data[32 + i] == face[i], 1510);
    U font = CreateFontIndirectA(data + 4);
    check(font && GetObjectA(font, 60, copy) == 60, 1511);
    for (U i = 0; i < 60; ++i)
        check(copy[i] == data[4 + i], 1512);
    check(DeleteObject(font), 1513);
    const U metrics[] = {41, 43, 45}, sizes[] = {340, 20, 76};
    for (U i = 0; i < 3; ++i)
    {
        fill(data, sizeof data);
        *(U *)(data + 4) = sizes[i];
        // The observed Win98 internal read branches validate cbSize, ignore uiParam.
        check(SystemParametersInfoA(metrics[i], 0, data + 4, 0), 1514);
        check(value(data) == 0xcdcdcdcd && value(data + 4) == sizes[i] &&
                  value(data + 4 + sizes[i]) == 0xcdcdcdcd,
              1515);
        if (i == 0)
            check(value(data + 8) == 1 && value(data + 12) == 17 && value(data + 28) == (U)-11 &&
                      value(data + 44) == 700,
                  1516);
        if (i == 1)
            check(value(data + 8) == 154 && value(data + 12) == 0 && value(data + 20) == 8, 1517);
        if (i == 2)
            check(value(data + 8) == 75 && value(data + 12) == 75 && value(data + 20) == (U)-11, 1518);
        fill(data, sizeof data);
        *(U *)(data + 4) = sizes[i] + 4;
        check(!SystemParametersInfoA(metrics[i], sizes[i], data + 4, 0), 1519);
        check(value(data + 8) == 0xcdcdcdcd, 1520);
        check(!SystemParametersInfoA(metrics[i], sizes[i], 0, 0), 1521);
    }
    fill(data, sizeof data);
    *(U *)data = 340;
    check(SystemParametersInfoA(41, 0, data, 0), 1530);
    check(GetSystemMetrics(4) == (int)value(data + 20) + GetSystemMetrics(6), 1531);
    check(GetSystemMetrics(51) == (int)value(data + 88) + GetSystemMetrics(6), 1532);
    check(GetSystemMetrics(15) == (int)value(data + 156) + GetSystemMetrics(6), 1533);
    check(GetSystemMetrics(32) == (int)(value(data + 4) + 1) * GetSystemMetrics(5) + GetSystemMetrics(45),
          1534);
    check(GetSystemMetrics(28) == 112 && GetSystemMetrics(29) == 27, 1535);
    check(GetSystemMetrics(51) == 17 && GetSystemMetrics(38) == 75 && GetSystemMetrics(36) == 4, 1536);
    check(GetSystemMetrics(17) == 600 - GetSystemMetrics(4) && GetSystemMetrics(61) == 808, 1537);
    SetLastError(9898);
    check(!GetSystemMetrics(-1) && !GetSystemMetrics(82) && !GetSystemMetrics(0x7fffffff), 1538);
    check(GetLastError() == 9898, 1539);
    fill(data, sizeof data);
    check(!SystemParametersInfoA(31, 59, data, 0) && GetLastError() == 87, 1522);
    check(value(data) == 0xcdcdcdcd, 1523);
    check(!SystemParametersInfoA(31, 60, 0, 0), 1524);
    check(!SystemParametersInfoA(41, 340, data, 4) && GetLastError() == 87, 1525);
    check(!SystemParametersInfoA(0xffff, 0, data, 0) && GetLastError() == 120, 1526);
    check(value(data) == 0xcdcdcdcd, 1527);
}
