// SPDX-License-Identifier: GPL-2.0-or-later
// Where the KMS host (tools/neo98_kms.cpp) shows its picture: the screen
// through DRM/KMS as the only graphics program, without X11 or Wayland, or
// for tests a picture in memory.
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
namespace HostKms
{
// A picture to draw into: 0x00RRGGBB, `stride` pixels from row to row.
struct Frame
{
    uint32_t *pixels = nullptr;
    unsigned width = 0, height = 0, stride = 0;
};

class Output
{
  public:
    virtual ~Output() = default;
    virtual bool Open(std::string &error) = 0;
    // The picture to draw the next frame into, when the last one is shown.
    virtual bool Ready() = 0;
    virtual Frame Back() = 0;
    virtual void Present() = 0;
    // The descriptor that becomes readable when a frame is shown (-1: none).
    virtual int Fd() const { return -1; }
    virtual void Handle() {}
    // Refresh rate in millihertz.
    virtual unsigned Refresh() const { return 60000; }
    // The picture last presented, for screenshots.
    virtual Frame Front() = 0;
    virtual std::string Describe() const = 0;
    // The render node of the same GPU, for clients that render (Kodi).
    virtual std::string RenderNode() const { return {}; }
    // A dmabuf of the screen's size shown as it is, without a copy, until
    // the next Present. False: the output cannot, the host copies.
    struct Buffer
    {
        int fd = -1;
        uint32_t offset = 0, stride = 0, width = 0, height = 0, format = 0;
        uint64_t modifier = 0;
    };
    virtual bool PresentBuffer(const Buffer &) { return false; }
};

// A picture in memory: NEO98_KMS_OUTPUT=image:WIDTHxHEIGHT.
class Image final : public Output
{
    std::vector<uint32_t> back, front;
    unsigned width, height;

  public:
    Image(unsigned w, unsigned h) : width(w), height(h) {}
    bool Open(std::string &) override
    {
        back.assign(size_t(width) * height, 0);
        front = back;
        return true;
    }
    bool Ready() override { return true; }
    Frame Back() override { return {back.data(), width, height, width}; }
    void Present() override { front = back; }
    Frame Front() override { return {front.data(), width, height, width}; }
    std::string Describe() const override { return "image " + std::to_string(width) + "x" + std::to_string(height); }
};

// The screen through DRM/KMS: NEO98 is DRM master, finds a connected output,
// sets its preferred mode (NEO98_KMS_MODE=WIDTHxHEIGHT chooses another) and
// flips between two dumb buffers. NEO98_KMS_CARD names the card.
class Drm final : public Output
{
    struct Dumb
    {
        uint32_t handle = 0, fb = 0, pitch = 0;
        size_t size = 0;
        uint32_t *map = nullptr;
    };
    int fd = -1;
    uint32_t connector = 0, crtc = 0;
    drmModeModeInfo mode{};
    drmModeCrtc *saved = nullptr;
    Dumb buffers[2];
    unsigned back = 1;
    bool flipping = false, refused = false;
    std::string card, render;
    // Framebuffers of a client's dmabufs, by the buffer's file.
    struct Imported { dev_t device = 0; ino_t inode = 0; uint32_t handle = 0, fb = 0; };
    std::vector<Imported> imported;

    static void Flipped(int, unsigned, unsigned, unsigned, void *data) { static_cast<Drm *>(data)->flipping = false; }

