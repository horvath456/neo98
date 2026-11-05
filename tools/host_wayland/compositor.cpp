// SPDX-License-Identifier: GPL-2.0-or-later
// The Wayland compositor for Kodi (compositor.h). One client draws one
// toplevel; its buffers (wl_shm or linear dmabufs) are copied when they are
// committed and released at once, so the host draws from its own copy.
#include "compositor.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"
#include <wayland-server.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
namespace HostWayland
{
namespace
{
// The formats clients may hand over, all with the linear layout, which the
// host can read with the CPU.
const uint32_t Formats[] = {DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888, DRM_FORMAT_XBGR8888, DRM_FORMAT_ABGR8888};

struct Dmabuf
{
    int fd = -1;
    uint32_t offset = 0, stride = 0, width = 0, height = 0, format = 0;
    uint64_t modifier = DRM_FORMAT_MOD_INVALID;
    void *map = nullptr;
    size_t size = 0;
    ~Dmabuf()
    {
        if (map) munmap(map, size);
        if (fd >= 0) close(fd);
    }
};
struct Params
{
    Dmabuf planes[4];
    bool used = false;
};
struct Surface;
struct Toplevel
{
    wl_resource *resource = nullptr, *xdg = nullptr;
    Surface *surface = nullptr;
};
struct Surface;
// Notices a pending buffer destroyed before the commit.
struct BufferGone
{
    wl_listener listener{};
    Surface *surface = nullptr;
};
struct Surface
{
    wl_resource *resource = nullptr;
    wl_resource *pending_buffer = nullptr;
    bool attached = false;
    BufferGone buffer_gone;
    std::vector<wl_resource *> pending_frames, frames;
    Toplevel *toplevel = nullptr;
    bool configured = false;
};
} // namespace

struct Compositor::State
{
    wl_display *display = nullptr;
    wl_event_loop *loop = nullptr;
    std::string socket, runtime, node;
    dev_t device = 0;
    unsigned width = 1280, height = 720, refresh = 60000;
    int table_fd = -1;
    uint32_t table_size = 0;
    Picture picture;
    bool mapped = false;
    // The newest committed buffer and the one a plane shows: the client
    // gets a buffer back when it is neither.
    struct Held
    {
        wl_listener gone{};
        wl_resource *buffer = nullptr;
        unsigned serial = 0;
    };
    wl_resource *latest = nullptr;
    unsigned latest_serial = 0, serials = 0;
    wl_listener latest_gone{};
    std::vector<Held *> held;
    bool Holds(wl_resource *buffer) const { for (auto *h : held) if (h->buffer == buffer) return true; return false; }
    Toplevel *toplevel = nullptr;
    std::vector<Surface *> surfaces;
    std::vector<wl_resource *> outputs;
    std::chrono::steady_clock::time_point next_frame = std::chrono::steady_clock::now();
    unsigned clients = 0;
    wl_listener client_created{};

    void Copy(wl_resource *buffer);
    void Latest(wl_resource *buffer);
    void Release(wl_resource *buffer);
    void Unmap();
    void SendConfigure();
    void FrameDone();
};

namespace
{
Compositor::State *&Active()
{
    static Compositor::State *state = nullptr;
    return state;
}

uint32_t Now()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// --- wl_buffer of a dmabuf ---------------------------------------------------
const struct wl_buffer_interface dmabuf_buffer_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
};
void DestroyDmabuf(wl_resource *r) { delete static_cast<Dmabuf *>(wl_resource_get_user_data(r)); }
bool IsDmabuf(wl_resource *r) { return wl_resource_instance_of(r, &wl_buffer_interface, &dmabuf_buffer_impl); }

// --- regions, subsurfaces, positioners: accepted and not used ---------------
const struct wl_region_interface region_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
    [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
};
const struct wl_subsurface_interface subsurface_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *, wl_resource *, int32_t, int32_t) {},
    [](wl_client *, wl_resource *, wl_resource *) {},
    [](wl_client *, wl_resource *, wl_resource *) {},
    [](wl_client *, wl_resource *) {},
    [](wl_client *, wl_resource *) {},
};
const struct wl_subcompositor_interface subcompositor_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *client, wl_resource *r, uint32_t id, wl_resource *, wl_resource *) {
        wl_resource *sub = wl_resource_create(client, &wl_subsurface_interface, wl_resource_get_version(r), id);
        if (sub) wl_resource_set_implementation(sub, &subsurface_impl, nullptr, nullptr);
    },
};
const struct xdg_positioner_interface positioner_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *, wl_resource *, int32_t, int32_t) {},
    [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
    [](wl_client *, wl_resource *, uint32_t) {},
    [](wl_client *, wl_resource *, uint32_t) {},
    [](wl_client *, wl_resource *, uint32_t) {},
    [](wl_client *, wl_resource *, int32_t, int32_t) {},
    [](wl_client *, wl_resource *) {},
    [](wl_client *, wl_resource *, int32_t, int32_t) {},
    [](wl_client *, wl_resource *, uint32_t) {},
};
const struct xdg_popup_interface popup_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *, wl_resource *, wl_resource *, uint32_t) {},
    [](wl_client *, wl_resource *, wl_resource *, uint32_t) {},
};

