// SPDX-License-Identifier: GPL-2.0-or-later
// X11/XWayland windows for USER's published top-level regions.
#pragma once
#include "host_dock_linux.h"
#include "host_seamless.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <map>
#include <memory>
#include <set>
#undef Status
#undef None
#undef Below
#undef Above
#include "host_seamless_wayland.h"

namespace LinuxHost
{
class Seamless
{
    WaylandSeamless wayland;
    bool IsWayland() const { return LinuxGtk::wayland_canvas; }
    struct Native
    {
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *texture = nullptr;
        HostSeamless::Item item;
        std::vector<unsigned> pixels;
        std::vector<HostSeamless::Rect> shape;
        bool listed = false, minimized = false;
        int width = 0, height = 0;
        unsigned identity_checked = 0;
        std::string identity_class;
        std::vector<unsigned> identity_icon;
        int icon_width = 0, icon_height = 0;
        ~Native()
        {
            if (texture)
                SDL_DestroyTexture(texture);
            if (renderer)
                SDL_DestroyRenderer(renderer);
            if (window)
                SDL_DestroyWindow(window);
        }
    };
    std::map<unsigned, std::unique_ptr<Native>> windows;
    HostSeamless::Mask mask;
    std::vector<HostSeamless::Item> items;
    SDL_Rect area = {};
    SDL_Cursor *cursor = nullptr;
    std::vector<unsigned> cursor_pixels;
    unsigned cursor_handle = 0;
    bool active = false;
    unsigned long long updates = 0;
    unsigned foreground = 0;

