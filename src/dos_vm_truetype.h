// SPDX-License-Identifier: GPL-2.0-or-later
// The TrueType face of the text mode of the MS-DOS window (Lucida Console
// on Windows 98, Courier New on Windows 95; Application::DosVmTrueTypeFace):
// the cells of the OEM code page in one size, black and white, rasterized by
// Win98's own font driver (src/gdi_truetype.cpp). src/dos_vm_truetype.cpp.
#pragma once
namespace GdiTrueType { struct Face; }
namespace DosVmTrueType
{
// The cell heights of the TrueType sizes PIFMGR.DLL carries, for a SYSTEM.INI
// without TTInitialSizes (Application::DosVmTrueTypeCells), and the cell
// width of a height Windows has not measured: the one Lucida Console has.
constexpr unsigned Heights[] = {12, 13, 14, 15, 16, 18, 20, 22};
constexpr unsigned WidthFor(unsigned height) { return (height * 3 + 2) / 5; }
class Face
{
  public:
    ~Face();
    // A.TTF of the tree through the font driver, which the font catalogue
    // started (LUCON.TTF).
    bool Open(const char *file);
    // The regular face of a family by its name ("Courier New"), among the
    // faces the driver has or from the file Windows ships it in.
    bool OpenFamily(const char *family);
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