    bool Create(Dumb &b, std::string &error)
    {
        drm_mode_create_dumb create{};
        create.width = mode.hdisplay;
        create.height = mode.vdisplay;
        create.bpp = 32;
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create)) { error = "no dumb buffer: " + std::string(strerror(errno)); return false; }
        b.handle = create.handle; b.pitch = create.pitch; b.size = create.size;
        const uint32_t handles[4] = {b.handle}, pitches[4] = {b.pitch}, offsets[4] = {0};
        if (drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b.fb, 0))
        { error = "no framebuffer: " + std::string(strerror(errno)); return false; }
        drm_mode_map_dumb map{};
        map.handle = b.handle;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) { error = "the buffer cannot be mapped"; return false; }
        void *bytes = mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, off_t(map.offset));
        if (bytes == MAP_FAILED) { error = "the buffer cannot be mapped"; return false; }
        b.map = static_cast<uint32_t *>(bytes);
        memset(b.map, 0, b.size);
        return true;
    }
    void Destroy(Dumb &b)
    {
        if (b.map) munmap(b.map, b.size);
        if (b.fb) drmModeRmFB(fd, b.fb);
        if (b.handle) { drm_mode_destroy_dumb destroy{}; destroy.handle = b.handle; drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy); }
        b = {};
    }
    // The first connected connector with a mode, and a CRTC its encoders can drive.
    bool Choose(drmModeRes *resources, std::string &error)
    {
        const char *wanted = getenv("NEO98_KMS_MODE");
        unsigned want_w = 0, want_h = 0;
        if (wanted) sscanf(wanted, "%ux%u", &want_w, &want_h);
        for (int i = 0; i < resources->count_connectors; ++i)
        {
            drmModeConnector *c = drmModeGetConnector(fd, resources->connectors[i]);
            if (!c) continue;
            if (c->connection != DRM_MODE_CONNECTED || !c->count_modes) { drmModeFreeConnector(c); continue; }
            int pick = 0;
            for (int m = 0; m < c->count_modes; ++m)
            {
                if (want_w && c->modes[m].hdisplay == want_w && c->modes[m].vdisplay == want_h) { pick = m; break; }
                if (!want_w && (c->modes[m].type & DRM_MODE_TYPE_PREFERRED)) { pick = m; break; }
            }
            mode = c->modes[pick];
            for (int e = 0; e < c->count_encoders && !crtc; ++e)
            {
                drmModeEncoder *encoder = drmModeGetEncoder(fd, c->encoders[e]);
                if (!encoder) continue;
                if (encoder->encoder_id == c->encoder_id && encoder->crtc_id) crtc = encoder->crtc_id;
                for (int k = 0; k < resources->count_crtcs && !crtc; ++k)
                    if (encoder->possible_crtcs & (1u << k)) crtc = resources->crtcs[k];
                drmModeFreeEncoder(encoder);
            }
            connector = c->connector_id;
            drmModeFreeConnector(c);
            if (crtc) return true;
        }
        error = "no connected screen on " + card;
        return false;
    }

  public:
    ~Drm() override
    {
        if (fd < 0) return;
        while (flipping) Handle();
        // The screen as it was before (the console).
        if (saved)
        {
            drmModeSetCrtc(fd, saved->crtc_id, saved->buffer_id, saved->x, saved->y, &connector, 1, &saved->mode);
            drmModeFreeCrtc(saved);
        }
        for (auto &b : buffers) Destroy(b);
        for (auto &i : imported)
        {
            drmModeRmFB(fd, i.fb);
            drm_gem_close close_handle{i.handle, 0};
            drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close_handle);
        }
        drmDropMaster(fd);
        close(fd);
    }
    bool Open(std::string &error) override
    {
        std::vector<std::string> cards;
        if (const char *chosen = getenv("NEO98_KMS_CARD")) cards.push_back(chosen);
        else for (unsigned i = 0; i < 16; ++i) cards.push_back("/dev/dri/card" + std::to_string(i));
        std::string why = "no graphics card (/dev/dri/card*)";
        for (const auto &path : cards)
        {
            fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (fd < 0) { if (errno != ENOENT) why = path + ": " + strerror(errno); continue; }
            card = path;
            drmModeRes *resources = drmModeGetResources(fd);
            if (!resources) { close(fd); fd = -1; why = path + " has no display"; continue; }
            crtc = 0;
            const bool found = Choose(resources, why);
            drmModeFreeResources(resources);
            if (found) break;
            close(fd); fd = -1;
        }
        if (fd < 0) { error = why; return false; }
        if (!drmIsMaster(fd) && drmSetMaster(fd))
        {
            error = "Another program shows the screen of " + card + " (a desktop or a display server). "
                    "Start neo98-kms from a text console (Ctrl+Alt+F3) without a desktop running.";
            close(fd); fd = -1;
            return false;
        }
        drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
        for (auto &b : buffers) if (!Create(b, error)) return false;
        saved = drmModeGetCrtc(fd, crtc);
        if (drmModeSetCrtc(fd, crtc, buffers[0].fb, 0, 0, &connector, 1, &mode))
        { error = "the mode cannot be set: " + std::string(strerror(errno)); return false; }
        back = 1;
        // The render node of this card, for Kodi's EGL.
        char *name = drmGetRenderDeviceNameFromFd(fd);
        if (name) { render = name; free(name); }
        return true;
    }
    bool Ready() override
    {
        if (flipping)
        {
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 0) > 0) Handle();
        }
        return !flipping;
    }
    void Handle() override
    {
        drmEventContext context{};
        context.version = 2;
        context.page_flip_handler = Flipped;
        drmHandleEvent(fd, &context);
    }
    int Fd() const override { return fd; }
    Frame Back() override { return {buffers[back].map, mode.hdisplay, mode.vdisplay, buffers[back].pitch / 4}; }
    Frame Front() override { return {buffers[back ^ 1].map, mode.hdisplay, mode.vdisplay, buffers[back ^ 1].pitch / 4}; }
    void Present() override
    {
        if (!drmModePageFlip(fd, crtc, buffers[back].fb, DRM_MODE_PAGE_FLIP_EVENT, this)) flipping = true;
        else drmModeSetCrtc(fd, crtc, buffers[back].fb, 0, 0, &connector, 1, &mode);
        back ^= 1;
    }
    unsigned Refresh() const override
    {
        if (!mode.htotal || !mode.vtotal) return 60000;
        return unsigned(uint64_t(mode.clock) * 1000000 / (uint64_t(mode.htotal) * mode.vtotal));
    }
    std::string Describe() const override
    {
        return card + " " + std::to_string(mode.hdisplay) + "x" + std::to_string(mode.vdisplay) + " " + std::to_string(Refresh() / 1000) + " Hz";
    }
    std::string RenderNode() const override { return render; }
    bool PresentBuffer(const Buffer &b) override
    {
        if (refused || b.width != mode.hdisplay || b.height != mode.vdisplay) return false;
        struct stat info{};
        if (fstat(b.fd, &info)) return false;
        uint32_t fb = 0;
        for (const auto &i : imported) if (i.device == info.st_dev && i.inode == info.st_ino) fb = i.fb;
        if (!fb)
        {
            Imported entry{info.st_dev, info.st_ino};
            if (drmPrimeFDToHandle(fd, b.fd, &entry.handle)) { refused = true; return false; }
            const uint32_t handles[4] = {entry.handle}, pitches[4] = {b.stride}, offsets[4] = {b.offset};
            const uint64_t modifiers[4] = {b.modifier};
            if (drmModeAddFB2WithModifiers(fd, b.width, b.height, b.format, handles, pitches, offsets, modifiers, &entry.fb, DRM_MODE_FB_MODIFIERS))
            {
                // A buffer the display engine cannot show (place, layout):
                // copies from now on.
                fprintf(stderr, "NEO98 KMS: Kodi's buffer cannot be shown directly (%s); copying\n", strerror(errno));
                drm_gem_close close_handle{entry.handle, 0};
                drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close_handle);
                refused = true;
                return false;
            }
            if (imported.size() >= 8)
            {
                drmModeRmFB(fd, imported.front().fb);
                drm_gem_close close_handle{imported.front().handle, 0};
                drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close_handle);
                imported.erase(imported.begin());
            }
            imported.push_back(entry);
            fb = entry.fb;
        }
        if (drmModePageFlip(fd, crtc, fb, DRM_MODE_PAGE_FLIP_EVENT, this)) return false;
        flipping = true;
        return true;
    }
};

