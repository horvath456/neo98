/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A Wayland client for the tests of the compositor of the KMS host
   (tools/host_wayland/compositor.cpp), standing in for Kodi: one xdg
   toplevel that shows one colour, as a linear dmabuf from GBM on the render
   node the compositor names (or with --shm as wl_shm). It commits a frame
   on every frame callback. Lines on stdin: "colour RRGGBB" changes the
   colour, "quit" ends it. What it sees goes to stdout, one line each:

     globals compositor=1 xdg=1 dmabuf=4 output=1 seat=1 shm=1
     output 1280x720
     feedback device= linear-xrgb=1
     configure 1280 720
     buffer dmabuf 1280x720
     frames 60
*/
#define _GNU_SOURCE
#include "xdg-shell-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include <wayland-client.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static struct wl_compositor *compositor;
static struct xdg_wm_base *wm_base;
static struct zwp_linux_dmabuf_v1 *dmabuf;
static struct wl_shm *shm;
static struct wl_output *output;
static struct wl_seat *seat;
static unsigned dmabuf_version, width = 640, height = 360, configured, frames, running = 1;
static dev_t main_device;
static int feedback_done, linear_xrgb;
static uint32_t colour = 0xd01010;
static int use_shm;
static struct wl_surface *surface;
static struct wl_buffer *buffer;
static struct gbm_device *gbm;
static struct gbm_bo *bo;
static void *shm_data;
static uint32_t shm_stride;
static const uint8_t *table;
static uint32_t table_size;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (!strcmp(interface, "wl_compositor")) compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, "xdg_wm_base")) wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(interface, "zwp_linux_dmabuf_v1"))
    { dmabuf_version = version < 4 ? version : 4; dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, dmabuf_version); }
    else if (!strcmp(interface, "wl_shm")) shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, "wl_output")) output = wl_registry_bind(registry, name, &wl_output_interface, version < 3 ? version : 3);
    else if (!strcmp(interface, "wl_seat")) seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sub, const char *make, const char *model, int32_t t) {}
static void output_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh) { printf("output %dx%d\n", w, h); }
static void output_done(void *d, struct wl_output *o) {}
static void output_scale(void *d, struct wl_output *o, int32_t s) {}
static const struct wl_output_listener output_listener = {output_geometry, output_mode, output_done, output_scale, NULL, NULL};

static void fb_done(void *d, struct zwp_linux_dmabuf_feedback_v1 *f) { feedback_done = 1; }
static void fb_table(void *d, struct zwp_linux_dmabuf_feedback_v1 *f, int32_t fd, uint32_t size)
{
    table = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    table_size = table == MAP_FAILED ? 0 : size;
    if (table == MAP_FAILED) table = NULL;
    close(fd);
}
static void fb_main(void *d, struct zwp_linux_dmabuf_feedback_v1 *f, struct wl_array *device)
{ if (device->size == sizeof(dev_t)) memcpy(&main_device, device->data, sizeof(dev_t)); }
static void fb_tranche_done(void *d, struct zwp_linux_dmabuf_feedback_v1 *f) {}
static void fb_target(void *d, struct zwp_linux_dmabuf_feedback_v1 *f, struct wl_array *device) {}
static void fb_formats(void *d, struct zwp_linux_dmabuf_feedback_v1 *f, struct wl_array *indices)
{
    uint16_t *index;
    wl_array_for_each(index, indices)
    {
        if (!table || (uint32_t)(*index + 1) * 16 > table_size) continue;
        uint32_t format; uint64_t modifier;
        memcpy(&format, table + *index * 16, 4); memcpy(&modifier, table + *index * 16 + 8, 8);
        if (format == DRM_FORMAT_XRGB8888 && modifier == DRM_FORMAT_MOD_LINEAR) linear_xrgb = 1;
    }
}
static void fb_flags(void *d, struct zwp_linux_dmabuf_feedback_v1 *f, uint32_t flags) {}
static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {fb_done, fb_table, fb_main, fb_tranche_done, fb_target, fb_formats, fb_flags};

static void ping(void *d, struct xdg_wm_base *b, uint32_t serial) { xdg_wm_base_pong(b, serial); }
static const struct xdg_wm_base_listener wm_base_listener = {ping};
static void toplevel_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states)
{
    printf("configure %d %d\n", w, h);
    if (w > 0 && h > 0 && !buffer) { width = (unsigned)w; height = (unsigned)h; }
}
static void toplevel_close(void *d, struct xdg_toplevel *t) { running = 0; }
static const struct xdg_toplevel_listener toplevel_listener = {toplevel_configure, toplevel_close, NULL, NULL};
static void surface_configure(void *d, struct xdg_surface *x, uint32_t serial) { xdg_surface_ack_configure(x, serial); configured = 1; }
static const struct xdg_surface_listener surface_listener = {surface_configure};

/* The render node the compositor names in its feedback. */
static int open_render_node(void)
{
    for (int i = 128; i < 136; ++i)
    {
        char path[64]; struct stat info;
        snprintf(path, sizeof path, "/dev/dri/renderD%d", i);
        if (stat(path, &info)) continue;
        if (!main_device || info.st_rdev == main_device) return open(path, O_RDWR | O_CLOEXEC);
    }
    return -1;
}