// --- wl_seat without devices --------------------------------------------------
const struct wl_pointer_interface pointer_impl = {
    [](wl_client *, wl_resource *, uint32_t, wl_resource *, int32_t, int32_t) {},
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
};
const struct wl_keyboard_interface keyboard_impl = {[](wl_client *, wl_resource *r) { wl_resource_destroy(r); }};
const struct wl_touch_interface touch_impl = {[](wl_client *, wl_resource *r) { wl_resource_destroy(r); }};
const struct wl_seat_interface seat_impl = {
    [](wl_client *client, wl_resource *r, uint32_t id) {
        if (wl_resource *p = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(r), id))
            wl_resource_set_implementation(p, &pointer_impl, nullptr, nullptr);
    },
    [](wl_client *client, wl_resource *r, uint32_t id) {
        if (wl_resource *k = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(r), id))
            wl_resource_set_implementation(k, &keyboard_impl, nullptr, nullptr);
    },
    [](wl_client *client, wl_resource *r, uint32_t id) {
        if (wl_resource *t = wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(r), id))
            wl_resource_set_implementation(t, &touch_impl, nullptr, nullptr);
    },
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
};
void BindSeat(wl_client *client, void *, uint32_t version, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &wl_seat_interface, int(std::min(version, 5u)), id);
    if (!r) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &seat_impl, nullptr, nullptr);
    wl_seat_send_capabilities(r, 0);
    if (version >= 2) wl_seat_send_name(r, "seat0");
}

// --- wl_output ----------------------------------------------------------------
const struct wl_output_interface output_impl = {[](wl_client *, wl_resource *r) { wl_resource_destroy(r); }};
void SendOutput(Compositor::State *s, wl_resource *r)
{
    wl_output_send_geometry(r, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN, "NEO98", "Windows 98", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, int32_t(s->width), int32_t(s->height), int32_t(s->refresh));
    if (wl_resource_get_version(r) >= 2) { wl_output_send_scale(r, 1); wl_output_send_done(r); }
}
void BindOutput(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto *s = static_cast<Compositor::State *>(data);
    wl_resource *r = wl_resource_create(client, &wl_output_interface, int(std::min(version, 3u)), id);
    if (!r) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &output_impl, s, [](wl_resource *r) {
        auto *s = static_cast<Compositor::State *>(wl_resource_get_user_data(r));
        s->outputs.erase(std::remove(s->outputs.begin(), s->outputs.end(), r), s->outputs.end());
    });
    s->outputs.push_back(r);
    SendOutput(s, r);
}

