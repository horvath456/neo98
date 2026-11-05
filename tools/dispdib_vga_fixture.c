/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Uses the shipped DISPDIB's WM_COPYDATA and DDM_BEGIN/END protocol. */
#include <windows.h>
static DWORD rows[128], used, written;
static struct { BITMAPINFOHEADER header; RGBQUAD colors[256]; } format;
static unsigned char pixels[320 * 400];
static HANDLE report;
static volatile unsigned char early_vga;
static void save(void) {
    SetFilePointer(report, 0, 0, FILE_BEGIN);
    WriteFile(report, rows, used * 4, &written, 0); FlushFileBuffers(report);
}
static void record(DWORD x) { rows[used++] = x; save(); }
static LONG WINAPI fault(EXCEPTION_POINTERS *p) {
    record(p->ExceptionRecord->ExceptionCode); record((DWORD)p->ExceptionRecord->ExceptionAddress);
    ExitProcess(1); return EXCEPTION_EXECUTE_HANDLER;
}
void mainCRTStartup(void) {
    report = CreateFileA("C:\\VGADIB.BIN", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (report == INVALID_HANDLE_VALUE) ExitProcess(98);
    SetUnhandledExceptionFilter(fault);
    STARTUPINFOA startup = {0}; PROCESS_INFORMATION process;
    startup.cb = sizeof startup;
    process.hProcess = process.hThread = (HANDLE)0xcccccccc;
    process.dwProcessId = process.dwThreadId = 0xcccccccc;
    record(0x62710008);
    /* Hellbender probes system-VM VGA before it loads DISPDIB. */
    early_vga = *(volatile unsigned char *)0xa0000;
    record(1);
    record(CreateProcessA("C:\\WINDOWS\\SYSTEM\\DISPDIB.DLL", 0, 0, 0, 0, 0, 0, 0, &startup, &process));
    record((DWORD)process.hProcess); record((DWORD)process.hThread);
    record(process.dwProcessId); record(process.dwThreadId);
    WNDCLASSA wc;
    record(GetClassInfoA(0, "DisplayDibWindow", &wc));
    HWND window = CreateWindowExA(0, "DisplayDibWindow", "VGA probe", WS_POPUP,
                                  0, 0, 320, 200, 0, 0, 0, 0);
    record(window != 0);
    if (!window) ExitProcess(2);
    format.header.biSize = sizeof(BITMAPINFOHEADER);
    format.header.biWidth = 320; format.header.biHeight = 200;
    format.header.biPlanes = 1; format.header.biBitCount = 8;
    format.header.biSizeImage = 320 * 200; format.header.biClrUsed = 256;
    for (unsigned i = 0; i < 256; ++i) {
        format.colors[i].rgbRed = i;
        format.colors[i].rgbGreen = 255 - i;
        format.colors[i].rgbBlue = (i & 1) ? 255 : 0;
    }
    for (unsigned y = 0; y < 200; ++y)
        for (unsigned x = 0; x < 320; ++x) pixels[y * 320 + x] = (x / 20 + y / 10 * 16) & 255;
    COPYDATASTRUCT copy = {0x400, sizeof format, &format};
    record(SendMessageA(window, WM_COPYDATA, 0, (LPARAM)&copy));
    record(SendMessageA(window, 0x403, 1, 0));
    copy.dwData = 0x401; copy.cbData = 320 * 200; copy.lpData = pixels;
    record(SendMessageA(window, WM_COPYDATA, 0, (LPARAM)&copy));
    Sleep(8000);
    record(SendMessageA(window, 0x404, 0, 0));
    DestroyWindow(window);
    CloseHandle(report); ExitProcess(0);
}
