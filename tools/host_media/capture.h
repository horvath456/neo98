// SPDX-License-Identifier: GPL-2.0-or-later
// Kodi's window as a picture for the Mac host (tools/host_media/
// capture_macos.mm, ScreenCaptureKit): while a video plays in Media Center
// 98's window, the host draws the latest picture into the video area.
// Kodi keeps playing in its own window behind NEO98; it gets no input from
// here.
#pragma once
#include <functional>
namespace HostMediaCapture
{
// Whether macOS lets NEO98 record the screen. The first time it asks the
// person (the system dialog) and says no for now. Any thread.
bool Allowed();
// Starts recording Kodi's window, or keeps trying while Kodi has none yet.
// Main thread, as often as the host likes.
void Start();
void Stop();
// The width / height of the video Kodi plays (0: unknown): only that part
// of Kodi's window is recorded, at full size. Main thread.
void Aspect(double aspect);
// Hands the latest picture (0xAARRGGBB rows, width and height in pixels)
// to `use` if one newer than `serial` came, without copying it.
bool Latest(const std::function<void(const unsigned *pixels, int width, int height)> &use, unsigned &serial);
}
