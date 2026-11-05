// SPDX-License-Identifier: GPL-2.0-or-later
// STATIC image-control behavior. SS_ICON loads its image from the creation
// name and ignores WM_SETTEXT. Other styles use DefaultWindowMessage and
// drawing in src/user.cpp.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
// Store the icon, resize to its dimensions unless SS_CENTERIMAGE is set, and
// invalidate visible controls with erasure. A null icon has zero dimensions.
unsigned Application::StaticSetBitmap(unsigned hwnd, unsigned bitmap)
{
    unsigned info[6] = {};
    int width = 0, height = 0;
    if (bitmap && Gdi::BitmapInfo(bitmap, info))
    {
        width = (int)info[1];
        height = (int)info[2];
    }
    const unsigned previous = User::SetLabelImage(hwnd, bitmap, width, height);
    if (!(User::Style(hwnd) & 0x200) && width && height)
    {
        Gdi::RECT window = {}, client = {};
        User::GetWindowRect(hwnd, &window);
        User::GetClientRect(hwnd, &client);
        const int frame_x = (window.right - window.left) - client.right, frame_y = (window.bottom - window.top) - client.bottom;
        const unsigned a[] = {hwnd, 0, 0, 0, (unsigned)(width + frame_x), (unsigned)(height + frame_y), 0x16}; // SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        Api("SetWindowPos", a);
    }
    return previous;
}

unsigned Application::StaticSetIcon(unsigned hwnd, unsigned icon)
{
    int width = 0, height = 0;
    // SS_BITMAP takes dimensions from GetObject; SS_ICON takes them from the
    // icon.
    if (icon && (User::Style(hwnd) & 0x1f) == 0x0e)
    {
        unsigned info[6] = {};
        if (Gdi::BitmapInfo(icon, info))
        {
            width = (int)info[1];
            height = (int)info[2];
        }
    }
    else if (icon)
        if (const auto *record = FindIcon(icon); record && record->data)
        {
            width = (int)record->data->width;
            height = (int)record->data->height;
        }
    const unsigned previous = User::SetLabelImage(hwnd, icon, width, height);
    const unsigned style = User::Style(hwnd);
    if (!(style & 0x200))
    {
        // Retain the parent's relative position and use
        // SWP_NOZORDER|SWP_NOACTIVATE. Add only the left and top frame
        // extents to the image dimensions.
        Gdi::RECT window = {};
        User::GetWindowRect(hwnd, &window);
        if (width && height)
        {
            const unsigned extended = User::ExtendedStyle(hwnd);
            const int units = int(FrameBorderUnits(style, extended) + ((extended & 0x200) ? 2 : 0));
            unsigned index = 5;
            width += units * int(Api("GetSystemMetrics", &index));
            index = 6;
            height += units * int(Api("GetSystemMetrics", &index));
        }
        Gdi::POINT at = {window.left, window.top};
        if (const unsigned parent = User::Parent(hwnd))
        {
            Gdi::POINT origin = {0, 0};
            User::ScreenToClient(parent, &origin);
            at = {at.x + origin.x, at.y + origin.y};
        }
        const unsigned a[] = {hwnd, 0, (unsigned)at.x, (unsigned)at.y, (unsigned)width, (unsigned)height, 0x14};
        Api("SetWindowPos", a);
        if (!User::IsWindow(hwnd))
            return previous;
    }
    // Invalidate visible controls with background erasure.
    if (User::Style(hwnd) & 0x10000000)
        User::InvalidateRect(hwnd, nullptr, true);
    return previous;
}

// Load the CREATESTRUCT image name. An empty name leaves the control
// unchanged; an initial 0xFF denotes a numeric resource identifier. Try the
// window instance, using LoadImage for SS_REALSIZEIMAGE, then fall back to
// the system instance. Store the result even when null.
void Application::StaticLoadIcon(unsigned hwnd, unsigned create)
{
    if (!create)
        return;
    const unsigned instance = Read32(create + 4), name = Read32(create + 0x24);
    if (!name)
        return;
    const char *text = ReadString(name);
    if (!text || !text[0])
        return;
    unsigned ordinal = 0;
    if ((unsigned char)text[0] == 0xff)
        ordinal = (unsigned char)text[1] | ((unsigned char)text[2] << 8);
    else if (text[0] == '#')
        for (const char *p = text + 1; *p >= '0' && *p <= '9'; ++p)
            ordinal = ordinal * 10 + (unsigned)(*p - '0');
    unsigned icon = 0;
    if (Win16Process() || Win16OwnsInstance(instance & 0xffff))
    {
        // The instance is a 16-bit module word. Load through the USER.174
        // path and convert to the native icon handle.
        char label[64] = {};
        if (!ordinal)
            snprintf(label, sizeof label, "%s", text);
        const unsigned id = ordinal ? 0x8000 | (ordinal & 0x7fff) : 0;
        icon = Win16ImageNative(Win16LoadImage(instance & 0xffff, false, id, ordinal ? nullptr : label));
        if (!icon)
            icon = Win16ImageNative(Win16LoadImage(0, false, id, ordinal ? nullptr : label));
    }
    else
    {
        const unsigned resource = (unsigned char)text[0] == 0xff ? ordinal : name;
        if (User::Style(hwnd) & 0x800)
        {
            const unsigned a[] = {instance, resource, 1, 0, 0, 0};
            icon = Api("LoadImageA", a);
        }
        else
        {
            const unsigned a[] = {instance, resource};
            icon = Api("LoadIconA", a);
        }
        if (!icon)
        {
            const unsigned a[] = {0, resource};
            icon = Api("LoadIconA", a);
        }
    }
    StaticSetIcon(hwnd, icon);
}

