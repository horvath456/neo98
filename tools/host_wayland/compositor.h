// SPDX-License-Identifier: GPL-2.0-or-later
// A tiny Wayland compositor for one program, Kodi: NEO98 stays the only
// graphics program on the screen and shows Kodi's picture itself, in full
// screen or in a Windows window. It offers wl_compositor, wl_shm, wl_output,
// an empty wl_seat, xdg_wm_base for one toplevel and zwp_linux_dmabuf_v1
// with linear buffers; Kodi gets no input.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace HostWayland
{
// The last picture of the toplevel, 0x00RRGGBB.
struct Picture
{
    std::vector<uint32_t> pixels;
    unsigned width = 0, height = 0, serial = 0;
};
// The newest buffer as a dmabuf, for a KMS plane without a copy.
struct Scanout
{
    int fd = -1;
    uint32_t offset = 0, stride = 0, width = 0, height = 0, format = 0;
    uint64_t modifier = 0;
    unsigned serial = 0;
};

class Compositor
{
  public:
    struct State;

  private:
    std::unique_ptr<State> state;

  public:
    Compositor();
    ~Compositor();
    // Listens on a socket of its own in XDG_RUNTIME_DIR. render_node names
    // the GPU clients render on (zwp_linux_dmabuf_v1 feedback); output the
    // size and refresh (millihertz) the output announces.
    bool Start(const std::string &render_node, unsigned width, unsigned height, unsigned refresh, std::string &error);
    // The value of WAYLAND_DISPLAY for clients (the socket's name).
    const std::string &Socket() const;
    // XDG_RUNTIME_DIR the socket lies in.
    const std::string &RuntimeDir() const;
    // The render node named to clients, also the one to import their
    // buffers on (tools/host_wayland/video.h).
    const std::string &RenderNode() const;
    int Fd() const;
    // Serves the clients without blocking; sends frame callbacks at the
    // output's refresh, also while nothing shows Kodi.
    void Dispatch();
    // The size the toplevel is asked to have (configure).
    void Configure(unsigned width, unsigned height);
    // The toplevel's picture, or nullptr when it has none. The newest
    // buffer is copied here, at most once per buffer the client commits.
    const Picture *Current();
    // The serial of the newest buffer (0: none), without copying it.
    unsigned Serial() const;
    // The newest buffer when it is a dmabuf.
    bool Buffer(Scanout &) const;
    // Keeps the newest buffer from the client while a plane shows it, and
    // gives one back when the plane shows another.
    void Hold(unsigned serial);
    void Unhold(unsigned serial);
    unsigned Clients() const;
};
}
