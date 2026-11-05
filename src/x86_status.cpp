// SPDX-License-Identifier: GPL-2.0-or-later
#include "user_draw.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
unsigned Application::StatusMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    StatusBar *bar = nullptr;
    for (auto &entry : status_bars)
        if (entry.hwnd == hwnd)
            bar = &entry;
    if (!bar && (message == 0x81 || message == 1))
        for (auto &entry : status_bars)
            if (!entry.hwnd)
            {
                entry = StatusBar{};
                entry.hwnd = hwnd;
                bar = &entry;
                User::GetWindowText(hwnd, bar->text[0], 256);
                break;
            }
    if (!bar)
    {
        Fail("Missing native status bar");
        return 0;
    }
    auto refresh = [&]() { User::InvalidateRect(hwnd, nullptr, false); };
    switch (message)
    {
    case 0x83: // COMCTL32 -> -> DefWindowProcA.
        return DefaultWindowMessage(hwnd, message, wp, lp);
    case 0x81:
        return 1;
    case 1:
    case 2:
    case 0xf:
        return 0;
    case 0x82:
        *bar = StatusBar{};
        return 0;
    case 5:
    {
        Gdi::RECT r = {};
        User::GetClientRect(User::Parent(hwnd), &r);
        User::MoveWindow(hwnd, 0, r.bottom - bar->height, r.right, bar->height);
        return 0;
    }
    case 0x30:
        bar->font = wp;
        if (lp)
            refresh();
        return 0;
    case 0x31:
        return bar->font;
    case 0x401:
    case 0x40b:
    {
        unsigned index = wp & 255;
        if (index == 255)
            index = 8;
        if (index != 8 && index >= bar->count)
            return 0;
        if (wp & 0x1000)
        {
            Fail("Owner-drawn status text not implemented");
            return 0;
        }
        char converted[256]{};
        const char *text=nullptr;
        if(message==0x40b){unsigned n=0;for(;n<256;++n){unsigned short ch=0;if(lp&&!Read(lp+n*2,&ch,2))return 0;if(!ch)break;if(n==255)return 0;converted[n]=AnsiNarrow(ch);}text=converted;}
        else text=ReadString(lp);
        if (strlen(text) >= 256)
            return 0;
        strcpy(bar->text[index], text);
        bar->flags[index] = wp & 0xff00;
        refresh();
        return 1;
    }
    case 0x402:
    case 0x403:
    {
        unsigned index = wp & 255;
        if (index == 255)
            index = 8;
        if (index != 8 && index >= bar->count)
            return 0;
        unsigned n = strlen(bar->text[index]);
        if (message == 0x402)
            PutString(lp, bar->text[index]);
        return n | (bar->flags[index] << 16);
    }
    case 0x404:
        if (!wp || wp > 8)
            return 0;
        Read(lp, bar->parts, wp * 4);
        bar->count = wp;
        refresh();
        return 1;
    case 0x406:
        if (lp)
            Store(lp, bar->parts, (wp < bar->count ? wp : bar->count) * 4);
        return bar->count;
    case 0x407:
    {
        unsigned borders[] = {1, 1, 2};
        Store(lp, borders, sizeof borders);
        return 1;
    }
    case 0x408:
        bar->height = wp < 18 ? 20 : wp > 128 ? 128 : wp + 2;
        return 0;
    case 0x409:
        bar->simple = wp;
        refresh();
        return 0;
    case 0x40a:
    {
        if (wp >= bar->count)
            return 0;
        Gdi::RECT client = {};
        User::GetClientRect(hwnd, &client);
        Gdi::RECT r = {wp ? bar->parts[wp - 1] : 0, 0, bar->parts[wp] < 0 ? client.right : bar->parts[wp],
                       client.bottom};
        Store(lp, &r, sizeof r);
        return 1;
    }
    case 0x40e:
        return bar->simple;
    case 0x2006:
        return 0;
    case 0x2005:
        if (wp)
            Fail("Unicode status messages not implemented");
        return 0;
    default:
        if (message >= 0x400)
        {
            char error[96];
            snprintf(error, sizeof error, "Unsupported status message %x", message);
            Fail(error);
            return 0;
        }
        return User::DefaultMessage(hwnd, message, wp, lp);
    }
}
void Application::DrawStatus(unsigned hwnd, Gdi::HDC dc, const Gdi::RECT &bounds)
{
    StatusBar *bar = nullptr;
    for (auto &entry : status_bars)
        if (entry.hwnd == hwnd)
            bar = &entry;
    if (!bar)
        return;
    auto brush = Gdi::CreateSolidBrush(0xc0c0c0);
    Gdi::FillRect(dc, &bounds, brush);
    Gdi::DeleteObject(brush);
    Gdi::SelectObject(dc, bar->font ? bar->font : system);
    Gdi::SetBkMode(dc, Gdi::TRANSPARENT);
    Gdi::SetTextColor(dc, 0);
    int left = 2;
    unsigned count = bar->simple ? 1 : bar->count;
    for (unsigned i = 0; i < count; ++i)
    {
        int right = bar->simple || bar->parts[i] < 0 ? bounds.right - 2 : bar->parts[i];
        Gdi::RECT r = {left, 2, right, bounds.bottom - 2};
        unsigned part = bar->simple ? 8 : i;
        if (!(bar->flags[part] & 0x100))
            Win98Draw::DrawEdge(dc, &r, bar->flags[part] & 0x200 ? 5 : 10, 15);
        Gdi::SaveDC(dc);
        Gdi::IntersectClipRect(dc, left, 2, right, bounds.bottom - 2);
        Gdi::TextOutA(dc, left + 3, 4, bar->text[part], strlen(bar->text[part]));
        Gdi::RestoreDC(dc, -1);
        left = right + 2;
    }
}
} // namespace X86
