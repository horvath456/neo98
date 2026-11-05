// SPDX-License-Identifier: GPL-2.0-or-later
// Lucida Console for the text mode of the MS-DOS window: the cells of the
// OEM code page in one size, black and white, rasterized by Win98's own font
// driver (src/gdi_truetype.cpp). src/dos_vm_truetype.cpp.
#pragma once
namespace GdiTrueType { struct Face; }
namespace DosVmTrueType
{
// The TrueType sizes the MS-DOS window offers, as heights of Lucida
// Console; the cell width is the one that face gives at that height. The
// Font page and the size box of the toolbar list the same.
constexpr unsigned Heights[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22};
constexpr unsigned WidthFor(unsigned height) { return (height * 3 + 2) / 5; }
class Face
{
  public:
    ~Face();
    // A.TTF of the tree through the font driver, which the font catalogue
    // started (LUCON.TTF).
    bool Open(const char *file);
    bool Ready() const { return face != nullptr; }
    // The ink of OEM character `character` in a cell of width x height, one
    // byte a pixel (0 or 1), valid until the next call with another size.
    const unsigned char *Glyph(unsigned character, unsigned width, unsigned height);

  private:
    const GdiTrueType::Face *face = nullptr;
    unsigned char *cache = nullptr;
    bool *cached = nullptr;
    unsigned cell_width = 0, cell_height = 0;
};
} // namespace DosVmTrueType