    static bool Info(SDL_Window *window, SDL_SysWMinfo &info)
    {
        SDL_VERSION(&info.version);
        return SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_X11;
    }
    void Shape(Native &native)
    {
        SDL_SysWMinfo info;
        if (!Info(native.window, info))
            return;
        std::vector<XRectangle> bounds;
        for (const auto &rect : native.item.rects)
        {
            auto clipped = HostSeamless::Clipped(rect, mask.width, mask.height);
            if (!clipped.Empty())
                bounds.push_back({short(clipped.left - native.item.frame.left),
                                  short(clipped.top - native.item.frame.top),
                                  static_cast<unsigned short>(clipped.right - clipped.left),
                                  static_cast<unsigned short>(clipped.bottom - clipped.top)});
        }
        XShapeCombineRectangles(info.info.x11.display, info.info.x11.window, ShapeBounding, 0, 0,
                                bounds.data(), int(bounds.size()), ShapeSet, Unsorted);
        XShapeCombineRectangles(info.info.x11.display, info.info.x11.window, ShapeInput, 0, 0,
                                native.item.through ? nullptr : bounds.data(),
                                native.item.through ? 0 : int(bounds.size()), ShapeSet, Unsorted);
        XFlush(info.info.x11.display);
    }
    void Identity(Native &native)
    {
        unsigned process = 0;
        char program[256] = {};
        int w = 0, h = 0;
        unsigned picture[128 * 128];
        X86::Application::HostWindowProgram(native.item.id, process, program, sizeof program, w, h, picture,
                                            128 * 128);
        SDL_SysWMinfo info;
        if (Info(native.window, info))
        {
            char path[PATH_MAX] = {};
            X86::Application::HostWindowProgramPath(native.item.id, path, sizeof path);
            std::string name = program[0] ? LinuxDock::Ensure(path[0] ? path : program, program, w, h, picture) : "Neo98";
            if (name != native.identity_class)
            {
                XClassHint hint{const_cast<char *>(name.c_str()), const_cast<char *>(name.c_str())};
                XSetClassHint(info.info.x11.display, info.info.x11.window, &hint);
                native.identity_class = name;
                if (native.item.desktop)
                {
                    Atom property = XInternAtom(info.info.x11.display, "_NET_WM_WINDOW_TYPE", False);
                    Atom value = XInternAtom(info.info.x11.display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
                    XChangeProperty(info.info.x11.display, info.info.x11.window, property, XA_ATOM, 32,
                                    PropModeReplace, reinterpret_cast<unsigned char *>(&value), 1);
                }
            }
        }
        if (w > 0 && h > 0 && w <= 128 && h <= 128 &&
            (w != native.icon_width || h != native.icon_height ||
             native.identity_icon.size() != size_t(w) * h ||
             !std::equal(native.identity_icon.begin(), native.identity_icon.end(), picture)))
        {
            SDL_Surface *icon =
                SDL_CreateRGBSurfaceWithFormatFrom(picture, w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
            if (icon)
            {
                SDL_SetWindowIcon(native.window, icon);
                SDL_FreeSurface(icon);
                native.icon_width = w; native.icon_height = h;
                native.identity_icon.assign(picture, picture + size_t(w) * h);
            }
        }
    }

  public:
    bool Input(const std::string &action,const std::string &value) { return IsWayland() && wayland.Input(action,value); }
    ~Seamless()
    {
        Close();
        if (cursor)
            SDL_FreeCursor(cursor);
    }
    bool Supported(SDL_Window *anchor)
    {
        if (IsWayland()) return true;
        SDL_SysWMinfo info;
        int event = 0, error = 0;
        return Info(anchor, info) && XShapeQueryExtension(info.info.x11.display, &event, &error);
    }
    SDL_Rect Area(SDL_Window *anchor)
    {
        int display = SDL_GetWindowDisplayIndex(anchor);
        if (SDL_GetDisplayUsableBounds(std::max(0, display), &area))
            SDL_GetDisplayBounds(0, &area);
        return area;
    }
    void Close()
    {
        wayland.Close();
        windows.clear();
        items.clear();
        mask = {};
        active = false;
        foreground = 0;
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_ShowCursor(SDL_ENABLE);
    }
    bool Contains(unsigned sdl_id) const
    {
        if (IsWayland()) return wayland.Contains(sdl_id);
        for (const auto &entry : windows)
            if (SDL_GetWindowID(entry.second->window) == sdl_id)
                return true;
        return false;
    }
    bool Translate(unsigned sdl_id, int &x, int &y, unsigned &guest) const
    {
        if (IsWayland()) return wayland.Translate(sdl_id,x,y,guest);
        for (const auto &entry : windows)
            if (SDL_GetWindowID(entry.second->window) == sdl_id)
            {
                x += entry.second->item.frame.left;
                y += entry.second->item.frame.top;
                guest = entry.second->item.id;
                return true;
            }
        return false;
    }
    unsigned Guest(unsigned sdl_id) const
    {
        if (IsWayland()) return wayland.Guest(sdl_id);
        int x = 0, y = 0;
        unsigned guest = 0;
        Translate(sdl_id, x, y, guest);
        return guest;
    }
    bool HasFocus() const
    {
        if (IsWayland()) return wayland.HasFocus();
        SDL_Window *window = SDL_GetKeyboardFocus();
        return window && Contains(SDL_GetWindowID(window));
    }
    void Warp(int x, int y)
    {
        if (IsWayland()) { wayland.Warp(x,y); return; }
        if (active)
            SDL_WarpMouseGlobal(area.x + x, area.y + y);
    }
    void Pointer()
    {
        unsigned identifier = 0, handle = 0, argb[128 * 128];
        int w = 0, h = 0, hot_x = 0, hot_y = 0;
        const bool shown =
            X86::Application::HostPointer(identifier, handle, w, h, hot_x, hot_y, argb, 128 * 128);
        SDL_ShowCursor(shown ? SDL_ENABLE : SDL_DISABLE);
        if (!shown || w <= 0 || h <= 0 || w > 128 || h > 128)
            return;
        std::vector<unsigned> picture(argb, argb + w * h);
        if (handle == cursor_handle && picture == cursor_pixels)
            return;
        SDL_Surface *surface =
            SDL_CreateRGBSurfaceWithFormatFrom(picture.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
        SDL_Cursor *made =
            surface ? SDL_CreateColorCursor(surface, std::clamp(hot_x, 0, w - 1), std::clamp(hot_y, 0, h - 1))
                    : nullptr;
        if (surface)
            SDL_FreeSurface(surface);
        if (made)
        {
            SDL_SetCursor(made);
            if (cursor)
                SDL_FreeCursor(cursor);
            cursor = made;
            cursor_handle = handle;
            cursor_pixels = std::move(picture);
        }
    }
    void Present(SDL_Window *anchor, const HostMenu::State &state, const unsigned char *pixels, bool changed)
    {
        if (IsWayland()) { wayland.Present(anchor,state,pixels,changed); return; }
        active = true;
        HostSeamless::View view;
        view.taskbar = state.seamless_taskbar;
        view.icons = state.seamless_icons;
        view.pixels = pixels;
        view.now = SDL_GetTicks();
        if (view.icons) {
            int width = 0, height = 0, depth = 0; User::ScreenMode(width, height, depth);
            view.icon_mask = X86::Application::HostDesktopIconMask(view.now, width, height);
        }
        view.trace = getenv("NEO98_SEAMLESS_TRACE") != nullptr;
        if (const char *grace = getenv("NEO98_SEAMLESS_GRACE"))
            view.grace = strtoul(grace, nullptr, 10);
        HostSeamless::Collect(mask, view);
        HostSeamless::Plan(mask, items);
        if (!state.seamless_windows)
        {
            HostSeamless::Item layer;
            layer.id = 0xfffffff0u;
            layer.title = "Neo98";
            layer.frame = {0, 0, mask.width, mask.height};
            layer.rects = mask.rects;
            layer.listed = state.seamless_dock;
            items = {std::move(layer)};
        }
        Area(anchor);
        std::set<unsigned> present;
        for (auto item : items)
        {
            if (item.id == HostSeamless::PointerItem)
                continue;
            if (!item.minimized)
                item.frame = HostSeamless::Clipped(item.frame, mask.width, mask.height);
            if (!item.minimized && (item.frame.Empty() || item.rects.empty()))
                continue;
            present.insert(item.id);
            auto &pointer = windows[item.id];
            bool fresh = !pointer;
            if (fresh)
            {
                if (item.minimized)
                {
                    windows.erase(item.id);
                    present.erase(item.id);
                    continue;
                }
                pointer = std::make_unique<Native>();
                unsigned flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN;
                if (!item.listed || !state.seamless_dock)
                    flags |= SDL_WINDOW_SKIP_TASKBAR;
                if (item.through)
                    flags |= SDL_WINDOW_TOOLTIP;
                pointer->window = SDL_CreateWindow(item.title.empty() ? "Neo98" : item.title.c_str(),
                                                   area.x + item.frame.left, area.y + item.frame.top,
                                                   item.frame.right - item.frame.left,
                                                   item.frame.bottom - item.frame.top, flags);
                if (!pointer->window)
                {
                    windows.erase(item.id);
                    continue;
                }
                pointer->renderer = SDL_CreateRenderer(pointer->window, -1, SDL_RENDERER_ACCELERATED);
                if (!pointer->renderer)
                    pointer->renderer = SDL_CreateRenderer(pointer->window, -1, SDL_RENDERER_SOFTWARE);
                if (!pointer->renderer)
                {
                    windows.erase(item.id);
                    continue;
                }
                fprintf(stderr, "NEO98 seamless create hwnd=%x native=%u title=%s\n", item.id,
                        SDL_GetWindowID(pointer->window), item.title.c_str());
            }
            Native &native = *pointer;
            if (item.minimized)
            {
                if (!native.minimized)
                    SDL_MinimizeWindow(native.window);
                native.minimized = true;
                continue;
            }
            const int w = item.frame.right - item.frame.left, h = item.frame.bottom - item.frame.top;
            const bool moved =
                native.item.frame.left != item.frame.left || native.item.frame.top != item.frame.top;
            const bool shape_changed =
                fresh || native.shape.size() != item.rects.size() ||
                (!item.rects.empty() && memcmp(native.shape.data(), item.rects.data(),
                                               item.rects.size() * sizeof(HostSeamless::Rect)));
            const bool resized = w != native.width || h != native.height;
            const bool title_changed = native.item.title != item.title;
            native.item = item;
            if (native.minimized)
            {
                SDL_RestoreWindow(native.window);
                native.minimized = false;
            }
            if (moved)
                SDL_SetWindowPosition(native.window, area.x + item.frame.left, area.y + item.frame.top);
            if (resized)
            {
                SDL_SetWindowSize(native.window, w, h);
                native.width = w;
                native.height = h;
                if (native.texture)
                    SDL_DestroyTexture(native.texture);
                native.texture = SDL_CreateTexture(native.renderer, SDL_PIXELFORMAT_ARGB8888,
                                                   SDL_TEXTUREACCESS_STREAMING, w, h);
            }
            if (title_changed)
                SDL_SetWindowTitle(native.window, item.title.c_str());
            if (fresh || title_changed || SDL_GetTicks() - native.identity_checked >= 1000)
            {
                Identity(native);
                native.identity_checked = SDL_GetTicks();
            }
            if (shape_changed || moved || resized)
            {
                Shape(native);
                native.shape = item.rects;
            }
            if (native.texture && (changed || fresh || shape_changed || resized))
            {
                HostSeamless::ComposeItem(item, pixels, mask.width, mask.height, native.pixels);
                SDL_UpdateTexture(native.texture, nullptr, native.pixels.data(), w * 4);
                SDL_RenderClear(native.renderer);
                SDL_RenderCopy(native.renderer, native.texture, nullptr, nullptr);
                SDL_RenderPresent(native.renderer);
                ++updates;
            }
            if (fresh)
            {
                SDL_SysWMinfo info;
                if (Info(native.window, info))
                {
                    auto owner = windows.find(User::Owner(item.id));
                    SDL_SysWMinfo parent;
                    if (owner != windows.end() && owner->second && Info(owner->second->window, parent))
                        XSetTransientForHint(info.info.x11.display, info.info.x11.window,
                                             parent.info.x11.window);
                }
                SDL_ShowWindow(native.window);
                if (item.desktop && Info(native.window, info))
                    XLowerWindow(info.info.x11.display, info.info.x11.window);
            }
        }
        for (auto it = windows.begin(); it != windows.end();)
            if (!present.count(it->first))
            {
                fprintf(stderr, "NEO98 seamless close hwnd=%x\n", it->first);
                it = windows.erase(it);
            }
            else
                ++it;
        const unsigned selected = User::GetForegroundWindow();
        if (selected != foreground)
        {
            foreground = selected;
            Raise(selected);
        }
        if (HasFocus())
            Pointer();
        else
        {
            SDL_SetCursor(SDL_GetDefaultCursor());
            SDL_ShowCursor(SDL_ENABLE);
        }
    }
    bool Raise(unsigned guest = 0)
    {
        if (IsWayland()) return wayland.Raise(guest);
        if (!guest)
            guest = User::GetForegroundWindow();
        auto found = windows.find(guest);
        if (found != windows.end())
        {
            SDL_RaiseWindow(found->second->window);
            SDL_SetWindowInputFocus(found->second->window);
            return true;
        }
        return false;
    }
    void Describe(const char *path) const
    {
        if (IsWayland()) { wayland.Describe(path); return; }
        FILE *file = fopen(path, "w");
        if (!file)
            return;
        fprintf(file, "view seamless=%d windows=%zu updates=%llu area=%d,%d,%d,%d\n", active, windows.size(),
                updates, area.x, area.y, area.w, area.h);
        for (const auto &entry : windows)
        {
            if (!entry.second)
                continue;
            const auto &native = *entry.second;
            int x = 0, y = 0;
            SDL_GetWindowPosition(native.window, &x, &y);
            fprintf(file,
                    "window hwnd=%x native=%u x=%d y=%d width=%d height=%d listed=%d minimized=%d title=%s\n",
                    entry.first, SDL_GetWindowID(native.window), x, y, native.width, native.height,
                    native.item.listed, native.minimized, native.item.title.c_str());
        }
        fclose(file);
    }
};
} // namespace LinuxHost
