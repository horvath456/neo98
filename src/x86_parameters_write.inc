// SPDX-License-Identifier: GPL-2.0-or-later
// Win32 adapter for the implemented SystemParametersInfo write branches.
// The original branch decisions live in win98_parameters_write.h; everything in
// this file is an NEO98 provider: guest buffers, the settings store, WIN.INI and
// the native window list used by the WM_WININICHANGE broadcast.
namespace
{
// USER runs the other way for a setter: LOGFONTA back to LOGFONT16.
void NarrowFont(const unsigned char *source, unsigned char *out)
{
    for (unsigned i = 0; i < 5; ++i)
    {
        const unsigned value = unsigned(source[i * 4]) | unsigned(source[i * 4 + 1]) << 8 |
                               unsigned(source[i * 4 + 2]) << 16 | unsigned(source[i * 4 + 3]) << 24;
        out[i * 2] = (unsigned char)value;
        out[i * 2 + 1] = (unsigned char)(value >> 8);
    }
    for (unsigned i = 0; i < 40; ++i)
        out[10 + i] = source[20 + i];
}
int Signed32(const unsigned char *p, unsigned offset)
{
    return int(unsigned(p[offset]) | unsigned(p[offset + 1]) << 8 | unsigned(p[offset + 2]) << 16 |
               unsigned(p[offset + 3]) << 24);
}
// The WIN.INI sections the original writes through USER. String table
// identifier 0x73 is "WindowMetrics" and 0x50 is "Desktop".
struct ProfileKey
{
    unsigned identifier;
    const char *section, *key;
};
const ProfileKey profile_keys[] = {
    {0x02, "Desktop", "Pattern"},
    {0x40, "Desktop", "IconSpacing"},
    {0x41, "Desktop", "IconVerticalSpacing"},
    {0x42, "Desktop", "IconTitleWrap"},
    {0x43, "Desktop", "Wallpaper"},
    {0x74, "WindowMetrics", "BorderWidth"},
    {0x75, "WindowMetrics", "CaptionWidth"},
    {0x76, "WindowMetrics", "CaptionHeight"},
    {0x77, "WindowMetrics", "CaptionFont"},
    {0x78, "WindowMetrics", "SmCaptionWidth"},
    {0x79, "WindowMetrics", "SmCaptionHeight"},
    {0x7a, "WindowMetrics", "SmCaptionFont"},
    {0x7b, "WindowMetrics", "MenuWidth"},
    {0x7c, "WindowMetrics", "MenuHeight"},
    {0x7d, "WindowMetrics", "MenuFont"},
    {0x7e, "WindowMetrics", "MinWidth"},
    {0x7f, "WindowMetrics", "MinHorzGap"},
    {0x80, "WindowMetrics", "MinVertGap"},
    {0x82, "WindowMetrics", "MinArrange"},
    {0x84, "WindowMetrics", "ScrollWidth"},
    {0x85, "WindowMetrics", "ScrollHeight"},
    {0x86, "Desktop", "IconFont"},
    {0x88, "WindowMetrics", "StatusFont"},
    {0x89, "WindowMetrics", "MessageFont"},
};
const ProfileKey *FindKey(unsigned identifier)
{
    for (const auto &entry : profile_keys)
        if (entry.identifier == identifier)
            return &entry;
    return nullptr;
}
// The broadcast section names the original loads with USER.
const char *SectionName(unsigned identifier)
{
    return identifier == 0x50 ? "Desktop" : identifier == 0x52 ? "(None)" : "";
}
} // namespace
bool Application::WriteSystemProfile(const char *section, const char *key, const char *value)
{
    auto &service = ProfileFiles::System();
    auto *buffer = service.Acquire(nullptr, true, Now() / 1000, files);
    if (!buffer)
        return false;
    unsigned char names[3][264] = {};
    const char *inputs[3] = {section, key, value};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned n = 0; inputs[i] && inputs[i][n] && n < 263; ++n)
            names[i][n] = (unsigned char)inputs[i][n];
    const unsigned result = Win98Profile::WriteString(service, *buffer, names[0], names[1], names[2]);
    if (result)
        service.Flush();
    return result != 0;
}
unsigned Application::SystemParametersWrite(const unsigned *a)
{
    struct WriteEnvironment final : Win98ParametersWrite::Environment
    {
        Application &app;
        User::Settings &s;
        explicit WriteEnvironment(Application &a) : app(a), s(User::SystemSettings()) {}
        void MonitorRect(Win98ParametersWrite::Rect &r) const override
        {
            r = {s.monitor[0], s.monitor[1], s.monitor[2], s.monitor[3]};
        }
        void WorkArea(Win98ParametersWrite::Rect &r) const override
        {
            r = {s.work[0], s.work[1], s.work[2], s.work[3]};
        }
        void SetWorkArea(const Win98ParametersWrite::Rect &asked) override
        {
            // A host that keeps the shell's taskbar out of sight wants the
            // whole monitor as the work area, as the shell itself sets it for
            // a taskbar that hides (Application::SetHostWorkArea). What the
            // shell asked for is kept for the way back.
            Win98ParametersWrite::Rect r = asked;
            if (!app.serving_host_work) HostWorkAsked(&asked);
            if (HostWorkWhole()) r = {s.monitor[0], s.monitor[1], s.monitor[2], s.monitor[3]};
            s.work[0] = short(r.left);
            s.work[1] = short(r.top);
            s.work[2] = short(r.right);
            s.work[3] = short(r.bottom);
        }
        void RecomputeMetrics() override { Win98Metrics::Recompute(s); }
        void RepositionWindows(const Win98ParametersWrite::Rect &) override
        {
            // USER walks the windows of the top level that are no tool
            // windows and places each one anew with DeferWindowPos and one
            // EndDeferWindowPos, SWP_NOZORDER and SWP_NOACTIVATE: a window is
            // asked with WM_WINDOWPOSCHANGING, gets WM_NCCALCSIZE and is told
            // with WM_WINDOWPOSCHANGED and WM_SIZE. The provider does this
            // for the windows that are maximized, with the reconstructed
            // maximized rectangle; the others stay where they are.
            unsigned windows[User::WindowCapacity], count = 0;
            if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count))
                return;
            unsigned positions[64][7], moved = 0;
            for (unsigned i = 0; i < count && moved < 64; ++i)
            {
                const unsigned hwnd = windows[i];
                if (!User::IsWindow(hwnd) || !(User::Style(hwnd) & 0x01000000) || (User::ExtendedStyle(hwnd) & 0x80))
                    continue;
                Gdi::RECT m, now = {};
                if (!User::MaximizedRect(hwnd, m) || !User::GetWindowRect(hwnd, &now) ||
                    (m.left == now.left && m.top == now.top && m.right == now.right && m.bottom == now.bottom))
                    continue;
                const unsigned entry[7] = {hwnd, 0, unsigned(m.left), unsigned(m.top), unsigned(m.right - m.left),
                                           unsigned(m.bottom - m.top), 0x14};
                memcpy(positions[moved++], entry, sizeof entry);
            }
            if (moved)
                app.PositionWindows(moved, positions);
        }
        // USER's two writers store the sizes and fonts of a window's
        // parts and of the icon grid under HKCU\Control Panel\Desktop\
        // WindowMetrics, not in WIN.INI: a size as "%d", in twips and
        // negative for the lengths (-MulDiv(pixels, 1440, dots per inch)), a
        // font as its LOGFONT16 with the height in points
        // (-MulDiv(height, 72, dots per inch) of a negative height). The
        // start reads them back for the font size the display has then
        // (Application::LoadWindowMetrics).
        static bool MetricsValue(unsigned identifier) { return identifier >= 0x40 && identifier != 0x43; }
        static bool Twips(unsigned identifier)
        {
            switch (identifier)
            {
            case 0x40: case 0x41: case 0x74: case 0x75: case 0x76: case 0x78: case 0x79: case 0x7b: case 0x7c:
            case 0x7e: case 0x7f: case 0x80: case 0x84: case 0x85:
                return true;
            default:
                return false;
            }
        }
        bool WriteMetricsValue(const char *name, unsigned type, const void *data, unsigned size)
        {
            auto &registry = Registry::System();
            unsigned key = Registry::Missing;
            bool created = false;
            return !registry.Open(registry.roots[1], "Control Panel\\Desktop\\WindowMetrics", true, key, created) &&
                   !registry.Set(key, name, type, data, size);
        }
        bool WriteProfileInt(unsigned identifier, int value) override
        {
            const auto *entry = FindKey(identifier);
            if (!entry)
                return false;
            if (Twips(identifier))
                value = -Gdi::PixelsToTwips(value);
            char text[16];
            int n = 0, magnitude = value < 0 ? -value : value;
            char digits[12];
            do
            {
                digits[n++] = char('0' + magnitude % 10);
                magnitude /= 10;
            } while (magnitude);
            int out = 0;
            if (value < 0)
                text[out++] = '-';
            while (n)
                text[out++] = digits[--n];
            text[out] = 0;
            if (MetricsValue(identifier))
                return WriteMetricsValue(entry->key, 1, text, unsigned(out) + 1);
            return app.WriteSystemProfile(entry->section, entry->key, text);
        }
        bool WriteProfileFont(unsigned identifier, const unsigned char *logfont) override
        {
            const auto *entry = FindKey(identifier);
            if (!entry || !logfont)
                return false;
            unsigned char stored[50];
            memcpy(stored, logfont, sizeof stored);
            const int height = short(stored[0] | stored[1] << 8);
            if (height < 0)
            {
                const int points = Gdi::DpiMulDiv(-height, 72, Gdi::Dpi().logical_y);
                stored[0] = (unsigned char)points;
                stored[1] = (unsigned char)(points >> 8);
            }
            return WriteMetricsValue(entry->key, 3, stored, sizeof stored);
        }
        bool WriteProfileFont32(unsigned identifier, const unsigned char *logfont) override
        {
            if (!logfont)
                return false;
            unsigned char narrow[50];
            NarrowFont(logfont, narrow);
            return WriteProfileFont(identifier, narrow);
        }
        void ApplyNonClientMetrics(const unsigned char *m) override
        {
            // USER: the fonts first (it makes and measures them), then the
            // sizes, held to the fonts.
            unsigned char narrow[50];
            const unsigned offsets[5] = {0x18, 0x5c, 0xa0, 0xdc, 0x118};
            const unsigned handles[5] = {0x502, 0x503, 0x501, 0x505, 0x506};
            for (unsigned i = 0; i < 5; ++i)
            {
                NarrowFont(m + offsets[i], narrow);
                s.SetFont(handles[i], narrow);
            }
            // What the windows that are open are measured against: the
            // sizing border (SM_CXFRAME, SM_CYFRAME), the heights the caption
            // and the menu bar were set to (SM_CYSIZE, SM_CYMENUSIZE) and the
            // size of a minimized window.
            const int frame_x = app.SystemMetric(32), frame_y = app.SystemMetric(33);
            const int caption = Signed32(m, 0x14) - app.SystemMetric(31), menu = Signed32(m, 0x9c) - app.SystemMetric(55);
            const int minimized_x = app.SystemMetric(57), minimized_y = app.SystemMetric(58);
            User::ApplyNonClientMetrics(
                {unsigned(Signed32(m, 4)), unsigned(Signed32(m, 8)), unsigned(Signed32(m, 0xc)),
                 unsigned(Signed32(m, 0x10)), unsigned(Signed32(m, 0x14)), unsigned(Signed32(m, 0x54)),
                 unsigned(Signed32(m, 0x58)), unsigned(Signed32(m, 0x98)), unsigned(Signed32(m, 0x9c))});
            // USER: the windows that are open get the new frame at once, the
            // minimized ones their new size, and the whole screen is painted
            // again (RDW_INVALIDATE, RDW_ERASE, RDW_ALLCHILDREN, RDW_FRAME).
            const int dx = app.SystemMetric(32) - frame_x, dy = app.SystemMetric(33) - frame_y;
            app.RecalculateFrames(0, dx || dy ? 3 : 2, dx, dy, caption, menu);
            const int wider = app.SystemMetric(57) - minimized_x, higher = app.SystemMetric(58) - minimized_y;
            if (wider || higher)
                app.RecalculateFrames(0, 4, wider, higher, 0, 0);
            User::RedrawWindow(User::GetDesktopWindow(), nullptr, 0, 0x485);
        }
        void ApplyMinimizedMetrics(const unsigned char *m) override
        {
            // Inverse of the implemented read branch for action 43.
            Win98Metrics::ApplyMinimizedMetrics(s, Signed32(m, 4), Signed32(m, 8), Signed32(m, 0xc),
                                                unsigned(Signed32(m, 0x10)));
        }
        void ApplyIconMetrics(const unsigned char *m) override
        {
            s.SetWord(0x32a, (unsigned short)Signed32(m, 4));
            s.SetWord(0x32c, (unsigned short)Signed32(m, 8));
            s.SetWord(0x24c, (unsigned short)Signed32(m, 0xc));
            unsigned char narrow[50];
            NarrowFont(m + 0x10, narrow);
            s.SetFont(0x504, narrow);
        }
        unsigned CreateIconTitleFont(const unsigned char *logfont) override
        {
            return s.SetFont(0x504, logfont) ? 0x504u : 0u;
        }
        unsigned IconTitleFont() const override { return s.Word(0x1b0); }
        void SetIconTitleFont(unsigned) override {}
        void DeleteFont(unsigned) override {}
        bool FontObject(unsigned font, unsigned char (&logfont)[50]) const override
        {
            return s.Font(font, logfont);
        }
        unsigned AppVersion() const override { return s.ExpectedVersion(); }
        void RepaintDesktopSetting(unsigned, unsigned) override {}
        void RepaintDesktop() override { User::InvalidateRect(User::GetDesktopWindow(), nullptr, true); }
        bool SetDeskWallpaper(const Win98ParametersWrite::Parameter &p) override
        {
            return app.SetDesktopWallpaper(p.text, p.sentinel);
        }
        bool SetDeskPattern(const Win98ParametersWrite::Parameter &p) override
        {
            return p.sentinel == 0xffffffffu || p.text != nullptr;
        }
        bool WriteProfileText(unsigned identifier, const char *value) override
        {
            const auto *entry = FindKey(identifier);
            if (!entry)
                return false;
            const char *text = value ? value : "";
            // The pattern and the wallpaper are kept in the registry as well,
            // HKCU\Control Panel\Desktop, where Display Properties reads
            // them back (the Background page of DESKCP16 asks RegQueryValueEx
            // only) and where a clean installation keeps Wallpaper and
            // TileWallpaper beside the [Desktop] section of WIN.INI.
            if (!strcmp(entry->section, "Desktop") && (identifier == 0x02 || identifier == 0x43))
            {
                auto &registry = app.registry.store;
                unsigned key = Registry::Missing;
                bool created = false;
                if (registry.Open(registry.roots[1], "Control Panel\\Desktop", true, key, created) ||
                    registry.Set(key, entry->key, 1, text, unsigned(strlen(text)) + 1))
                    return false;
            }
            return app.WriteSystemProfile(entry->section, entry->key, text);
        }
        bool SectionString(unsigned identifier, char *out, unsigned size) override
        {
            const char *text = SectionName(identifier);
            unsigned n = 0;
            while (text[n] && n + 1 < size)
            {
                out[n] = text[n];
                ++n;
            }
            out[n] = 0;
            return n != 0;
        }
        void Broadcast(unsigned action, const char *section) override
        {
            unsigned address = 0;
            unsigned length = 1;
            while (section[length - 1])
                ++length;
            address = app.Allocate(length);
            if (address)
                app.Store(address, section, length);
            app.BroadcastMessage(Win98Broadcast::Send, 0x1a, action, address);
            if (address)
                app.Free(address);
        }
    };
    WriteEnvironment environment(*this);
    Win98ParametersWrite::Parameter parameter;
    unsigned char buffer[344] = {};
    char text[264] = {};
    unsigned uiParam = a[1];
    parameter.null = a[2] == 0;
    parameter.sentinel = a[2];
    if (a[0] == 20 || a[0] == 21)
    {
        if (a[2] && a[2] != 0xffffffffu && a[2] != 0xfffffffeu)
        {
            unsigned n = 0;
            for (; n + 1 < sizeof text; ++n)
            {
                if (!Read(a[2] + n, text + n, 1))
                    return 0;
                if (!text[n])
                    break;
            }
            text[n] = 0;
            parameter.text = text;
        }
        else
            parameter.null = a[2] == 0;
    }
    else if (a[0] == 34 && a[2])
    {
        // The original Win16 service takes a LOGFONT16 with uiParam 0x32. USER32's
        // thunk narrows the public LOGFONTA; this adapter does the same.
        unsigned char wide[60];
        if (a[1] < 60 || !Read(a[2], wide, sizeof wide))
        {
            last_error = 87;
            return 0;
        }
        NarrowFont(wide, buffer);
        parameter.data = buffer;
        parameter.capacity = 50;
        uiParam = 0x32;
    }
    else if (a[0] == 47 && a[2])
    {
        // USER32's thunk narrows the public RECT to the internal RECT16.
        int wide[4];
        if (!Read(a[2], wide, sizeof wide))
            return 0;
        for (unsigned i = 0; i < 4; ++i)
        {
            buffer[i * 2] = (unsigned char)wide[i];
            buffer[i * 2 + 1] = (unsigned char)(unsigned(wide[i]) >> 8);
        }
        parameter.data = buffer;
        parameter.capacity = 8;
    }
    else if (a[2] && a[2] != 0xffffffffu && a[2] != 0xfffffffeu)
    {
        const unsigned size = a[0] == 42 ? 0x154u : a[0] == 44 ? 0x14u : 0x4cu;
        if (!Read(a[2], buffer, size))
            return 0;
        parameter.data = buffer;
        parameter.capacity = size;
    }
    const int result = Win98ParametersWrite::Write(environment, a[0], uiParam, parameter, a[3]);
    if (result < 0)
    {
        last_error = 120;
        return 0;
    }
    return unsigned(result);
}