unsigned Application::StaticProcedure(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned kind = User::Style(hwnd) & 0x1f;
    // USER sizes etched horizontal/vertical lines to the
    // corresponding edge metric, preserving their other dimension.
    if(message==1&&(kind==0x10||kind==0x11)){
        Gdi::RECT rect{};User::GetClientRect(hwnd,&rect);
        unsigned index=kind==0x10?46:45; // SM_CYEDGE / SM_CXEDGE
        const unsigned edge=Api("GetSystemMetrics",&index);
        const unsigned args[]={hwnd,0,0,0,kind==0x11?edge:unsigned(rect.right),kind==0x10?edge:unsigned(rect.bottom),0x16};
        Api("SetWindowPos",args);return 0;
    }
    // Handle STM_SETICON/GETICON and STM_SETIMAGE/GETIMAGE. Icon and cursor
    // images require SS_ICON; mismatched types return null without changing
    // state. SS_BITMAP stores bitmap images and adopts their dimensions
    // unless SS_CENTERIMAGE is set.
    if (kind == 0xe && (message == 0x172 || message == 0x173))
    {
        if (wp != 0)
            return 0;
        if (message == 0x173)
            return User::LabelImage(hwnd);
        return StaticSetBitmap(hwnd, lp);
    }
    if (kind == 0xe && message == 1)
    {
        // A resource named in the template, "#5002" or a name.
        const unsigned create = lp;
        const unsigned instance = create ? Read32(create + 4) : 0, text = create ? Read32(create + 0x24) : 0;
        if (text)
        {
            unsigned char first[3] = {};
            Read(text, first, 3);
            const unsigned resource = first[0] == 0xff ? (unsigned)(first[1] | first[2] << 8) : text;
            unsigned bitmap = 0;
            if (Win16Process() || Win16OwnsInstance(instance & 0xffff))
            {
                // A 16-bit template ("#137" in SYSDM's wizard pages): the
                // bitmap comes out of the NE module like USER.175 LoadBitmap.
                const char *name = first[0] == 0xff ? nullptr : ReadString(text);
                unsigned id = first[0] == 0xff ? 0x8000 | (resource & 0x7fff) : 0;
                char label[64] = {};
                if (name && name[0] == '#')
                {
                    unsigned value = 0;
                    for (const char *p = name + 1; *p >= '0' && *p <= '9'; ++p)
                        value = value * 10 + unsigned(*p - '0');
                    id = 0x8000 | (value & 0x7fff);
                }
                else if (name)
                    snprintf(label, sizeof label, "%s", name);
                bitmap = Win16LoadBitmap(instance & 0xffff, id, id ? nullptr : label);
            }
            else
            {
                const unsigned a[] = {instance, resource};
                bitmap = Api("LoadBitmapA", a);
            }
            StaticSetIcon(hwnd, bitmap);
        }
        return 0;
    }
    if (message >= 0x170 && message <= 0x173)
    {
        // IMAGE_BITMAP selects SS_BITMAP. Store bitmap and icon images
        // through the same path; DESK.CPL uses this for the Settings monitor
        // image.
        if (kind == 0x0e && (message == 0x172 || message == 0x173))
        {
            if (wp != 0) return 0;
            return message == 0x172 ? StaticSetIcon(hwnd, lp) : User::LabelImage(hwnd);
        }
        if (kind != 3)
            return 0;
        switch (message)
        {
        case 0x170: return StaticSetIcon(hwnd, wp);
        case 0x171: return User::LabelImage(hwnd);
        case 0x172: return wp == 1 || wp == 2 ? StaticSetIcon(hwnd, lp) : 0;
        default: return wp == 1 || wp == 2 ? User::LabelImage(hwnd) : 0;
        }
    }
    // For 32-bit processes, SS_BITMAP loads its creation image first from the
    // window instance, then from the system instance.
    if (kind == 0x0e && message == 1 && !Win16Process() && lp)
    {
        const unsigned instance = Read32(lp + 4), name = Read32(lp + 0x24);
        const char *text = name ? ReadString(name) : nullptr;
        if (text && text[0])
        {
            const unsigned resource = (unsigned char)text[0] == 0xff
                ? ((unsigned char)text[1] | ((unsigned char)text[2] << 8)) : name;
            unsigned a[] = {instance, resource};
            unsigned bitmap = Api("LoadBitmapA", a);
            if (!bitmap) { a[0] = 0; bitmap = Api("LoadBitmapA", a); }
            StaticSetIcon(hwnd, bitmap);
        }
        return 0;
    }
    // WM_PAINT paints the label into the DC of wParam or of
    // BeginPaint. It arrives here when a program's subclass hands it on
    // (src/user.cpp PaintControlSurface).
    if (message == 0xf && User::IsLabel(hwnd))
    {
        User::Paint paint;
        const unsigned dc = wp ? wp : BeginPaintWindow(hwnd, paint);
        if (dc)
        {
            User::PaintLabel(hwnd, dc);
            User::DrawingChanged(dc);
            if (!wp) User::EndPaint(hwnd, paint);
        }
        return 0;
    }
    // WM_ERASEBKGND answers 1 and paints nothing; the paint fills
    // with the brush of WM_CTLCOLORSTATIC.
    if (message == 0x14 && User::IsLabel(hwnd))
        return 1;
    if (kind == 0x0e && message == 0xc)
        return 0;
    if (kind == 3)
    {
        if (message == 1)
        {
            StaticLoadIcon(hwnd, lp);
            return 0;
        }
        if (message == 0xc)
            return 0;
    }
    return DefaultWindowMessage(hwnd, message, wp, lp);
}
} // namespace X86
