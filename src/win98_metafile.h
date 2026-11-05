// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Windows metafile records. The nine-word header contains mtType (1 for
// memory, 2 for file), mtHeaderSize=9, mtVersion=0x0300 and the total word
// count. CloseMetaFile places it at the start of the serialized image.
// The low byte of rdFunction selects the operation; its high byte gives the
// parameter-word count for the table-driven group. Parameters follow 16-bit
// Pascal order, so rdParm stores the last argument first.
namespace Win98Metafile {
constexpr unsigned HeaderBytes = 18, HeaderWords = 9;
constexpr unsigned short MemoryType = 1, FileType = 2, Version = 0x0300;
constexpr unsigned Placeable = 0x9ac6cdd7;
// rdFunction stores the parameter WORD count in its high byte and the player
// index in its low byte.
constexpr unsigned ParameterWords(unsigned function) { return (function >> 8) & 255; }
constexpr unsigned Selector(unsigned function) { return function & 255; }

// shape describes Win32 arguments after HDC in call order: w is a signed
// WORD, u an unsigned WORD, d a DWORD with its low word first, and 0 an
// implicit null argument absent from the record. Read/write shape in reverse
// because rdParm stores the last argument first.
struct Entry { unsigned short function; const char *api; const char *shape; };
inline constexpr Entry Table[] = {
    {0x0201, "SetBkColor", "d"},
    {0x0102, "SetBkMode", "u"},
    {0x0103, "SetMapMode", "u"},
    {0x0104, "SetROP2", "u"},
    {0x0105, "SetRelAbs", "u"},
    {0x0106, "SetPolyFillMode", "u"},
    {0x0107, "SetStretchBltMode", "u"},
    {0x0108, "SetTextCharacterExtra", "w"},
    {0x0209, "SetTextColor", "d"},
    {0x020a, "SetTextJustification", "ww"},
    {0x020b, "SetWindowOrgEx", "ww0"},
    {0x020c, "SetWindowExtEx", "ww0"},
    {0x020d, "SetViewportOrgEx", "ww0"},
    {0x020e, "SetViewportExtEx", "ww0"},
    {0x020f, "OffsetWindowOrgEx", "ww0"},
    {0x0410, "ScaleWindowExtEx", "wwww0"},
    {0x0211, "OffsetViewportOrgEx", "ww0"},
    {0x0412, "ScaleViewportExtEx", "wwww0"},
    {0x0213, "LineTo", "ww"},
    {0x0214, "MoveToEx", "ww0"},
    {0x0415, "ExcludeClipRect", "wwww"},
    {0x0416, "IntersectClipRect", "wwww"},
    {0x0817, "Arc", "wwwwwwww"},
    {0x0418, "Ellipse", "wwww"},
    {0x0419, "FloodFill", "wwd"},
    {0x081a, "Pie", "wwwwwwww"},
    {0x041b, "Rectangle", "wwww"},
    {0x061c, "RoundRect", "wwwwww"},
    {0x061d, "PatBlt", "wwwwd"},
    {0x001e, "SaveDC", ""},
    {0x041f, "SetPixel", "wwd"},
    {0x0220, "OffsetClipRgn", "ww"},
    {0x0127, "RestoreDC", "w"},
    {0x012e, "SetTextAlign", "u"},
    {0x0830, "Chord", "wwwwwwww"},
    {0x0231, "SetMapperFlags", "d"},
    {0x0548, "ExtFloodFill", "wwdu"},
};
constexpr unsigned TableCount = sizeof Table / sizeof Table[0];
inline const Entry *Find(unsigned function)
{
    for (unsigned i = 0; i < TableCount; ++i)
        if (Table[i].function == function)
            return &Table[i];
    return nullptr;
}
inline const Entry *FindApi(unsigned id, unsigned (*hash)(const char *))
{
    for (unsigned i = 0; i < TableCount; ++i)
        if (hash(Table[i].api) == id)
            return &Table[i];
    return nullptr;
}
// Supported records with dedicated handlers outside the table-driven group.
constexpr unsigned short TextOut = 0x0521, ExtTextOut = 0x0a32, Polygon = 0x0324,
                         Polyline = 0x0325, PolyPolygon = 0x0538, SelectObject = 0x012d,
                         DeleteObject = 0x01f0, CreatePenIndirect = 0x02fa,
                         CreateFontIndirect = 0x02fb, CreateBrushIndirect = 0x02fc,
                         Escape = 0x0626, Eof = 0x0000;
// META_ESCAPE with MFCOMMENT contains a comment and has no playback
// operation.
constexpr unsigned short MfComment = 15;
// Parameter WORD count for a fixed-size record.
inline unsigned WordsOfShape(const char *shape)
{
    unsigned words = 0;
    for (const char *p = shape; *p; ++p)
        words += *p == 'd' ? 2 : *p == '0' ? 0 : 1;
    return words;
}
}
