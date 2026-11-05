// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "user_settings.h"
#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
void Application::LoadDesktopWallpaper()
{
    if (!User::SystemSettings().wallpaper_checked) SetDesktopWallpaper(nullptr, 0xffffffffu);
}
// USER reads HKCU\Control Panel\Desktop. The profile fallback is
// needed before the user's hive has been loaded during session startup.
void Application::DesktopSetting(const char *key, const char *fallback, char *out, unsigned capacity)
{
    auto &registry = Registry::System();
    unsigned node = Registry::Missing;
    bool created = false;
    const Registry::Value *value = nullptr;
    if (!registry.Open(registry.roots[1], "Control Panel\\Desktop", false, node, created) &&
        !registry.Query(node, key, value) && value && value->type == 1 && value->data && value->size)
    {
        const unsigned size = value->size < capacity - 1 ? value->size : capacity - 1;
        memcpy(out, value->data, size);
        out[size] = 0;
        return;
    }
    ProfileString("Desktop", key, fallback, out, capacity);
}

// USER, with bitmap loading. The bitmap belongs
// to USER, so the process which applied the setting may exit afterwards.
bool Application::SetDesktopWallpaper(const char *filename, unsigned sentinel)
{
    auto &s = User::SystemSettings();
    if (sentinel == 0xfffffffeu && !(s.wallpaper_style & 2)) return true;
    char path[260] = {};
    const bool configured = !filename || sentinel == 0xffffffffu || sentinel == 0xfffffffeu;
    if (configured)
        DesktopSetting("Wallpaper", "", path, sizeof path);
    else
        snprintf(path, sizeof path, "%s", filename);
    s.wallpaper_checked = true;
    if (s.wallpaper_bitmap) Gdi::SysDeleteObject(s.wallpaper_bitmap);
    s.wallpaper_bitmap = 0;
    s.wallpaper_style = 0;
    User::InvalidateRect(User::GetDesktopWindow(), nullptr, true);
    if (configured && !path[0]) return false; // empty profile query
    if (!path[0] || (path[0] == '(' && (path[1] | 32) == 'n' &&
        (path[2] | 32) == 'o' && (path[3] | 32) == 'n' &&
        (path[4] | 32) == 'e' && path[5] == ')' && !path[6])) return true;
    char tile[16] = {}, style[16] = {};
    DesktopSetting("TileWallpaper", "1", tile, sizeof tile);
    DesktopSetting("WallpaperStyle", "0", style, sizeof style);
    unsigned mode = unsigned(atoi(tile) | atoi(style)) & 65535;
    // USER enables stretching only with Plus! and without tiling.
    if (!s.plus || (mode & 1)) mode &= ~2u;
    unsigned name = Allocate(unsigned(strlen(path)) + 1);
    if (!name) return false;
    Store(name, path, unsigned(strlen(path)) + 1);
    const unsigned load[] = {0, name, 0,
        mode & 2 ? unsigned(s.monitor[2] - s.monitor[0]) : 0,
        mode & 2 ? unsigned(s.monitor[3] - s.monitor[1]) : 0, 0x2010};
    unsigned bitmap = ImageApi("LoadImageA", load);
    Free(name);
    if (!bitmap) return false;
    Gdi::SetObjectOwner(bitmap, 0);
    Gdi::MakeObjectPrivate(bitmap, true);
    for (auto &object : gdi_objects) if (object.handle == bitmap) object = {};
    s.wallpaper_bitmap = bitmap;
    s.wallpaper_style = mode;
    return true;
}

unsigned Application::PaintDesktop(unsigned dc)
{
    if (!User::SystemSettings().wallpaper_checked) SetDesktopWallpaper(nullptr, 0xffffffffu);
    const unsigned result = User::PaintDesktop(dc);
    if (result) User::DrawingChanged(dc);
    return result;
}
} // namespace X86
