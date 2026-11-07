// SPDX-License-Identifier: GPL-2.0-or-later
// The fonts GDI knows, loaded from the Win98 installation.
// Win98 GDI reads three files named in SYSTEM.INI [boot] at start-up: fonts.fon
// is the System font, fixedfon.fon Fixedsys and oemfonts.fon Terminal. Every
// further font is a value of HKEY_CURRENT_CONFIG\Display\Fonts (USER.EXE),
// of HKLM\Software\Microsoft\Windows\CurrentVersion\Fonts (KRNL386.EXE) or a
// line of WIN.INI [fonts]. A.FON is an NE module whose RT_FONT resources
// are Windows 2.x/3.0 FNT images; each becomes one face here. A.TTF becomes
// one face through Win98's own font driver (src/gdi_truetype.cpp); its `font`
// is the realization at GDI's default height and describes the face for
// enumeration, CreateFont realizes it anew at the requested size. Nothing in
// this catalogue is a constant: without the
// files, it stays empty. Only standalone comparison runners install
// built-in SSERIFE.FON resources instead (see InstallFnt).
#pragma once
#include "dib_text.h"
namespace GdiTrueType { struct Face; }
namespace GdiFonts
{
enum Role { SystemRole, FixedRole, OemRole, RoleCount };
enum Type { Raster = 1, TrueType = 4 };
struct Face
{
    DibText::Font font;
    unsigned type = 0;
    unsigned ppem = 0;   // TrueType: the em height of `font`
    const GdiTrueType::Face *truetype = nullptr;
    char file[16] = {};  // the file as the registry or SYSTEM.INI names it
    char name[32] = {};  // dfFace
};
void Reset();
unsigned Count();
const Face *At(unsigned index);
// Every RT_FONT resource of an NE font file. Answers the number of faces added.
unsigned InstallFontFile(const char *file, const unsigned char *bytes, unsigned size);
// One FNT image. `copy` false keeps a reference to data that lives as long as
// the process (the built-in resources).
bool InstallFnt(const char *file, const unsigned char *fnt, unsigned size, bool copy);
// A TrueType file of the installation through the font driver. Answers the
// number of faces added (0 or 1).
unsigned InstallTrueType(const char *file);
bool Installed(const char *file);
void SetRole(Role, const char *file);
const Face *RoleFace(Role);
// WIN.INI [FontSubstitutes]: a face name that stands for another.
void AddSubstitute(const char *from, const char *to);
const char *Substitute(const char *face);
bool SameFace(const char *a, const char *b);
// The physical font for a logical request, following the rules of
// Null when no installed face carries the name.
const Face *Match(const char *face, int height, unsigned weight, bool italic, unsigned charset);
} // namespace GdiFonts