// --- wl_surface -----------------------------------------------------------------
Surface *SurfaceOf(wl_resource *r) { return static_cast<Surface *>(wl_resource_get_user_data(r)); }
void ForgetBuffer(Surface *s)
{
    if (s->pending_buffer) wl_list_remove(&s->buffer_gone.listener.link);
    s->pending_buffer = nullptr;
}
const struct wl_surface_interface surface_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *, wl_resource *r, wl_resource *buffer, int32_t, int32_t) {
        Surface *s = SurfaceOf(r);
        ForgetBuffer(s);
        s->pending_buffer = buffer;
        s->attached = true;
        if (buffer)
        {
            s->buffer_gone.surface = s;
            s->buffer_gone.listener.notify = [](wl_listener *listener, void *) {
                Surface *s = reinterpret_cast<BufferGone *>(listener)->surface;
                wl_list_remove(&s->buffer_gone.listener.link);
                s->pending_buffer = nullptr;
            };
            wl_resource_add_destroy_listener(buffer, &s->buffer_gone.listener);
        }
    },
    [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
    [](wl_client *client, wl_resource *r, uint32_t id) {
        wl_resource *callback = wl_resource_create(client, &wl_callback_interface, 1, id);
        if (!callback) { wl_client_post_no_memory(client); return; }
        Surface *s = SurfaceOf(r);
        wl_resource_set_implementation(callback, nullptr, s, [](wl_resource *c) {
            // A callback whose surface is gone is in no list any more.
            auto *state = Active();
            if (!state) return;
            for (Surface *s : state->surfaces)
            {
                s->pending_frames.erase(std::remove(s->pending_frames.begin(), s->pending_frames.end(), c), s->pending_frames.end());
                s->frames.erase(std::remove(s->frames.begin(), s->frames.end(), c), s->frames.end());
            }
        });
        s->pending_frames.push_back(callback);
    },
    [](wl_client *, wl_resource *, wl_resource *) {},
    [](wl_client *, wl_resource *, wl_resource *) {},
    [](wl_client *, wl_resource *r) {
        Surface *s = SurfaceOf(r);
        auto *state = Active();
        s->frames.insert(s->frames.end(), s->pending_frames.begin(), s->pending_frames.end());
        s->pending_frames.clear();
        if (!s->attached) return;
        s->attached = false;
        wl_resource *buffer = s->pending_buffer;
        ForgetBuffer(s);
        const bool shown = s->toplevel && s->toplevel == state->toplevel && s->configured;
        if (shown && buffer) state->Latest(buffer);
        else if (shown) state->Unmap();
        else if (buffer) wl_buffer_send_release(buffer);
    },
    [](wl_client *, wl_resource *, int32_t) {},
    [](wl_client *, wl_resource *, int32_t) {},
    [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
};
void DestroySurface(wl_resource *r)
{
    Surface *s = SurfaceOf(r);
    auto *state = Active();
    ForgetBuffer(s);
    for (wl_resource *c : s->pending_frames) wl_resource_set_user_data(c, nullptr);
    for (wl_resource *c : s->frames) wl_resource_set_user_data(c, nullptr);
    if (state)
    {
        state->surfaces.erase(std::remove(state->surfaces.begin(), state->surfaces.end(), s), state->surfaces.end());
        if (s->toplevel) s->toplevel->surface = nullptr;
        if (s->toplevel && s->toplevel == state->toplevel) state->Unmap();
    }
    delete s;
}
const struct wl_compositor_interface compositor_impl = {
    [](wl_client *client, wl_resource *r, uint32_t id) {
        wl_resource *surface = wl_resource_create(client, &wl_surface_interface, wl_resource_get_version(r), id);
        if (!surface) { wl_client_post_no_memory(client); return; }
        auto *s = new Surface;
        s->resource = surface;
        Active()->surfaces.push_back(s);
        wl_resource_set_implementation(surface, &surface_impl, s, DestroySurface);
    },
    [](wl_client *client, wl_resource *r, uint32_t id) {
        wl_resource *region = wl_resource_create(client, &wl_region_interface, wl_resource_get_version(r), id);
        if (region) wl_resource_set_implementation(region, &region_impl, nullptr, nullptr);
    },
};

// --- xdg_shell: one toplevel ------------------------------------------------------
const struct xdg_toplevel_interface toplevel_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *, wl_resource *, wl_resource *) {},
    [](wl_client *, wl_resource *, const char *title) { fprintf(stderr, "NEO98 HOST compositor title %s\n", title); },
    [](wl_client *, wl_resource *, const char *) {},
    [](wl_client *, wl_resource *, wl_resource *, uint32_t, int32_t, int32_t) {},
    [](wl_client *, wl_resource *, wl_resource *, uint32_t) {},
    [](wl_client *, wl_resource *, wl_resource *, uint32_t, uint32_t) {},
    [](wl_client *, wl_resource *, int32_t, int32_t) {},
    [](wl_client *, wl_resource *, int32_t, int32_t) {},
    [](wl_client *, wl_resource *) { Active()->SendConfigure(); },
    [](wl_client *, wl_resource *) { Active()->SendConfigure(); },
    [](wl_client *, wl_resource *, wl_resource *) { Active()->SendConfigure(); },
    [](wl_client *, wl_resource *) { Active()->SendConfigure(); },
    [](wl_client *, wl_resource *) {},
};
void DestroyToplevel(wl_resource *r)
{
    auto *t = static_cast<Toplevel *>(wl_resource_get_user_data(r));
    auto *state = Active();
    if (state && state->toplevel == t) { state->Unmap(); state->toplevel = nullptr; }
    if (t->surface) t->surface->toplevel = nullptr;
    delete t;
}
const struct xdg_surface_interface xdg_surface_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *client, wl_resource *r, uint32_t id) {
        auto *surface = static_cast<Surface *>(wl_resource_get_user_data(r));
        wl_resource *top = wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(r), id);
        if (!top || !surface) { wl_client_post_no_memory(client); return; }
        auto *t = new Toplevel;
        t->resource = top; t->xdg = r; t->surface = surface;
        surface->toplevel = t;
        wl_resource_set_implementation(top, &toplevel_impl, t, DestroyToplevel);
        auto *state = Active();
        // The newest toplevel is the one shown; Kodi has one.
        state->Unmap();
        state->toplevel = t;
        state->SendConfigure();
    },
    [](wl_client *client, wl_resource *r, uint32_t id, wl_resource *, wl_resource *) {
        wl_resource *popup = wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(r), id);
        if (!popup) return;
        wl_resource_set_implementation(popup, &popup_impl, nullptr, nullptr);
        xdg_popup_send_popup_done(popup);
    },
    [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) {},
    [](wl_client *, wl_resource *r, uint32_t) {
        if (auto *surface = static_cast<Surface *>(wl_resource_get_user_data(r))) surface->configured = true;
    },
};
const struct xdg_wm_base_interface wm_base_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *client, wl_resource *r, uint32_t id) {
        wl_resource *p = wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(r), id);
        if (p) wl_resource_set_implementation(p, &positioner_impl, nullptr, nullptr);
    },
    [](wl_client *client, wl_resource *r, uint32_t id, wl_resource *surface) {
        wl_resource *x = wl_resource_create(client, &xdg_surface_interface, wl_resource_get_version(r), id);
        if (!x) { wl_client_post_no_memory(client); return; }
        Surface *s = SurfaceOf(surface);
        wl_resource_set_implementation(x, &xdg_surface_impl, s, [](wl_resource *x) {
            auto *state = Active();
            if (!state) return;
            for (Surface *s : state->surfaces) if (s->toplevel && s->toplevel->xdg == x) s->toplevel->xdg = nullptr;
        });
    },
    [](wl_client *, wl_resource *, uint32_t) {},
};

