/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The font size of the display ("Small Fonts", "Large Fonts", custom).

   "set N" stores the size N dots per inch for the next start of Windows the
   way the Settings page of Display Properties does: the four DPI values of
   HKEY_CURRENT_CONFIG\Display\Settings, the system fonts and the fonts of
   Display\Fonts from the nearer set under
   HKLM\Software\Microsoft\Windows\CurrentVersion\FontSize, the same system
   fonts in SYSTEM.INI [boot] and "aspect" in [boot.description].

   "read" writes C:\FONTSIZE.TXT, one "name value" line for everything that
   follows the size: the device capabilities, the sizes and fonts of
   NONCLIENTMETRICS and ICONMETRICS, system metrics, the stock fonts, the
   dialog base units, a dialog of 200 by 100 units in 8 point MS Sans Serif,
   the button of a combo box, the mapping modes and DEVMODE.

   tools/test_font_size_host.py */
#define WINVER 0x0410
#define _WIN32_WINNT 0x0410
#include <windows.h>

static HANDLE file;

static void line(const char *name, int value)
{
    char text[96];
    DWORD written = 0;
    wsprintfA(text, "%s %d\r\n", name, value);
    WriteFile(file, text, lstrlenA(text), &written, NULL);
}

static int number(const char *text)
{
    int value = 0;
    while (*text >= '0' && *text <= '9')
        value = value * 10 + (*text++ - '0');
    return value;
}

/* What DESK.CPL does when the font size is applied. */
static void set_size(const char *size)
{
    static const char sizes[] = "Software\\Microsoft\\Windows\\CurrentVersion\\FontSize";
    HKEY root, settings, fonts, from;
    char chosen[32] = "", name[128], data[128], path[200];
    int wanted = number(size), best = 0;
    DWORD index, name_size, data_size;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sizes, 0, KEY_READ, &root))
        return;
    for (index = 0; name_size = sizeof name, !RegEnumKeyExA(root, index, name, &name_size, 0, 0, 0, 0); ++index)
    {
        int this_one = number(name), a = best - wanted, b = this_one - wanted;
        if (a < 0) a = -a;
        if (b < 0) b = -b;
        if (!best || a > b)
        {
            best = this_one;
            lstrcpyA(chosen, name);
        }
    }
    if (!best || RegCreateKeyA(HKEY_CURRENT_CONFIG, "Display\\Settings", &settings))
        return;
    wsprintfA(path, "%s\\System", chosen);
    if (!RegOpenKeyExA(root, path, 0, KEY_READ, &from))
    {
        for (index = 0; name_size = sizeof name, data_size = sizeof data,
             !RegEnumValueA(from, index, name, &name_size, 0, 0, (BYTE *)data, &data_size); ++index)
        {
            RegSetValueExA(settings, data, 0, REG_SZ, (const BYTE *)name, lstrlenA(name));
            WritePrivateProfileStringA("boot", data, name, "system.ini");
        }
        RegCloseKey(from);
    }
    wsprintfA(path, "%s\\User", chosen);
    if (!RegOpenKeyExA(root, path, 0, KEY_READ, &from))
    {
        RegDeleteKeyA(HKEY_CURRENT_CONFIG, "Display\\Fonts");
        if (!RegCreateKeyA(HKEY_CURRENT_CONFIG, "Display\\Fonts", &fonts))
        {
            for (index = 0; name_size = sizeof name, data_size = sizeof data,
                 !RegEnumValueA(from, index, name, &name_size, 0, 0, (BYTE *)data, &data_size); ++index)
            {
                RegSetValueExA(fonts, data, 0, REG_SZ, (const BYTE *)name, lstrlenA(name));
                WriteProfileStringA("fonts", data, NULL);
            }
            RegCloseKey(fonts);
        }
        RegCloseKey(from);
    }
    RegSetValueExA(settings, "DPILogicalX", 0, REG_SZ, (const BYTE *)size, lstrlenA(size));
    RegSetValueExA(settings, "DPILogicalY", 0, REG_SZ, (const BYTE *)size, lstrlenA(size));
    RegSetValueExA(settings, "DPIPhysicalX", 0, REG_SZ, (const BYTE *)size, lstrlenA(size));
    RegSetValueExA(settings, "DPIPhysicalY", 0, REG_SZ, (const BYTE *)size, lstrlenA(size));
    wsprintfA(path, "100,%s,%s", size, size);
    WritePrivateProfileStringA("boot.description", "aspect", path, "system.ini");
    RegCloseKey(settings);
    RegCloseKey(root);
}

