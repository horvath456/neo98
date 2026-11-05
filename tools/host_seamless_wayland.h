// SPDX-License-Identifier: GPL-2.0-or-later
// GTK toplevels with complete USER backing images and window-local input.
#pragma once
#include "host_gtk_linux.h"
#include "host_seamless.h"
#include <memory>
#include <set>
namespace LinuxHost
{
class WaylandSeamless
{
    struct Native
    {
        unsigned guest = 0;
        GtkWidget *window = nullptr, *canvas = nullptr;
        Gdi::RECT frame{};
        std::vector<unsigned> pixels;
        int width = 0, height = 0;
        bool popup = false, focused = false, minimized = false;
        double scroll = 0;
        ~Native()
        {
            if (window)
                gtk_widget_destroy(window);
        }
        unsigned Id() const
        {
            return guest | 0x80000000u;
        }
        void Event(Uint8 code, int a = 0, int b = 0)
        {
            SDL_Event e{};
            e.type = SDL_WINDOWEVENT;
            e.window.windowID = Id();
            e.window.event = code;
            e.window.data1 = a;
            e.window.data2 = b;
            SDL_PushEvent(&e);
        }
        void Point(double x, double y, int &gx, int &gy) const
        {
            const int w = gtk_widget_get_allocated_width(canvas), h = gtk_widget_get_allocated_height(canvas);
            gx = w ? int(x * width / w) : 0;
            gy = h ? int(y * height / h) : 0;
        }
    };
    std::map<unsigned, std::unique_ptr<Native>> windows;
    unsigned foreground = 0;
    unsigned long long updates = 0;
    GdkCursor *cursor = nullptr;
    unsigned cursor_handle = ~0u;
    std::vector<unsigned> cursor_pixels;
    bool cursor_visible = true;
    void Pointer()
    {
        unsigned id = 0, handle = 0, argb[128 * 128];
        int w = 0, h = 0, hx = 0, hy = 0;
        const bool visible = X86::Application::HostPointer(id, handle, w, h, hx, hy, argb, 128 * 128);
        std::vector<unsigned> pixels;
        if (visible && w > 0 && h > 0 && w <= 128 && h <= 128)
            pixels.assign(argb, argb + w * h);
        if (handle == cursor_handle && pixels == cursor_pixels && visible == cursor_visible)
            return;
        if (cursor)
            g_object_unref(cursor);
        cursor = nullptr;
        auto *display = gdk_display_get_default();
        if (!visible)
            cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
        else if (!pixels.empty())
        {
            auto *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
            auto *rgba = gdk_pixbuf_get_pixels(pixbuf);
            const int stride = gdk_pixbuf_get_rowstride(pixbuf);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto pixel = pixels[size_t(y) * w + x];
                    auto *to = rgba + size_t(y) * stride + x * 4;
                    to[0] = pixel >> 16;
                    to[1] = pixel >> 8;
                    to[2] = pixel;
                    to[3] = pixel >> 24;
                }
            cursor = gdk_cursor_new_from_pixbuf(display, pixbuf, hx, hy);
            g_object_unref(pixbuf);
        }
        cursor_handle = handle;
        cursor_visible = visible;
        cursor_pixels = std::move(pixels);
        for (auto &entry : windows)
            if (auto *window = gtk_widget_get_window(entry.second->canvas))
                gdk_window_set_cursor(window, cursor);
    }
    static void Install(Native &n)
    {
        auto *p = &n;
        gtk_widget_set_can_focus(n.canvas, TRUE);
        gtk_widget_add_events(n.canvas, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                            GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK |
                                            GDK_SMOOTH_SCROLL_MASK | GDK_ENTER_NOTIFY_MASK |
                                            GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(
            n.canvas, "draw", G_CALLBACK(+[](GtkWidget *widget, cairo_t *cr, gpointer data) -> gboolean {
                auto &n = *static_cast<Native *>(data);
                if (n.pixels.empty())
                    return TRUE;
                auto *image =
                    cairo_image_surface_create_for_data(reinterpret_cast<unsigned char *>(n.pixels.data()),
                                                        CAIRO_FORMAT_ARGB32, n.width, n.height, n.width * 4);
                cairo_scale(cr, double(gtk_widget_get_allocated_width(widget)) / n.width,
                            double(gtk_widget_get_allocated_height(widget)) / n.height);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_surface(cr, image, 0, 0);
                cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
                cairo_paint(cr);
                cairo_surface_destroy(image);
                return TRUE;
            }),
            p);
        auto key = +[](GtkWidget *, GdkEventKey *e, gpointer data) -> gboolean {
            auto &n = *static_cast<Native *>(data);
            LinuxGtk::CanvasKey(*e, n.Id());
            return TRUE;
        };
        g_signal_connect(n.window, "key-press-event", G_CALLBACK(key), p);
        g_signal_connect(n.window, "key-release-event", G_CALLBACK(key), p);
        g_signal_connect(n.window, "focus-in-event",
                         G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                             auto &n = *static_cast<Native *>(data);
                             n.focused = true;
                             gtk_widget_grab_focus(n.canvas);
                             n.Event(SDL_WINDOWEVENT_FOCUS_GAINED);
                             return FALSE;
                         }),
                         p);
        g_signal_connect(n.window, "focus-out-event",
                         G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                             auto &n = *static_cast<Native *>(data);
                             n.focused = false;
                             LinuxGtk::canvas_keys.fill(false);
                             n.Event(SDL_WINDOWEVENT_FOCUS_LOST);
                             return FALSE;
                         }),
                         p);
        g_signal_connect(n.window, "delete-event",
                         G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                             static_cast<Native *>(data)->Event(SDL_WINDOWEVENT_CLOSE);
                             return TRUE;
                         }),
                         p);
        g_signal_connect(n.window, "window-state-event",
                         G_CALLBACK(+[](GtkWidget *, GdkEventWindowState *e, gpointer data) -> gboolean {
                             auto &n = *static_cast<Native *>(data);
                             if (e->changed_mask & GDK_WINDOW_STATE_ICONIFIED)
                             {
                                 n.minimized = e->new_window_state & GDK_WINDOW_STATE_ICONIFIED;
                                 n.Event(n.minimized ? SDL_WINDOWEVENT_MINIMIZED : SDL_WINDOWEVENT_RESTORED);
                             }
                             return FALSE;
                         }),
                         p);
        g_signal_connect(n.canvas, "size-allocate",
                         G_CALLBACK(+[](GtkWidget *, GtkAllocation *r, gpointer data) {
                             auto &n = *static_cast<Native *>(data);
                             if (!n.popup && n.width && (r->width != n.width || r->height != n.height))
                                 n.Event(SDL_WINDOWEVENT_SIZE_CHANGED, r->width, r->height);
                         }),
                         p);
        g_signal_connect(n.canvas, "motion-notify-event",
                         G_CALLBACK(+[](GtkWidget *, GdkEventMotion *e, gpointer data) -> gboolean {
                             auto &n = *static_cast<Native *>(data);
                             SDL_Event out{};
                             out.type = SDL_MOUSEMOTION;
                             out.motion.windowID = n.Id();
                             n.Point(e->x, e->y, out.motion.x, out.motion.y);
                             SDL_PushEvent(&out);
                             return TRUE;
                         }),
                         p);
        auto button = +[](GtkWidget *, GdkEventButton *e, gpointer data) -> gboolean {
            auto &n = *static_cast<Native *>(data);
            if (e->type != GDK_BUTTON_PRESS && e->type != GDK_BUTTON_RELEASE)
                return TRUE;
            if (e->button > 3)
                return FALSE;
            gtk_widget_grab_focus(n.canvas);
            int x = 0, y = 0;
            n.Point(e->x, e->y, x, y);
            if (!n.popup && e->type == GDK_BUTTON_PRESS && e->button == 1)
            {
                const auto hit = User::FrameHitTest(n.guest, x + n.frame.left, y + n.frame.top);
                if (hit == 2)
                {
                    gtk_window_begin_move_drag(GTK_WINDOW(n.window), 1, int(e->x_root), int(e->y_root),
                                               e->time);
                    return TRUE;
                }
                const std::pair<unsigned, GdkWindowEdge> edges[] = {
                    {10, GDK_WINDOW_EDGE_WEST},       {11, GDK_WINDOW_EDGE_EAST},
                    {12, GDK_WINDOW_EDGE_NORTH},      {13, GDK_WINDOW_EDGE_NORTH_WEST},
                    {14, GDK_WINDOW_EDGE_NORTH_EAST}, {15, GDK_WINDOW_EDGE_SOUTH},
                    {16, GDK_WINDOW_EDGE_SOUTH_WEST}, {17, GDK_WINDOW_EDGE_SOUTH_EAST}};
                for (auto edge : edges)
                    if (hit == edge.first)
                    {
                        gtk_window_begin_resize_drag(GTK_WINDOW(n.window), edge.second, 1, int(e->x_root),
                                                     int(e->y_root), e->time);
                        return TRUE;
                    }
            }
            SDL_Event out{};
            out.type = e->type == GDK_BUTTON_PRESS ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            out.button.windowID = n.Id();
            out.button.x = x;
            out.button.y = y;
            out.button.button = e->button == 2   ? SDL_BUTTON_MIDDLE
                                : e->button == 3 ? SDL_BUTTON_RIGHT
                                                 : SDL_BUTTON_LEFT;
            SDL_PushEvent(&out);
            return TRUE;
        };
        g_signal_connect(n.canvas, "button-press-event", G_CALLBACK(button), p);
        g_signal_connect(n.canvas, "button-release-event", G_CALLBACK(button), p);
        g_signal_connect(n.canvas, "scroll-event",
                         G_CALLBACK(+[](GtkWidget *, GdkEventScroll *e, gpointer data) -> gboolean {
                             auto &n = *static_cast<Native *>(data);
                             SDL_Event out{};
                             out.type = SDL_MOUSEWHEEL;
                             out.wheel.windowID = n.Id();
                             if (e->direction == GDK_SCROLL_SMOOTH)
                             {
                                 n.scroll -= e->delta_y;
                                 out.wheel.y = int(n.scroll);
                                 n.scroll -= out.wheel.y;
                             }
                             else
                                 out.wheel.y = e->direction == GDK_SCROLL_UP     ? 1
                                               : e->direction == GDK_SCROLL_DOWN ? -1
                                                                                 : 0;
                             SDL_PushEvent(&out);
                             return TRUE;
                         }),
                         p);
    }

  public:
    ~WaylandSeamless()
    {
        Close();
        if (cursor)
            g_object_unref(cursor);
    }
    bool Input(const std::string &action, const std::string &value)
    {
        const auto separator = value.find('|');
        const auto klass = value.substr(0, separator);
        const auto argument = separator == std::string::npos ? std::string() : value.substr(separator + 1);
        for (auto &entry : windows)
        {
            auto &n = *entry.second;
            char name[256]{};
            User::GetClassName(n.guest, name, sizeof name);
            if (klass != name)
                continue;
            if (action == "native-shot")
            {
                const int w = gtk_widget_get_allocated_width(n.canvas),
                          h = gtk_widget_get_allocated_height(n.canvas);
                auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
                auto *cr = cairo_create(surface);
                gtk_widget_draw(n.canvas, cr);
                cairo_destroy(cr);
                const bool okay =
                    cairo_surface_write_to_png(surface, argument.c_str()) == CAIRO_STATUS_SUCCESS;
                cairo_surface_destroy(surface);
                return okay;
            }
            if (action == "native-size")
            {
                int w = 0, h = 0;
                if (sscanf(argument.c_str(), "%d,%d", &w, &h) != 2)
                    return false;
                gtk_window_resize(GTK_WINDOW(n.window), w, h);
                return true;
            }
            if (action == "native-click")
            {
                GdkEventButton e{};
                if (sscanf(argument.c_str(), "%lf,%lf", &e.x, &e.y) != 2)
                    return false;
                e.window = gtk_widget_get_window(n.canvas);
                e.button = 1;
                e.time = GDK_CURRENT_TIME;
                gboolean handled = FALSE;
                e.type = GDK_BUTTON_PRESS;
                g_signal_emit_by_name(n.canvas, "button-press-event", &e, &handled);
                e.type = GDK_BUTTON_RELEASE;
                g_signal_emit_by_name(n.canvas, "button-release-event", &e, &handled);
                return true;
            }
            if (action == "native-key" || action == "native-ctrl")
            {
                const unsigned vk = strtoul(argument.c_str(), nullptr, 16);
                const auto scan = vk >= 'A' && vk <= 'Z' ? SDL_Scancode(SDL_SCANCODE_A + vk - 'A')
                                  : vk == 13             ? SDL_SCANCODE_RETURN
                                  : vk == 27             ? SDL_SCANCODE_ESCAPE
                                                         : SDL_SCANCODE_UNKNOWN;
                auto key = [&](SDL_Scancode scan, bool down) {
                    GdkEventKey e{};
                    e.type = down ? GDK_KEY_PRESS : GDK_KEY_RELEASE;
                    e.window = gtk_widget_get_window(n.canvas);
                    for (unsigned code = 8; code < 256; ++code)
                        if (LinuxGtk::PhysicalKey(code) == scan)
                        {
                            e.hardware_keycode = code;
                            break;
                        }
                    gboolean handled = FALSE;
                    g_signal_emit_by_name(n.window, down ? "key-press-event" : "key-release-event", &e,
                                          &handled);
                };
                if (scan == SDL_SCANCODE_UNKNOWN)
                    return false;
                if (action == "native-ctrl")
                    key(SDL_SCANCODE_LCTRL, true);
                key(scan, true);
                key(scan, false);
                if (action == "native-ctrl")
                    key(SDL_SCANCODE_LCTRL, false);
                return true;
            }
            return false;
        }
        return false;
    }
    bool Contains(unsigned id) const
    {
        return (id & 0x80000000u) && windows.count(id & 0x7fffffffu);
    }
    unsigned Guest(unsigned id) const
    {
        return Contains(id) ? id & 0x7fffffffu : 0;
    }
    bool Translate(unsigned id, int &x, int &y, unsigned &guest) const
    {
        guest = Guest(id);
        auto found = windows.find(guest);
        if (found == windows.end())
            return false;
        x += found->second->frame.left;
        y += found->second->frame.top;
        User::SetHostPointerWindow(guest);
        return true;
    }
    bool HasFocus() const
    {
        for (const auto &entry : windows)
            if (entry.second->focused)
                return true;
        return false;
    }
    void Warp(int, int)
    {
    } // Wayland has no absolute pointer warp for ordinary windows.
    bool Raise(unsigned guest = 0)
    {
        if (!guest)
            guest = User::GetForegroundWindow();
        auto found = windows.find(guest);
        if (found == windows.end())
            return false;
        gtk_window_present(GTK_WINDOW(found->second->window));
        return true;
    }
    void Close()
    {
        windows.clear();
        foreground = 0;
        User::SetHostPointerWindow(0);
    }
    void Present(SDL_Window *, const HostMenu::State &state, const unsigned char *, bool changed)
    {
        std::set<unsigned> live;
        unsigned snapshot[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, snapshot, User::WindowCapacity, count))
            return;
        unsigned hidden = 0;
        if (!state.seamless_taskbar)
            for (unsigned i = 0; i < count; ++i)
            {
                char klass[256]{};
                User::GetClassName(snapshot[i], klass, sizeof klass);
                if (!strcmp(klass, "Shell_TrayWnd"))
                    hidden = snapshot[i];
            }
        User::SetHostHiddenWindow(hidden);
        // Owners must exist before their transient windows and popup menus.
        for (unsigned i = count; i > 0; --i)
        {
            const unsigned h = snapshot[i - 1];
            if (!User::IsVisible(h))
                continue;
            char klass[256]{};
            User::GetClassName(h, klass, sizeof klass);
            if (!strcmp(klass, "Progman") || !strcmp(klass, "WorkerW") || !strcmp(klass, "SHELLDLL_DefView"))
                continue;
            if (h == hidden)
                continue;
            live.insert(h);
            const bool minimized = User::Style(h) & 0x20000000u;
            auto found = windows.find(h);
            if (minimized)
            {
                if (found != windows.end() && !found->second->minimized)
                    gtk_window_iconify(GTK_WINDOW(found->second->window));
                continue;
            }
            int w = 0, ht = 0, stride = 0;
            const auto *bits = User::HostWindowPixels(h, w, ht, stride);
            if (!bits || w <= 0 || ht <= 0)
                continue;
            bool created = found == windows.end();
            if (created)
            {
                auto owned = std::make_unique<Native>();
                auto &n = *owned;
                n.guest = h;
                n.popup = !strcmp(klass, "#32768") || !strcmp(klass, "tooltips_class32") ||
                          !strcmp(klass, "ComboLBox");
                const unsigned owner = User::HostWindowOwner(h);
                auto parent = windows.find(owner);
                if (n.popup && parent == windows.end())
                {
                    parent = windows.find(User::GetForegroundWindow());
                    if (parent == windows.end())
                        continue;
                }
                n.window = gtk_window_new(n.popup ? GTK_WINDOW_POPUP : GTK_WINDOW_TOPLEVEL);
                n.canvas = gtk_drawing_area_new();
                gtk_window_set_decorated(GTK_WINDOW(n.window), FALSE);
                gtk_widget_set_app_paintable(n.window, TRUE);
                if (auto *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(n.window)))
                    gtk_widget_set_visual(n.window, visual);
                gtk_window_set_skip_taskbar_hint(GTK_WINDOW(n.window), !HostSeamless::TaskbarWindow(h));
                gtk_window_set_type_hint(GTK_WINDOW(n.window), n.popup ? GDK_WINDOW_TYPE_HINT_POPUP_MENU
                                                                       : GDK_WINDOW_TYPE_HINT_NORMAL);
                if (parent != windows.end())
                    gtk_window_set_transient_for(GTK_WINDOW(n.window), GTK_WINDOW(parent->second->window));
                gtk_container_add(GTK_CONTAINER(n.window), n.canvas);
                Install(n);
                found = windows.emplace(h, std::move(owned)).first;
            }
            auto &n = *found->second;
            User::GetWindowRect(h, &n.frame);
            const bool resized = n.width != w || n.height != ht;
            n.width = w;
            n.height = ht;
            gtk_window_set_title(GTK_WINDOW(n.window), HostSeamless::WindowTitle(h).c_str());
            if (created || resized)
                gtk_window_resize(GTK_WINDOW(n.window), w, ht);
            if (created || resized || changed)
            {
                n.pixels.assign(size_t(w) * ht, 0);
                std::vector<Gdi::RECT> rects(User::WindowVisibleRects(h, nullptr, 0));
                User::WindowVisibleRects(h, rects.data(), unsigned(rects.size()));
                for (auto r : rects)
                    for (int y = std::max(r.top, n.frame.top); y < std::min(r.bottom, n.frame.bottom); ++y)
                        for (int x = std::max(r.left, n.frame.left); x < std::min(r.right, n.frame.right);
                             ++x)
                            n.pixels[size_t(y - n.frame.top) * w + x - n.frame.left] =
                                reinterpret_cast<const unsigned *>(bits + size_t(y - n.frame.top) *
                                                                              stride)[x - n.frame.left] |
                                0xff000000u;
                gtk_widget_queue_draw(n.canvas);
                ++updates;
                cairo_region_t *shape = cairo_region_create();
                for (auto r : rects)
                {
                    cairo_rectangle_int_t local{r.left - n.frame.left, r.top - n.frame.top, r.right - r.left,
                                                r.bottom - r.top};
                    cairo_region_union_rectangle(shape, &local);
                }
                gtk_widget_input_shape_combine_region(n.window, shape);
                cairo_region_destroy(shape);
            }
            if (created)
            {
                gtk_widget_show_all(n.window);
                gdk_window_set_cursor(gtk_widget_get_window(n.canvas), cursor);
                if (n.popup)
                {
                    auto parent = windows.find(User::HostWindowOwner(h));
                    if (parent == windows.end())
                        parent = windows.find(User::GetForegroundWindow());
                    if (parent != windows.end())
                    {
                        GdkRectangle at{n.frame.left - parent->second->frame.left,
                                        n.frame.top - parent->second->frame.top, 1, 1};
                        gdk_window_move_to_rect(gtk_widget_get_window(n.window), &at, GDK_GRAVITY_NORTH_WEST,
                                                GDK_GRAVITY_NORTH_WEST,
                                                GdkAnchorHints(GDK_ANCHOR_SLIDE_X | GDK_ANCHOR_SLIDE_Y), 0,
                                                0);
                    }
                }
            }
        }
        for (auto i = windows.begin(); i != windows.end();)
            if (!live.count(i->first))
                i = windows.erase(i);
            else
                ++i;
        const unsigned selected = User::GetForegroundWindow();
        if (selected != foreground)
        {
            foreground = selected;
            Raise(selected);
        }
        Pointer();
    }
    void Describe(const char *path) const
    {
        FILE *file = fopen(path, "w");
        if (!file)
            return;
        fprintf(file, "view seamless=1 backend=wayland windows=%zu updates=%llu\n", windows.size(), updates);
        for (const auto &entry : windows)
        {
            const auto &n = *entry.second;
            fprintf(file, "window hwnd=%x native=%u width=%d height=%d popup=%d focused=%d\n", entry.first,
                    n.Id(), n.width, n.height, n.popup, n.focused);
        }
        fclose(file);
    }
};
} // namespace LinuxHost