// --- zwp_linux_dmabuf_v1 ------------------------------------------------------------
void CreateBuffer(wl_client *client, wl_resource *params, uint32_t id, int32_t width, int32_t height, uint32_t format, bool immediate)
{
    auto *p = static_cast<Params *>(wl_resource_get_user_data(params));
    if (p->used) { wl_resource_post_error(params, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "params already used"); return; }
    p->used = true;
    auto fail = [&](const char *why) {
        fprintf(stderr, "NEO98 HOST compositor dmabuf refused: %s\n", why);
        if (immediate) wl_resource_post_error(params, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER, "%s", why);
        else zwp_linux_buffer_params_v1_send_failed(params);
    };
    Dmabuf &plane = p->planes[0];
    const bool known = std::find(std::begin(Formats), std::end(Formats), format) != std::end(Formats);
    if (plane.fd < 0) return fail("no plane");
    if (!known) return fail("format");
    if (plane.modifier != DRM_FORMAT_MOD_LINEAR && plane.modifier != DRM_FORMAT_MOD_INVALID) return fail("modifier");
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || plane.stride < uint32_t(width) * 4) return fail("size");
    auto *buffer = new Dmabuf;
    *buffer = {};
    buffer->fd = plane.fd; plane.fd = -1;
    buffer->offset = plane.offset; buffer->stride = plane.stride;
    buffer->width = uint32_t(width); buffer->height = uint32_t(height); buffer->format = format;
    buffer->modifier = plane.modifier;
    wl_resource *r = wl_resource_create(client, &wl_buffer_interface, 1, id);
    if (!r) { delete buffer; wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &dmabuf_buffer_impl, buffer, DestroyDmabuf);
    if (!immediate) zwp_linux_buffer_params_v1_send_created(params, r);
}
const struct zwp_linux_buffer_params_v1_interface params_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *, wl_resource *r, int32_t fd, uint32_t index, uint32_t offset, uint32_t stride, uint32_t hi, uint32_t lo) {
        auto *p = static_cast<Params *>(wl_resource_get_user_data(r));
        if (index >= 4 || p->planes[index].fd >= 0)
        { close(fd); wl_resource_post_error(r, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "plane index"); return; }
        auto &plane = p->planes[index];
        plane.fd = fd; plane.offset = offset; plane.stride = stride;
        plane.modifier = uint64_t(hi) << 32 | lo;
    },
    [](wl_client *client, wl_resource *r, int32_t w, int32_t h, uint32_t format, uint32_t) { CreateBuffer(client, r, 0, w, h, format, false); },
    [](wl_client *client, wl_resource *r, uint32_t id, int32_t w, int32_t h, uint32_t format, uint32_t) { CreateBuffer(client, r, id, w, h, format, true); },
};
const struct zwp_linux_dmabuf_feedback_v1_interface feedback_impl = {[](wl_client *, wl_resource *r) { wl_resource_destroy(r); }};
void SendFeedback(Compositor::State *s, wl_resource *f)
{
    wl_array device;
    wl_array_init(&device);
    *static_cast<dev_t *>(wl_array_add(&device, sizeof(dev_t))) = s->device;
    zwp_linux_dmabuf_feedback_v1_send_format_table(f, s->table_fd, s->table_size);
    zwp_linux_dmabuf_feedback_v1_send_main_device(f, &device);
    zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(f, &device);
    wl_array indices;
    wl_array_init(&indices);
    for (uint16_t i = 0; i < std::size(Formats); ++i) *static_cast<uint16_t *>(wl_array_add(&indices, sizeof(uint16_t))) = i;
    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(f, &indices);
    zwp_linux_dmabuf_feedback_v1_send_tranche_flags(f, 0);
    zwp_linux_dmabuf_feedback_v1_send_tranche_done(f);
    zwp_linux_dmabuf_feedback_v1_send_done(f);
    wl_array_release(&indices);
    wl_array_release(&device);
}
void NewFeedback(wl_client *client, wl_resource *r, uint32_t id)
{
    wl_resource *f = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(r), id);
    if (!f) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(f, &feedback_impl, nullptr, nullptr);
    SendFeedback(Active(), f);
}
const struct zwp_linux_dmabuf_v1_interface dmabuf_impl = {
    [](wl_client *, wl_resource *r) { wl_resource_destroy(r); },
    [](wl_client *client, wl_resource *r, uint32_t id) {
        wl_resource *p = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(r), id);
        if (!p) { wl_client_post_no_memory(client); return; }
        wl_resource_set_implementation(p, &params_impl, new Params, [](wl_resource *p) {
            auto *params = static_cast<Params *>(wl_resource_get_user_data(p));
            for (auto &plane : params->planes) if (plane.fd >= 0) { close(plane.fd); plane.fd = -1; }
            delete params;
        });
    },
    [](wl_client *client, wl_resource *r, uint32_t id) { NewFeedback(client, r, id); },
    [](wl_client *client, wl_resource *r, uint32_t id, wl_resource *) { NewFeedback(client, r, id); },
};
void BindDmabuf(wl_client *client, void *, uint32_t version, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, int(std::min(version, 4u)), id);
    if (!r) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &dmabuf_impl, nullptr, nullptr);
    if (version < 4)
        for (uint32_t format : Formats)
        {
            if (version >= 3) zwp_linux_dmabuf_v1_send_modifier(r, format, uint32_t(DRM_FORMAT_MOD_LINEAR >> 32), uint32_t(DRM_FORMAT_MOD_LINEAR));
            else zwp_linux_dmabuf_v1_send_format(r, format);
        }
}
void BindCompositor(wl_client *client, void *, uint32_t version, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &wl_compositor_interface, int(std::min(version, 4u)), id);
    if (r) wl_resource_set_implementation(r, &compositor_impl, nullptr, nullptr);
}
void BindSubcompositor(wl_client *client, void *, uint32_t, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &wl_subcompositor_interface, 1, id);
    if (r) wl_resource_set_implementation(r, &subcompositor_impl, nullptr, nullptr);
}
void BindWmBase(wl_client *client, void *, uint32_t version, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &xdg_wm_base_interface, int(std::min(version, 2u)), id);
    if (r) wl_resource_set_implementation(r, &wm_base_impl, nullptr, nullptr);
}
} // namespace

