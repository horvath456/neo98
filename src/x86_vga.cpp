// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "pc_vga.h"
#include <stdio.h>
namespace X86
{
bool Application::EnsureVga()
{
    if (vga) return true;
    vga = new (std::nothrow) PcVga::Device;
    if (!vga) return false;
    // Check marks a mapping failure fatal. Keep the device alive until its
    // CPU is destroyed, including a partially installed secondary mapping.
    return BindVga();
}
bool Application::BindVga()
{
    if (!Check(cpu->MapDevice(0xa0000, 0x20000,
        [](void *context, unsigned address, unsigned char *bytes, unsigned count, bool write) {
            auto &v = *static_cast<PcVga::Device *>(context);
            for (unsigned i = 0; i < count; ++i)
                if (write) v.Write(address + i, bytes[i]); else bytes[i] = v.Read(address + i);
        }, vga))) return false;
    return true;
}
void Application::EndVga()
{
    if (vga) vga->active = false;
    if (vga_width) User::SetScreenMode(vga_width, vga_height, vga_depth);
    User::SetExclusiveMode(false);
    User::ScreenChanged();
}
bool Application::VgaInterrupt()
{
    const unsigned ax = Register(Reg::Eax) & 65535;
    // DISPDIB Windows 98 checks the BIOS display combination code before it
    // registers DisplayDibWindow. VGA colour is code 8 (INT 10h/1A00h).
    if (ax == 0x1a00)
    {
        if (!EnsureVga()) return false;
        Register(Reg::Eax, (Register(Reg::Eax) & ~255u) | 0x1a);
        Register(Reg::Ebx, (Register(Reg::Ebx) & ~65535u) | 8);
        return true;
    }
    if (!(ax & 0xff00) && ((ax & 0x7f) == 3 || (ax & 0x7f) == 0x12 || (ax & 0x7f) == 0x13))
    {
        if (!EnsureVga()) return false;
        if (!vga->active && (ax & 0x7f) != 3)
            User::ScreenMode(vga_width, vga_height, vga_depth);
        vga->Mode(ax);
        if (log)
        {
            char text[96];
            snprintf(text, sizeof text, "VGA BIOS mode=%02x active=%u", vga->mode, unsigned(vga->active));
            log(text);
        }
        if (!vga->active && vga_width)
            EndVga();
        Register(Reg::Eax, (Register(Reg::Eax) & ~255u) | 0x20);
        return true;
    }
    if ((ax & 0xff00) == 0x0f00 && vga)
    {
        Register(Reg::Eax, (Register(Reg::Eax) & ~65535u) | ((vga->mode == 0x13 ? 40u : 80u) << 8) | vga->mode);
        Register(Reg::Ebx, Register(Reg::Ebx) & ~0xff00u);
        return true;
    }
    return false;
}
bool Application::VgaScanout()
{
    if (!vga || !vga->active) return false;
    const unsigned width = vga->Width(), height = vga->Height();
    // Intermediate CRTC programming may momentarily describe an invalid mode.
    if (!width || !height || width > 1024 || height > 1024) return false;
    int w, h, depth; User::ScreenMode(w, h, depth);
    if (w != int(width) || h != int(height) || depth != 32)
        if (!User::SetVgaScreenMode(width, height)) return false;
    User::SetExclusiveMode(true);
    auto *bits = const_cast<unsigned char *>(User::ScreenBits());
    if (!bits) return false;
    for (unsigned y = 0; y < height; ++y)
    {
        auto *row = reinterpret_cast<unsigned *>(bits + y * User::ScreenStride());
        for (unsigned x = 0; x < width; ++x) row[x] = vga->Pixel(x, y);
    }
    User::PublishScreen();
    return true;
}
}
