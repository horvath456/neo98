// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Gpu { class Backend; }
namespace HostVulkan
{
// Vulkan provider for Direct3D fixed function, programmable shaders and Glide.
// Driver capabilities reflect the provider and optional runtime compiler.
Gpu::Backend *Install();
// Optional desktop presentation on the SDL X11 child. Other hosts and native
// Wayland keep their existing presentation path. Detach before destroying it.
bool AttachX11(void *display, unsigned long window);
void Detach();
bool CanPresent(const unsigned char *bits);
bool Present(const unsigned char *bits, int width, int height, int canvas_width = 0, int canvas_height = 0);
void ReadScanout(const unsigned char *bits);
}