void Compositor::State::SendConfigure()
{
    if (!toplevel || !toplevel->resource || !toplevel->xdg) return;
    wl_array states;
    wl_array_init(&states);
    *static_cast<uint32_t *>(wl_array_add(&states, sizeof(uint32_t))) = XDG_TOPLEVEL_STATE_FULLSCREEN;
    *static_cast<uint32_t *>(wl_array_add(&states, sizeof(uint32_t))) = XDG_TOPLEVEL_STATE_ACTIVATED;
    xdg_toplevel_send_configure(toplevel->resource, int32_t(width), int32_t(height), &states);
    wl_array_release(&states);
    xdg_surface_send_configure(toplevel->xdg, wl_display_next_serial(display));
}

// Gives a buffer back to its client unless it is still the newest or held.
void Compositor::State::Release(wl_resource *buffer)
{
    if (buffer && buffer != latest && !Holds(buffer)) wl_buffer_send_release(buffer);
}
void Compositor::State::Latest(wl_resource *buffer)
{
    wl_resource *previous = latest;
    if (latest) wl_list_remove(&latest_gone.link);
    latest = buffer;
    latest_serial = ++serials;
    latest_gone.notify = [](wl_listener *, void *) {
        auto *s = Active();
        wl_list_remove(&s->latest_gone.link);
        s->latest = nullptr;
    };
    wl_resource_add_destroy_listener(buffer, &latest_gone);
    mapped = true;
    if (previous != buffer) Release(previous);
}
void Compositor::State::Unmap()
{
    wl_resource *previous = latest;
    if (latest) wl_list_remove(&latest_gone.link);
    latest = nullptr;
    latest_serial = 0;
    mapped = false;
    picture = {};
    Release(previous);
}
void Compositor::State::Copy(wl_resource *buffer)
{
    auto store = [&](const uint8_t *bytes, unsigned w, unsigned h, unsigned stride, uint32_t format) {
        picture.width = w; picture.height = h;
        picture.pixels.resize(size_t(w) * h);
        const bool swap = format == DRM_FORMAT_XBGR8888 || format == DRM_FORMAT_ABGR8888;
        for (unsigned y = 0; y < h; ++y)
        {
            const uint32_t *row = reinterpret_cast<const uint32_t *>(bytes + size_t(y) * stride);
            uint32_t *out = &picture.pixels[size_t(y) * w];
            if (!swap) for (unsigned x = 0; x < w; ++x) out[x] = row[x] & 0xffffff;
            else for (unsigned x = 0; x < w; ++x) { const uint32_t p = row[x]; out[x] = (p & 0xff) << 16 | (p & 0xff00) | (p >> 16 & 0xff); }
        }
        picture.serial = latest_serial;
    };
    if (wl_shm_buffer *shm = wl_shm_buffer_get(buffer))
    {
        const uint32_t format = wl_shm_buffer_get_format(shm);
        wl_shm_buffer_begin_access(shm);
        store(static_cast<const uint8_t *>(wl_shm_buffer_get_data(shm)), unsigned(wl_shm_buffer_get_width(shm)), unsigned(wl_shm_buffer_get_height(shm)),
              unsigned(wl_shm_buffer_get_stride(shm)), format == WL_SHM_FORMAT_ARGB8888 || format == WL_SHM_FORMAT_XRGB8888 ? DRM_FORMAT_XRGB8888 : format);
        wl_shm_buffer_end_access(shm);
        return;
    }
    if (!IsDmabuf(buffer)) return;
    auto *d = static_cast<Dmabuf *>(wl_resource_get_user_data(buffer));
    if (!d->map)
    {
        d->size = size_t(d->offset) + size_t(d->stride) * d->height;
        void *map = mmap(nullptr, d->size, PROT_READ, MAP_SHARED, d->fd, 0);
        if (map == MAP_FAILED) { fprintf(stderr, "NEO98 HOST compositor dmabuf cannot be read: %s\n", strerror(errno)); return; }
        d->map = map;
    }
    dma_buf_sync sync{DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ};
    ioctl(d->fd, DMA_BUF_IOCTL_SYNC, &sync);
    store(static_cast<const uint8_t *>(d->map) + d->offset, d->width, d->height, d->stride, d->format);
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    ioctl(d->fd, DMA_BUF_IOCTL_SYNC, &sync);
}

