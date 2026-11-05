// SPDX-License-Identifier: GPL-2.0-or-later
// USER dispatches to the background brush or the bitmap painter.
// The bitmap can be tiled or centered. The target
// DC's clip remains in force for every blit.
unsigned PaintDesktop(unsigned dc)
{
    auto &s = User::SystemSettings();
    Gdi::RECT clip = {};
    if (!Gdi::GetClipBox(dc, &clip)) return 0;
    const unsigned color = Win98Frame::SystemColors()[1];
    unsigned brush = Gdi::CreateSolidBrush(Gdi::RGB(color >> 16, color >> 8, color));
    const bool filled = Gdi::FillRect(dc, &clip, brush);
    Gdi::DeleteObject(brush);
    if (!filled) return 0;
    int width = 0, height = 0;
    if (s.wallpaper_bitmap && Gdi::BitmapDimensions(s.wallpaper_bitmap, width, height))
    {
        unsigned source = Gdi::CreateCompatibleDC(dc);
        unsigned old = source ? Gdi::SelectObject(source, s.wallpaper_bitmap) : 0;
        if (old)
        {
            if (s.wallpaper_style & 1)
            {
                int x = s.monitor[0], y = s.monitor[1];
                while (x + width < clip.left) x += width;
                while (x > clip.left) x -= width;
                while (y + height < clip.top) y += height;
                while (y > clip.top) y -= height;
                for (int row = y; row < clip.bottom; row += height)
                    for (int col = x; col < clip.right; col += width)
                        Gdi::BitBlt(dc, col, row, width, height, source, 0, 0, 0x00cc0020);
            }
            else
            {
                const int x = (s.monitor[0] + s.monitor[2] - width) / 2;
                const int y = (s.monitor[1] + s.monitor[3] - height) / 2;
                Gdi::BitBlt(dc, x, y, width, height, source, 0, 0, 0x00cc0020);
            }
            Gdi::SelectObject(source, old);
        }
        if (source) Gdi::DeleteDC(source);
    }
    return 1;
}
