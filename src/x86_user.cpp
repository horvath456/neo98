// SPDX-License-Identifier: GPL-2.0-or-later
#include "user_settings.h"
#include "win98_parameters.h"
#include "win98_parameters_write.h"
#include "win98_broadcast.h"
#include "win98_gdi_parameters.h"
#include "win98_frame_control.h"
#include "registry.h"
#include "sound.h"
#include "files.h"
#include "profile_files.h"
#include "win98_profile_struct.h"
#include "x86_app.h"
#include "gdi_font_catalog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
// USER builds the recipient list and decides who receives the message.
unsigned Application::BroadcastMessage(unsigned mode, unsigned message, unsigned wp, unsigned lp)
{
    unsigned windows[User::WindowCapacity], count = 0;
    if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count))
    {
        last_error = 8;
        return 0;
    }
    Win98Broadcast::Request request;
    request.message = message;
    request.wparam = wp;
    request.has_name = lp != 0;
    for (unsigned i = 0; i < count; ++i)
    {
        if (!User::IsWindow(windows[i]))
            continue;
        Win98Broadcast::Window window;
        window.handle = windows[i];
        window.queue = User::WindowThread(windows[i]);
        window.owner = 0;
        // NEO98 has no menu or dropdown class atoms in the top-level list, and no
        // window carries the original per-window broadcast exclusion bits yet.
        char klass[64] = {};
        User::GetClassName(windows[i], klass, sizeof klass);
        window.excluded = !strcmp(klass, "#32768");
        if (!Win98Broadcast::Accepts(request, window))
            continue;
        if (mode == Win98Broadcast::Post)
        {
            if (Win98Broadcast::Postable(window))
                User::PostMessage(windows[i], message, wp, lp);
            continue;
        }
        if (mode == Win98Broadcast::Notify) SendNotify(windows[i], message, wp, lp);
        else {
            const unsigned result=Send(windows[i], message, wp, lp);
            if(mode==Win98Broadcast::Query&&result==0x424d5144u)return 0;
        }
        if (state == Failed)
            break;
    }
    // USER reports success once the walk finished, not a per-window result.
    return 1;
}
unsigned Application::SystemMetric(unsigned index)
{
    auto &provider = User::SystemSettings();
    return Win98Metrics::Query(provider, static_cast<int>(index));
}
#include "x86_parameters_write.inc"
#include "x86_accessibility.inc"
#include "x86_control_panel.inc"
// USER reads its settings once at start. It opens HKLM\Software\Microsoft\Plus!\Setup,
// reads the value "cool.dll" into a 104h-byte buffer
// and, when that succeeds with a nonzero length, loads
// COOL.DLL. It keeps the module at DS:264h only when
// ordinal 2 answers DX:AX 4152h:4245h; otherwise 264h
// stays zero. COOL.DLL is a 16-bit module and its ordinal 2
// only returns that constant, so the port does not map it: the readable file
// stands for the load and 264h gets a nonzero marker instead of a handle.
void Application::LoadUserStartSettings()
{
    auto &settings = User::SystemSettings();
    if (settings.plus_checked)
        return;
    settings.plus_checked = true;
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    const Registry::Value *value = nullptr;
    // Windows 98 reads HKCU\Control Panel\Mouse "SwapMouseButtons" (default
    // "" from DS:270h) and swaps when it starts with 'Y', 'y' or '1'.
    if (!registry.Open(registry.roots[1], "Control Panel\\Mouse", false, key, created) &&
        !registry.Query(key, "SwapMouseButtons", value) && value && value->size && value->data)
    {
        const char first = char(value->data[0]);
        User::SwapMouseButtons(first == 'Y' || first == 'y' || first == '1');
    }
    key=Registry::Missing;
    if(!registry.Open(registry.roots[1],"Control Panel\\Mouse",false,key,created)&&
       !registry.Query(key,"MouseSensitivity",value)&&value&&value->data&&value->size<=12){
        unsigned speed=0;
        for(unsigned i=0;i<value->size&&value->data[i]>='0'&&value->data[i]<='9';++i)speed=speed*10+value->data[i]-'0';
        if(speed>=1&&speed<=20)settings.mouse_speed=speed;
    }
    key=Registry::Missing;
    if(!registry.Open(registry.roots[1],"Control Panel\\Desktop",false,key,created)&&
       !registry.Query(key,"UserPreferencemask",value)&&value&&value->type==3&&value->data&&value->size>=4)
        settings.gradient_captions=(value->data[0]&0x10)!=0;
    // The screen saver and the power features of the display: the
    // timeouts in seconds, negative while inactive (USER DS:DAh, DCh, DEh),
    // from the values USER writes under HKCU\Control Panel\Desktop
    // ( for the power pair, profile identifiers 63h and 64h for the
    // screen saver).
    key = Registry::Missing;
    if (!registry.Open(registry.roots[1], "Control Panel\\Desktop", false, key, created))
    {
        auto number = [&](const char *name, int fallback) {
            const Registry::Value *text = nullptr;
            if (registry.Query(key, name, text) || !text || !text->size || text->size > 15)
                return fallback;
            int result = 0, sign = 1;
            unsigned i = 0;
            if (text->data[0] == '-')
            {
                sign = -1;
                i = 1;
            }
            for (; i < text->size && text->data[i] >= '0' && text->data[i] <= '9'; ++i)
                result = result * 10 + (text->data[i] - '0');
            return result * sign;
        };
        static const char *const timeouts[3] = {"ScreenSaveTimeOut", "ScreenSaveLowPowerTimeout",
                                                "ScreenSavePowerOffTimeout"};
        static const char *const actives[3] = {"ScreenSaveActive", "ScreenSaveLowPowerActive",
                                               "ScreenSavePowerOffActive"};
        for (unsigned i = 0; i < 3; ++i)
        {
            const int timeout = number(timeouts[i], 0);
            settings.saver[i] = short(number(actives[i], 0) ? timeout : -timeout);
        }
    }
    LoadWindowMetrics();
    LoadControlPanelSettings();
    key = Registry::Missing;
    value = nullptr;
    if (registry.Open(registry.roots[2], "Software\\Microsoft\\Plus!\\Setup", false, key, created) ||
        registry.Query(key, "cool.dll", value) || !value || !value->size || value->size > 0x104)
        return;
    // KERNEL.95 looks in the Windows and the system directory.
    static const char *const paths[] = {"C:\\WINDOWS\\COOL.DLL", "C:\\WINDOWS\\SYSTEM\\COOL.DLL"};
    Files::Session session(Files::System());
    for (const char *path : paths)
    {
        unsigned handle = 0, status = 0;
        if (session.Open(path, 1, 3, 3, 0, handle, status))
            continue;
        session.Close(handle);
        settings.SetWord(0x264, 1);
        return;
    }
}
// USER sizes the parts of a window at its start by the values of
// HKCU\Control Panel\Desktop\WindowMetrics that Setup writes. A size is a string, in twips when
// negative (Windows 98: BorderWidth -15, ScrollWidth -240, CaptionWidth -270,
// SmCaptionWidth -195, SmCaptionHeight -225, MenuHeight -270, IconSpacing
// -1125) and then MulDiv(twips, dots per inch, 1440) pixels, in pixels when
// positive; a missing one is 1 for the border, 16 for the scroll bars, 18 for
// caption and menu and 13 for the small caption, and for the icon spacing 75
// pixels at 96 dots per inch. A font is the fifty bytes of a LOGFONT16 whose
// height is in points when positive, -MulDiv(points, dots per inch, 72)
// pixels; without a height it is 8 points (4 for the small
// caption, 6 for the icon titles), without a weight 400 (700 for the caption),
// without a face MS Sans Serif. So everything here follows the font size of
// the display (src/gdi_display_dpi.h). SPI_SETNONCLIENTMETRICS and
// SPI_SETICONMETRICS with SPIF_UPDATEINIFILE store the same way
// (src/x86_parameters_write.inc).
namespace
{
constexpr const char *WindowMetricsKey = "Control Panel\\Desktop\\WindowMetrics";
}
void Application::LoadWindowMetrics()
{
    LoadInstalledFonts();
    auto &settings = User::SystemSettings();
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    const bool hive = !registry.Open(registry.roots[1], WindowMetricsKey, false, key, created);
    // USER's reader of a number: blanks, a sign, digits.
    auto size = [&](const char *name, int fallback, bool twips) {
        const Registry::Value *value = nullptr;
        if (!hive || registry.Query(key, name, value) || !value || value->type != 1 || !value->data ||
            !value->size || value->size > 31)
            return fallback;
        unsigned at = 0;
        int sign = 1, number = 0;
        while (at < value->size && value->data[at] == ' ')
            ++at;
        if (at < value->size && (value->data[at] == '-' || value->data[at] == '+'))
            sign = value->data[at++] == '-' ? -1 : 1;
        for (; at < value->size && value->data[at] >= '0' && value->data[at] <= '9' && number < 100000; ++at)
            number = number * 10 + (value->data[at] - '0');
        return twips && sign < 0 ? Gdi::TwipsToPixels(number) : sign * number;
    };
    // USER's reader of a font.
    const auto *system_face = GdiFonts::RoleFace(GdiFonts::SystemRole);
    auto font = [&](unsigned handle, const char *name, int points, unsigned weight) {
        unsigned char logfont[50] = {};
        const Registry::Value *value = nullptr;
        if (hive && !registry.Query(key, name, value) && value && value->data)
            memcpy(logfont, value->data, value->size < sizeof logfont ? value->size : sizeof logfont);
        logfont[49] = 0;
        if (!logfont[18])
            memcpy(logfont + 18, "MS Sans Serif", 14);
        int height = short(logfont[0] | logfont[1] << 8);
        if (!height)
            height = points;
        if (height > 0)
            height = -Gdi::PointsToPixels(height);
        logfont[0] = (unsigned char)height;
        logfont[1] = (unsigned char)(height >> 8);
        if (!(logfont[8] | logfont[9]))
        {
            logfont[8] = (unsigned char)weight;
            logfont[9] = (unsigned char)(weight >> 8);
        }
        logfont[13] = system_face ? system_face->font.data[0x55] : 0;
        logfont[14] = logfont[15] = logfont[16] = 0;
        settings.SetFont(handle, logfont);
    };
    font(0x502, "CaptionFont", 8, 700);
    font(0x503, "SmCaptionFont", 4, 400);
    font(0x501, "MenuFont", 8, 400);
    font(0x505, "StatusFont", 8, 400);
    font(0x506, "MessageFont", 8, 400);
    font(0x504, "IconFont", 6, 400);
    User::SetMetricFontProvider(CreateMetricFont);
    Win98Metrics::NonClient sizes;
    sizes.border_width = unsigned(size("BorderWidth", 1, true));
    sizes.scroll_width = unsigned(size("ScrollWidth", 16, true));
    sizes.scroll_height = unsigned(size("ScrollHeight", 16, true));
    sizes.caption_width = unsigned(size("CaptionWidth", 18, true));
    sizes.caption_height = unsigned(size("CaptionHeight", 18, true));
    sizes.small_caption_width = unsigned(size("SmCaptionWidth", 13, true));
    sizes.small_caption_height = unsigned(size("SmCaptionHeight", 13, true));
    sizes.menu_width = unsigned(size("MenuWidth", 18, true));
    sizes.menu_height = unsigned(size("MenuHeight", 18, true));
    User::ApplyNonClientMetrics(sizes);
    // USER, the minimized windows: MinWidth (154 pixels without it),
    // MinHorzGap, MinVertGap and MinArrange, where SPI_SETMINIMIZEDMETRICS
    // stores them. The bit of SM_ARRANGE that hides minimized windows is not
    // part of the stored value.
    Win98Metrics::ApplyMinimizedMetrics(settings, size("MinWidth", 154, true), size("MinHorzGap", 0, true),
                                        size("MinVertGap", 0, true),
                                        unsigned(size("MinArrange", 0, false)) | (settings.Word(0x34e) & 8u));
    // USER, the grid of icons: IconSpacing and IconVerticalSpacing,
    // or 75 pixels at 96 dots per inch scaled to the font size (and by
    // IconSpacingFactor percent across), at least an icon.
    const auto &dpi = Gdi::Dpi();
    const int factor = size("IconSpacingFactor", 100, false);
    int across = size("IconSpacing", dpi.logical_x * 75 / 96 * factor / 100, true);
    int down = size("IconVerticalSpacing", dpi.logical_y * 75 / 96, true);
    if (across < Win98Metrics::Signed(settings.Word(0x2f4)))
        across = Win98Metrics::Signed(settings.Word(0x2f4));
    if (down < Win98Metrics::Signed(settings.Word(0x2f6)))
        down = Win98Metrics::Signed(settings.Word(0x2f6));
    settings.SetWord(0x32a, (unsigned short)across);
    settings.SetWord(0x32c, (unsigned short)down);
}
// USER: SPI_GETFONTSMOOTHING (4Ah) and SPI_SETFONTSMOOTHING (4Bh).
// Without the Plus module at DS:264h, 4Ah writes a zero WORD, both answer
// zero. With it, USER hands the call to GDI.528.
unsigned Application::FontSmoothingParameters(const unsigned *a)
{
    unsigned short word = 0;
    unsigned result = 0;
    if (User::SystemSettings().Word(0x264))
        result = Win98GdiParameters::Query(Win98GdiParameters::System(), a[0], a[1], a[3], word,
                                           [&](const char *text)
                                           { WriteSystemProfile("TrueType", "FontSmoothing", text); });
    if (a[0] == 0x4a)
    {
        // The public entry widens the WORD like every scalar answer.
        const unsigned widened = (unsigned)(int)(short)word;
        if (!Store(a[2], &widened, 4))
            return 0;
    }
    return result;
}
// USER: SPI_SETMOUSEBUTTONSWAP calls SwapMouseButton with
// uiParam != 0. With SPIF_UPDATEINIFILE it formats "%d" of uiParam == 1
// and writes it as REG_SZ "SwapMouseButtons" under
// HKCU\Control Panel\Mouse, the value
// Windows 98 reads at start. No WM_SETTINGCHANGE follows.
unsigned Application::MouseButtonSwapParameter(const unsigned *a)
{
    User::SwapMouseButtons((a[1] & 0xffff) != 0);
    if (a[3] & 1)
    {
        auto &registry = Registry::System();
        unsigned key = Registry::Missing;
        bool created = false;
        const char *text = (a[1] & 0xffff) == 1 ? "1" : "0";
        if (registry.Open(registry.roots[1], "Control Panel\\Mouse", true, key, created) ||
            registry.Set(key, "SwapMouseButtons", 1, text, 2))
            return 0;
    }
    return 1;
}
// USER (SPI_SETSCREENSAVETIMEOUT, 0Fh), (SPI_SETSCREENSAVEACTIVE,
// 11h) and (SPI_SETLOWPOWERTIMEOUT 51h, SPI_SETPOWEROFFTIMEOUT 52h,
// SPI_SETLOWPOWERACTIVE 55h, SPI_SETPOWEROFFACTIVE 56h, the last four only
// when the display has the power state). Each restarts the idle
// time and ends a running screen saver. A timeout keeps the
// sign of the word, "active" flips it: 0 turns a positive word negative, any
// other value a negative one positive. The power pair writes its value as
// "%d" under HKCU\Control Panel\Desktop every time; the
// screen saver pair with SPIF_UPDATEINIFILE (profile
// identifiers 63h "ScreenSaveTimeOut" and 64h "ScreenSaveActive"). With
// SPIF_SENDCHANGE as well WM_WININICHANGE goes to every window.
unsigned Application::ScreenSaveParameters(const unsigned *a)
{
    auto &settings = User::SystemSettings();
    const unsigned action = a[0], value = a[1] & 0xffff;
    unsigned index = 0;
    bool timeout = true;
    if (action == 0x61)
    {
        // SPI_SCREENSAVERRUNNING: the previous state out, the new one in.
        const unsigned previous = settings.saver_running ? 1 : 0;
        if (a[2] && !Store(a[2], &previous, 4))
            return 0;
        settings.saver_running = value != 0;
        return 1;
    }
    switch (action)
    {
    case 0x0f: index = 0; timeout = true; break;
    case 0x11: index = 0; timeout = false; break;
    case 0x51: case 0x52: index = action - 0x50; timeout = true; break;
    case 0x55: case 0x56: index = action - 0x54; timeout = false; break;
    default: return 0;
    }
    if (index && !(settings.PowerStates() & (index == 1 ? 4u : 8u)))
        return 0;
    NoteHardwareInput(); // a change restarts the idle time
    short &word = settings.saver[index];
    if (timeout)
        word = short(word < 0 ? -int(value) : int(value));
    else if ((value && word < 0) || (!value && word > 0))
        word = short(-word);
    const bool store = index || (a[3] & 1);
    if (store)
    {
        static const char *const timeouts[3] = {"ScreenSaveTimeOut", "ScreenSaveLowPowerTimeout",
                                                "ScreenSavePowerOffTimeout"};
        static const char *const actives[3] = {"ScreenSaveActive", "ScreenSaveLowPowerActive",
                                               "ScreenSavePowerOffActive"};
        auto &registry = Registry::System();
        unsigned key = Registry::Missing;
        bool created = false;
        char text[12];
        snprintf(text, sizeof text, "%d", timeout ? int(value) : (value ? 1 : 0));
        if (registry.Open(registry.roots[1], "Control Panel\\Desktop", true, key, created) ||
            registry.Set(key, timeout ? timeouts[index] : actives[index], 1, text, unsigned(strlen(text)) + 1))
            return 0;
    }
    if ((a[3] & 3) == 3)
        BroadcastMessage(Win98Broadcast::Send, 0x1a, action, 0);
    return 1;
}
// NEO98_TRACE_SETTINGS=1 logs every call of SystemParametersInfo with its
// answer, one line each: which actions a program (an applet of the Control
// Panel) uses, and which of them the port refuses.
unsigned Application::SystemParameters(const unsigned *a)
{
    static const bool trace = getenv("NEO98_TRACE_SETTINGS") != nullptr;
    const unsigned result = SystemParametersService(a);
    if (trace && log)
    {
        char text[160];
        snprintf(text, sizeof text, "SETTINGS SystemParametersInfo action=%u param=%u pointer=%08x flags=%u result=%u",
                 a[0], a[1], a[2], a[3], result);
        log(text);
    }
    return result;
}
unsigned Application::SystemParametersService(const unsigned *a)
{
    // This is a native Win32 adapter. The original USER32 fast-thunk machinery
    // is replaced, while the selected USER.EXE query branches run as ARM code.
    if (a[3] & ~3u)
    {
        last_error = 87;
        return 0;
    }
    if(a[0]==0x70||a[0]==0x71){
        auto&settings=User::SystemSettings();
        if(a[0]==0x70){
            if(!a[2]||!virtual_memory.Access(a[2],4,true)){last_error=87;return 0;}
            return Store(a[2],&settings.mouse_speed,4);
        }
        if(a[2]<1||a[2]>20)return 0;
        settings.mouse_speed=a[2];
        if(a[3]&1){
            auto&registry=Registry::System();unsigned key=Registry::Missing;bool created=false;char text[12];
            snprintf(text,sizeof text,"%u",settings.mouse_speed);
            if(registry.Open(registry.roots[1],"Control Panel\\Mouse",true,key,created)||
               registry.Set(key,"MouseSensitivity",1,text,unsigned(strlen(text))+1))return 0;
        }
        if(a[3]&2)BroadcastMessage(Win98Broadcast::Send,0x1a,a[0],0);
        return 1;
    }
    if(a[0]==0x1009){
        auto&settings=User::SystemSettings();
        if(a[3]&1){
            auto&r=Registry::System();unsigned key=Registry::Missing;bool created=false;
            if(r.Open(r.roots[1],"Control Panel\\Desktop",true,key,created))return 0;
            const Registry::Value*value=nullptr;unsigned char mask[4]={};
            if(!r.Query(key,"UserPreferencemask",value)&&value&&value->type==3&&value->data&&value->size>=4)
                memcpy(mask,value->data,4);
            mask[0]=(mask[0]&~0x10u)|(a[2]?0x10:0);
            if(r.Set(key,"UserPreferencemask",3,mask,4))return 0;
        }
        settings.gradient_captions=a[2]!=0;
        User::InvalidateRect(0,nullptr,false);
        if(a[3]&2)BroadcastMessage(Win98Broadcast::Send,0x1a,a[0],0);
        return 1;
    }
    if(a[0]==0x1008){
        int width,height,depth;User::ScreenMode(width,height,depth);
        const unsigned enabled=User::SystemSettings().gradient_captions&&depth>8;
        if(!a[2]||!virtual_memory.Access(a[2],4,true)){last_error=87;return 0;}
        return Store(a[2],&enabled,4);
    }
    // FILTERKEYS to SCREENREADER, src/x86_accessibility.inc.
    if (a[0] >= 0x32 && a[0] <= 0x47)
        return AccessibilityParameters(a);
    // The plain numbers of the Control Panel, src/x86_control_panel.inc.
    if (unsigned result = 0; ControlPanelParameters(a, result))
        return result;
    // Actions that change a setting run the implemented write branches.
    if (a[0] == 20 || a[0] == 21 || a[0] == 34 || a[0] == 42 || a[0] == 44 || a[0] == 46 || a[0] == 47)
        return SystemParametersWrite(a);
    if (a[0] == 0x4a || a[0] == 0x4b)
        return FontSmoothingParameters(a);
    if (a[0] == 33)
        return MouseButtonSwapParameter(a);
    if (a[0] == 0x0f || a[0] == 0x11 || a[0] == 0x51 || a[0] == 0x52 || a[0] == 0x55 || a[0] == 0x56 ||
        a[0] == 0x61)
        return ScreenSaveParameters(a);
    auto &provider = User::SystemSettings();
    unsigned char data[340] = {};
    unsigned written = 0;
    if (a[0] == 31)
    {
        if (!a[2] || a[1] < 60)
        {
            last_error = 87;
            return 0;
        }
        unsigned char font[50];
        if (Win98Parameters::Query(provider, 31, 50, font, sizeof font, written) != 1)
            return 0;
        Win98Parameters::ExpandFont(font, data);
        return Store(a[2], data, 60);
    }
    const bool metrics = a[0] == 41 || a[0] == 43 || a[0] == 45;
    if (metrics && a[2] && !Read(a[2], data, 4))
        return 0;
    const int result =
        Win98Parameters::Query(provider, a[0], a[1], a[2] ? data : nullptr, sizeof data, written);
    if (result < 0)
    {
        last_error = 120;
        return 0;
    }
    if (written && !metrics)
    {
        // Widen internal WORD arrays to the public Win32 INT/BOOL/RECT format.
        unsigned values[4] = {};
        for (unsigned i = 0; i < written / 2; ++i)
            values[i] = int((short)(data[i * 2] | unsigned(data[i * 2 + 1]) << 8));
        if (!Store(a[2], values, written * 2))
            return 0;
    }
    else if (written && !Store(a[2], data, written))
        return 0;
    return result;
}
unsigned Application::EraseBackground(unsigned hwnd, unsigned dc)
{
    unsigned brush = User::Background(hwnd);
    if (!brush)
        return 0;
    if (brush <= 32)
    {
        unsigned color = brush - 1;
        brush = Api("GetSysColorBrush", &color);
    }
    // USER fills the clip box of the DC it is handed, so a window DC clipped
    // to the frame gets its frame filled: MFC's control bars (WordPad's
    // toolbars) erase their borders that way. A class with CS_PARENTDC
    // fills its client rectangle instead, in the DC's logical units; the
    // clip box of the parent's DC would cover the parent.
    Gdi::RECT rect;
    if (User::ClassAccess(hwnd, -26, 0, 2) & 0x80)
    {
        if (!User::GetClientRect(hwnd, &rect) || !Gdi::LogicalRect(dc, rect))
            return 0;
    }
    else
    {
        const int kind = Gdi::GetClipBox(dc, &rect);
        if (!kind)
            return 0;
        if (kind == 1)   // NULLREGION: nothing to fill, and nothing failed
            return 1;
    }
    const auto result = Gdi::FillRect(dc, &rect, brush);
    if (result)
        User::DrawingChanged(dc);
    return result;
}
// BeginPaint hands out the paint DC, then sends WM_ERASEBKGND to the window
// with that DC. It sets the still-to-erase bit again when the result is zero. The 32-bit entry and the Win16 gate of USER.39 differ only in how
// they read their arguments and write PAINTSTRUCT back, so they share this
// body. Rebuilding it on the 16-bit side is what lost the erase for a Win16
// window once.
// The WM_NCPAINT BeginPaint sends first, when the window owes one: with
// its update region (src/user.cpp, TakeFramePaint). A style change leaves
// frame work for the next invalidated paint; RDW_INTERNALPAINT alone does
// not make that nonclient work due. The port's own window procedures that
// paint with User::BeginPaint call it as well.
void Application::SendFramePaint(unsigned hwnd)
{
    if (User::GetUpdateRect(hwnd, nullptr))
        if (const unsigned region = User::TakeFramePaint(hwnd)) { CallWindow(hwnd, 0x85, region, 0); User::DropFramePaint(region); }
}
unsigned Application::BeginPaintWindow(unsigned hwnd, User::Paint &paint)
{
    paint = User::Paint{};
    SendFramePaint(hwnd);
    if (!User::IsWindow(hwnd)) return 0;
    const bool erase_requested = User::NeedsErase(hwnd);
    const auto dc = User::BeginPaint(hwnd, paint);
    if (!dc)
        return 0;
    if (erase_requested && !Win98::IsRectEmpty(&paint.rect))
    {
        Gdi::RECT clip{};
        paint.erase = Gdi::GetClipBox(dc,&clip)>1 && !CallWindow(hwnd, 0x14, dc, 0);
        User::RememberErase(hwnd, !paint.erase);
    }
    // The erase handler may destroy the window it was asked to paint.
    if (!User::IsWindow(hwnd))
        return 0;
    // A zero answer is reported as fErase. The send-erase bit comes back only
    // for a window older than 3.1: Windows 98 sets fErase (2Dh bit 4) and
    // sets send-erase (2Dh bit 2) only when WFWIN31COMPAT (2Eh bit 4) is
    // clear. Re-arming it for every window made Welcome's WM_ERASEBKGND, which
    // answers zero and invalidates without erase, repaint forever.
    if (erase_requested && paint.erase && EraseAgain())
        User::KeepErase(hwnd);
    return dc;
}
unsigned Application::DefaultPaint(unsigned hwnd)
{
    User::Paint paint;
    if (const auto dc = BeginPaintWindow(hwnd, paint))
    {
        (void)dc;
        User::EndPaint(hwnd, paint);
    }
    return 0;
}
unsigned Application::DispatchPaint(unsigned hwnd, unsigned wp, unsigned lp, bool resumed)
{
    auto &thread = threads[current_thread];
    bool outer = dispatching_posted, kept = false;
    if (resumed)
    {
        outer = thread.host_frames[thread.host_resume_at++].d != 0;
        if (thread.host_resume_at >= thread.host_frame_count) thread.host_resume = false;
        kept = true;
    }
    else if (TakeFresh()) kept = PushHostFrame(HostDispatchPaint, hwnd, wp, lp, outer);
    dispatching_posted = true;
    unsigned result;
    if (resumed) result = ResumeInner();
    else
    {
        GiveTail(kept ? TailRecord : TailNone);
        result = Send(hwnd, 0xf, wp, lp);
    }
    dispatching_posted = outer;
    if (kept) PopHostFrame();
    // DispatchMessage services the frame and erase left by a WM_PAINT
    // procedure that did not call BeginPaint. The client stays invalid.
    RedrawImmediate(hwnd, 2, 0);
    return result;
}
// The immediate paths of RedrawWindow. delivers WM_PAINT for
// RDW_UPDATENOW and the erase for RDW_ERASENOW; both recurse into the
// children the flags allow. Mode 8 is RDW_ALLCHILDREN, 4 the default and 0
// RDW_NOCHILDREN. The default erase walk descends at WS_CLIPCHILDREN.
void Application::RedrawImmediate(unsigned hwnd, unsigned kind, unsigned parameter, bool descendant)
{
    if (!User::IsWindow(hwnd))
        return;
    // Keep the update region and internal-paint flag until the lock releases.
    if (User::ScreenLockedElsewhere(process_id, ThreadId()))
        return;
    // USER checks before entering its erase walk. The pending
    // child erase survives until its ancestor's painting has been handled.
    if (kind == 2 && !descendant && User::AncestorNeedsPaint(hwnd))
        return;
    if (kind == 2 && User::GetUpdateRect(hwnd, nullptr))
        if (const unsigned region = User::TakeFramePaint(hwnd)) { CallWindow(hwnd, 0x85, region, 0); User::DropFramePaint(region); }
    // UPDATENOW enters WM_PAINT first; BeginPaint sends the frame and erase.
    if (kind == 2 && User::GetUpdateRect(hwnd, nullptr) && User::NeedsErase(hwnd))
    {
        const unsigned token = User::EraseToken(hwnd);
        if (const auto dc = User::GetUpdateDC(hwnd))
        {
            // Windows 98 clears send-erase before asks; see BeginPaintWindow.
            Gdi::RECT clip{};
            const bool handled = Gdi::GetClipBox(dc,&clip)<=1 || CallWindow(hwnd, 0x14, dc, 0);
            User::RememberErase(hwnd, handled);
            if (handled || !EraseAgain())
                User::Erased(hwnd, token);
            User::ReleaseDC(hwnd, dc);
        }
    }
    const bool internal = kind == 1 && User::TakeInternalPaint(hwnd);
    if (kind == 1 && (internal || User::GetUpdateRect(hwnd, nullptr)))
    {
        User::ClipAncestorUpdates(hwnd);   // ->
        CallWindow(hwnd, 0xf, 0, 0);
    }
    if (kind == 1 ? parameter == 0 : parameter != 8 && !(parameter == 4 && (User::Style(hwnd) & 0x02000000u)))
        return;
    unsigned children[64] = {}, count = 0;
    if (!User::WindowSnapshot(hwnd, children, 64, count))
        return;
    for (unsigned i = 0; i < count; ++i)
        RedrawImmediate(children[i], kind, parameter, true);
}
void Application::UpdateWindowTree(unsigned hwnd, unsigned depth)
{
    if (!User::IsWindow(hwnd) || depth > 64)
        return;
    // UpdateWindow services native control paints immediately on the owning
    // thread. STATIC already has a WM_PAINT/BeginPaint path; drawing it here
    // again would leave frame work for a duplicate paint.
    if (!User::IsLabel(hwnd) && User::WindowThread(hwnd) == User::CurrentThread())
        User::UpdateControl(hwnd);
    const bool internal = User::TakeInternalPaint(hwnd);
    if (internal || User::GetUpdateRect(hwnd, nullptr))
    {
        User::ClipAncestorUpdates(hwnd);   // ->
        CallWindow(hwnd, 0xf, 0, 0);
    }
    unsigned children[256] = {}, count = 0;
    if (!User::WindowSnapshot(hwnd, children, 256, count))
        return;
    for (unsigned i = 0; i < count; ++i)
        if (User::Parent(children[i]) == hwnd && User::IsVisible(children[i]))
            UpdateWindowTree(children[i], depth + 1);
}
unsigned Application::PaintApi(ApiName name, const unsigned *a)
{
    if (!User::IsWindow(a[0]))
    {
        last_error = 1400;
        return 0;
    }
    // USER calls with flag 1, and Windows 98 walks the children
    // ([pwnd+4], then each next sibling) with the same flag after the window
    // itself had its WM_PAINT. So UpdateWindow paints the whole visible tree
    // below the window at once; erasing stays with BeginPaint (the flag has
    // no bit 4). The port painted the window alone, and a splash form
    // whose picture lives in a child panel stayed grey while its thread went
    // on loading (Dev-C++).
    if (API_IS(name,"UpdateWindow"))
    {
        UpdateWindowTree(a[0], 0);
        User::Render();
        return 1;
    }
    if (API_IS(name,"ValidateRect"))
    {
        Gdi::RECT rect;
        if (a[1] && !Read(a[1], &rect, sizeof rect))
            return 0;
        return User::RedrawWindow(a[0], a[1] ? &rect : nullptr, 0, Win98RedrawWindow::Validate);
    }
    if (API_IS(name,"GetUpdateRect"))
    {
        if (a[2] && User::GetUpdateRect(a[0], nullptr))
            if (const unsigned region = User::TakeFramePaint(a[0])) { CallWindow(a[0], 0x85, region, 0); User::DropFramePaint(region); }
        if (a[2] && User::GetUpdateRect(a[0], nullptr) && User::NeedsErase(a[0]))
        {
            unsigned token = User::EraseToken(a[0]);
            auto dc = User::GetUpdateDC(a[0]);
            if (dc)
            {
                const bool handled = CallWindow(a[0], 0x14, dc, 0) != 0;
                User::RememberErase(a[0], handled);
                if (handled || !EraseAgain())
                    User::Erased(a[0], token);
                User::ReleaseDC(a[0], dc);
            }
        }
        Gdi::RECT rect = {};
        const bool result = User::GetUpdateRect(a[0], &rect);
        if (a[1])
            Store(a[1], &rect, sizeof rect);
        return result;
    }
    if (!a[1])
    {
        last_error = 87;
        return 0;
    }
    if (API_IS(name,"EndPaint"))
    {
        User::Paint paint = {};
        if (!Read(a[1], &paint, 48))
            return 0;
        User::EndPaint(a[0], paint);
        // USER.EXE returns TRUE after attempting to release the DC.
        return 1;
    }
    User::Paint paint;
    const auto dc = BeginPaintWindow(a[0], paint);
    if (!dc)
    {
        // A window the erase handler destroyed is gone, not a failure to hand
        // out a device context.
        if (User::IsWindow(a[0]))
            last_error = 8;
        return 0;
    }
    // Original USER32 BeginPaint expands only 48 bytes of PAINTSTRUCT32.
    // Native handles keep their generation bits instead of narrowing to HWND16.
    User::Paint wire;
    Win98Paint::NativeOutput(paint, wire);
    if (!Store(a[1], &wire, 48))
    {
        User::EndPaint(a[0], paint);
        return 0;
    }
    return dc;
}
} // namespace X86
