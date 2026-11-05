/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Stage 2 of the SDK for ARM64 programs: what a guest application needs beyond the
   first stage. One check per function family; prints one line per failed
   check and a summary, the exit code is the number of failures.
   tools/test_arm64_stage2_native.py, docs/ARM64-APPS.md. */
#include <windows.h>

static int checks, failures;
static HANDLE output;

static void Print(const char *text)
{
    DWORD written;
    WriteFile(output, text, (DWORD)lstrlenA(text), &written, NULL);
}
static void Check(int id, int condition)
{
    char line[64];
    ++checks;
    if (condition) return;
    ++failures;
    wsprintfA(line, "FAIL %d\r\n", id);
    Print(line);
}

static int window_done_calls;
static WAVEHDR *window_done_header;
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_APP + 1) return (LRESULT)(wParam + 0x70000000u);
    if (message == MM_WOM_DONE)
    {
        ++window_done_calls;
        window_done_header = (WAVEHDR *)lParam;
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static void Objects(void)
{
    HDC screen = GetDC(NULL), memory = CreateCompatibleDC(screen);
    BITMAP bitmap;
    DIBSECTION section;
    LOGBRUSH brush;
    LOGPEN pen;
    LOGFONTA font;
    BITMAPINFO info;
    void *bits = NULL;
    HBITMAP plain, dib;
    HBRUSH solid;
    HPEN line;
    HFONT face;
    unsigned char guard[sizeof(DIBSECTION) + 16];
    int result;

    plain = CreateCompatibleBitmap(screen, 37, 11);
    memset(&bitmap, 0x55, sizeof bitmap);
    result = GetObjectA(plain, sizeof bitmap, &bitmap);
    Check(100, result == (int)sizeof bitmap);
    Check(101, bitmap.bmWidth == 37 && bitmap.bmHeight == 11 && bitmap.bmPlanes == 1 && bitmap.bmBits == NULL);
    /* Windows 98 keeps a compatible bitmap as a DIB and names the size of a
       DIBSECTION here; Windows NT names the BITMAP. */
    result = GetObjectA(plain, 0, NULL);
    Check(102, result == (int)sizeof bitmap || result == (int)sizeof(DIBSECTION));
    /* Whichever struct the system fills where a DIBSECTION has room, it
       names its size in the layout of the program and leaves the rest alone. */
    memset(guard, 0x77, sizeof guard);
    result = GetObjectA(plain, sizeof(DIBSECTION), guard);
    Check(103, (result == (int)sizeof bitmap || result == (int)sizeof(DIBSECTION)) && guard[result] == 0x77 &&
                   ((BITMAP *)guard)->bmWidth == 37 && ((BITMAP *)guard)->bmBitsPixel != 0x7777);
    Check(104, GetObjectType(plain) == OBJ_BITMAP);

    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = 20;
    info.bmiHeader.biHeight = -6;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    Check(110, dib != NULL && bits != NULL);
    memset(&section, 0x55, sizeof section);
    result = GetObjectA(dib, sizeof section, &section);
    Check(111, result == (int)sizeof section);
    Check(112, section.dsBm.bmWidth == 20 && section.dsBm.bmHeight == 6 && section.dsBm.bmBitsPixel == 32);
    Check(113, section.dsBm.bmBits == bits);
    Check(114, section.dsBmih.biWidth == 20 && section.dsBmih.biBitCount == 32 && section.dshSection == NULL);
    memset(&bitmap, 0x55, sizeof bitmap);
    result = GetObjectA(dib, sizeof bitmap, &bitmap);
    Check(115, result == (int)sizeof bitmap && bitmap.bmBits == bits && bitmap.bmWidthBytes == 80);
    /* The program writes pixels, GetDIBits of another format reads them. */
    if (bits)
    {
        unsigned *pixel = (unsigned *)bits;
        unsigned row[120];
        BITMAPINFO query;
        HGDIOBJ old;
        pixel[0] = 0x00112233;
        pixel[19] = 0x00445566;
        memset(&query, 0, sizeof query);
        query.bmiHeader.biSize = sizeof query.bmiHeader;
        query.bmiHeader.biWidth = 20;
        query.bmiHeader.biHeight = -6;
        query.bmiHeader.biPlanes = 1;
        query.bmiHeader.biBitCount = 32;
        Check(116, GetDIBits(screen, dib, 0, 6, row, &query, DIB_RGB_COLORS) == 6);
        Check(117, (row[0] & 0xffffff) == 0x112233 && (row[19] & 0xffffff) == 0x445566);
        old = SelectObject(memory, dib);
        Check(118, SetPixelV(memory, 3, 2, RGB(1, 2, 3)) && (pixel[2 * 20 + 3] & 0xffffff) == 0x010203);
        Check(119, GetCurrentObject(memory, OBJ_BITMAP) == (HGDIOBJ)dib);
        SelectObject(memory, old);
    }

    solid = CreateSolidBrush(RGB(10, 20, 30));
    memset(&brush, 0x55, sizeof brush);
    Check(120, GetObjectA(solid, sizeof brush, &brush) == (int)sizeof brush);
    Check(121, brush.lbStyle == BS_SOLID && brush.lbColor == RGB(10, 20, 30));
    line = CreatePen(PS_DOT, 1, RGB(9, 8, 7));
    Check(122, GetObjectA(line, sizeof pen, &pen) == (int)sizeof pen && pen.lopnStyle == PS_DOT &&
                   pen.lopnColor == RGB(9, 8, 7));
    face = CreateFontA(-13, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "MS Sans Serif");
    Check(123, GetObjectA(face, sizeof font, &font) == (int)sizeof font && font.lfHeight == -13 &&
                   font.lfWeight == FW_BOLD && !lstrcmpA(font.lfFaceName, "MS Sans Serif"));
    {
        char name[64];
        HGDIOBJ old = SelectObject(memory, face);
        Check(124, GetTextFaceA(memory, sizeof name, name) > 0 && name[0]);
        SelectObject(memory, old);
    }
    Check(125, GetObjectA(NULL, sizeof bitmap, &bitmap) == 0);

    {
        POINT origin = {-1, -1};
        Check(130, SetViewportOrgEx(memory, 5, 7, &origin) && origin.x == 0 && origin.y == 0);
        Check(131, SetViewportOrgEx(memory, 0, 0, &origin) && origin.x == 5 && origin.y == 7);
        Check(132, SetStretchBltMode(memory, COLORONCOLOR) != 0 && GetStretchBltMode(memory) == COLORONCOLOR);
    }
    DeleteObject(face);
    DeleteObject(line);
    DeleteObject(solid);
    DeleteObject(dib);
    DeleteObject(plain);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
}

static void Regions(HWND window)
{
    HRGN a = CreateRectRgn(0, 0, 10, 10), b = CreateRectRgn(5, 5, 20, 20), c = CreateRectRgn(0, 0, 0, 0);
    RECT box;
    Check(200, CombineRgn(c, a, b, RGN_AND) == SIMPLEREGION);
    Check(201, GetRgnBox(c, &box) == SIMPLEREGION && box.left == 5 && box.top == 5 && box.right == 10 && box.bottom == 10);
    Check(202, CombineRgn(c, a, b, RGN_OR) == COMPLEXREGION);
    Check(203, PtInRegion(c, 15, 15) && !PtInRegion(c, 15, 2));
    Check(204, CombineRgn(c, a, b, RGN_DIFF) == COMPLEXREGION && OffsetRgn(c, 100, 0) != ERROR);
    Check(205, GetRgnBox(c, &box) != ERROR && box.left == 100);
    /* The system owns the region after SetWindowRgn. */
    Check(206, SetWindowRgn(window, CreateRectRgn(0, 0, 50, 40), FALSE) != 0);
    Check(207, GetWindowRgn(window, c) != ERROR && GetRgnBox(c, &box) && box.right == 50 && box.bottom == 40);
    SetWindowRgn(window, NULL, FALSE);
    DeleteObject(a);
    DeleteObject(b);
    DeleteObject(c);
}

static void Windows(HWND window)
{
    static int marker;
    POINT point = {-1, -1};
    DWORD_PTR answer = 0;
    HMENU menu;
    RECT rect = {0, 0, 16, 16};
    HDC dc;
    char oem[8];
    int index = COLOR_BTNFACE;
    COLORREF before, changed = RGB(1, 2, 3);

    Check(300, SetPropA(window, "Stage2.Pointer", (HANDLE)&marker));
    Check(301, GetPropA(window, "Stage2.Pointer") == (HANDLE)&marker);
    Check(302, GetPropA(window, "Stage2.Missing") == NULL);
    Check(303, RemovePropA(window, "Stage2.Pointer") == (HANDLE)&marker && GetPropA(window, "Stage2.Pointer") == NULL);

    Check(310, CreateCaret(window, NULL, 2, 12));
    Check(311, SetCaretPos(7, 9) && GetCaretPos(&point) && point.x == 7 && point.y == 9);
    Check(312, ShowCaret(window) && HideCaret(window));
    Check(313, DestroyCaret());

    Check(320, SendMessageTimeoutA(window, WM_APP + 1, 5, 0, SMTO_NORMAL, 1000, &answer) != 0);
    Check(321, answer == 0x70000005u);
    Check(322, GetLastActivePopup(window) == window);
    Check(323, GetScrollPos(window, SB_VERT) == 0);

    menu = CreateMenu();
    AppendMenuA(menu, MF_STRING, 41, "One");
    AppendMenuA(menu, MF_STRING, 42, "Two");
    Check(330, DeleteMenu(menu, 41, MF_BYCOMMAND) && GetMenuItemCount(menu) == 1);
    DestroyMenu(menu);

    dc = GetDC(window);
    Check(340, DrawFrameControl(dc, &rect, DFC_BUTTON, DFCS_BUTTONPUSH));
    Check(341, DrawIconEx(dc, 0, 0, LoadIconA(NULL, IDI_APPLICATION), 32, 32, 0, NULL, DI_NORMAL));
    ReleaseDC(window, dc);

    Check(350, (VkKeyScanA('a') & 0xff) == 'A' && (VkKeyScanA('A') & 0x100));
    Check(351, MapVirtualKeyA(VK_RETURN, 0) == 0x1c && MapVirtualKeyA(0x1c, 1) == VK_RETURN);
    Check(352, CharToOemA("A\xe4", oem) && oem[0] == 'A' && (unsigned char)oem[1] == 0x84);

    before = GetSysColor(COLOR_BTNFACE);
    Check(360, SetSysColors(1, &index, &changed) && GetSysColor(COLOR_BTNFACE) == changed);
    SetSysColors(1, &index, &before);
    Check(361, GetSysColor(COLOR_BTNFACE) == before);

    keybd_event(VK_SHIFT, 0x2a, 0, 0);
    Check(370, (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    keybd_event(VK_SHIFT, 0x2a, KEYEVENTF_KEYUP, 0);
    Check(371, (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0);
}

static void Kernel(void)
{
    DWORD sectors = 0, bytes = 0, free_clusters = 0, clusters = 0;
    SYSTEM_POWER_STATUS power;
    SYSTEMTIME time = {1998, 6, 4, 25, 12, 30, 15, 0}, back;
    FILETIME local, utc, again;

    Check(400, (GetLogicalDrives() & 4) != 0);
    Check(401, GetDiskFreeSpaceA("C:\\", &sectors, &bytes, &free_clusters, &clusters) && sectors && bytes && clusters);
    Check(402, GetDriveTypeA("C:\\") == DRIVE_FIXED);
    memset(&power, 0x55, sizeof power);
    Check(403, GetSystemPowerStatus(&power) && power.ACLineStatus != 0x55);
    Check(410, SystemTimeToFileTime(&time, &local) && LocalFileTimeToFileTime(&local, &utc));
    Check(411, FileTimeToLocalFileTime(&utc, &again) && !CompareFileTime(&local, &again));
    Check(412, FileTimeToSystemTime(&local, &back) && back.wYear == 1998 && back.wMonth == 6 && back.wDay == 25 &&
                   back.wHour == 12 && back.wMinute == 30 && back.wSecond == 15 && back.wDayOfWeek == 4);
    GetSystemTimeAsFileTime(&utc);
    Check(413, utc.dwHighDateTime > 0x01b00000u);
}


/* --- WINMM ---------------------------------------------------------------- */
static int wave_open_calls, wave_done_calls, wave_close_calls;
static WAVEHDR *wave_done_header;
static DWORD_PTR wave_instance_seen;
static void CALLBACK WaveProc(HWAVEOUT wave, UINT message, DWORD_PTR instance, DWORD_PTR first, DWORD_PTR second)
{
    (void)wave; (void)second;
    wave_instance_seen = instance;
    if (message == WOM_OPEN) ++wave_open_calls;
    if (message == WOM_CLOSE) ++wave_close_calls;
    if (message == WOM_DONE) { ++wave_done_calls; wave_done_header = (WAVEHDR *)first; }
}
static int timer_calls;
static DWORD_PTR timer_user;
static void CALLBACK TimeProc(UINT id, UINT message, DWORD_PTR user, DWORD_PTR first, DWORD_PTR second)
{
    (void)id; (void)message; (void)first; (void)second;
    ++timer_calls;
    timer_user = user;
}
static void Pump(HWND window, DWORD milliseconds, const int *until)
{
    const DWORD start = GetTickCount();
    MSG message;
    (void)window;
    while (GetTickCount() - start < milliseconds && !(until && *until))
    {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
        Sleep(5);
    }
}
static void Wave(HWND window)
{
    static short samples[800];
    static int marker;
    WAVEFORMATEX format;
    WAVEHDR header;
    HWAVEOUT wave = NULL;
    MMTIME position;
    DWORD volume = 0;
    int i;

    Check(500, sizeof(WAVEHDR) == 48 && sizeof(MIXERLINEA) == 172 && sizeof(MIXERCONTROLDETAILS) == 32 &&
                   sizeof(MIXERLINECONTROLSA) == 28 && sizeof(MIXERCONTROLA) == 148 && sizeof(MMTIME) == 12);
    Check(501, waveOutGetNumDevs() >= 1);
    memset(&format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 8000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = 16000;
    Check(502, waveOutOpen(&wave, WAVE_MAPPER, &format, (DWORD_PTR)WaveProc, (DWORD_PTR)&marker, CALLBACK_FUNCTION) ==
                   MMSYSERR_NOERROR);
    Check(503, wave != NULL && wave_open_calls == 1 && wave_instance_seen == (DWORD_PTR)&marker);
    for (i = 0; i < 800; ++i) samples[i] = (short)((i & 31) * 200);
    memset(&header, 0, sizeof header);
    header.lpData = (LPSTR)samples;
    header.dwBufferLength = sizeof samples;
    header.dwUser = (DWORD_PTR)&marker;
    Check(504, waveOutPrepareHeader(wave, &header, sizeof header) == MMSYSERR_NOERROR);
    Check(505, (header.dwFlags & WHDR_PREPARED) && header.lpData == (LPSTR)samples && header.dwUser == (DWORD_PTR)&marker);
    Check(506, waveOutWrite(wave, &header, sizeof header) == MMSYSERR_NOERROR);
    /* The system finishes the block while the program only looks at its own struct. */
    for (i = 0; i < 600 && !(header.dwFlags & WHDR_DONE); ++i) Sleep(5);
    Check(507, (header.dwFlags & WHDR_DONE) && !(header.dwFlags & WHDR_INQUEUE));
    Pump(window, 500, &wave_done_calls);
    Check(508, wave_done_calls == 1 && wave_done_header == &header);
    memset(&position, 0, sizeof position);
    position.wType = TIME_BYTES;
    Check(509, waveOutGetPosition(wave, &position, sizeof position) == MMSYSERR_NOERROR && position.u.cb == sizeof samples);
    Check(510, waveOutSetVolume(wave, 0x80008000) == MMSYSERR_NOERROR && waveOutGetVolume(wave, &volume) == MMSYSERR_NOERROR &&
                   (volume & 0xffff) == 0x8000);
    /* A second round with the same header. */
    Check(511, waveOutWrite(wave, &header, sizeof header) == MMSYSERR_NOERROR && (header.dwFlags & WHDR_INQUEUE));
    Check(512, waveOutUnprepareHeader(wave, &header, sizeof header) == WAVERR_STILLPLAYING);
    Check(513, waveOutReset(wave) == MMSYSERR_NOERROR);
    for (i = 0; i < 200 && !(header.dwFlags & WHDR_DONE); ++i) Sleep(5);
    Check(514, (header.dwFlags & WHDR_DONE) != 0);
    Check(515, waveOutUnprepareHeader(wave, &header, sizeof header) == MMSYSERR_NOERROR && !(header.dwFlags & WHDR_PREPARED));
    Check(516, waveOutClose(wave) == MMSYSERR_NOERROR);
    Pump(window, 300, &wave_close_calls);
    Check(517, wave_close_calls == 1);

    /* The same with a window as the callback. */
    wave = NULL;
    Check(520, waveOutOpen(&wave, WAVE_MAPPER, &format, (DWORD_PTR)window, 0, CALLBACK_WINDOW) == MMSYSERR_NOERROR);
    memset(&header, 0, sizeof header);
    header.lpData = (LPSTR)samples;
    header.dwBufferLength = sizeof samples;
    Check(521, waveOutPrepareHeader(wave, &header, sizeof header) == MMSYSERR_NOERROR);
    Check(522, waveOutWrite(wave, &header, sizeof header) == MMSYSERR_NOERROR);
    Pump(window, 3000, &window_done_calls);
    Check(523, window_done_calls == 1 && window_done_header == &header && (header.dwFlags & WHDR_DONE));
    Check(524, waveOutUnprepareHeader(wave, &header, sizeof header) == MMSYSERR_NOERROR);
    Check(525, waveOutClose(wave) == MMSYSERR_NOERROR);
    Check(526, waveOutOpen(&wave, 7, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_BADDEVICEID);

    /* A thread as the callback: MM_WOM_DONE arrives in a message of the
       queue, and its lParam is the header of the program. */
    wave = NULL;
    Check(530, waveOutOpen(&wave, WAVE_MAPPER, &format, (DWORD_PTR)GetCurrentThreadId(), 0, CALLBACK_THREAD) == MMSYSERR_NOERROR);
    memset(&header, 0, sizeof header);
    header.lpData = (LPSTR)samples;
    header.dwBufferLength = sizeof samples;
    Check(531, waveOutPrepareHeader(wave, &header, sizeof header) == MMSYSERR_NOERROR &&
                   waveOutWrite(wave, &header, sizeof header) == MMSYSERR_NOERROR);
    {
        const DWORD start = GetTickCount();
        MSG message;
        int seen = 0;
        while (!seen && GetTickCount() - start < 3000)
        {
            if (PeekMessageA(&message, NULL, MM_WOM_DONE, MM_WOM_DONE, PM_REMOVE)) seen = message.lParam == (LPARAM)&header ? 1 : 2;
            else Sleep(5);
        }
        Check(532, seen == 1);
    }
    Check(533, waveOutUnprepareHeader(wave, &header, sizeof header) == MMSYSERR_NOERROR && waveOutClose(wave) == MMSYSERR_NOERROR);

    /* Wave input and MIDI output cross as well. */
    {
        HMIDIOUT midi = NULL;
        MIDIOUTCAPSA caps;
        Check(540, sizeof(MIDIHDR) == 112 && sizeof(WAVEINCAPSA) == 48);
        Check(541, waveInGetNumDevs() < 16);
        Check(542, midiOutGetNumDevs() >= 1 && midiOutGetDevCapsA(0, &caps, sizeof caps) == MMSYSERR_NOERROR && caps.szPname[0]);
        Check(543, midiOutOpen(&midi, MIDI_MAPPER, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR && midi != NULL);
        Check(544, midiOutShortMsg(midi, 0x007f3c90) == MMSYSERR_NOERROR && midiOutShortMsg(midi, 0x00003c80) == MMSYSERR_NOERROR);
        Check(545, midiOutReset(midi) == MMSYSERR_NOERROR && midiOutClose(midi) == MMSYSERR_NOERROR);
    }
}
static void Timer(HWND window)
{
    static int marker;
    const DWORD start = timeGetTime();
    UINT id;
    Check(600, timeBeginPeriod(1) == TIMERR_NOERROR);
    id = timeSetEvent(20, 5, TimeProc, (DWORD_PTR)&marker, TIME_ONESHOT);
    Check(601, id != 0);
    Pump(window, 2000, &timer_calls);
    Check(602, timer_calls == 1 && timer_user == (DWORD_PTR)&marker);
    Check(603, timeGetTime() - start >= 15);
    id = timeSetEvent(10, 5, TimeProc, 0, TIME_PERIODIC);
    Pump(window, 200, NULL);
    Check(604, id != 0 && timer_calls >= 3 && timeKillEvent(id) == TIMERR_NOERROR);
    timeEndPeriod(1);
}
static void Mixer(void)
{
    HMIXER mixer = NULL;
    MIXERLINEA line;
    MIXERLINECONTROLSA controls;
    MIXERCONTROLA control;
    MIXERCONTROLDETAILS details;
    MIXERCONTROLDETAILS_UNSIGNED value[2], before[2];
    char text[128];

    Check(700, mixerGetNumDevs() >= 1);
    Check(701, mixerOpen(&mixer, 0, 0, 0, MIXER_OBJECTF_MIXER) == MMSYSERR_NOERROR && mixer != NULL);
    memset(&line, 0, sizeof line);
    line.cbStruct = sizeof line;
    line.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;
    Check(702, mixerGetLineInfoA((HMIXEROBJ)mixer, &line, MIXER_GETLINEINFOF_COMPONENTTYPE) == MMSYSERR_NOERROR);
    Check(703, line.cbStruct == sizeof line && line.cChannels >= 1 && line.cControls >= 1 && line.szName[0]);
    memset(&controls, 0, sizeof controls);
    memset(&control, 0, sizeof control);
    controls.cbStruct = sizeof controls;
    controls.dwLineID = line.dwLineID;
    controls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    controls.cControls = 1;
    controls.cbmxctrl = sizeof control;
    controls.pamxctrl = &control;
    Check(704, mixerGetLineControlsA((HMIXEROBJ)mixer, &controls, MIXER_GETLINECONTROLSF_ONEBYTYPE) == MMSYSERR_NOERROR);
    Check(705, control.dwControlType == MIXERCONTROL_CONTROLTYPE_VOLUME && control.Bounds.dwMaximum == 0xffff &&
                   controls.pamxctrl == &control);
    memset(&details, 0, sizeof details);
    details.cbStruct = sizeof details;
    details.dwControlID = control.dwControlID;
    details.cChannels = 1;
    details.cbDetails = sizeof value[0];
    details.paDetails = before;
    Check(706, mixerGetControlDetailsA((HMIXEROBJ)mixer, &details, MIXER_GETCONTROLDETAILSF_VALUE) == MMSYSERR_NOERROR);
    value[0].dwValue = 0x4321;
    details.paDetails = value;
    Check(707, mixerSetControlDetails((HMIXEROBJ)mixer, &details, MIXER_SETCONTROLDETAILSF_VALUE) == MMSYSERR_NOERROR);
    value[0].dwValue = 0;
    Check(708, mixerGetControlDetailsA((HMIXEROBJ)mixer, &details, MIXER_GETCONTROLDETAILSF_VALUE) == MMSYSERR_NOERROR &&
                   value[0].dwValue == 0x4321 && details.paDetails == value);
    details.paDetails = before;
    mixerSetControlDetails((HMIXEROBJ)mixer, &details, MIXER_SETCONTROLDETAILSF_VALUE);
    Check(709, mixerClose(mixer) == MMSYSERR_NOERROR);

    Check(720, PlaySoundA(NULL, NULL, 0));
    {
        MCIERROR error = mciSendStringA("status nosuchdevice mode", text, sizeof text, NULL);
        Check(721, error != 0);
        text[0] = 0;
        Check(722, mciGetErrorStringA(error, text, sizeof text) && text[0]);
    }
}

/* --- ADVAPI32 --------------------------------------------------------------- */
static void Registry(void)
{
    HKEY key = NULL, classes = NULL;
    DWORD disposition = 0, type = 0, size, number = 0x12345678, index;
    char text[128], name[64];
    Check(800, RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\NEO98\\Stage2", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS,
                               NULL, &key, &disposition) == ERROR_SUCCESS && key != NULL);
    Check(801, disposition == REG_CREATED_NEW_KEY || disposition == REG_OPENED_EXISTING_KEY);
    Check(802, RegSetValueExA(key, "Text", 0, REG_SZ, "hello", 6) == ERROR_SUCCESS);
    Check(803, RegSetValueExA(key, "Number", 0, REG_DWORD, &number, 4) == ERROR_SUCCESS);
    size = sizeof text;
    Check(804, RegQueryValueExA(key, "Text", NULL, &type, (LPBYTE)text, &size) == ERROR_SUCCESS && type == REG_SZ &&
                   size == 6 && !lstrcmpA(text, "hello"));
    number = 0;
    size = 4;
    Check(805, RegQueryValueExA(key, "Number", NULL, &type, (LPBYTE)&number, &size) == ERROR_SUCCESS && type == REG_DWORD &&
                   number == 0x12345678);
    size = 2;
    Check(806, RegQueryValueExA(key, "Text", NULL, NULL, (LPBYTE)text, &size) == ERROR_MORE_DATA && size == 6);
    for (index = 0;; ++index)
    {
        DWORD length = sizeof name;
        if (RegEnumValueA(key, index, name, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
    }
    Check(807, index == 2);
    Check(808, RegDeleteValueA(key, "Text") == ERROR_SUCCESS && RegQueryValueExA(key, "Text", NULL, NULL, NULL, NULL) != ERROR_SUCCESS);
    Check(809, RegCloseKey(key) == ERROR_SUCCESS);
    Check(810, RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\NEO98\\Stage2") == ERROR_SUCCESS);
    Check(811, RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\NEO98\\Stage2", 0, KEY_READ, &key) != ERROR_SUCCESS);
    /* What a document is opened with: the association the shell reads. */
    Check(820, RegOpenKeyExA(HKEY_CLASSES_ROOT, ".txt", 0, KEY_READ, &classes) == ERROR_SUCCESS);
    size = sizeof text;
    text[0] = 0;
    Check(821, RegQueryValueExA(classes, NULL, NULL, &type, (LPBYTE)text, &size) == ERROR_SUCCESS && !lstrcmpA(text, "txtfile"));
    RegCloseKey(classes);
    size = sizeof name;
    /* Nobody is logged on in a session without a network logon: error 1245. */
    Check(822, GetUserNameA(name, &size) ? name[0] != 0 : GetLastError() == 1245);
}

int main(void)
{
    WNDCLASSA wc;
    HWND window;
    char line[96];
    output = GetStdHandle(STD_OUTPUT_HANDLE);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "Stage2Fixture";
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassA(&wc);
    window = CreateWindowExA(0, "Stage2Fixture", "stage 2", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 200, 150, NULL,
                             NULL, wc.hInstance, NULL);
    Check(1, window != NULL);
    Objects();
    Regions(window);
    Windows(window);
    Kernel();
    Wave(window);
    Timer(window);
    Mixer();
    Registry();
    DestroyWindow(window);
    wsprintfA(line, "STAGE2 checks=%d failures=%d\r\n", checks, failures);
    Print(line);
    return failures;
}
