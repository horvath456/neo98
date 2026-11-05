// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Enhanced metafile records. Windows 98 PlayEnhMetaFileRecord returns FALSE
// for unsupported record types. Each record starts with DWORD iType and
// nSize; nSize includes the header and is divisible by four. Fields retain
// their natural order, unlike WMF parameter words.
namespace Win98MetafileEmf {
constexpr unsigned HeaderBytes = 88;
constexpr unsigned Signature = 0x464d4520; // ' EMF'
constexpr unsigned VersionValue = 0x00010000;
constexpr unsigned Header = 1, Eof = 14, SelectObject = 37, CreatePen = 38,
                   CreateBrushIndirect = 39, DeleteObject = 40, Polygon16 = 86,
                   Polyline16 = 87, ExtCreateFontIndirectW = 82,
                   ExtTextOutA = 83, ExtTextOutW = 84, StockObject = 0x80000000;
// shape describes arguments after HDC in call order: l is a signed LONG, d a
// DWORD and 0 an implicit null argument absent from the record.
struct Entry { unsigned type; const char *api; const char *shape; };
inline constexpr Entry Table[] = {
    {15, "SetPixelV", "lld"},
    {9, "SetWindowExtEx", "ll0"},
    {10, "SetWindowOrgEx", "ll0"},
    {11, "SetViewportExtEx", "ll0"},
    {12, "SetViewportOrgEx", "ll0"},
    {13, "SetBrushOrgEx", "ll0"},
    {17, "SetMapMode", "d"},
    {18, "SetBkMode", "d"},
    {19, "SetPolyFillMode", "d"},
    {20, "SetROP2", "d"},
    {21, "SetStretchBltMode", "d"},
    {22, "SetTextAlign", "d"},
    {24, "SetTextColor", "d"},
    {25, "SetBkColor", "d"},
    {26, "OffsetClipRgn", "ll"},
    {27, "MoveToEx", "ll0"},
    {29, "ExcludeClipRect", "llll"},
    {30, "IntersectClipRect", "llll"},
    {31, "ScaleViewportExtEx", "llll0"},
    {32, "ScaleWindowExtEx", "llll0"},
    {33, "SaveDC", ""},
    {34, "RestoreDC", "l"},
    {42, "Ellipse", "llll"},
    {43, "Rectangle", "llll"},
    {44, "RoundRect", "llllll"},
    {45, "Arc", "llllllll"},
    {46, "Chord", "llllllll"},
    {47, "Pie", "llllllll"},
    {54, "LineTo", "ll"},
    {57, "SetArcDirection", "d"},
    {59, "BeginPath", ""},
    {60, "EndPath", ""},
    {61, "CloseFigure", ""},
    {68, "AbortPath", ""},
};
constexpr unsigned TableCount = sizeof Table / sizeof Table[0];
inline const Entry *Find(unsigned type)
{
    for (unsigned i = 0; i < TableCount; ++i)
        if (Table[i].type == type)
            return &Table[i];
    return nullptr;
}
}
