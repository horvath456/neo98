// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_metrics.h"
#include "win98_parameters.h"
namespace User
{
// New display/input environment for NEO98's current 800x600 desktop. These
// defaults are not captured live settings from the original Windows machine.
struct Settings final : Win98Parameters::Environment, Win98Metrics::Environment
{
    unsigned short metrics[82] = {};
    unsigned short expected = 0x400, border = 1;
    // Monitor records contain the display rectangle and the work-area rectangle.
    // The work area is mutable so the shell can reserve taskbar space.
    short monitor[4] = {0, 0, 800, 600}, work[4] = {0, 0, 800, 600};
    // DS:DAh, DCh and DEh: the timeouts in seconds of the screen saver, low
    // power and power off; a negative value is an inactive one (USER). Read at start from HKCU\Control Panel\Desktop.
    short saver[3] = {0, 0, 0};
    // SPI_SCREENSAVERRUNNING (61h) keeps it at DS:28Ch, inverted.
    bool saver_running = false;
    unsigned mouse_speed = 10;
    bool gradient_captions = true;
    // DS:264h, the COOL.DLL module USER loads at start.
    unsigned short plus = 0;
    bool plus_checked = false;
    // The display mode of HKCC\Display\Settings is read once, at the first
    // process of a desktop session (src/x86_user_session.cpp).
    bool display_checked = false;
    // USER-owned wallpaper, independent of the process applying SPI.
    unsigned wallpaper_bitmap = 0, wallpaper_style = 0;
    bool wallpaper_checked = false;
    explicit Settings(unsigned version = 0x400) : expected(version)
    {
        metrics[0] = metrics[78] = 800;
        metrics[1] = metrics[79] = 600;
        metrics[5] = metrics[6] = metrics[19] = metrics[80] = metrics[81] = 1;
        metrics[7] = metrics[8] = metrics[43] = 3;
        metrics[11] = metrics[12] = metrics[13] = metrics[14] = 32;
        metrics[36] = metrics[37] = metrics[68] = metrics[69] = 4;
        metrics[38] = metrics[39] = 75;
        metrics[45] = metrics[46] = 2;
        metrics[47] = metrics[57] = 160;
        metrics[48] = 24;
        metrics[56] = 8;
        metrics[71] = metrics[72] = 13;
        metrics[75] = 1; // Native input delivers mouse-wheel messages.
        DefaultFonts();
        Win98Metrics::ApplyNonClientMetrics(*this, {1, 17, 17, 18, 18, 16, 16, 19, 19});
    }
    void SetWord(unsigned offset, unsigned short value) override
    {
        if (offset >= 0x2de && offset < 0x382 && !(offset & 1))
            metrics[(offset - 0x2de) / 2] = value;
        else if (offset == 0x1014)
            border = value;
        else if (offset >= 0x11b2 && offset < 0x11ba)
            monitor[(offset - 0x11b2) / 2] = short(value);
        else if (offset >= 0x11ba && offset < 0x11c2)
            work[(offset - 0x11ba) / 2] = short(value);
        else if (offset == 0x264)
            plus = value;
        else if (offset == 0xda || offset == 0xdc || offset == 0xde)
            saver[(offset - 0xda) / 2] = short(value);
    }
    unsigned short ExpectedVersion() const override { return expected; }
    // The display of this port can go to low power and off (a black screen
    // until the next input, src/x86_screen_saver.cpp), so VDD function 91h
    // answers both states.
    unsigned PowerStates() const override { return 0xc; }
    unsigned PenWindows() override { return 0; }
    unsigned short Word(unsigned offset) const override
    {
        if (offset >= 0x2de && offset < 0x382 && !(offset & 1))
            return metrics[(offset - 0x2de) / 2];
        if (offset >= 0x11b2 && offset < 0x11ba)
            return (unsigned short)monitor[(offset - 0x11b2) / 2];
        if (offset >= 0x11ba && offset < 0x11c2)
            return (unsigned short)work[(offset - 0x11ba) / 2];
        switch (offset)
        {
        case 0x1a4:
            return 1; // Beep enabled.
        case 0xd8:
            return 31 | (1 << 5); // Keyboard rate and delay.
        case 0xda:
        case 0xdc:
        case 0xde:
            return (unsigned short)saver[(offset - 0xda) / 2];
        case 0x1014:
            return border;
        case 0x24c:
            return 1;
        case 0x264:
            return plus;
        case 0x234:
            return 0; // DragFullWindows is "0" in the local install.
        case 0x6e8:
        case 0x6ea:
            return 4;
        case 0xedc:
            return 7; // Average caption-font width for the current native font profile.
        case 0x86a:
            return 6;
        case 0x86c:
            return 10;
        case 0x86e:
            return 1;
        case 0x1a8:
            return 0x501; // Menu.
        case 0x1ac:
            return 0x502; // Caption.
        case 0x1ae:
            return 0x503; // Small caption.
        case 0x1b0:
            return 0x504; // Icon title.
        case 0x1b2:
            return 0x505; // Status.
        case 0x1b4:
            return 0x506; // Message.
        default:
            return 0;
        }
    }
    bool Font(unsigned handle, unsigned char (&out)[50]) const override
    {
        if (handle < 0x501 || handle > 0x506)
            return false;
        for (unsigned i = 0; i < 50; ++i)
            out[i] = fonts[handle - 0x501][i];
        return true;
    }
    // SPI_SETNONCLIENTMETRICS and SPI_SETICONTITLELOGFONT replace these records.
    // The handles stay synthetic; no GDI object is created for them.
    bool SetFont(unsigned handle, const unsigned char *logfont)
    {
        if (handle < 0x501 || handle > 0x506 || !logfont)
            return false;
        for (unsigned i = 0; i < 50; ++i)
            fonts[handle - 0x501][i] = logfont[i];
        return true;
    }
    unsigned char fonts[6][50] = {};
    void DefaultFonts()
    {
        for (unsigned handle = 0x501; handle <= 0x506; ++handle)
        {
            auto &out = fonts[handle - 0x501];
            for (auto &b : out)
                b = 0;
            const unsigned height = (unsigned short)-11, weight = handle == 0x502 ? 700 : 400;
            out[0] = (unsigned char)height;
            out[1] = height >> 8;
            out[8] = weight;
            out[9] = weight >> 8;
            const char face[] = "MS Sans Serif";
            for (unsigned i = 0; i < sizeof face; ++i)
                out[18 + i] = face[i];
        }
    }
};
Settings &SystemSettings();
} // namespace User