static void font(const char *name, HFONT handle)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, handle);
    TEXTMETRICA metrics;
    SIZE extent;
    char label[64];
    GetTextMetricsA(dc, &metrics);
    GetTextExtentPoint32A(dc, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &extent);
    wsprintfA(label, "%s.height", name);
    line(label, metrics.tmHeight);
    wsprintfA(label, "%s.leading", name);
    line(label, metrics.tmInternalLeading);
    wsprintfA(label, "%s.width", name);
    line(label, (extent.cx / 26 + 1) / 2);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
}

static void logfont(const char *name, const LOGFONTA *log)
{
    char label[64];
    HFONT handle = CreateFontIndirectA(log);
    wsprintfA(label, "%s.lfHeight", name);
    line(label, log->lfHeight);
    wsprintfA(label, "%s.lfWeight", name);
    line(label, log->lfWeight);
    font(name, handle);
    DeleteObject(handle);
}

static INT_PTR CALLBACK dialog(HWND window, UINT message, WPARAM w, LPARAM l)
{
    (void)w;
    (void)l;
    if (message == WM_INITDIALOG)
    {
        RECT client, units = {0, 0, 4, 8}, button;
        GetClientRect(window, &client);
        line("dialog.width", client.right);
        line("dialog.height", client.bottom);
        MapDialogRect(window, &units);
        line("dialog.unit.x", units.right);
        line("dialog.unit.y", units.bottom);
        GetWindowRect(GetDlgItem(window, IDOK), &button);
        line("dialog.button.width", button.right - button.left);
        line("dialog.button.height", button.bottom - button.top);
        EndDialog(window, 0);
        return TRUE;
    }
    return FALSE;
}

/* DLGTEMPLATE: DS_SETFONT, 200 by 100 units, 8 point MS Sans Serif, one
   button of 50 by 14 units. */
