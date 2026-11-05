// SPDX-License-Identifier: GPL-2.0-or-later
// Complete top-level images. GDI keeps guest screen coordinates in each image.
namespace
{
HWND SurfaceRoot(HWND h)
{
    for (unsigned guard = 0; h && h != Desktop && guard < MaxWindows; ++guard)
    {
        const auto *w = Find(h);
        if (!w)
            return 0;
        const HWND parent = Container(*w);
        if (!parent || parent == Desktop)
            return h;
        h = parent;
    }
    return 0;
}
void FreeHostSurface(Window &w)
{
    if (w.host_bits)
        Gdi::RetargetSurface(w.host_bits, ScreenBitmapView(), nullptr, 0);
    if (w.host_dc)
        Gdi::DeleteDC(w.host_dc);
    delete[] w.host_bits;
    w.host_bits = nullptr;
    w.host_bytes = 0;
    w.host_dc = 0;
    w.host_width = w.host_height = 0;
}
DibEng::Bitmap HostBitmap(HWND h)
{
    const auto base = ScreenBitmapView();
    const HWND root = SurfaceRoot(h);
    auto *w = Find(root);
    if (!host_window_surfaces || !w || base.depth != 32)
        return base;
    const auto rect = WindowScreenRect(*w);
    if (!w->host_bits || w->host_width != base.width || w->host_height != base.height)
    {
        FreeHostSurface(*w);
        w->host_width = base.width;
        w->host_height = base.height;
        w->host_bytes = unsigned(base.width) * unsigned(base.height) * 4;
        w->host_bits = new unsigned char[w->host_bytes]{};
        w->host_x = rect.left;
        w->host_y = rect.top;
        InvalidateOne(root, nullptr, true);
    }
    else if (rect.left != w->host_x || rect.top != w->host_y)
    {
        const int dx = rect.left - w->host_x, dy = rect.top - w->host_y;
        auto *moved = new unsigned char[w->host_bytes]{};
        const int left = dx > 0 ? dx : 0, top = dy > 0 ? dy : 0;
        const int right = dx < 0 ? base.width + dx : base.width;
        const int bottom = dy < 0 ? base.height + dy : base.height;
        if (right > left)
            for (int y = top; y < bottom; ++y)
                memcpy(moved + (size_t(y) * base.width + left) * 4,
                       w->host_bits + (size_t(y - dy) * base.width + left - dx) * 4,
                       size_t(right - left) * 4);
        Gdi::RetargetSurface(w->host_bits, {moved, base.width, base.height, base.width * 4, 32}, nullptr, 0);
        delete[] w->host_bits;
        w->host_bits = moved;
        w->host_x = rect.left;
        w->host_y = rect.top;
    }
    return {w->host_bits, base.width, base.height, base.width * 4, 32};
}
Gdi::HDC HostFrameDC(HWND h)
{
    auto *w = Find(SurfaceRoot(h));
    if (!w || !host_window_surfaces)
        return screen;
    const auto bitmap = HostBitmap(h);
    if (bitmap.depth != 32)
        return screen;
    if (!w->host_dc)
    {
        w->host_dc = Gdi::CreateSurfaceDC(bitmap);
        Gdi::SelectObject(w->host_dc, system_font);
    }
    return w->host_dc ? w->host_dc : screen;
}
} // namespace
bool HostWindowSurfaces()
{
    return host_window_surfaces;
}
void SetHostWindowSurfaces(bool enabled)
{
    if (host_window_surfaces == enabled)
        return;
    host_window_surfaces = enabled;
    host_pointer_window = 0;
    if (!enabled)
        for (auto &w : windows)
            FreeHostSurface(w);
    ResetShown();
    for (unsigned i = 0; i < MaxWindows; ++i)
        if (windows[i].used)
        {
            windows[i].native_invalid = true;
            InvalidateOne(Handle(i), nullptr, true);
        }
    InvalidateOne(Desktop, nullptr, true);
    dirty.Mark();
}
const unsigned char *HostWindowPixels(HWND h, int &width, int &height, int &stride)
{
    width = height = stride = 0;
    if (!host_window_surfaces || !Find(h))
        return nullptr;
    SettleGeometry();
    const auto bitmap = HostBitmap(h);
    if (bitmap.depth != 32)
        return nullptr;
    const auto frame = WindowScreenRect(*Find(h));
    if (frame.left < 0 || frame.top < 0 || frame.right > bitmap.width || frame.bottom > bitmap.height)
        return nullptr;
    width = frame.right - frame.left;
    height = frame.bottom - frame.top;
    stride = bitmap.stride;
    return bitmap.bits + size_t(frame.top) * stride + size_t(frame.left) * 4;
}
void SetHostPointerWindow(HWND h)
{
    host_pointer_window = IsWindow(h) ? h : 0;
}
void ComposeHostWindows()
{
    HWND order[MaxWindows];
    unsigned count = 0;
    for (HWND h = desktop.first; h && count < MaxWindows;)
    {
        const auto *w = Find(h);
        if (!w)
            break;
        order[count++] = h;
        h = w->next;
    }
    const auto target = ScreenBitmapView();
    while (count)
    {
        const HWND h = order[--count];
        auto *w = Find(h);
        if (!w || !ShowsTree(h) || !w->host_bits)
            continue;
        const auto source = HostBitmap(h);
        RectList shape;
        WindowShape(*w, true, shape);
        shape.Clip({0, 0, target.width, target.height});
        for (unsigned i = 0; i < shape.n; ++i)
        {
            const auto r = shape.r[i];
            for (int y = r.top; y < r.bottom; ++y)
                memcpy(target.bits + size_t(y) * target.stride + size_t(r.left) * 4,
                       source.bits + size_t(y) * source.stride + size_t(r.left) * 4,
                       size_t(r.right - r.left) * 4);
        }
    }
}