void Compositor::State::FrameDone()
{
    const uint32_t now = Now();
    for (Surface *s : surfaces)
    {
        for (wl_resource *c : s->frames)
        {
            wl_resource_set_user_data(c, nullptr);
            wl_callback_send_done(c, now);
            wl_resource_destroy(c);
        }
        s->frames.clear();
    }
}

Compositor::Compositor() : state(std::make_unique<State>()) {}
Compositor::~Compositor()
{
    if (state->display)
    {
        wl_display_destroy_clients(state->display);
        wl_display_destroy(state->display);
    }
    if (state->table_fd >= 0) close(state->table_fd);
    if (Active() == state.get()) Active() = nullptr;
}

bool Compositor::Start(const std::string &render_node, unsigned width, unsigned height, unsigned refresh, std::string &error)
{
    State &s = *state;
    s.width = width; s.height = height; s.refresh = refresh ? refresh : 60000;
    // The GPU clients render on: the render node of the screen's card, or
    // the first one (the picture in memory has no card).
    std::string node = render_node;
    if (node.empty()) for (unsigned i = 128; i < 136 && node.empty(); ++i) if (!access(("/dev/dri/renderD" + std::to_string(i)).c_str(), F_OK)) node = "/dev/dri/renderD" + std::to_string(i);
    struct stat info{};
    if (!node.empty() && !stat(node.c_str(), &info)) s.device = info.st_rdev;
    s.node = node;
    // The format table of zwp_linux_dmabuf_feedback_v1: format, padding, modifier.
    s.table_fd = memfd_create("neo98-dmabuf-formats", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    std::vector<uint8_t> table;
    for (uint32_t format : Formats)
    {
        uint8_t entry[16] = {};
        const uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
        memcpy(entry, &format, 4);
        memcpy(entry + 8, &modifier, 8);
        table.insert(table.end(), entry, entry + 16);
    }
    s.table_size = uint32_t(table.size());
    if (s.table_fd < 0 || write(s.table_fd, table.data(), table.size()) != ssize_t(table.size()))
    { error = "no memory file for the format table"; return false; }
    fcntl(s.table_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    // A console without logind has no XDG_RUNTIME_DIR: one of our own.
    if (const char *runtime = getenv("XDG_RUNTIME_DIR"); runtime && *runtime) s.runtime = runtime;
    else
    {
        s.runtime = "/tmp/neo98-runtime-" + std::to_string(getuid());
        mkdir(s.runtime.c_str(), 0700);
        setenv("XDG_RUNTIME_DIR", s.runtime.c_str(), 1);
    }
    s.display = wl_display_create();
    if (!s.display) { error = "wl_display_create failed"; return false; }
    s.socket = "neo98-kodi-" + std::to_string(getpid());
    if (wl_display_add_socket(s.display, s.socket.c_str())) { error = "no socket " + s.runtime + "/" + s.socket; return false; }
    Active() = &s;
    wl_display_init_shm(s.display);
    if (!wl_global_create(s.display, &wl_compositor_interface, 4, &s, BindCompositor) ||
        !wl_global_create(s.display, &wl_subcompositor_interface, 1, &s, BindSubcompositor) ||
        !wl_global_create(s.display, &wl_output_interface, 3, &s, BindOutput) ||
        !wl_global_create(s.display, &wl_seat_interface, 5, &s, BindSeat) ||
        !wl_global_create(s.display, &xdg_wm_base_interface, 2, &s, BindWmBase) ||
        (s.device && !wl_global_create(s.display, &zwp_linux_dmabuf_v1_interface, 4, &s, BindDmabuf)))
    { error = "the globals cannot be created"; return false; }
    s.client_created.notify = [](wl_listener *, void *data) {
        auto *client = static_cast<wl_client *>(data);
        auto *state = Active();
        ++state->clients;
        fprintf(stderr, "NEO98 HOST compositor client connected\n");
        // One listener per client; Kodi is one client at a time.
        auto *listener = new wl_listener;
        listener->notify = [](wl_listener *l, void *) {
            if (Active()) --Active()->clients;
            fprintf(stderr, "NEO98 HOST compositor client left\n");
            wl_list_remove(&l->link);
            delete l;
        };
        wl_client_add_destroy_listener(client, listener);
    };
    wl_display_add_client_created_listener(s.display, &s.client_created);
    s.loop = wl_display_get_event_loop(s.display);
    fprintf(stderr, "NEO98 HOST compositor socket=%s dir=%s device=%s %ux%u\n", s.socket.c_str(), s.runtime.c_str(), node.c_str(), width, height);
    return true;
}
const std::string &Compositor::Socket() const { return state->socket; }
const std::string &Compositor::RuntimeDir() const { return state->runtime; }
const std::string &Compositor::RenderNode() const { return state->node; }
int Compositor::Fd() const { return state->loop ? wl_event_loop_get_fd(state->loop) : -1; }
void Compositor::Dispatch()
{
    State &s = *state;
    if (!s.display) return;
    wl_event_loop_dispatch(s.loop, 0);
    const auto now = std::chrono::steady_clock::now();
    if (now >= s.next_frame)
    {
        s.FrameDone();
        // On the output's beat, not a period after the late dispatch: the
        // host's loop comes a few milliseconds late and Kodi would lose
        // frames each second.
        const auto period = std::chrono::microseconds(1000000000ull / s.refresh);
        s.next_frame += period;
        if (s.next_frame <= now) s.next_frame = now + period;
    }
    wl_display_flush_clients(s.display);
}
unsigned Compositor::Serial() const { return state->latest ? state->latest_serial : 0; }
bool Compositor::Buffer(Scanout &out) const
{
    State &s = *state;
    if (!s.latest || !IsDmabuf(s.latest)) return false;
    const auto *d = static_cast<const Dmabuf *>(wl_resource_get_user_data(s.latest));
    out.fd = d->fd; out.offset = d->offset; out.stride = d->stride; out.width = d->width; out.height = d->height;
    out.format = d->format; out.modifier = d->modifier == DRM_FORMAT_MOD_INVALID ? DRM_FORMAT_MOD_LINEAR : d->modifier;
    out.serial = s.latest_serial;
    return true;
}
void Compositor::Hold(unsigned serial)
{
    State &s = *state;
    if (!serial || !s.latest || serial != s.latest_serial) return;
    auto *h = new State::Held;
    h->buffer = s.latest;
    h->serial = serial;
    h->gone.notify = [](wl_listener *listener, void *) {
        auto *h = reinterpret_cast<State::Held *>(listener);
        if (auto *a = Active()) a->held.erase(std::remove(a->held.begin(), a->held.end(), h), a->held.end());
        wl_list_remove(&h->gone.link);
        delete h;
    };
    wl_resource_add_destroy_listener(h->buffer, &h->gone);
    s.held.push_back(h);
}
void Compositor::Unhold(unsigned serial)
{
    State &s = *state;
    for (auto i = s.held.begin(); i != s.held.end(); ++i)
        if ((*i)->serial == serial)
        {
            State::Held *h = *i;
            s.held.erase(i);
            wl_list_remove(&h->gone.link);
            s.Release(h->buffer);
            delete h;
            return;
        }
}
const Picture *Compositor::Current()
{
    State &s = *state;
    if (!s.mapped) return nullptr;
    if (s.latest && s.picture.serial != s.latest_serial) s.Copy(s.latest);
    return s.picture.pixels.empty() ? nullptr : &s.picture;
}
void Compositor::Configure(unsigned width, unsigned height)
{
    State &s = *state;
    if (width == s.width && height == s.height) return;
    s.width = width; s.height = height;
    for (wl_resource *r : s.outputs) SendOutput(&s, r);
    s.SendConfigure();
}
unsigned Compositor::Clients() const { return state->clients; }
}