static void show_dialog(void)
{
    static WORD words[160];
    WORD *p = words;
    const char *face = "MS Sans Serif", *label = "OK";
    DLGTEMPLATE *header = (DLGTEMPLATE *)p;
    DLGITEMTEMPLATE *item;
    header->style = DS_SETFONT | WS_POPUP | WS_CAPTION | DS_MODALFRAME;
    header->cdit = 1;
    header->cx = 200;
    header->cy = 100;
    p = (WORD *)(header + 1);
    *p++ = 0;
    *p++ = 0;
    *p++ = 0;
    *p++ = 8;
    while ((*p++ = (WORD)*face++) != 0)
        ;
    p = (WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
    item = (DLGITEMTEMPLATE *)p;
    item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;
    item->x = 10;
    item->y = 10;
    item->cx = 50;
    item->cy = 14;
    item->id = IDOK;
    p = (WORD *)(item + 1);
    *p++ = 0xffff;
    *p++ = 0x0080;
    while ((*p++ = (WORD)*label++) != 0)
        ;
    *p++ = 0;
    DialogBoxIndirectParamA(GetModuleHandleA(NULL), header, NULL, dialog, 0);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    const char *set = command;
    NONCLIENTMETRICSA n;
    ICONMETRICSA icons;
    DEVMODEA mode;
    LOGFONTA log;
    HDC dc;
    POINT point;
    DWORD units;
    while (*set && !(set[0] == 's' && set[1] == 'e' && set[2] == 't' && set[3] == ' '))
        ++set;
    if (*set)
    {
        set_size(set + 4);
        ExitProcess(0);
    }
    file = CreateFileA("C:\\FONTSIZE.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    dc = GetDC(NULL);
    line("LOGPIXELSX", GetDeviceCaps(dc, LOGPIXELSX));
    line("LOGPIXELSY", GetDeviceCaps(dc, LOGPIXELSY));
    line("HORZRES", GetDeviceCaps(dc, HORZRES));
    line("HORZSIZE", GetDeviceCaps(dc, HORZSIZE));
    line("VERTSIZE", GetDeviceCaps(dc, VERTSIZE));
    /* An inch in each mapping mode, in pixels. */
    SetMapMode(dc, MM_LOMETRIC);
    point.x = 254;
    point.y = -254;
    LPtoDP(dc, &point, 1);
    line("MM_LOMETRIC.inch.x", point.x);
    line("MM_LOMETRIC.inch.y", point.y);
    SetMapMode(dc, MM_TWIPS);
    point.x = 1440;
    point.y = -1440;
    LPtoDP(dc, &point, 1);
    line("MM_TWIPS.inch.x", point.x);
    SetMapMode(dc, MM_TEXT);
    ReleaseDC(NULL, dc);

    n.cbSize = sizeof n;
    line("NONCLIENTMETRICS", SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0));
    line("iBorderWidth", n.iBorderWidth);
    line("iScrollWidth", n.iScrollWidth);
    line("iScrollHeight", n.iScrollHeight);
    line("iCaptionWidth", n.iCaptionWidth);
    line("iCaptionHeight", n.iCaptionHeight);
    line("iSmCaptionWidth", n.iSmCaptionWidth);
    line("iSmCaptionHeight", n.iSmCaptionHeight);
    line("iMenuWidth", n.iMenuWidth);
    line("iMenuHeight", n.iMenuHeight);
    logfont("lfCaptionFont", &n.lfCaptionFont);
    logfont("lfSmCaptionFont", &n.lfSmCaptionFont);
    logfont("lfMenuFont", &n.lfMenuFont);
    logfont("lfStatusFont", &n.lfStatusFont);
    logfont("lfMessageFont", &n.lfMessageFont);
    icons.cbSize = sizeof icons;
    line("ICONMETRICS", SystemParametersInfoA(SPI_GETICONMETRICS, sizeof icons, &icons, 0));
    line("iHorzSpacing", icons.iHorzSpacing);
    line("iVertSpacing", icons.iVertSpacing);
    logfont("lfIconFont", &icons.lfFont);

    line("SM_CXVSCROLL", GetSystemMetrics(SM_CXVSCROLL));
    line("SM_CYHSCROLL", GetSystemMetrics(SM_CYHSCROLL));
    line("SM_CYCAPTION", GetSystemMetrics(SM_CYCAPTION));
    line("SM_CYSMCAPTION", GetSystemMetrics(SM_CYSMCAPTION));
    line("SM_CYMENU", GetSystemMetrics(SM_CYMENU));
    line("SM_CXSIZE", GetSystemMetrics(SM_CXSIZE));
    line("SM_CYSIZE", GetSystemMetrics(SM_CYSIZE));
    line("SM_CXSMSIZE", GetSystemMetrics(SM_CXSMSIZE));
    line("SM_CXMENUCHECK", GetSystemMetrics(SM_CXMENUCHECK));
    line("SM_CYMENUCHECK", GetSystemMetrics(SM_CYMENUCHECK));
    line("SM_CXICONSPACING", GetSystemMetrics(SM_CXICONSPACING));
    line("SM_CYICONSPACING", GetSystemMetrics(SM_CYICONSPACING));
    line("SM_CXICON", GetSystemMetrics(SM_CXICON));
    line("SM_CXMIN", GetSystemMetrics(SM_CXMIN));
    line("SM_CYMIN", GetSystemMetrics(SM_CYMIN));

    font("SYSTEM_FONT", GetStockObject(SYSTEM_FONT));
    font("SYSTEM_FIXED_FONT", GetStockObject(SYSTEM_FIXED_FONT));
    font("OEM_FIXED_FONT", GetStockObject(OEM_FIXED_FONT));
    font("ANSI_FIXED_FONT", GetStockObject(ANSI_FIXED_FONT));
    font("ANSI_VAR_FONT", GetStockObject(ANSI_VAR_FONT));
    font("DEFAULT_GUI_FONT", GetStockObject(DEFAULT_GUI_FONT));
    GetObjectA(GetStockObject(DEFAULT_GUI_FONT), sizeof log, &log);
    line("DEFAULT_GUI_FONT.lfHeight", log.lfHeight);
    GetObjectA(GetStockObject(ANSI_VAR_FONT), sizeof log, &log);
    line("ANSI_VAR_FONT.lfHeight", log.lfHeight);

    /* Fonts asked for in points: raster sizes and a TrueType face. */
    ZeroMemory(&log, sizeof log);
    lstrcpyA(log.lfFaceName, "MS Sans Serif");
    log.lfHeight = -MulDiv(8, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
    logfont("sans8", &log);
    log.lfHeight = -MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
    logfont("sans10", &log);
    log.lfHeight = -MulDiv(24, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
    logfont("sans24", &log);
    lstrcpyA(log.lfFaceName, "Arial");
    log.lfHeight = -MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72);
    logfont("arial10", &log);
    lstrcpyA(log.lfFaceName, "Courier");
    logfont("courier10", &log);

    /* The button of a drop-down combo box is as wide as a scroll bar. */
    {
        COMBOBOXINFO info;
        HWND parent = CreateWindowA("STATIC", "", WS_POPUP, 0, 0, 300, 200, NULL, NULL, NULL, NULL);
        HWND combo = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 10, 10, 200, 120,
                                   parent, (HMENU)1, NULL, NULL);
        ZeroMemory(&info, sizeof info);
        info.cbSize = sizeof info;
        if (combo && GetComboBoxInfo(combo, &info))
            line("combo.button.width", info.rcButton.right - info.rcButton.left);
        DestroyWindow(parent);
    }

    units = GetDialogBaseUnits();
    line("GetDialogBaseUnits.x", LOWORD(units));
    line("GetDialogBaseUnits.y", HIWORD(units));
    show_dialog();

    ZeroMemory(&mode, sizeof mode);
    mode.dmSize = sizeof mode;
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode))
        line("dmLogPixels", mode.dmLogPixels);
    CloseHandle(file);
    ExitProcess(0);
}