// NEO98_KMS_OUTPUT: "image:WIDTHxHEIGHT", otherwise the screen.
inline std::unique_ptr<Output> Choose()
{
    const char *chosen = getenv("NEO98_KMS_OUTPUT");
    unsigned w = 0, h = 0;
    if (chosen && sscanf(chosen, "image:%ux%u", &w, &h) == 2 && w >= 320 && h >= 200 && w <= 7680 && h <= 4320)
        return std::make_unique<Image>(w, h);
    return std::make_unique<Drm>();
}

// The guest picture (0x00RRGGBB, rows without gaps) in the middle of the
// frame, as large as it fits with its proportions, black around it. Returns
// where it went, for the pointer and for the video.
struct Placement
{
    int x = 0, y = 0, width = 0, height = 0;
    unsigned guest_width = 0, guest_height = 0;
    // A guest point on the screen, and a screen point in the guest.
    int ScreenX(int gx) const { return x + int(int64_t(gx) * width / int(guest_width ? guest_width : 1)); }
    int ScreenY(int gy) const { return y + int(int64_t(gy) * height / int(guest_height ? guest_height : 1)); }
    int GuestX(int sx) const { return width ? int(int64_t(sx - x) * int(guest_width) / width) : 0; }
    int GuestY(int sy) const { return height ? int(int64_t(sy - y) * int(guest_height) / height) : 0; }
};
inline Placement Place(unsigned gw, unsigned gh, unsigned fw, unsigned fh)
{
    Placement p;
    p.guest_width = gw; p.guest_height = gh;
    if (!gw || !gh) return p;
    p.width = int(fw); p.height = int(uint64_t(gh) * fw / gw);
    if (p.height > int(fh)) { p.height = int(fh); p.width = int(uint64_t(gw) * fh / gh); }
    p.x = (int(fw) - p.width) / 2; p.y = (int(fh) - p.height) / 2;
    return p;
}
struct Box { int left = 0, top = 0, right = 0, bottom = 0; };
// The video in Media Center 98's video area : inside
// `to`, every pixel of the frame in the key colour, and with `fill` every
// pixel outside the guest's placement too, becomes the picture (`pw` x `ph`
// pixels, `stride` from row to row), fitted with its proportions and black
// around it. Anything else the guest drew there stays on top.
inline void DrawKeyed(const Frame &f, const uint32_t *pixels, unsigned pw, unsigned ph, unsigned stride, Box to, uint32_t key,
                      const Placement *fill)
{
    Box clip{std::max(to.left, 0), std::max(to.top, 0), std::min(to.right, int(f.width)), std::min(to.bottom, int(f.height))};
    if (clip.left >= clip.right || clip.top >= clip.bottom) return;
    const Placement inner = pw && ph ? Place(pw, ph, unsigned(to.right - to.left), unsigned(to.bottom - to.top)) : Placement{};
    const int ix = to.left + inner.x, iy = to.top + inner.y;
    std::vector<unsigned> columns(size_t(std::max(inner.width, 0)));
    for (int x = 0; x < inner.width; ++x) columns[size_t(x)] = unsigned(int64_t(x) * pw / inner.width);
    for (int y = clip.top; y < clip.bottom; ++y)
    {
        uint32_t *row = f.pixels + size_t(y) * f.stride;
        const bool inside_y = y >= iy && y < iy + inner.height;
        const uint32_t *source = inside_y ? pixels + size_t(int64_t(y - iy) * ph / inner.height) * stride : nullptr;
        const bool guest_row = fill && y >= fill->y && y < fill->y + fill->height;
        for (int x = clip.left; x < clip.right; ++x)
        {
            const bool outside = fill && (!guest_row || x < fill->x || x >= fill->x + fill->width);
            if (!outside && row[x] != key) continue;
            row[x] = inside_y && x >= ix && x < ix + inner.width ? source[columns[size_t(x - ix)]] & 0xffffff : 0;
        }
    }
}
inline void Draw(const Frame &f, const uint32_t *guest, const Placement &p)
{
    std::vector<unsigned> columns(size_t(p.width));
    for (int x = 0; x < p.width; ++x) columns[size_t(x)] = unsigned(int64_t(x) * p.guest_width / p.width);
    for (unsigned y = 0; y < f.height; ++y)
    {
        uint32_t *row = f.pixels + size_t(y) * f.stride;
        if (int(y) < p.y || int(y) >= p.y + p.height) { memset(row, 0, f.width * 4); continue; }
        const uint32_t *source = guest + size_t(int64_t(int(y) - p.y) * p.guest_height / p.height) * p.guest_width;
        if (p.x > 0) memset(row, 0, size_t(p.x) * 4);
        for (int x = 0; x < p.width; ++x) row[p.x + x] = source[columns[size_t(x)]] & 0xffffff;
        if (p.x + p.width < int(f.width)) memset(row + p.x + p.width, 0, size_t(int(f.width) - p.x - p.width) * 4);
    }
}
}