static void fill(void)
{
    if (use_shm)
    {
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) ((uint32_t *)((uint8_t *)shm_data + y * shm_stride))[x] = 0xff000000u | colour;
        return;
    }
    uint32_t stride; void *map_data = NULL;
    uint8_t *p = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE, &stride, &map_data);
    if (!p) { fprintf(stderr, "gbm_bo_map failed\n"); exit(2); }
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) ((uint32_t *)(p + y * stride))[x] = 0xff000000u | colour;
    gbm_bo_unmap(bo, map_data);
}

static void make_buffer(void)
{
    if (use_shm)
    {
        shm_stride = width * 4;
        const size_t size = (size_t)shm_stride * height;
        int fd = memfd_create("neo98-test-client", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, (off_t)size)) exit(2);
        shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
        buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)width, (int32_t)height, (int32_t)shm_stride, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        fill();
        printf("buffer shm %ux%u\n", width, height);
        return;
    }
    int fd = open_render_node();
    if (fd < 0) { fprintf(stderr, "no render node\n"); exit(2); }
    gbm = gbm_create_device(fd);
    uint64_t linear = DRM_FORMAT_MOD_LINEAR;
    bo = gbm_bo_create_with_modifiers2(gbm, width, height, GBM_FORMAT_XRGB8888, &linear, 1, 0);
    if (!bo) bo = gbm_bo_create(gbm, width, height, GBM_FORMAT_XRGB8888, GBM_BO_USE_LINEAR);
    if (!bo) { fprintf(stderr, "no GBM buffer\n"); exit(2); }
    fill();
    struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    zwp_linux_buffer_params_v1_add(params, gbm_bo_get_fd(bo), 0, gbm_bo_get_offset(bo, 0), gbm_bo_get_stride(bo),
                                   (uint32_t)(DRM_FORMAT_MOD_LINEAR >> 32), (uint32_t)DRM_FORMAT_MOD_LINEAR);
    buffer = zwp_linux_buffer_params_v1_create_immed(params, (int32_t)width, (int32_t)height, DRM_FORMAT_XRGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    printf("buffer dmabuf %ux%u\n", width, height);
}

static void frame_done(void *d, struct wl_callback *c, uint32_t time);
static const struct wl_callback_listener frame_listener = {frame_done};
static void commit(void)
{
    struct wl_callback *c = wl_surface_frame(surface);
    wl_callback_add_listener(c, &frame_listener, NULL);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, (int32_t)width, (int32_t)height);
    wl_surface_commit(surface);
}
static void frame_done(void *d, struct wl_callback *c, uint32_t time)
{
    wl_callback_destroy(c);
    if (++frames % 60 == 0) { printf("frames %u\n", frames); fflush(stdout); }
    commit();
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--shm")) use_shm = 1;
        else if (!strcmp(argv[i], "--colour") && i + 1 < argc) colour = (uint32_t)strtoul(argv[++i], NULL, 16);
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) { fprintf(stderr, "no Wayland display\n"); return 1; }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    printf("globals compositor=%d xdg=%d dmabuf=%u output=%d seat=%d shm=%d\n", !!compositor, !!wm_base, dmabuf ? dmabuf_version : 0,
           !!output, !!seat, !!shm);
    if (!compositor || !wm_base || (!dmabuf && !use_shm) || (use_shm && !shm)) return 1;
    if (output) wl_output_add_listener(output, &output_listener, NULL);
    if (dmabuf && dmabuf_version >= 4)
    {
        struct zwp_linux_dmabuf_feedback_v1 *feedback = zwp_linux_dmabuf_v1_get_default_feedback(dmabuf);
        zwp_linux_dmabuf_feedback_v1_add_listener(feedback, &feedback_listener, NULL);
        while (!feedback_done && wl_display_dispatch(display) > 0) {}
        printf("feedback device=%u:%u linear-xrgb=%d\n", major(main_device), minor(main_device), linear_xrgb);
    }
    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg, &surface_listener, NULL);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_title(toplevel, "NEO98 test client");
    wl_surface_commit(surface);
    while (!configured && wl_display_dispatch(display) > 0) {}
    make_buffer();
    commit();
    char line[128]; size_t used = 0;
    while (running)
    {
        while (wl_display_prepare_read(display)) wl_display_dispatch_pending(display);
        wl_display_flush(display);
        struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0}, {0, POLLIN, 0}};
        if (poll(fds, 2, 1000) < 0) { wl_display_cancel_read(display); break; }
        if (fds[0].revents & POLLIN) { if (wl_display_read_events(display) < 0) break; }
        else wl_display_cancel_read(display);
        if (fds[0].revents & (POLLHUP | POLLERR)) break;
        if (wl_display_dispatch_pending(display) < 0) break;
        if (fds[1].revents & (POLLIN | POLLHUP))
        {
            const ssize_t n = read(0, line + used, sizeof line - 1 - used);
            if (n <= 0) { fds[1].fd = -1; continue; }
            used += (size_t)n; line[used] = 0;
            char *end;
            while ((end = strchr(line, '\n')))
            {
                *end = 0;
                if (!strncmp(line, "colour ", 7)) { colour = (uint32_t)strtoul(line + 7, NULL, 16); fill(); printf("colour %06x\n", colour); }
                else if (!strcmp(line, "quit")) running = 0;
                memmove(line, end + 1, strlen(end + 1) + 1);
                used = strlen(line);
            }
        }
    }
    printf("end frames=%u\n", frames);
    wl_display_disconnect(display);
    return 0;
}
