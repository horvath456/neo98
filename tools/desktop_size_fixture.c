/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An x86 program that writes the size of the screen and whether the display
   driver lists 720 by 1560 and 1560 by 720 into DESKSIZE.TXT beside itself.
   tools/test_desktop_size_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API int GetSystemMetrics(int);
API int EnumDisplaySettingsA(const char *, U, void *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

void mainCRTStartup(void)
{
    /* DEVMODEA: dmSize at 36, dmBitsPerPel at 104, dmPelsWidth at 108, dmPelsHeight at 112. */
    static unsigned char mode[156];
    char text[128];
    U upright = 0, lying = 0, written = 0, file;
    for (U i = 0; i < 200; ++i)
    {
        for (U k = 0; k < sizeof mode; ++k) mode[k] = 0;
        *(unsigned short *)(mode + 36) = 148;
        if (!EnumDisplaySettingsA(0, i, mode)) break;
        const U width = *(U *)(mode + 108), height = *(U *)(mode + 112);
        if (width == 720 && height == 1560) upright = 1;
        if (width == 1560 && height == 720) lying = 1;
    }
    wsprintfA(text, "metrics=%dx%d\r\nupright=%u\r\nlying=%u\r\n", GetSystemMetrics(0), GetSystemMetrics(1), upright, lying);
    file = CreateFileA("C:\\DESKSIZE.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
