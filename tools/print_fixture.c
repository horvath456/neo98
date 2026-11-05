// SPDX-License-Identifier: GPL-2.0-or-later
// Guest of tools/test_print_guest.py: one page with a line of text and a
// rectangle on the installed PostScript printer. The
// printer comes from tools/install_printer.py; the program names it the way
// COMDLG32 does, driver "winspool" and the device name, and GDI32 finds the
// driver in the [devices] line of WIN.INI. The exit code of a failed run is
// the number of the failed check.
typedef unsigned long DWORD;
typedef void *HDC;
typedef struct { int cbSize; const char *lpszDocName, *lpszOutput, *lpszDatatype; DWORD fwType; } DOCINFOA;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(unsigned);
API DWORD GetLastError(void);
API void OutputDebugStringA(const char *);
API DWORD GetProfileStringA(const char *, const char *, const char *, char *, DWORD);
API HDC CreateDCA(const char *, const char *, const char *, const void *);
API int DeleteDC(HDC);
API int GetDeviceCaps(HDC, int);
API int StartDocA(HDC, const DOCINFOA *);
API int StartPage(HDC);
API int EndPage(HDC);
API int EndDoc(HDC);
API int TextOutA(HDC, int, int, const char *, int);
API int Rectangle(HDC, int, int, int, int);
API int GetTextMetricsA(HDC, void *);
int _fltused;
void *memcpy(void *destination, const void *source, unsigned bytes)
{
    unsigned char *to = destination;
    const unsigned char *from = source;
    for (unsigned i = 0; i < bytes; ++i) to[i] = from[i];
    return destination;
}

static void check(int ok, unsigned code)
{
    if (!ok)
    {
        char text[64] = "PRINTFIX fail ";
        text[14] = (char)('0' + code / 10);
        text[15] = (char)('0' + code % 10);
        text[16] = 0;
        OutputDebugStringA(text);
        ExitProcess(code);
    }
}

void mainCRTStartup(void)
{
    char device[128];
    // The default printer the wizard wrote: [windows] device=.
    check(GetProfileStringA("windows", "device", "", device, sizeof device) > 0, 1);
    for (char *p = device; *p; ++p)
        if (*p == ',')
            *p = 0;
    HDC dc = CreateDCA("winspool", device, 0, 0);
    check(dc != 0, 2);
    // PSCRIPT describes the page of the LaserWriter Plus at 300 dots per inch.
    check(GetDeviceCaps(dc, 2) == 2, 3);    // TECHNOLOGY: DT_RASPRINTER
    check(GetDeviceCaps(dc, 88) == 300, 4); // LOGPIXELSX
    check(GetDeviceCaps(dc, 90) == 300, 5); // LOGPIXELSY
    check(GetDeviceCaps(dc, 8) > 2000, 6);  // HORZRES
    check(GetDeviceCaps(dc, 10) > 3000, 7); // VERTRES
    DOCINFOA info = {sizeof info, "NEO98 print test", 0, 0, 0};
    check(StartDocA(dc, &info) > 0, 8);
    check(StartPage(dc) > 0, 9);
    unsigned metrics[14] = {0};
    check(GetTextMetricsA(dc, metrics) && metrics[0] > 0, 10);
    check(TextOutA(dc, 300, 300, "Hello from NEO98", 16), 11);
    check(Rectangle(dc, 300, 600, 1500, 1200), 12);
    check(EndPage(dc) > 0, 13);
    check(EndDoc(dc) > 0, 14);
    check(DeleteDC(dc), 15);
    OutputDebugStringA("PRINTFIX done");
    ExitProcess(0);
}
