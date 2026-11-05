// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gpu_backend.h"
#include "../src/win98_display_modes.h"
#include "host_vulkan.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
int main()
{
    Display *display = XOpenDisplay(nullptr);
    if (!display) return 77;
    const auto window = XCreateSimpleWindow(display, DefaultRootWindow(display), 30, 30, 256, 256, 0, 0, 0);
    XMapWindow(display, window); XSync(display, False);
    auto *gpu = HostVulkan::Install();
    if (!gpu || !HostVulkan::AttachX11(display, window)) { XDestroyWindow(display, window); XCloseDisplay(display); return 77; }
    for (unsigned bits : {16u, 32u})
    {
        std::vector<unsigned char> memory(256 * 256 * (bits / 8), 0xa5);
        Gpu::Surface surface;
        surface.bits = memory.data(); surface.width = surface.height = 256;
        surface.bit_count = bits; surface.pitch = 256 * (bits / 8);
        surface.r_mask = bits == 16 ? 0x7c00 : 0xff0000;
        surface.g_mask = bits == 16 ? 0x3e0 : 0xff00; surface.b_mask = bits == 16 ? 31 : 255;
        Gpu::State state; state.viewport_width = state.viewport_height = 256;
        assert(gpu->Begin(surface, nullptr));
        for (unsigned x = 0; x < 256; ++x) {
            int rect[] = {int(x), 0, int(x + 1), 256};
            unsigned color = 0xff000000 | (x << 16) | ((255 - x) << 8) | (x ^ 0x55);
            gpu->Clear(1, color, 1, 0, rect, 1, state);
        }
        gpu->End();
        assert(HostVulkan::CanPresent(memory.data()));
        assert(HostVulkan::Present(memory.data(), 256, 256));
        for (auto byte : memory) assert(byte == 0xa5); // presentation did not read back
        gpu->Sync(surface, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(40)); XSync(display, False);
        XImage *image = XGetImage(display, window, 0, 0, 256, 256, AllPlanes, ZPixmap);
        assert(image && image->red_mask == 0xff0000 && image->green_mask == 0xff00 && image->blue_mask == 0xff);
        Win98DisplayModes::Surface reference{memory.data(), 256, 256, surface.pitch, int(bits), nullptr};
        for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
            const auto actual = XGetPixel(image, x, y) & 0xffffff;
            const auto expected = Win98DisplayModes::Pixel(reference, x, y);
            if (actual != expected) fprintf(stderr, "pixel %u-bit %d,%d got=%06lx expected=%06x\n", bits, x, y, actual, expected);
            assert(actual == expected);
        }
        XDestroyImage(image);
        // USER first scales the guest mode to its published canvas, then SDL
        // scales that canvas into the host window. Preserve both rounding steps.
        assert(HostVulkan::Present(memory.data(), 256, 256, 160, 120));
        std::this_thread::sleep_for(std::chrono::milliseconds(40)); XSync(display, False);
        image = XGetImage(display, window, 0, 0, 256, 256, AllPlanes, ZPixmap); assert(image);
        std::vector<unsigned char> published(160 * 120 * 4);
        Win98DisplayModes::Publish(reference, published.data(), 160, 120, 160 * 4);
        for (int y = 32; y < 224; ++y) for (int x = 0; x < 256; ++x) {
            const int lx = int((x + .5) / 1.6), ly = int((y - 32 + .5) / 1.6);
            const auto *p = published.data() + (ly * 160 + lx) * 4;
            const unsigned expected = (unsigned(p[2]) << 16) | (unsigned(p[1]) << 8) | p[0];
            assert((XGetPixel(image, x, y) & 0xffffff) == expected);
        }
        assert((XGetPixel(image, 0, 0) & 0xffffff) == 0);
        XDestroyImage(image);
        XResizeWindow(display, window, 320, 240); XSync(display, False);
        assert(HostVulkan::Present(memory.data(), 320, 240));
        XResizeWindow(display, window, 256, 256); XSync(display, False);
        assert(HostVulkan::Present(memory.data(), 256, 256));
        gpu->Sync(surface, true);
        assert(!HostVulkan::CanPresent(memory.data()));
        gpu->Release(surface.bits, memory.size());
    }
    HostVulkan::Detach();
    XDestroyWindow(display, window); XCloseDisplay(display);
    puts("VULKAN PRESENT PASS: exact RGB555/RGB888 pixels, no presentation readback, resize, CPU lock, detach");
}
