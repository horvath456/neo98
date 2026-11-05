// SPDX-License-Identifier: GPL-2.0-or-later
// Kodi's picture in memory for the Linux hosts : the
// GPU copies the newest buffer of the compositor's toplevel
// (tools/host_wayland/compositor.h) into linear pixels, on a thread of its
// own with EGL on the render node, cut to the part the video fills. The CPU
// never reads Kodi's dmabuf, which is slow to read (uncached graphics
// memory). A wl_shm buffer is copied by the CPU.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace HostWayland
{
class Compositor;

class Video
{
  public:
    struct State;

  private:
    std::unique_ptr<State> state;

  public:
    Video();
    ~Video();
    // Opens EGL on the render node and starts the copy thread. Without it
    // (false) Feed falls back to the compositor's own copy.
    bool Start(const std::string &render_node, std::string &error);
    // On the host's main thread after Compositor::Dispatch: hands the
    // newest buffer to the copy thread when it is free, and gives the
    // client back the buffers it is done with. aspect: the video's width /
    // height, only that part of the middle is copied (0: all of it).
    // wanted: false while nothing shows the video, nothing new is copied.
    void Feed(Compositor &, double aspect, bool wanted);
    // Readable when a new picture is ready, for the host's poll (-1: none).
    int Fd() const;
    // Hands the newest picture (0xFFRRGGBB rows without gaps) to `use` when
    // it is newer than `serial`, which it updates. Main thread.
    bool Latest(const std::function<void(const uint32_t *pixels, unsigned width, unsigned height)> &use, unsigned &serial);
    // The serial of the newest picture, 0 for none.
    unsigned Serial() const;
};
}
