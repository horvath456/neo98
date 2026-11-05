// SPDX-License-Identifier: GPL-2.0-or-later
// Native Windows ABI adapter. New infrastructure, with native USER behavior.
#include "x86_app.h"
#include "pc_vga.h"
#include "arm64_cpu.h"
#include "arm64_machine.h"
#include "cd_audio.h"
#include "sound.h"
#include "dos_vm.h"
#include "dll_routes.h"
#include "win98_broadcast.h"
#include "win98_menu_public.h"
#include "win98_menu_exports.h"
#include "interface_kernel_unsupported.h"
#include "win98_win_event.h"
#include "win98_clipboard_exports.h"
#include "win98_image_exports.h"
#include "win98_class_exports.h"
#include "win98_keyboard_exports.h"
#include "win98_dc_exports.h"
#include "win98_gdi_arc_exports.h"
#include "win98_gdi_missing_exports.h"
#include "win98_font_exports.h"
#include "win98_font_width_exports.h"
#include "win98_font_create_exports.h"
#include "win98_window_text_exports.h"
#include "win98_message_box_exports.h"
#include "win98_print_exports.h"
#include "win98_security_identity_exports.h"
#include "win98_advapi32_exports.h"
#include "win98_winmm_exports.h"
#include "win98_caption_exports.h"
#include "win98_coordinates.h"
#include "win98_monitors.h"
#include "win98_gdi_text_layout.h"
#include "win98_crt_output.h"
#include "win98_user_character_exports.h"
#include "win98_user32_wide_exports.h"
#include "win98_shell_icon_exports.h"
#include "win98_shell_search_exports.h"
#include "win98_shell_path_syntax_exports.h"
#include "win98_path_probe_exports.h"
#include "interface_native_module_exports.h"
#include "interface_export_contract.h"
#include "jit_memory.h"
#include "user_draw.h"
#include "win98_frame_control.h"
#include "win98_rect.h"
#include "win98_send_message.h"
#include "win98_kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // strcasecmp: newlib keeps it out of string.h
#include "../sdk/lib/neo98_touch_abi.h"

namespace X86
{
namespace
{
constexpr unsigned Gates = Application::GateBase, Callback = 0x7000fff0, Exit = 0x7000ffe0;
struct ApiSpec
{
    const char *dll, *name;
    unsigned argc;
};
const ApiSpec specs[] = {{"NEO98TOUCH.DLL", "Neo98TouchGetCaps", 3},
                         {"NEO98TOUCH.DLL", "Neo98TouchRegister", 3},
                         {"NEO98TOUCH.DLL", "Neo98TouchUnregister", 1},
                         {"NEO98TOUCH.DLL", "Neo98TouchRead", 3},
                         {"GDI32.DLL", "GetBkColor", 1},
                         {"GDI32.DLL", "GetBkMode", 1},
                         {"GDI32.DLL", "GdiGetBatchLimit", 0},
                         {"GDI32.DLL", "GdiSetBatchLimit", 1},
                         {"GDI32.DLL", "SetGraphicsMode", 2},
                         {"GDI32.DLL", "GetICMProfileA", 3},
                         {"GDI32.DLL", "ColorMatchToTarget", 3},
                         {"GDI32.DLL", "GetRasterizerCaps", 2},
                         {"GDI32.DLL", "UpdateColors", 1},
                         {"GDI32.DLL", "GetTextColor", 1},
                         {"USER32.DLL", "ScrollDC", 7},
                         {"GDI32.DLL", "EnumObjects", 4},
                         {"GDI32.DLL", "AddFontResourceA", 1},
                         {"GDI32.DLL", "RemoveFontResourceA", 1},
                         {"GDI32.DLL", "CreateScalableFontResourceA", 4},
                         {"GDI32.DLL", "CreateColorSpaceA", 1},
                         {"GDI32.DLL", "DeleteColorSpace", 1},
                         {"GDI32.DLL", "GetLogColorSpaceA", 3},
                         {"GDI32.DLL", "SetColorSpace", 2},
                         {"GDI32.DLL", "GetColorSpace", 1},
                         {"GDI32.DLL", "SetICMProfileA", 2},
                         {"GDI32.DLL", "ordinal_100", 5},
                         {"GDI32.DLL", "ordinal_101", 4},
                         {"GDI32.DLL", "ordinal_102", 7},
                         {"GDI32.DLL", "ordinal_103", 4},
                         {"GDI32.DLL", "ordinal_104", 5},
                         {"WINMM.DLL", "DriverCallback", 7},
                         {"WINMM.DLL", "waveInGetErrorTextA", 3},
                         {"WINMM.DLL", "midiInGetErrorTextA", 3},
                         {"KERNEL32.DLL", "GetDaylightFlag", 0},
                         {"KERNEL32.DLL", "SetDaylightFlag", 1},
                         {"KERNEL32.DLL", "CompareStringA", 6},
                         {"USER32.DLL", "CharLowerA", 1},
                         {"USER32.DLL", "CharUpperA", 1},
                         {"USER32.DLL", "CharLowerBuffA", 2},
                         {"USER32.DLL", "CharUpperBuffA", 2},
                         {"USER32.DLL", "DrawCaption", 4},
                         {"USER32.DLL", "DrawCaptionTempA", 7},
                         {"KERNEL32.DLL", "WinExec", 2},
                         {"KERNEL32.DLL", "LoadModule", 2},
                         {"KERNEL32.DLL", "CreateProcessA", 10},
                         {"KERNEL32.DLL", "GetExitCodeProcess", 2},
                         {"KERNEL32.DLL", "AddAtomA", 1},
                         {"GDI32.DLL", "CreatePen", 3},
                         {"GDI32.DLL", "CreatePenIndirect", 1},
                         {"GDI32.DLL", "ExtCreatePen", 5},
                         {"GDI32.DLL", "MoveToEx", 4},
                         {"GDI32.DLL", "GetCurrentPositionEx", 2},
                         {"GDI32.DLL", "LineTo", 3},
                         {"GDI32.DLL", "Polyline", 3},
                         {"GDI32.DLL", "Rectangle", 5},
                         {"GDI32.DLL", "Ellipse", 5},
                         {"GDI32.DLL", "Arc", 9},
                         {"GDI32.DLL", "Pie", 9},
                         {"GDI32.DLL", "Chord", 9},
                         {"GDI32.DLL", "RoundRect", 7},
                         {"GDI32.DLL", "Polygon", 3},
                         {"GDI32.DLL", "PolyPolygon", 4},
                         {"GDI32.DLL", "GetPolyFillMode", 1},
                         {"GDI32.DLL", "SetPolyFillMode", 2},
                         {"GDI32.DLL", "SetROP2", 2},
                         {"GDI32.DLL", "GetROP2", 1},

                         {"GDI32.DLL", "CreateCompatibleBitmap", 3},
                         {"GDI32.DLL", "CreateDiscardableBitmap", 3},
                         {"GDI32.DLL", "StretchBlt", 11},
                         {"GDI32.DLL", "GetStretchBltMode", 1},
                         {"GDI32.DLL", "SetStretchBltMode", 2},
                         {"GDI32.DLL", "CreateDIBitmap", 6},
                         {"GDI32.DLL", "CreateDIBSection", 6},
                         {"GDI32.DLL", "GetDIBits", 7},
                         {"GDI32.DLL", "SetDIBits", 7},
                         {"GDI32.DLL", "SetDIBitsToDevice", 12},
                         {"GDI32.DLL", "StretchDIBits", 13},
                         {"GDI32.DLL", "GetDIBColorTable", 4},
                         {"GDI32.DLL", "SetDIBColorTable", 4},
                         {"KERNEL32.DLL", "GlobalAddAtomA", 1},
                         {"KERNEL32.DLL", "GlobalFindAtomA", 1},
                         {"KERNEL32.DLL", "GlobalDeleteAtom", 1},
                         {"KERNEL32.DLL", "GlobalGetAtomNameA", 3},
                         {"KERNEL32.DLL", "GlobalAddAtomW", 1},
                         {"KERNEL32.DLL", "GlobalFindAtomW", 1},
                         {"KERNEL32.DLL", "GlobalGetAtomNameW", 3},
                         {"USER32.DLL", "GetPropA", 2},
                         {"USER32.DLL", "SetPropA", 3},
                         {"USER32.DLL", "RemovePropA", 2},
                         {"KERNEL32.DLL", "FindAtomA", 1},
                         {"KERNEL32.DLL", "DeleteAtom", 1},
                         {"KERNEL32.DLL", "GetAtomNameA", 3},
                         {"KERNEL32.DLL", "InitAtomTable", 1},
                         {"GDI32.DLL", "CreatePalette", 1},
                         {"GDI32.DLL", "CreateHalftonePalette", 1},
                         {"GDI32.DLL", "GetPaletteEntries", 4},
                         {"GDI32.DLL", "SetPaletteEntries", 4},
                         {"GDI32.DLL", "ResizePalette", 2},
                         {"GDI32.DLL", "GetNearestPaletteIndex", 2},
                         {"GDI32.DLL", "SelectPalette", 3},
                         {"GDI32.DLL", "RealizePalette", 1},
                         {"GDI32.DLL", "AnimatePalette", 4},
                         {"GDI32.DLL", "GetSystemPaletteEntries", 4},
                         {"GDI32.DLL", "GetSystemPaletteUse", 1},
                         {"GDI32.DLL", "SetSystemPaletteUse", 2},
                         {"GDI32.DLL", "CreateSolidBrush", 1},
{"GDI32.DLL", "CreatePatternBrush", 1},
{"GDI32.DLL", "CreateHatchBrush", 2},
{"GDI32.DLL", "CreateBrushIndirect", 1},
{"GDI32.DLL", "CreateEllipticRgn", 4},
{"GDI32.DLL", "CreateEllipticRgnIndirect", 1},
{"GDI32.DLL", "CreateRoundRectRgn", 6},
{"GDI32.DLL", "FillRgn", 3},
{"GDI32.DLL", "FrameRgn", 5},
{"GDI32.DLL", "InvertRgn", 2},
{"GDI32.DLL", "PaintRgn", 2},
{"GDI32.DLL", "UnrealizeObject", 1},
{"GDI32.DLL", "GdiFlush", 0},
{"GDI32.DLL", "GetGraphicsMode", 1},
{"GDI32.DLL", "GetBitmapDimensionEx", 2},
{"GDI32.DLL", "SetBitmapDimensionEx", 4},
{"GDI32.DLL", "GetTextExtentExPointA", 7},
{"GDI32.DLL", "SetMapperFlags", 2},
{"GDI32.DLL", "GetCharacterPlacementA", 6},
{"GDI32.DLL", "GetGlyphOutlineA", 7},
{"GDI32.DLL", "GetFontData", 5},
{"GDI32.DLL", "GetOutlineTextMetricsA", 3},
{"GDI32.DLL", "GetKerningPairsA", 3},
{"GDI32.DLL", "Escape", 5},
{"GDI32.DLL", "ExtEscape", 6},
{"GDI32.DLL", "StartDocA", 2},
{"GDI32.DLL", "EndDoc", 1},
{"GDI32.DLL", "StartPage", 1},
{"GDI32.DLL", "EndPage", 1},
{"GDI32.DLL", "AbortDoc", 1},
{"GDI32.DLL", "SetAbortProc", 2},
{"GDI32.DLL", "ResetDCA", 2},
{"GDI32.DLL", "ChoosePixelFormat", 2},
{"GDI32.DLL", "DescribePixelFormat", 4},
{"GDI32.DLL", "SetPixelFormat", 3},
{"GDI32.DLL", "GetPixelFormat", 1},
{"GDI32.DLL", "SwapBuffers", 1},
{"GDI32.DLL", "GetDeviceGammaRamp", 2},
{"GDI32.DLL", "SetDeviceGammaRamp", 2},
{"GDI32.DLL", "SetICMMode", 2},
{"GDI32.DLL", "BeginPath", 1},
{"GDI32.DLL", "EndPath", 1},
{"GDI32.DLL", "AbortPath", 1},
{"GDI32.DLL", "CloseFigure", 1},
{"GDI32.DLL", "FillPath", 1},
{"GDI32.DLL", "StrokePath", 1},
{"GDI32.DLL", "StrokeAndFillPath", 1},
{"GDI32.DLL", "FlattenPath", 1},
{"GDI32.DLL", "WidenPath", 1},
{"GDI32.DLL", "PathToRegion", 1},
{"GDI32.DLL", "SelectClipPath", 2},
{"GDI32.DLL", "GetPath", 4},
{"GDI32.DLL", "SetArcDirection", 2},
{"GDI32.DLL", "GetArcDirection", 1},
{"GDI32.DLL", "PolyBezier", 3},
{"GDI32.DLL", "PolyBezierTo", 3},
{"GDI32.DLL", "PolylineTo", 3},
{"GDI32.DLL", "PolyPolyline", 4},
{"GDI32.DLL", "FloodFill", 4},
{"GDI32.DLL", "ExtFloodFill", 5},
{"GDI32.DLL", "CreateMetaFileA", 1},
{"GDI32.DLL", "CreateMetaFileW", 1},
{"GDI32.DLL", "CloseMetaFile", 1},
{"GDI32.DLL", "DeleteMetaFile", 1},
{"GDI32.DLL", "CopyMetaFileA", 2},
{"GDI32.DLL", "GetMetaFileA", 1},
{"GDI32.DLL", "GetMetaFileBitsEx", 3},
{"GDI32.DLL", "SetMetaFileBitsEx", 2},
{"GDI32.DLL", "PlayMetaFile", 2},
{"GDI32.DLL", "PlayMetaFileRecord", 4},
{"GDI32.DLL", "EnumMetaFile", 4},
{"GDI32.DLL", "CreateEnhMetaFileA", 4},
{"GDI32.DLL", "CloseEnhMetaFile", 1},
{"GDI32.DLL", "DeleteEnhMetaFile", 1},
{"GDI32.DLL", "CopyEnhMetaFileA", 2},
{"GDI32.DLL", "GetEnhMetaFileBits", 3},
{"GDI32.DLL", "SetEnhMetaFileBits", 2},
{"GDI32.DLL", "SetWinMetaFileBits", 4},
{"GDI32.DLL", "GetWinMetaFileBits", 5},
{"GDI32.DLL", "GetEnhMetaFileHeader", 3},
{"GDI32.DLL", "GetEnhMetaFilePaletteEntries", 3},
{"GDI32.DLL", "GetEnhMetaFileDescriptionA", 3},
{"GDI32.DLL", "PlayEnhMetaFile", 3},
{"GDI32.DLL", "PlayEnhMetaFileRecord", 4},
{"GDI32.DLL", "EnumEnhMetaFile", 5},
{"GDI32.DLL", "gdiPlaySpoolStream", 5},
                         {"GDI32.DLL", "CreateRectRgn", 4},
                         {"GDI32.DLL", "CreatePolygonRgn", 3},
                         {"GDI32.DLL", "CreateRectRgnIndirect", 1},
                         {"GDI32.DLL", "SetRectRgn", 5},
                         {"GDI32.DLL", "CombineRgn", 4},
                         {"GDI32.DLL", "OffsetRgn", 3},
                         {"GDI32.DLL", "GetRgnBox", 2},
                         {"GDI32.DLL", "PtInRegion", 3},
                         {"GDI32.DLL", "RectInRegion", 2},
                         {"GDI32.DLL", "EqualRgn", 2},
                         {"GDI32.DLL", "GetRegionData", 3},
                         {"GDI32.DLL", "ExtCreateRegion", 3},
                         {"GDI32.DLL", "SelectClipRgn", 2},
                         {"GDI32.DLL", "ExtSelectClipRgn", 3},
                         {"GDI32.DLL", "GetClipRgn", 2},
                         {"GDI32.DLL", "GetRandomRgn", 3},
                         {"GDI32.DLL", "OffsetClipRgn", 3},
{"GDI32.DLL", "CreateBitmap", 5},
{"GDI32.DLL", "CreateBitmapIndirect", 1},
{"GDI32.DLL", "GetBitmapBits", 3},
{"GDI32.DLL", "SetBitmapBits", 3},
{"GDI32.DLL", "CreateDCA", 4},
                         {"GDI32.DLL", "CreateICA", 4},
                         {"GDI32.DLL", "CreateCompatibleDC", 1},
{"GDI32.DLL", "DeleteDC", 1},
{"GDI32.DLL", "SaveDC", 1},
{"GDI32.DLL", "RestoreDC", 2},
{"GDI32.DLL", "SetViewportOrgEx", 4},
{"KERNEL32.DLL", "FindResourceW", 3},
{"KERNEL32.DLL", "FindResourceExW", 4},
{"KERNEL32.DLL", "EnumResourceTypesA", 3},
{"KERNEL32.DLL", "EnumResourceTypesW", 3},
{"KERNEL32.DLL", "EnumResourceNamesA", 4},
{"KERNEL32.DLL", "EnumResourceNamesW", 4},
{"KERNEL32.DLL", "CopyFileA", 3},
{"KERNEL32.DLL", "CreateDirectoryExA", 3},
{"KERNEL32.DLL", "GetFileAttributesExA", 3},
{"KERNEL32.DLL", "GetFileInformationByHandle", 2},
{"KERNEL32.DLL", "RtlFillMemory", 3},
{"KERNEL32.DLL", "RtlZeroMemory", 2},
{"KERNEL32.DLL", "GetSystemTimeAsFileTime", 1},
{"GDI32.DLL", "GetBrushOrgEx", 2},
{"USER32.DLL", "FrameRect", 3},
{"USER32.DLL", "InvertRect", 2},
{"GDI32.DLL", "SetBrushOrgEx", 4},
{"GDI32.DLL", "GetMapMode", 1},
{"GDI32.DLL", "SetMapMode", 2},
{"GDI32.DLL", "GetWindowOrgEx", 2},
{"GDI32.DLL", "GetViewportOrgEx", 2},
{"GDI32.DLL", "GetWindowExtEx", 2},
{"GDI32.DLL", "GetViewportExtEx", 2},
{"GDI32.DLL", "GetDCOrgEx", 2},
{"GDI32.DLL", "SetWindowOrgEx", 4},
{"GDI32.DLL", "OffsetWindowOrgEx", 4},
{"GDI32.DLL", "OffsetViewportOrgEx", 4},
{"GDI32.DLL", "SetWindowExtEx", 4},
{"GDI32.DLL", "SetViewportExtEx", 4},
{"GDI32.DLL", "ScaleWindowExtEx", 6},
{"GDI32.DLL", "ScaleViewportExtEx", 6},
{"GDI32.DLL", "LPtoDP", 3},
{"GDI32.DLL", "DPtoLP", 3},
{"GDI32.DLL", "IntersectClipRect", 5},
{"GDI32.DLL", "ExcludeClipRect", 5},
{"GDI32.DLL", "SetBkMode", 2},
{"GDI32.DLL", "SetTextCharacterExtra", 2},
{"GDI32.DLL", "SetTextJustification", 3},
{"GDI32.DLL", "SetTextAlign", 2},
{"GDI32.DLL", "SetObjectOwner", 2},
{"GDI32.DLL", "RectVisible", 2},
{"GDI32.DLL", "GetClipBox", 2},
{"GDI32.DLL", "PtVisible", 3},
{"GDI32.DLL", "GetObjectType", 1},
{"GDI32.DLL", "GetCurrentObject", 2},
{"GDI32.DLL", "GetTextAlign", 1},
{"GDI32.DLL", "GetTextCharacterExtra", 1},
{"GDI32.DLL", "GetTextExtentPoint32A", 4},
{"GDI32.DLL", "GetCharWidth32A", 4},
{"GDI32.DLL", "TextOutA", 5},
{"GDI32.DLL", "ExtTextOutA", 8},
{"GDI32.DLL", "ExtTextOutW", 8},
{"GDI32.DLL", "TextOutW", 5},
{"GDI32.DLL", "GetTextExtentPointW", 4},
{"GDI32.DLL", "GetTextExtentPoint32W", 4},
{"GDI32.DLL", "PatBlt", 6},
{"GDI32.DLL", "BitBlt", 9},
{"GDI32.DLL", "GetPixel", 3},
{"GDI32.DLL", "SetPixel", 4},
{"GDI32.DLL", "SetPixelV", 4},
{"GDI32.DLL", "GetNearestColor", 2},
{"USER32.DLL", "FillRect", 3},
{"USER32.DLL", "DrawFocusRect", 2},
{"COMCTL32.DLL", "InitCommonControlsEx", 1},{"COMCTL32.DLL", "DllGetVersion", 1},
{"COMCTL32.DLL", "InitCommonControls", 0},{"KERNEL32.DLL", "GetEnvironmentStrings", 0},
                         {"KERNEL32.DLL", "GetEnvironmentStringsA", 0},
                         {"KERNEL32.DLL", "FreeEnvironmentStringsA", 1},
                         {"KERNEL32.DLL", "GetEnvironmentVariableA", 3},
                         {"KERNEL32.DLL", "SetEnvironmentVariableA", 2},
                         {"KERNEL32.DLL", "ExpandEnvironmentStringsA", 3},
                         {"KERNEL32.DLL", "GetACP", 0},
                         {"KERNEL32.DLL", "GetOEMCP", 0},
                         {"KERNEL32.DLL", "GetCPInfo", 2},
                         {"KERNEL32.DLL", "GetStringTypeA", 5},
                         {"KERNEL32.DLL", "GetStringTypeExA", 5},
                         {"KERNEL32.DLL", "IsDBCSLeadByte", 1},
                         {"KERNEL32.DLL", "IsDBCSLeadByteEx", 2},
                         {"KERNEL32.DLL", "MultiByteToWideChar", 6},
                         {"KERNEL32.DLL", "WideCharToMultiByte", 8},
                         {"USER32.DLL", "GetDoubleClickTime", 0},
                         {"USER32.DLL", "SetDoubleClickTime", 1},
                         {"USER32.DLL", "GetWindowPlacement", 2},
                         {"USER32.DLL", "SetWindowPlacement", 2},
                         {"SHLWAPI.DLL", "PathGetArgsA", 1},
                         {"SHLWAPI.DLL", "PathGetArgsW", 1},
                         {"SHLWAPI.DLL", "PathRemoveArgsA", 1},
                         {"SHLWAPI.DLL", "PathRemoveArgsW", 1},
                         {"SHLWAPI.DLL", "PathQuoteSpacesA", 1},
                         {"SHLWAPI.DLL", "PathQuoteSpacesW", 1},
                         {"SHLWAPI.DLL", "PathUnquoteSpacesA", 1},
                         {"SHLWAPI.DLL", "PathUnquoteSpacesW", 1},
                         {"SHLWAPI.DLL", "PathRemoveBlanksA", 1},
                         {"SHLWAPI.DLL", "PathRemoveBlanksW", 1},
                         {"SHLWAPI.DLL", "PathFindExtensionW", 1},
                         {"SHLWAPI.DLL", "PathFindFileNameW", 1},
                         {"SHLWAPI.DLL", "PathFindNextComponentA", 1},
                         {"SHLWAPI.DLL", "PathFindNextComponentW", 1},
                         {"SHLWAPI.DLL", "PathAddExtensionA", 2},
                         {"SHLWAPI.DLL", "PathAddExtensionW", 2},
                         {"SHLWAPI.DLL", "PathRenameExtensionA", 2},
                         {"SHLWAPI.DLL", "PathRenameExtensionW", 2},
                         {"SHLWAPI.DLL", "PathRemoveExtensionA", 1},
                         {"SHLWAPI.DLL", "PathRemoveExtensionW", 1},
                         {"SHLWAPI.DLL", "PathGetDriveNumberA", 1},
                         {"SHLWAPI.DLL", "PathGetDriveNumberW", 1},
                         {"SHLWAPI.DLL", "PathIsFileSpecA", 1},
                         {"SHLWAPI.DLL", "PathIsFileSpecW", 1},
                         {"SHLWAPI.DLL", "PathParseIconLocationA", 1},
                         {"SHLWAPI.DLL", "PathParseIconLocationW", 1},
                         {"SHLWAPI.DLL", "PathGetCharTypeA", 1},
                         {"SHLWAPI.DLL", "PathGetCharTypeW", 1},
                         {"SHLWAPI.DLL", "StrCpyW", 2},
                         {"SHLWAPI.DLL", "StrCpyNW", 3},
                         {"SHLWAPI.DLL", "StrCatW", 2},
                         {"SHLWAPI.DLL", "StrCatBuffA", 3},
                         {"SHLWAPI.DLL", "StrCatBuffW", 3},
                         {"SHLWAPI.DLL", "StrNCatA", 3},
                         {"SHLWAPI.DLL", "StrNCatW", 3},
                         {"SHLWAPI.DLL", "StrDupA", 1},
                         {"SHLWAPI.DLL", "StrDupW", 1},
                         {"SHLWAPI.DLL", "SHStrDupA", 2},
                         {"SHLWAPI.DLL", "SHStrDupW", 2},
                         {"SHLWAPI.DLL", "StrToIntA", 1},
                         {"SHLWAPI.DLL", "StrToIntW", 1},
                         {"SHLWAPI.DLL", "StrToIntExA", 3},
                         {"SHLWAPI.DLL", "StrToIntExW", 3},
                         {"SHLWAPI.DLL", "StrCmpW", 2},
                         {"SHLWAPI.DLL", "StrCmpIW", 2},
                         {"SHLWAPI.DLL", "StrCmpNW", 3},
                         {"SHLWAPI.DLL", "StrCmpNIW", 3},
                         {"SHLWAPI.DLL", "StrCmpNA", 3},
                         {"SHLWAPI.DLL", "StrCmpNIA", 3},
                         {"SHLWAPI.DLL", "ChrCmpIA", 2},
                         {"SHLWAPI.DLL", "ChrCmpIW", 2},
                         {"SHLWAPI.DLL", "StrChrA", 2},
                         {"SHLWAPI.DLL", "StrChrW", 2},
                         {"SHLWAPI.DLL", "StrRChrA", 3},
                         {"SHLWAPI.DLL", "StrRChrW", 3},
                         {"SHLWAPI.DLL", "StrPBrkA", 2},
                         {"SHLWAPI.DLL", "StrPBrkW", 2},
                         {"SHLWAPI.DLL", "StrSpnA", 2},
                         {"SHLWAPI.DLL", "StrSpnW", 2},
                         {"SHLWAPI.DLL", "StrCSpnA", 2},
                         {"SHLWAPI.DLL", "StrCSpnW", 2},
                         {"SHLWAPI.DLL", "SHRegOpenUSKeyA", 5},
                         {"SHLWAPI.DLL", "SHRegCreateUSKeyA", 5},
                         {"SHLWAPI.DLL", "SHRegQueryUSValueA", 8},
                         {"SHLWAPI.DLL", "SHRegGetUSValueA", 8},
                         {"SHLWAPI.DLL", "SHRegSetUSValueA", 6},
                         {"SHLWAPI.DLL", "SHRegWriteUSValueA", 6},
                         {"SHLWAPI.DLL", "SHRegDeleteUSValueA", 3},
                         {"SHLWAPI.DLL", "SHRegDeleteEmptyUSKeyA", 3},
                         {"SHLWAPI.DLL", "SHRegCloseUSKey", 1},
                         {"SHLWAPI.DLL", "SHRegEnumUSKeyA", 5},
                         {"SHLWAPI.DLL", "SHRegEnumUSValueA", 8},
                         {"SHLWAPI.DLL", "SHRegQueryInfoUSKeyA", 6},
                         {"SHLWAPI.DLL", "SHGetValueA", 6},
                         {"SHLWAPI.DLL", "SHSetValueA", 6},
                         {"SHLWAPI.DLL", "SHDeleteValueA", 3},
                         {"SHLWAPI.DLL", "SHDeleteEmptyKeyA", 2},
                         {"SHLWAPI.DLL", "SHDeleteOrphanKeyA", 2},
                         {"SHLWAPI.DLL", "SHRegDuplicateHKey", 1},
                         {"SHLWAPI.DLL", "SHQueryValueExA", 6},
                         {"SHLWAPI.DLL", "SHEnumKeyExA", 4},
                         {"SHLWAPI.DLL", "SHEnumValueA", 7},
                         {"SHLWAPI.DLL", "SHQueryInfoKeyA", 5},
                         {"SHLWAPI.DLL", "PathAddBackslashA", 1},
                         {"SHLWAPI.DLL", "ordinal_451", 1},
                         {"SHLWAPI.DLL", "PathCanonicalizeA", 2},
                         {"SHLWAPI.DLL", "ordinal_459", 2},
                         {"SHLWAPI.DLL", "PathCombineA", 3},
                         {"SHLWAPI.DLL", "ordinal_461", 3},
                         {"SHLWAPI.DLL", "PathFindExtensionA", 1},
                         {"SHLWAPI.DLL", "ordinal_473", 1},
                         {"SHLWAPI.DLL", "PathFindFileNameA", 1},
                         {"SHLWAPI.DLL", "ordinal_475", 1},
                         {"SHELL32.DLL", "SHGetFileInfo", 5},
                         {"SHELL32.DLL", "SHGetFileInfoA", 5},
                         {"SHELL32.DLL", "SHGetFileInfoW", 5},
                         {"SHLWAPI.DLL", "PathIsRelativeA", 1},
                         {"SHLWAPI.DLL", "ordinal_503", 1},
                         {"SHLWAPI.DLL", "PathIsRootA", 1},
                         {"SHLWAPI.DLL", "ordinal_505", 1},
                         {"SHLWAPI.DLL", "PathIsUNCA", 1},
                         {"SHLWAPI.DLL", "ordinal_511", 1},
                         {"SHLWAPI.DLL", "PathRemoveFileSpecA", 1},
                         {"SHLWAPI.DLL", "ordinal_539", 1},
                         {"SHLWAPI.DLL", "PathSkipRootA", 1},
                         {"SHLWAPI.DLL", "ordinal_547", 1},
                         {"SHLWAPI.DLL", "PathStripToRootA", 1},
                         {"SHLWAPI.DLL", "ordinal_551", 1},

                         {"USER32.DLL", "SetMenuDefaultItem", 3},
                         {"USER32.DLL", "InsertMenuA", 5},
                         {"USER32.DLL", "ModifyMenuA", 5},
                         {"USER32.DLL", "GetMenuItemInfoA", 4},
                         {"USER32.DLL", "SetMenuItemInfoA", 4},
                         {"USER32.DLL", "InsertMenuItemA", 4},
                         {"USER32.DLL", "GetMenuState", 3},
                         {"USER32.DLL", "GetMenuStringA", 5},
                         {"USER32.DLL", "GetMenuDefaultItem", 3},
                         {"USER32.DLL", "IsMenu", 1},
                         {"USER32.DLL", "GetMenuContextHelpId", 1},
                         {"USER32.DLL", "SetMenuContextHelpId", 2},
                         {"USER32.DLL", "GetMenuInfo", 2},
                         {"USER32.DLL", "SetMenuInfo", 2},
                         {"USER32.DLL", "SetMenuItemBitmaps", 5},
                         {"USER32.DLL", "AppendMenuA", 4},
                         {"USER32.DLL", "GetMenuBarInfo", 4},
                         {"USER32.DLL", "TrackPopupMenu", 7},
                         {"USER32.DLL", "TrackPopupMenuEx", 6},
                         {"USER32.DLL", "EndMenu", 0},
                         {"USER32.DLL", "GetMenuItemRect", 4},
                         {"USER32.DLL", "MenuItemFromPoint", 4},
                         {"USER32.DLL", "GetMenuCheckMarkDimensions", 0},
                         {"USER32.DLL", "CreateMenu", 0},
                         {"USER32.DLL", "CreatePopupMenu", 0},
                         {"USER32.DLL", "GetMenuItemID", 2},
                         {"USER32.DLL", "wsprintfA", 0x80000002u},
                         {"USER32.DLL", "wvsprintfA", 3},
                         {"USER32.DLL", "DrawAnimatedRects", 4},
                         {"USER32.DLL", "IntersectRect", 3},
                         {"USER32.DLL", "SetRect", 5},
                         {"USER32.DLL", "SetRectEmpty", 1},
                         {"USER32.DLL", "CopyRect", 2},
                         {"USER32.DLL", "IsRectEmpty", 1},
                         {"USER32.DLL", "PtInRect", 3},
                         {"USER32.DLL", "InflateRect", 3},
                         {"USER32.DLL", "EqualRect", 2},
                         {"USER32.DLL", "SetTimer", 4},
                         {"USER32.DLL", "CreateCaret", 4},
                         {"USER32.DLL", "DestroyCaret", 0},
                         {"USER32.DLL", "ShowCaret", 1},
                         {"USER32.DLL", "HideCaret", 1},
                         {"USER32.DLL", "SetCaretPos", 2},
                         {"USER32.DLL", "GetCaretPos", 1},
                         {"USER32.DLL", "SetCaretBlinkTime", 1},
                         {"USER32.DLL", "GetCaretBlinkTime", 0},
                         {"USER32.DLL", "KillTimer", 2},
                         {"USER32.DLL", "BeginDeferWindowPos", 1},
                         {"USER32.DLL", "DeferWindowPos", 8},
                         {"USER32.DLL", "EndDeferWindowPos", 1},
                         {"USER32.DLL", "DeleteMenu", 3},
                         {"USER32.DLL", "RemoveMenu", 3},
                         {"USER32.DLL", "DestroyMenu", 1},
                         {"USER32.DLL", "GetMenuItemCount", 1},
                         {"SHELL32.DLL", "ordinal_181", 2},
                         {"SHELL32.DLL", "ordinal_100", 1},
                         {"SHELL32.DLL", "ordinal_244", 2},
                         {"SHELL32.DLL", "ordinal_651", 2},
                         {"SHELL32.DLL", "ordinal_652", 1},
                         {"SHELL32.DLL", "ordinal_654", 2},
                         {"SHELL32.DLL", "SHGetSettings", 2},
                         {"SHELL32.DLL", "ordinal_653", 4},
                         {"SHELL32.DLL", "ordinal_51", 3},
                         {"SHELL32.DLL", "ordinal_40", 1},
                         {"SHLWAPI.DLL", "ordinal_3", 2},
                         {"SHLWAPI.DLL", "ordinal_5", 3},
                         {"SHLWAPI.DLL", "PathFindOnPathA", 2},
                         {"SHLWAPI.DLL", "PathFileExistsA", 1},
                         {"SHELL32.DLL", "ordinal_45", 1},
                         {"SHLWAPI.DLL", "PathFileExistsW", 1},
                         {"SHLWAPI.DLL", "PathIsDirectoryA", 1},
                         {"SHLWAPI.DLL", "PathIsDirectoryW", 1},
                         {"SHLWAPI.DLL", "PathIsDirectoryEmptyA", 1},
                         {"SHLWAPI.DLL", "PathIsDirectoryEmptyW", 1},
                         {"SHLWAPI.DLL", "PathIsUNCServerA", 1},
                         {"SHLWAPI.DLL", "PathIsUNCServerW", 1},
                         {"SHLWAPI.DLL", "PathIsUNCServerShareA", 1},
                         {"SHLWAPI.DLL", "PathIsUNCServerShareW", 1},

                         {"SHELL32.DLL", "ordinal_86", 2},
                         {"SHELL32.DLL", "ordinal_87", 1},
                         {"SHELL32.DLL", "ordinal_88", 5},
                         {"SHELL32.DLL", "ordinal_660", 1},
                         {"SHELL32.DLL", "ordinal_71", 2},
                         {"SHELL32.DLL", "ordinal_505", 1},
                         {"SHELL32.DLL", "ordinal_506", 3},
                         {"SHELL32.DLL", "ordinal_507", 3},
                         {"SHELL32.DLL", "ordinal_508", 4},
                         {"SHELL32.DLL", "ordinal_509", 6},
                         {"SHELL32.DLL", "ordinal_510", 4},
                         {"SHELL32.DLL", "ordinal_511", 6},
                         {"SHLWAPI.DLL", "SHQueryValueExW", 6},
                         {"COMCTL32.DLL", "ordinal_2", 7},
                         {"COMCTL32.DLL", "MenuHelp", 7},
                         {"COMCTL32.DLL", "ordinal_6", 4},
                         {"COMCTL32.DLL", "ordinal_4", 3},
                         {"COMCTL32.DLL", "ordinal_71", 1},
                         {"COMCTL32.DLL", "ordinal_72", 2},
                         {"COMCTL32.DLL", "ordinal_73", 1},
                         {"COMCTL32.DLL", "ordinal_350", 2},
                         {"COMCTL32.DLL", "ordinal_357", 1},
                         {"COMCTL32.DLL", "ordinal_365", 1},
                         {"USER32.DLL", "SubtractRect", 3},
                         {"USER32.DLL", "UnionRect", 3},
                         {"COMDLG32.DLL", "FindTextA", 1},
                         {"COMDLG32.DLL", "ReplaceTextA", 1},
                         {"COMDLG32.DLL", "GetFileTitleA", 3},
                         {"COMDLG32.DLL", "CommDlgExtendedError", 0},
                         {"COMDLG32.DLL", "PrintDlgA", 1},
                         {"COMDLG32.DLL", "GetOpenFileNameA", 1},
                         {"COMDLG32.DLL", "GetSaveFileNameA", 1},
                         {"KERNEL32.DLL", "GetStdHandle", 1},
                         {"KERNEL32.DLL", "SetHandleCount", 1},
                         {"KERNEL32.DLL", "GetVersion", 0},
                         {"KERNEL32.DLL", "GetProcessVersion", 1},
                         {"KERNEL32.DLL", "ThunkConnect32", 6},
                         {"KERNEL32.DLL", "ordinal_41", 5},
                         {"KERNEL32.DLL", "ordinal_20", 1},
                         {"KERNEL32.DLL", "ordinal_21", 1},
                         {"KERNEL32.DLL", "ordinal_22", 1},
                         {"KERNEL32.DLL", "ordinal_1", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_2", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_3", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_4", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_5", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_6", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_7", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_8", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_9", 0x40000001u},
                         {"KERNEL32.DLL", "ordinal_10", 2},
                         {"KERNEL32.DLL", "ordinal_11", 3},
                         {"KERNEL32.DLL", "ordinal_12", 2},
                         {"KERNEL32.DLL", "ordinal_13", 3},
                         {"KERNEL32.DLL", "ordinal_14", 4},
                         {"KERNEL32.DLL", "ordinal_15", 0x80000002u},
                         {"KERNEL32.DLL", "ordinal_16", 3},
                         {"KERNEL32.DLL", "ordinal_17", 0x40000000u},
                         {"KERNEL32.DLL", "ordinal_19", 1},
                         {"KERNEL32.DLL", "ordinal_23", 1},
                         {"KERNEL32.DLL", "ordinal_24", 2},
                         {"KERNEL32.DLL", "ordinal_25", 1},
                         {"KERNEL32.DLL", "ordinal_26", 1},
                         {"KERNEL32.DLL", "ordinal_27", 1},
                         {"KERNEL32.DLL", "ordinal_28", 1},
                         {"KERNEL32.DLL", "ordinal_29", 1},
                         {"KERNEL32.DLL", "ordinal_30", 1},
                         {"KERNEL32.DLL", "ordinal_31", 1},
                         {"KERNEL32.DLL", "ordinal_32", 1},
                         {"KERNEL32.DLL", "ordinal_33", 0},
                         {"KERNEL32.DLL", "ordinal_34", 0},
                         {"KERNEL32.DLL", "ordinal_38", 0x40000000u},
                         {"KERNEL32.DLL", "ordinal_39", 0x40000000u},
                         {"KERNEL32.DLL", "ordinal_40", 0x40000000u},
                         {"KERNEL32.DLL", "ordinal_42", 1},
                         {"KERNEL32.DLL", "ordinal_43", 5},
                         {"KERNEL32.DLL", "ordinal_44", 1},
                         {"KERNEL32.DLL", "ordinal_45", 0x40000000u},
                         {"KERNEL32.DLL", "ordinal_46", 5},
                         {"KERNEL32.DLL", "ordinal_47", 1},
                         {"KERNEL32.DLL", "ordinal_48", 1},
                         {"KERNEL32.DLL", "ordinal_49", 1},
                         {"KERNEL32.DLL", "ordinal_51", 0x40000000u},
                         {"KERNEL32.DLL", "ordinal_52", 0},
                         {"KERNEL32.DLL", "ordinal_53", 2},
                         {"KERNEL32.DLL", "ordinal_54", 2},
                         {"KERNEL32.DLL", "ordinal_55", 5},
                         {"KERNEL32.DLL", "ordinal_56", 3},
                         {"KERNEL32.DLL", "ordinal_57", 2},
                         {"KERNEL32.DLL", "ordinal_58", 2},
                         {"KERNEL32.DLL", "ordinal_59", 2},
                         {"KERNEL32.DLL", "ordinal_60", 1},
                         {"KERNEL32.DLL", "GetVersionExA", 1},
                         {"KERNEL32.DLL", "GetCurrentProcess", 0},
                         {"KERNEL32.DLL", "AllocConsole", 0},
                         {"KERNEL32.DLL", "FreeConsole", 0},
                         {"KERNEL32.DLL", "CreateConsoleScreenBuffer", 5},
                         {"KERNEL32.DLL", "GetConsoleCP", 0},
                         {"KERNEL32.DLL", "GetConsoleOutputCP", 0},
                         {"KERNEL32.DLL", "GetConsoleMode", 2},
                         {"KERNEL32.DLL", "SetConsoleMode", 2},
                         {"KERNEL32.DLL", "GetConsoleCursorInfo", 2},
                         {"KERNEL32.DLL", "SetConsoleCursorInfo", 2},
                         {"KERNEL32.DLL", "GetConsoleScreenBufferInfo", 2},
                         {"KERNEL32.DLL", "GetConsoleTitleA", 2},
                         {"KERNEL32.DLL", "SetConsoleTitleA", 1},
                         {"KERNEL32.DLL", "GetLargestConsoleWindowSize", 1},
                         {"KERNEL32.DLL", "GetNumberOfConsoleInputEvents", 2},
                         {"KERNEL32.DLL", "GetNumberOfConsoleMouseButtons", 1},
                         {"KERNEL32.DLL", "SetConsoleActiveScreenBuffer", 1},
                         {"KERNEL32.DLL", "SetConsoleCursorPosition", 2},
                         {"KERNEL32.DLL", "SetConsoleScreenBufferSize", 2},
                         {"KERNEL32.DLL", "SetConsoleTextAttribute", 2},
                         {"KERNEL32.DLL", "SetConsoleWindowInfo", 3},
                         {"KERNEL32.DLL", "FillConsoleOutputAttribute", 5},
                         {"KERNEL32.DLL", "FillConsoleOutputCharacterA", 5},
                         {"KERNEL32.DLL", "FlushConsoleInputBuffer", 1},
                         {"KERNEL32.DLL", "PeekConsoleInputA", 4},
                         {"KERNEL32.DLL", "ReadConsoleA", 5},
                         {"KERNEL32.DLL", "ReadConsoleInputA", 4},
                         {"KERNEL32.DLL", "ReadConsoleOutputA", 5},
                         {"KERNEL32.DLL", "ReadConsoleOutputAttribute", 5},
                         {"KERNEL32.DLL", "ReadConsoleOutputCharacterA", 5},
                         {"KERNEL32.DLL", "ScrollConsoleScreenBufferA", 5},
                         {"KERNEL32.DLL", "WriteConsoleA", 5},
                         {"KERNEL32.DLL", "WriteConsoleInputA", 4},
                         {"KERNEL32.DLL", "WriteConsoleOutputA", 5},
                         {"KERNEL32.DLL", "WriteConsoleOutputAttribute", 5},
                         {"KERNEL32.DLL", "WriteConsoleOutputCharacterA", 5},
                         {"KERNEL32.DLL", "SetConsoleCtrlHandler", 2},
                         {"KERNEL32.DLL", "GenerateConsoleCtrlEvent", 2},
                         {"KERNEL32.DLL", "SetStdHandle", 2},
                         {"KERNEL32.DLL", "SleepEx", 2},
                         {"KERNEL32.DLL", "WaitForSingleObjectEx", 3},
                         {"KERNEL32.DLL", "QueueUserAPC", 3},
                         {"KERNEL32.DLL", "TerminateThread", 2},
                         {"KERNEL32.DLL", "TerminateProcess", 2},
                         {"KERNEL32.DLL", "FreeLibraryAndExitThread", 2},
                         {"KERNEL32.DLL", "PulseEvent", 1},
                         {"KERNEL32.DLL", "UninitializeCriticalSection", 1},
                         {"KERNEL32.DLL", "GetThreadContext", 2},
                         {"KERNEL32.DLL", "SetThreadContext", 2},
                         {"KERNEL32.DLL", "CreateToolhelp32Snapshot", 2},
                         {"KERNEL32.DLL", "Process32First", 2},
                         {"KERNEL32.DLL", "Process32Next", 2},
                         {"KERNEL32.DLL", "Thread32First", 2},
                         {"KERNEL32.DLL", "Thread32Next", 2},
                         {"KERNEL32.DLL", "Module32First", 2},
                         {"KERNEL32.DLL", "Module32Next", 2},
                         {"KERNEL32.DLL", "Heap32ListFirst", 2},
                         {"KERNEL32.DLL", "Heap32ListNext", 2},
                         {"KERNEL32.DLL", "Heap32First", 3},
                         {"KERNEL32.DLL", "Heap32Next", 1},
                         {"KERNEL32.DLL", "Toolhelp32ReadProcessMemory", 5},
                         {"KERNEL32.DLL", "LocalCompact", 1},
                         {"KERNEL32.DLL", "LocalFlags", 1},
                         {"KERNEL32.DLL", "LocalHandle", 1},
                         {"KERNEL32.DLL", "LocalShrink", 2},
                         {"KERNEL32.DLL", "GlobalHandle", 1},
                         {"KERNEL32.DLL", "GlobalFix", 1},
                         {"KERNEL32.DLL", "GlobalUnfix", 1},
                         {"KERNEL32.DLL", "GlobalWire", 1},
                         {"KERNEL32.DLL", "GlobalUnWire", 1},
                         {"KERNEL32.DLL", "MoveFileExA", 3},
                         {"KERNEL32.DLL", "OpenFile", 3},
                         {"KERNEL32.DLL", "LockFile", 5},
                         {"KERNEL32.DLL", "UnlockFile", 5},
                         {"KERNEL32.DLL", "FlushViewOfFile", 2},
                         {"KERNEL32.DLL", "ReadFileEx", 5},
                         {"KERNEL32.DLL", "WriteFileEx", 5},
                         {"KERNEL32.DLL", "CancelIo", 1},
                         {"KERNEL32.DLL", "GetOverlappedResult", 4},
                         {"KERNEL32.DLL", "SetVolumeLabelA", 2},
                         {"KERNEL32.DLL", "GetCPInfoExA", 3},
                         {"KERNEL32.DLL", "IsValidCodePage", 1},
                         {"KERNEL32.DLL", "IsValidLocale", 2},
                         {"KERNEL32.DLL", "ConvertDefaultLocale", 1},
                         {"KERNEL32.DLL", "EnumSystemCodePagesA", 2},
                         {"KERNEL32.DLL", "EnumSystemLocalesA", 2},
                         {"KERNEL32.DLL", "VerLanguageNameA", 3},
                         {"KERNEL32.DLL", "EnumCalendarInfoA", 4},
                         {"KERNEL32.DLL", "EnumCalendarInfoExA", 4},
                         {"KERNEL32.DLL", "GetCalendarInfoA", 6},
                         {"KERNEL32.DLL", "SetCalendarInfoA", 4},
                         {"KERNEL32.DLL", "EnumDateFormatsExA", 3},
                         {"KERNEL32.DLL", "SetLocaleInfoA", 3},
                         {"KERNEL32.DLL", "GetErrorMode", 0},
                         {"KERNEL32.DLL", "GetProcessFlags", 1},
                         {"KERNEL32.DLL", "RegisterServiceProcess", 2},
                         {"KERNEL32.DLL", "GetSystemPowerStatus", 1},
                         {"KERNEL32.DLL", "IsSystemResumeAutomatic", 0},
                         {"KERNEL32.DLL", "SetThreadExecutionState", 1},
                         {"KERNEL32.DLL", "BuildCommDCBA", 2},
                         {"KERNEL32.DLL", "BuildCommDCBAndTimeoutsA", 3},
                         {"KERNEL32.DLL", "ClearCommBreak", 1},
                         {"KERNEL32.DLL", "ClearCommError", 3},
                         {"KERNEL32.DLL", "CommConfigDialogA", 3},
                         {"KERNEL32.DLL", "EscapeCommFunction", 2},
                         {"KERNEL32.DLL", "GetCommConfig", 3},
                         {"KERNEL32.DLL", "GetCommMask", 2},
                         {"KERNEL32.DLL", "GetCommModemStatus", 2},
                         {"KERNEL32.DLL", "GetCommProperties", 2},
                         {"KERNEL32.DLL", "GetCommState", 2},
                         {"KERNEL32.DLL", "GetCommTimeouts", 2},
                         {"KERNEL32.DLL", "GetDefaultCommConfigA", 3},
                         {"KERNEL32.DLL", "PurgeComm", 2},
                         {"KERNEL32.DLL", "SetCommBreak", 1},
                         {"KERNEL32.DLL", "SetCommConfig", 3},
                         {"KERNEL32.DLL", "SetCommMask", 2},
                         {"KERNEL32.DLL", "SetCommState", 2},
                         {"KERNEL32.DLL", "SetCommTimeouts", 2},
                         {"KERNEL32.DLL", "SetDefaultCommConfigA", 3},
                         {"KERNEL32.DLL", "SetupComm", 3},
                         {"KERNEL32.DLL", "TransmitCommChar", 2},
                         {"KERNEL32.DLL", "WaitCommEvent", 3},
                         {"KERNEL32.DLL", "GetSystemInfo", 1},
                         {"KERNEL32.DLL", "ReadProcessMemory", 5},
                         {"KERNEL32.DLL", "WriteProcessMemory", 5},
                         {"KERNEL32.DLL", "VirtualQueryEx", 4},
                         {"KERNEL32.DLL", "VirtualProtectEx", 5},
                         {"KERNEL32.DLL", "FlushInstructionCache", 3},
                         {"KERNEL32.DLL", "GetProcessAffinityMask", 3},
                         {"KERNEL32.DLL", "IsDebuggerPresent", 0},
                         {"KERNEL32.DLL", "GetCommandLineW", 0},
                         {"KERNEL32.DLL", "VirtualUnlock", 2},
                         {"KERNEL32.DLL", "GlobalMemoryStatus", 1},
                         {"KERNEL32.DLL", "VirtualAlloc", 4},
                         {"KERNEL32.DLL", "InterlockedExchange", 2},
                         {"KERNEL32.DLL", "InterlockedCompareExchange", 3},
                         {"KERNEL32.DLL", "InterlockedIncrement", 1},
                         {"KERNEL32.DLL", "InterlockedDecrement", 1},
                         {"KERNEL32.DLL", "InterlockedExchangeAdd", 2},
                         {"KERNEL32.DLL", "VirtualFree", 3},
                         {"KERNEL32.DLL", "VirtualProtect", 4},
                         {"KERNEL32.DLL", "VirtualQuery", 3},
                         {"KERNEL32.DLL", "IsBadCodePtr", 1},
                         {"KERNEL32.DLL", "IsBadHugeReadPtr", 2},
                         {"KERNEL32.DLL", "IsBadHugeWritePtr", 2},
                         {"KERNEL32.DLL", "IsBadReadPtr", 2},
                         {"KERNEL32.DLL", "IsBadStringPtrA", 2},
                         {"KERNEL32.DLL", "IsBadStringPtrW", 2},
                         {"KERNEL32.DLL", "IsBadWritePtr", 2},
                         {"KERNEL32.DLL", "FindResourceA", 3},
                         {"KERNEL32.DLL", "EnumResourceLanguagesA", 5},
                         {"KERNEL32.DLL", "EnumResourceLanguagesW", 5},
                         {"KERNEL32.DLL", "FindResourceExA", 4},
                         {"KERNEL32.DLL", "LoadResource", 2},
                         {"KERNEL32.DLL", "LockResource", 1},
                         {"KERNEL32.DLL", "FreeResource", 1},
                         {"KERNEL32.DLL", "SizeofResource", 2},
                         {"KERNEL32.DLL", "HeapCreate", 3},
                         {"KERNEL32.DLL", "HeapDestroy", 1},
                         {"KERNEL32.DLL", "GetProcessHeap", 0},
                         {"KERNEL32.DLL", "HeapAlloc", 3},
                         {"KERNEL32.DLL", "HeapReAlloc", 4},
                         {"KERNEL32.DLL", "HeapFree", 3},
                         {"KERNEL32.DLL", "HeapSize", 3},
                         {"KERNEL32.DLL", "HeapValidate", 3},

                         {"KERNEL32.DLL", "FormatMessageA", 7},
                         {"KERNEL32.DLL", "GetVolumeInformationA", 8},
                         {"KERNEL32.DLL", "GetDriveTypeA", 1},
                         {"KERNEL32.DLL", "GetDiskFreeSpaceA", 5},
                         {"KERNEL32.DLL", "GetDiskFreeSpaceExA", 4},
                         {"KERNEL32.DLL", "GetLogicalDrives", 0},
                         {"KERNEL32.DLL", "GetLogicalDriveStringsA", 2},
                         {"KERNEL32.DLL", "CreateFileA", 7},
                         {"KERNEL32.DLL", "CreatePipe", 4},
                         {"KERNEL32.DLL", "PeekNamedPipe", 6},
                         {"KERNEL32.DLL", "CallNamedPipeA", 7},
                         {"KERNEL32.DLL", "WaitNamedPipeA", 2},
                         {"KERNEL32.DLL", "TransactNamedPipe", 7},
                         {"KERNEL32.DLL", "GetNamedPipeHandleStateA", 7},
                         {"KERNEL32.DLL", "SetNamedPipeHandleState", 4},
                         {"KERNEL32.DLL", "GetNamedPipeInfo", 5},
                         {"KERNEL32.DLL", "CreateMailslotA", 4},
                         {"KERNEL32.DLL", "GetMailslotInfo", 5},
                         {"KERNEL32.DLL", "SetMailslotInfo", 2},
                         {"KERNEL32.DLL", "DeviceIoControl", 8},
                         {"KERNEL32.DLL", "DefineDosDeviceA", 3},
                         {"KERNEL32.DLL", "QueryDosDeviceA", 3},
                         {"KERNEL32.DLL", "OpenVxDHandle", 1},
                         {"KERNEL32.DLL", "CreateSocketHandle", 0},
                         {"KERNEL32.DLL", "GetQueuedCompletionStatus", 5},
                         {"KERNEL32.DLL", "GetHandleContext", 1},
                         {"KERNEL32.DLL", "SetHandleContext", 2},
                         {"KERNEL32.DLL", "CloseSystemHandle", 1},
                         {"KERNEL32.DLL", "ReadFile", 5},
                         {"KERNEL32.DLL", "WriteFile", 5},
                         {"KERNEL32.DLL", "SetFilePointer", 4},
                         {"KERNEL32.DLL", "SetEndOfFile", 1},
                         {"KERNEL32.DLL", "GetFileSize", 2},
                         {"KERNEL32.DLL", "GetFileTime", 4},
                         {"KERNEL32.DLL", "SetFileTime", 4},
                         {"KERNEL32.DLL", "GetFileType", 1},
                         {"KERNEL32.DLL", "FindFirstChangeNotificationA", 3},
                         {"KERNEL32.DLL", "FindFirstChangeNotificationW", 3},
                         {"KERNEL32.DLL", "FindNextChangeNotification", 1},
                         {"KERNEL32.DLL", "FindCloseChangeNotification", 1},
                         {"KERNEL32.DLL", "FlushFileBuffers", 1},
                         {"KERNEL32.DLL", "GetFileAttributesA", 1},
                         {"KERNEL32.DLL", "SetFileAttributesA", 2},
                         {"KERNEL32.DLL", "DeleteFileA", 1},
                         {"KERNEL32.DLL", "CreateDirectoryA", 2},
                         {"KERNEL32.DLL", "RemoveDirectoryA", 1},
                         {"KERNEL32.DLL", "MoveFileA", 2},
                         {"KERNEL32.DLL", "FindFirstFileA", 2},
                         {"KERNEL32.DLL", "FindNextFileA", 2},
                         {"KERNEL32.DLL", "FindClose", 1},
                         {"KERNEL32.DLL", "AreFileApisANSI", 0},
                         {"KERNEL32.DLL", "GetFullPathNameA", 4},
                         {"KERNEL32.DLL", "GetShortPathNameA", 3},
                         {"KERNEL32.DLL", "GetLongPathNameA", 3},
                         {"KERNEL32.DLL", "GetTempFileNameA", 4},
                         {"KERNEL32.DLL", "GetCurrentDirectoryA", 2},
                         {"KERNEL32.DLL", "SetCurrentDirectoryA", 1},
                         {"KERNEL32.DLL", "GetWindowsDirectoryA", 2},
                         {"KERNEL32.DLL", "GetSystemDirectoryA", 2},
                         {"KERNEL32.DLL", "GetTempPathA", 2},
                         {"KERNEL32.DLL", "_lopen", 2},
                         {"KERNEL32.DLL", "_lcreat", 2},
                         {"KERNEL32.DLL", "_lclose", 1},
                         {"KERNEL32.DLL", "_lread", 3},
                         {"KERNEL32.DLL", "_lwrite", 3},
                         {"KERNEL32.DLL", "_hread", 3},
                         {"KERNEL32.DLL", "_hwrite", 3},
                         {"KERNEL32.DLL", "_llseek", 3},

                         {"USER32.DLL", "RegisterClipboardFormatA", 1},
                         {"USER32.DLL", "GetShellWindow", 0},
                         {"USER32.DLL", "SetShellWindow", 1},
                         {"SHELL32.DLL", "Control_RunDLL", 4},
                         {"SHELL32.DLL", "Control_RunDLLA", 4},
                         {"SHELL32.DLL", "ShellExecuteA", 6},
                         {"SHELL32.DLL", "ShellExecuteExA", 1},
                         {"USER32.DLL", "RegisterClassA", 1},
                         {"USER32.DLL", "SetClassWord", 3},
                         {"USER32.DLL", "GetClassWord", 2},
                         {"USER32.DLL", "SetClassLongA", 3},
                         {"USER32.DLL", "UnregisterClassA", 2},
                         {"USER32.DLL", "GetClassInfoExA", 3},
                         {"USER32.DLL", "GetClassInfoA", 3},
                         {"USER32.DLL", "CallWindowProcA", 5},
                         {"USER32.DLL", "FindWindowA", 2},
                         {"USER32.DLL", "FindWindowExA", 4},
                         {"USER32.DLL", "IsWindowUnicode", 1},
                         {"USER32.DLL", "PaintDesktop", 1},
                         {"USER32.DLL", "GetClassNameA", 3},
                         {"USER32.DLL", "IsWindowVisible", 1},
                         {"USER32.DLL", "IsWindow", 1},
                         {"USER32.DLL", "IsWindowEnabled", 1},
                         {"KERNEL32.DLL", "CreateThread", 6},
                         {"SHLWAPI.DLL", "ordinal_16", 4},
                         {"SHLWAPI.DLL", "ordinal_437", 1},
                         {"SHLWAPI.DLL", "ordinal_241", 0},
                         {"SHLWAPI.DLL", "ordinal_250", 0},
                         {"SHLWAPI.DLL", "ordinal_83", 1},
                         {"SHLWAPI.DLL", "ordinal_80", 3},
                         {"SHLWAPI.DLL", "ordinal_105", 3},
                         {"SHLWAPI.DLL", "SHCreateThread", 4},
                         {"SHELL32.DLL", "ordinal_176", 1},
                         {"SHELL32.DLL", "SHGetInstanceExplorer", 1},
                         {"SHLWAPI.DLL", "SHGetThreadRef", 1},
                         {"SHLWAPI.DLL", "SHSetThreadRef", 1},
                         {"KERNEL32.DLL", "CreateWaitableTimerA", 3},
                         {"KERNEL32.DLL", "OpenWaitableTimerA", 3},
                         {"KERNEL32.DLL", "SetWaitableTimer", 6},
                         {"KERNEL32.DLL", "CancelWaitableTimer", 1},
                         {"KERNEL32.DLL", "CreateEventA", 4},
                         {"KERNEL32.DLL", "OpenEventA", 3},
                         {"KERNEL32.DLL", "SetEvent", 1},
                         {"KERNEL32.DLL", "ResetEvent", 1},
                         {"KERNEL32.DLL", "CloseHandle", 1},
                         {"KERNEL32.DLL", "DuplicateHandle", 7},
                         {"KERNEL32.DLL", "WaitForSingleObject", 2},
                         {"KERNEL32.DLL", "WaitForMultipleObjects", 4},
                         {"KERNEL32.DLL", "WaitForMultipleObjectsEx", 5},
                         {"USER32.DLL", "MsgWaitForMultipleObjects", 5},
                         {"USER32.DLL", "MsgWaitForMultipleObjectsEx", 5},
                         {"USER32.DLL", "WaitMessage", 0},
                         {"USER32.DLL", "SetMessageQueue", 1},
                         {"USER32.DLL", "MapVirtualKeyA", 2},
                         {"USER32.DLL", "GetKeyNameTextA", 3},
                         {"USER32.DLL", "SetCursorPos", 2},
                         {"USER32.DLL", "ChangeDisplaySettingsA", 2},
                         {"WINMM.DLL", "mmioOpenA", 3},
                         {"WINMM.DLL", "mmioStringToFOURCCA", 2},
                         {"WINMM.DLL", "mmsystemGetVersion", 0},
                         {"WINMM.DLL", "timeGetSystemTime", 2},
                         {"WINMM.DLL", "mmioClose", 2},
                         {"WINMM.DLL", "mmioRead", 3},
                         {"WINMM.DLL", "mmioSeek", 3},
                         {"WINMM.DLL", "mmioDescend", 4},
                         {"WINMM.DLL", "mmioAscend", 3},
                         {"WINMM.DLL", "mciSendCommandA", 4},
                         {"WINMM.DLL", "waveOutOpen", 6},
                         {"WINMM.DLL", "waveOutGetDevCapsA", 3},
                         {"WINMM.DLL", "waveOutGetPosition", 3},
                         {"WINMM.DLL", "waveOutUnprepareHeader", 3},
                         {"WINMM.DLL", "waveOutPrepareHeader", 3},
                         {"WINMM.DLL", "waveOutClose", 1},
                         {"WINMM.DLL", "waveOutReset", 1},
                         {"WINMM.DLL", "waveOutWrite", 3},
                         {"WINMM.DLL", "waveOutRestart", 1},
                         {"WINMM.DLL", "waveOutPause", 1},
                         {"WINMM.DLL", "waveOutBreakLoop", 1},
                         {"WINMM.DLL", "waveOutGetPitch", 2},
                         {"WINMM.DLL", "waveOutSetPitch", 2},
                         {"WINMM.DLL", "waveOutGetPlaybackRate", 2},
                         {"WINMM.DLL", "waveOutSetPlaybackRate", 2},
                         {"KERNEL32.DLL", "SetFileApisToANSI", 0},
                         {"KERNEL32.DLL", "SetFileApisToOEM", 0},
                         {"KERNEL32.DLL", "lstrlen", 1},
                         {"KERNEL32.DLL", "lstrcpy", 2},
                         {"KERNEL32.DLL", "lstrcpyn", 3},
                         {"KERNEL32.DLL", "lstrcat", 2},
                         {"KERNEL32.DLL", "lstrcmp", 2},
                         {"KERNEL32.DLL", "lstrcmpi", 2},
                         {"WINMM.DLL", "sndPlaySoundA", 2},
                         {"WINMM.DLL", "timeGetTime", 0},
                         {"WINMM.DLL", "waveOutGetNumDevs", 0},
                         {"WINMM.DLL", "waveInGetNumDevs", 0},
                         {"WINMM.DLL", "timeBeginPeriod", 1},
                         {"WINMM.DLL", "timeEndPeriod", 1},
                         {"WINMM.DLL", "timeGetDevCaps", 2},
                         {"WINMM.DLL", "timeSetEvent", 5},
                         {"WINMM.DLL", "timeKillEvent", 1},
                         {"WINMM.DLL", "midiOutGetNumDevs", 0},
                         {"WINMM.DLL", "midiInGetNumDevs", 0},
                         {"WINMM.DLL", "mixerGetNumDevs", 0},
                         {"WINMM.DLL", "auxGetNumDevs", 0},
                         {"WINMM.DLL", "joyGetNumDevs", 0},
                         {"WINMM.DLL", "waveInGetDevCapsA", 3},
                         {"WINMM.DLL", "midiOutGetDevCapsA", 3},
                         {"WINMM.DLL", "midiInGetDevCapsA", 3},
                         {"WINMM.DLL", "mixerGetDevCapsA", 3},
                         {"WINMM.DLL", "auxGetDevCapsA", 3},
                         {"WINMM.DLL", "joyGetDevCapsA", 3},
                         {"WINMM.DLL", "joyGetPos", 2},
                         {"WINMM.DLL", "joyGetPosEx", 2},
                         {"WINMM.DLL", "waveOutGetVolume", 2},
                         {"WINMM.DLL", "waveOutSetVolume", 2},
                         {"WINMM.DLL", "midiOutGetVolume", 2},
                         {"WINMM.DLL", "midiOutSetVolume", 2},
                         {"WINMM.DLL", "waveOutGetID", 2},
                         {"WINMM.DLL", "mixerGetID", 3},
                         {"WINMM.DLL", "mciGetErrorStringA", 3},
                         {"WINMM.DLL", "waveOutGetErrorTextA", 3},
                         {"WINMM.DLL", "midiOutGetErrorTextA", 3},
                         {"WINMM.DLL", "waveInOpen", 6},
                         {"WINMM.DLL", "midiOutOpen", 5},
                         {"WINMM.DLL", "midiInOpen", 5},
                         {"WINMM.DLL", "midiStreamOpen", 6},
                         {"WINMM.DLL", "mixerOpen", 5},
                         {"WINMM.DLL", "waveInClose", 1},
                         {"WINMM.DLL", "waveInStart", 1},
                         {"WINMM.DLL", "waveInStop", 1},
                         {"WINMM.DLL", "waveInReset", 1},
                         {"WINMM.DLL", "waveInAddBuffer", 3},
                         {"WINMM.DLL", "waveInPrepareHeader", 3},
                         {"WINMM.DLL", "waveInUnprepareHeader", 3},
                         {"WINMM.DLL", "waveInGetPosition", 3},
                         {"WINMM.DLL", "waveInMessage", 4},
                         {"WINMM.DLL", "waveOutMessage", 4},
                         {"WINMM.DLL", "midiOutMessage", 4},
                         {"WINMM.DLL", "midiOutClose", 1},
                         {"WINMM.DLL", "midiOutReset", 1},
                         {"WINMM.DLL", "midiOutShortMsg", 2},
                         {"WINMM.DLL", "midiOutLongMsg", 3},
                         {"WINMM.DLL", "midiOutPrepareHeader", 3},
                         {"WINMM.DLL", "midiOutUnprepareHeader", 3},
                         {"WINMM.DLL", "midiInClose", 1},
                         {"WINMM.DLL", "midiInStart", 1},
                         {"WINMM.DLL", "midiInStop", 1},
                         {"WINMM.DLL", "midiInReset", 1},
                         {"WINMM.DLL", "midiStreamClose", 1},
                         {"WINMM.DLL", "midiStreamOut", 3},
                         {"WINMM.DLL", "midiStreamPause", 1},
                         {"WINMM.DLL", "midiStreamRestart", 1},
                         {"WINMM.DLL", "midiStreamStop", 1},
                         {"WINMM.DLL", "midiStreamProperty", 3},
                         {"WINMM.DLL", "midiStreamPosition", 3},
                         {"WINMM.DLL", "mixerClose", 1},
                         {"WINMM.DLL", "mixerMessage", 4},
                         {"WINMM.DLL", "mixerGetLineInfoA", 3},
                         {"WINMM.DLL", "mixerGetLineControlsA", 3},
                         {"WINMM.DLL", "mixerGetControlDetailsA", 3},
                         {"WINMM.DLL", "mixerSetControlDetails", 3},
                         {"WINMM.DLL", "PlaySoundA", 3},
                         {"WINMM.DLL", "mciSendStringA", 4},
                         {"WINMM.DLL", "mciGetDeviceIDA", 1},
                         {"WINMM.DLL", "OpenDriver", 3},
                         {"WINMM.DLL", "CloseDriver", 3},
                         {"WINMM.DLL", "SendDriverMessage", 4},
                         {"WINMM.DLL", "OpenDriverA", 3},
                         {"WINMM.DLL", "DrvOpen", 3},
                         {"WINMM.DLL", "DrvOpenA", 3},
                         {"WINMM.DLL", "DrvClose", 3},
                         {"WINMM.DLL", "DrvSendMessage", 4},
                         {"WINMM.DLL", "DefDriverProc", 5},
                         {"WINMM.DLL", "DrvDefDriverProc", 5},
                         {"WINMM.DLL", "GetDriverModuleHandle", 1},
                         {"WINMM.DLL", "DrvGetModuleHandle", 1},
                         {"WINMM.DLL", "GetDriverFlags", 1},
                         {"WINMM.DLL", "mmioWrite", 3},
                         {"WINMM.DLL", "mmioCreateChunk", 3},
                         {"WINMM.DLL", "mmioGetInfo", 3},
                         {"KERNEL32.DLL", "Sleep", 1},
                         {"KERNEL32.DLL", "SuspendThread", 1},
                         {"KERNEL32.DLL", "GetThreadPriority", 1},
                         {"KERNEL32.DLL", "SetThreadPriority", 2},
                         {"KERNEL32.DLL", "GetPriorityClass", 1},
                         {"KERNEL32.DLL", "SetPriorityClass", 2},
                         {"KERNEL32.DLL", "SetThreadAffinityMask", 2},
                         {"KERNEL32.DLL", "ResumeThread", 1},
                         {"KERNEL32.DLL", "GetCurrentThreadId", 0},
                         {"KERNEL32.DLL", "GetCurrentThread", 0},
                         {"KERNEL32.DLL", "OpenProcess", 3},
                         {"KERNEL32.DLL", "SearchPathA", 6},
                         {"KERNEL32.DLL", "LCMapStringA", 6},
                         {"KERNEL32.DLL", "ordinal_35", 1},
                         {"KERNEL32.DLL", "ordinal_36", 1},
                         {"KERNEL32.DLL", "ordinal_37", 2},
                         {"KERNEL32.DLL", "GetCurrentProcessId", 0},
                         {"KERNEL32.DLL", "OutputDebugStringA", 1},
                         {"KERNEL32.DLL", "GetExitCodeThread", 2},
                         {"KERNEL32.DLL", "ExitThread", 1},
                         {"KERNEL32.DLL", "SetLastError", 1},
                         {"KERNEL32.DLL", "ordinal_99", 1},
                         {"KERNEL32.DLL", "ordinal_61", 1},
                         {"KERNEL32.DLL", "ordinal_62", 1},
                         {"KERNEL32.DLL", "ordinal_63", 3},
                         {"KERNEL32.DLL", "ordinal_64", 1},
                         {"KERNEL32.DLL", "ordinal_65", 2},
                         {"KERNEL32.DLL", "ordinal_66", 0},
                         {"KERNEL32.DLL", "ordinal_67", 1},
                         {"KERNEL32.DLL", "ordinal_68", 3},
                         {"KERNEL32.DLL", "ordinal_69", 1},
                         {"KERNEL32.DLL", "ordinal_70", 2},
                         {"KERNEL32.DLL", "ordinal_71", 1},
                         {"KERNEL32.DLL", "ordinal_72", 4},
                         {"KERNEL32.DLL", "ordinal_73", 2},
                         {"KERNEL32.DLL", "ordinal_74", 2},
                         {"KERNEL32.DLL", "ordinal_75", 4},
                         {"KERNEL32.DLL", "ordinal_76", 4},
                         {"KERNEL32.DLL", "ordinal_77", 5},
                         {"KERNEL32.DLL", "ordinal_78", 3},
                         {"KERNEL32.DLL", "ordinal_79", 3},
                         {"KERNEL32.DLL", "ordinal_80", 3},
                         {"KERNEL32.DLL", "ordinal_81", 3},
                         {"KERNEL32.DLL", "ordinal_82", 2},
                         {"KERNEL32.DLL", "ordinal_83", 4},
                         {"KERNEL32.DLL", "ordinal_84", 1},
                         {"KERNEL32.DLL", "ordinal_85", 1},
                         {"KERNEL32.DLL", "ordinal_86", 1},
                         {"KERNEL32.DLL", "ordinal_87", 0},
                         {"KERNEL32.DLL", "ordinal_88", 0x80000000u | 3},
                         {"KERNEL32.DLL", "ordinal_89", 0x40000000u | 2},
                         {"KERNEL32.DLL", "ordinal_90", 0x40000000u | 0},
                         {"KERNEL32.DLL", "ordinal_91", 1},
                         {"KERNEL32.DLL", "ordinal_92", 0},
                         {"KERNEL32.DLL", "ordinal_93", 1},
                         {"KERNEL32.DLL", "ordinal_94", 1},
                         {"KERNEL32.DLL", "ordinal_95", 1},
                         {"KERNEL32.DLL", "ordinal_96", 0},
                         {"KERNEL32.DLL", "ordinal_97", 1},
                         {"KERNEL32.DLL", "ordinal_98", 1},
                         {"KERNEL32.DLL", "ordinal_100", 3},
                         {"KERNEL32.DLL", "ordinal_101", 1},
                         {"KERNEL32.DLL", "ordinal_102", 0},
                         {"KERNEL32.DLL", "ordinal_103", 1},
                         {"KERNEL32.DLL", "ordinal_104", 0x40000000u | 0},
                         {"KERNEL32.DLL", "ordinal_105", 3},
                         {"KERNEL32.DLL", "ordinal_106", 2},
                         {"KERNEL32.DLL", "ordinal_107", 1},
                         {"KERNEL32.DLL", "ordinal_108", 2},
                         {"KERNEL32.DLL", "ordinal_109", 2},
                         {"KERNEL32.DLL", "ordinal_110", 3},
                         {"KERNEL32.DLL", "ordinal_111", 2},
                         {"KERNEL32.DLL", "ordinal_112", 2},
                         {"KERNEL32.DLL", "ordinal_113", 2},
                         {"KERNEL32.DLL", "ordinal_114", 2},
                         {"KERNEL32.DLL", "ordinal_115", 3},
                         {"KERNEL32.DLL", "ordinal_116", 3},
                         {"KERNEL32.DLL", "ordinal_117", 2},
                         {"KERNEL32.DLL", "ordinal_119", 1},
                         {"KERNEL32.DLL", "ordinal_120", 0},
                         {"KERNEL32.DLL", "ordinal_121", 6},
                         {"KERNEL32.DLL", "ordinal_122", 2},
                         {"KERNEL32.DLL", "GetTimeZoneInformation", 1},
                         {"KERNEL32.DLL", "SetTimeZoneInformation", 1},
                         {"KERNEL32.DLL", "GetLocalTime", 1},
                         {"KERNEL32.DLL", "GetSystemTime", 1},
                         {"KERNEL32.DLL", "SetLocalTime", 1},
                         {"KERNEL32.DLL", "SetSystemTime", 1},
                         {"KERNEL32.DLL", "SystemTimeToFileTime", 2},
                         {"KERNEL32.DLL", "FileTimeToSystemTime", 2},
                         {"KERNEL32.DLL", "CompareFileTime", 2},
                         {"KERNEL32.DLL", "DosDateTimeToFileTime", 3},
                         {"KERNEL32.DLL", "FileTimeToDosDateTime", 3},
                         {"KERNEL32.DLL", "FileTimeToLocalFileTime", 2},
                         {"KERNEL32.DLL", "LocalFileTimeToFileTime", 2},
                         {"KERNEL32.DLL", "GetTickCount", 0},
                         {"KERNEL32.DLL", "QueryPerformanceCounter", 1},
                         {"KERNEL32.DLL", "QueryPerformanceFrequency", 1},

                         {"USER32.DLL", "GetSysColor", 1},
                         {"USER32.DLL", "GetSysColorBrush", 1},
                         {"USER32.DLL", "DrawTextA", 5},
                         {"USER32.DLL", "DrawTextExA", 6},
                         {"USER32.DLL", "DrawEdge", 4},
                         {"USER32.DLL", "DrawFrameControl", 4},
                         {"USER32.DLL", "OffsetRect", 3},
                         {"USER32.DLL", "SetWindowPos", 7},
                         {"USER32.DLL", "GetQueueStatus", 1},
                         {"USER32.DLL", "GetInputState", 0},
                         {"KERNEL32.DLL", "GlobalFlags", 1},
                         {"USER32.DLL", "WaitForInputIdle", 2},
                         {"USER32.DLL", "SendMessageCallbackA", 6},
                         {"USER32.DLL", "ScrollWindowEx", 8},
                         {"USER32.DLL", "ScrollWindow", 5},
                         {"USER32.DLL", "MapWindowPoints", 4},
                         {"USER32.DLL", "CheckMenuRadioItem", 5},
                         {"USER32.DLL", "MessageBeep", 1},
                         {"GDI32.DLL", "SetTextColor", 2},
                         {"GDI32.DLL", "SetBkColor", 2},
                         {"KERNEL32.DLL", "WritePrivateProfileStructA", 5},
                         {"KERNEL32.DLL", "GetPrivateProfileStructA", 5},
                         {"KERNEL32.DLL", "WritePrivateProfileSectionA", 3},
                         {"KERNEL32.DLL", "WriteProfileSectionA", 2},
                         {"KERNEL32.DLL", "WritePrivateProfileStringA", 4},
                         {"KERNEL32.DLL", "GetPrivateProfileSectionNamesA", 3},
                         {"KERNEL32.DLL", "GetPrivateProfileSectionA", 4},
                         {"KERNEL32.DLL", "GetProfileSectionA", 3},
                         {"KERNEL32.DLL", "GetPrivateProfileStringA", 6},
                         {"KERNEL32.DLL", "GetPrivateProfileIntA", 4},
                         {"KERNEL32.DLL", "GetProfileIntA", 3},
                         {"KERNEL32.DLL", "WriteProfileStringA", 3},
                         {"KERNEL32.DLL", "GlobalSize", 1},
                         {"KERNEL32.DLL", "GlobalCompact", 1},
                         {"KERNEL32.DLL", "GlobalReAlloc", 3},
                         {"ADVAPI32.DLL", "RegOpenKeyExA", 5},

                         {"MSVCRT.DLL", "strtod", 0x80000000u | 2},
                         {"MSVCRT.DLL", "atof", 0x80000000u | 1},
                         {"MSVCRT.DLL", "sprintf", 0x80000000u | 2},
                         {"MSVCRT.DLL", "vsprintf", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_snprintf", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_vsnprintf", 0x80000000u | 4},
                         {"MSVCRT.DLL", "swprintf", 0x80000000u | 2},
                         {"MSVCRT.DLL", "vswprintf", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_snwprintf", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_vsnwprintf", 0x80000000u | 4},
                         {"MSVCRT.DLL", "_EH_prolog", 0xc0000000u},
                         {"MSVCRT.DLL", "__set_app_type", 0x80000000u | 1},
                         {"MSVCRT.DLL", "strftime", 0x80000000u | 4},
                         {"MSVCRT.DLL", "_Strftime", 0x80000000u | 5},
                         {"MSVCRT.DLL", "wcsftime", 0x80000000u | 4},
                         {"MSVCRT.DLL", "_strdate", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_strtime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_wstrdate", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_wstrtime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "difftime", 0x80000000u | 2},
                         {"MSVCRT.DLL", "clock", 0x80000000u | 0},
                         {"MSVCRT.DLL", "_getsystime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_setsystime", 0x80000000u | 2},
                         {"MSVCRT.DLL", "mktime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "gmtime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "localtime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "time", 0x80000000u | 1},
                         {"MSVCRT.DLL", "asctime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "ctime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_wasctime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_wctime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_tzset", 0x80000000u | 0},
                         {"MSVCRT.DLL", "_ftime", 0x80000000u | 1},
                         {"MSVCRT.DLL", "__p__timezone", 0x80000000u | 0},
                         {"MSVCRT.DLL", "__p__daylight", 0x80000000u | 0},
                         {"MSVCRT.DLL", "__p__dstbias", 0x80000000u | 0},
                         {"MSVCRT.DLL", "__p__tzname", 0x80000000u | 0},
                         {"MSVCRT.DLL", "__p__fmode", 0x80000000u | 0},
                         {"MSVCRT.DLL", "__p__acmdln", 0x80000000u},
                         {"MSVCRT.DLL", "__p___argc", 0x80000000u},
                         {"MSVCRT.DLL", "__p___argv", 0x80000000u},
                         {"MSVCRT.DLL", "__p__environ", 0x80000000u},
                         {"MSVCRT.DLL", "__p__pgmptr", 0x80000000u},

                         {"MSVCRT.DLL", "__p__commode", 0x80000000u | 0},
                         {"MSVCRT.DLL", "__setusermatherr", 0x80000000u | 1},
                         {"MSVCRT.DLL", "__getmainargs", 0x80000000u | 5},
                         {"MSVCRT.DLL", "__p___initenv", 0x80000000u | 0},
                         {"MSVCRT.DLL", "_initterm", 0x80000000u | 2},
                         {"MSVCRT.DLL", "__dllonexit", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_onexit", 0x80000000u | 1},
                         {"MSVCRT.DLL", "atexit", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_cexit", 0x80000000u},
                         {"MSVCRT.DLL", "_c_exit", 0x80000000u},
                         {"MSVCRT.DLL", "_ftol", 0x80000000u},
                         {"MSVCRT.DLL", "_fpreset", 0x80000000u},
                         {"MSVCRT.DLL", "_statusfp", 0x80000000u},
                         {"MSVCRT.DLL", "_clearfp", 0x80000000u},
                         {"MSVCRT.DLL", "_controlfp", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_setmbcp", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_getmbcp", 0x80000000u},
                         {"MSVCRT.DLL", "setlocale", 0x80000000u | 2},
                         {"MSVCRT.DLL", "localeconv", 0x80000000u},
                         {"MSVCRT.DLL", "__p__pctype", 0x80000000u},
                         {"MSVCRT.DLL", "__p__pwctype", 0x80000000u},
                         {"MSVCRT.DLL", "__p___mb_cur_max", 0x80000000u},
                         {"MSVCRT.DLL", "_control87", 0x80000000u | 2},
                         {"MSVCRT.DLL", "exit", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_exit", 0x80000000u | 1},
                         {"MSVCRT.DLL", "toupper", 0x80000000u | 1},
                         {"MSVCRT.DLL", "tolower", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_isctype", 0x80000000u | 2},
                         {"MSVCRT.DLL", "memmove", 0x80000000u | 3},
                         {"MSVCRT.DLL", "memcpy", 0x80000000u | 3},
                         {"MSVCRT.DLL", "memcmp", 0x80000000u | 3},
                         {"MSVCRT.DLL", "memset", 0x80000000u | 3},
                         {"MSVCRT.DLL", "malloc", 0x80000000u | 1},
                         {"MSVCRT.DLL", "calloc", 0x80000000u | 2},
                         {"MSVCRT.DLL", "realloc", 0x80000000u | 2},
                         {"MSVCRT.DLL", "free", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_msize", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_errno", 0x80000000u},
                         {"MSVCRT.DLL", "_beginthreadex", 0x80000000u | 6},
                         {"MSVCRT.DLL", "_endthreadex", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_beginthread", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_endthread", 0x80000000u},
                         {"MSVCRT.DLL", "__doserrno", 0x80000000u},
                         {"MSVCRT.DLL", "__threadid", 0x80000000u},
                         {"MSVCRT.DLL", "__threadhandle", 0x80000000u},
                         {"MSVCRT.DLL", "srand", 0x80000000u | 1},
                         {"MSVCRT.DLL", "rand", 0x80000000u},
                         {"MSVCRT.DLL", "strtok", 0x80000000u | 2},
                         {"MSVCRT.DLL", "strchr", 0x80000000u | 2},
                         {"MSVCRT.DLL", "strrchr", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsrchr", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbspbrk", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbscspn", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsspn", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsspnp", 0x80000000u | 2},
                         {"MSVCRT.DLL", "strpbrk", 0x80000000u | 2},
                         {"MSVCRT.DLL", "strcspn", 0x80000000u | 2},
                         {"MSVCRT.DLL", "strspn", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbschr", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsinc", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_mbscmp", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsncmp", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_mbsnbcmp", 0x80000000u | 3},
                         {"MSVCRT.DLL", "_mbslen", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_mbsdec", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsninc", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsnextc", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_mbclen", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_mbsnbcnt", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_mbsnccnt", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_ismbblead", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_ismbbtrail", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_ismbslead", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_ismbstrail", 0x80000000u | 2},
                         {"MSVCRT.DLL", "_strdup", 0x80000000u | 1},
                         {"MSVCRT.DLL", "_strrev", 0x80000000u | 1},

                         {"USER32.DLL", "CreateDialogParamA", 5},
                         {"USER32.DLL", "CreateDialogIndirectParamA", 5},
                         {"USER32.DLL", "DialogBoxIndirectParamA", 5},
                         {"USER32.DLL", "DialogBoxParamA", 5},
                         {"USER32.DLL", "EndDialog", 2},
                         {"USER32.DLL", "GetDlgItem", 2},
                         {"USER32.DLL", "GetParent", 1},
                         {"USER32.DLL", "GetDesktopWindow", 0},
                         {"USER32.DLL", "GetWindow", 2},
                         {"USER32.DLL", "GetTopWindow", 1},
                         {"USER32.DLL", "GetCapture", 0},
                         {"USER32.DLL", "SetCapture", 1},
                         {"USER32.DLL", "ReleaseCapture", 0},
                         {"USER32.DLL", "BringWindowToTop", 1},
                         {"USER32.DLL", "SetParent", 2},
                         {"USER32.DLL", "GetWindowThreadProcessId", 2},
                         {"USER32.DLL", "GetClassLongA", 2},
                         {"USER32.DLL", "SendMessageTimeoutA", 7},
                         {"USER32.DLL", "MonitorFromPoint", 3},
                         {"USER32.DLL", "MonitorFromRect", 2},
                         {"USER32.DLL", "MonitorFromWindow", 2},
                         {"USER32.DLL", "GetMonitorInfoA", 2},
                         {"USER32.DLL", "GetMonitorInfoW", 2},
                         {"USER32.DLL", "EnumDisplayMonitors", 4},
                         {"USER32.DLL", "EnumWindows", 2},
                         {"USER32.DLL", "EnumChildWindows", 3},
                         {"USER32.DLL", "EnumThreadWindows", 3},
                         {"USER32.DLL", "IsChild", 2},
                         {"USER32.DLL", "CheckDlgButton", 3},
                         {"USER32.DLL", "CheckRadioButton", 4},
                         {"USER32.DLL", "IsDlgButtonChecked", 2},
                         {"USER32.DLL", "SetDlgItemInt", 4},
                         {"USER32.DLL", "MapDialogRect", 2},
                         {"USER32.DLL", "GetDialogBaseUnits", 0},
                         {"USER32.DLL", "GetNextDlgTabItem", 3},
                         {"USER32.DLL", "GetNextDlgGroupItem", 3},
                         {"USER32.DLL", "DefDlgProcA", 4},
                         {"USER32.DLL", "MessageBoxA", 4},
                         {"USER32.DLL", "MessageBoxExA", 5},
                         {"USER32.DLL", "MessageBoxW", 4},
                         {"USER32.DLL", "MessageBoxExW", 5},
                         {"USER32.DLL", "MessageBoxIndirectA", 1},
                         {"USER32.DLL", "GetWindowLongA", 2},
                         {"KERNEL32.DLL", "GetCommandLineA", 0},
                         {"KERNEL32.DLL", "GetComputerNameA", 2},
                         {"KERNEL32.DLL", "SetComputerNameA", 1},
                         {"KERNEL32.DLL", "GetStartupInfoA", 1},
                         {"KERNEL32.DLL", "GetModuleHandleA", 1},
                         {"KERNEL32.DLL", "GetModuleFileNameA", 3},
                         {"KERNEL32.DLL", "GetProcAddress", 2},
                         {"KERNEL32.DLL", "LoadLibraryA", 1},
                         {"KERNEL32.DLL", "LoadLibraryExA", 3},
                         {"KERNEL32.DLL", "FreeLibrary", 1},
                         {"KERNEL32.DLL", "DisableThreadLibraryCalls", 1},
                         {"KERNEL32.DLL", "ExitProcess", 1},
                         {"KERNEL32.DLL", "GetLastError", 0},
                         {"KERNEL32.DLL", "SetErrorMode", 1},
                         {"KERNEL32.DLL", "CreateMutexA", 3},
                         {"KERNEL32.DLL", "OpenMutexA", 3},
                         {"KERNEL32.DLL", "ReleaseMutex", 1},
                         {"KERNEL32.DLL", "CreateSemaphoreA", 4},
                         {"KERNEL32.DLL", "OpenSemaphoreA", 3},
                         {"KERNEL32.DLL", "ReleaseSemaphore", 3},
                         {"KERNEL32.DLL", "CreateFileMappingA", 6},
                         {"KERNEL32.DLL", "OpenFileMappingA", 3},
                         {"KERNEL32.DLL", "MapViewOfFile", 5},
                         {"KERNEL32.DLL", "MapViewOfFileEx", 6},
                         {"KERNEL32.DLL", "UnmapViewOfFile", 1},
                         {"KERNEL32.DLL", "TlsAlloc", 0},
                         {"KERNEL32.DLL", "TlsFree", 1},
                         {"KERNEL32.DLL", "TlsGetValue", 1},
                         {"KERNEL32.DLL", "TlsSetValue", 2},
                         {"KERNEL32.DLL", "InitializeCriticalSection", 1},
                         {"KERNEL32.DLL", "MakeCriticalSectionGlobal", 1},
                         {"KERNEL32.DLL", "InitializeCriticalSectionAndSpinCount", 2},
                         {"KERNEL32.DLL", "DeleteCriticalSection", 1},
                         {"KERNEL32.DLL", "ReinitializeCriticalSection", 1},
                         {"KERNEL32.DLL", "EnterCriticalSection", 1},
                         {"KERNEL32.DLL", "LeaveCriticalSection", 1},
                         {"KERNEL32.DLL", "LocalAlloc", 2},
                         {"KERNEL32.DLL", "GlobalAlloc", 2},
                         {"KERNEL32.DLL", "LocalLock", 1},
                         {"KERNEL32.DLL", "GlobalLock", 1},
                         {"KERNEL32.DLL", "LocalUnlock", 1},
                         {"KERNEL32.DLL", "GlobalUnlock", 1},
                         {"KERNEL32.DLL", "LocalFree", 1},
                         {"KERNEL32.DLL", "LocalSize", 1},
                         {"KERNEL32.DLL", "ConvertToGlobalHandle", 1},
                         {"KERNEL32.DLL", "ordinal_18", 2},
                         {"KERNEL32.DLL", "GlobalFree", 1},
                         {"KERNEL32.DLL", "LocalReAlloc", 3},
                         {"KERNEL32.DLL", "RtlMoveMemory", 3},
                         {"KERNEL32.DLL", "MulDiv", 3},
                         {"KERNEL32.DLL", "lstrlenA", 1},
                         {"KERNEL32.DLL", "lstrlenW", 1},
                         {"KERNEL32.DLL", "lstrcpyW", 2},
                         {"KERNEL32.DLL", "lstrcatW", 2},
                         {"KERNEL32.DLL", "lstrcpynW", 3},
                         {"KERNEL32.DLL", "lstrcpyA", 2},
                         {"KERNEL32.DLL", "lstrcpynA", 3},
                         {"KERNEL32.DLL", "lstrcatA", 2},
                         {"KERNEL32.DLL", "lstrcmpA", 2},
                         {"KERNEL32.DLL", "lstrcmpiA", 2},
                         {"USER32.DLL", "CharNextA", 1},
                         {"USER32.DLL", "CharPrevA", 2},
                         {"USER32.DLL", "CharNextExA", 3},
                         {"USER32.DLL", "CharPrevExA", 4},
                         {"USER32.DLL", "IsCharLowerA", 1},
                         {"USER32.DLL", "IsCharUpperA", 1},
                         {"USER32.DLL", "IsCharAlphaNumericA", 1},
                         {"USER32.DLL", "IsCharAlphaA", 1},
                         {"USER32.DLL", "LoadStringA", 4},
                         {"USER32.DLL", "LoadAcceleratorsA", 2},
                         {"USER32.DLL", "CopyImage", 5},
                         {"USER32.DLL", "NotifyWinEvent", 4},
                         {"USER32.DLL", "LoadImageA", 6},
                         {"USER32.DLL", "LoadBitmapA", 2},
                         {"USER32.DLL", "LoadCursorFromFileA", 1},
                         {"USER32.DLL", "LookupIconIdFromDirectory", 2},
                         {"USER32.DLL", "LookupIconIdFromDirectoryEx", 5},
                         {"USER32.DLL", "CreateIconFromResource", 4},
                         {"USER32.DLL", "CreateIconFromResourceEx", 7},
                         {"USER32.DLL", "CreateIconIndirect", 1},
                         {"USER32.DLL", "CreateIcon", 7},
                         {"USER32.DLL", "CreateCursor", 7},
                         {"USER32.DLL", "DestroyCursor", 1},
                         {"USER32.DLL", "DestroyIcon", 1},
                         {"USER32.DLL", "DrawIcon", 4},
                         {"USER32.DLL", "DrawIconEx", 9},
                         {"COMCTL32.DLL", "ImageList_Create", 5},
                         {"COMCTL32.DLL", "ImageList_Remove", 2},
                         {"COMCTL32.DLL", "ImageList_GetIconSize", 3},
                         {"COMCTL32.DLL", "ImageList_SetIconSize", 3},
                         {"COMCTL32.DLL", "ImageList_Destroy", 1},
                         {"COMCTL32.DLL", "ImageList_ReplaceIcon", 3},
                         {"COMCTL32.DLL", "ImageList_SetBkColor", 2},
                         {"COMCTL32.DLL", "ImageList_GetImageCount", 1},
                         {"COMCTL32.DLL", "ImageList_Draw", 6},
                         {"USER32.DLL", "LoadCursorA", 2},
                         {"USER32.DLL", "LoadIconA", 2},
                         {"USER32.DLL", "RegisterClassExA", 1},
                         {"USER32.DLL", "CreateWindowExA", 12},
                         {"USER32.DLL", "DefWindowProcA", 4},
                         {"USER32.DLL", "DestroyWindow", 1},
                         {"USER32.DLL", "SetScrollInfo", 4},
                         {"USER32.DLL", "GetScrollInfo", 3},
                         {"USER32.DLL", "SetScrollPos", 4},
                         {"USER32.DLL", "GetScrollPos", 2},
                         {"USER32.DLL", "SetScrollRange", 5},
                         {"USER32.DLL", "GetScrollRange", 4},
                         {"USER32.DLL", "ShowScrollBar", 3},
                         {"USER32.DLL", "EnableScrollBar", 3},

                         {"USER32.DLL", "SetWindowLongA", 3},
                         {"USER32.DLL", "GetDC", 1},
                         {"USER32.DLL", "ReleaseDC", 2},
                         {"USER32.DLL", "WindowFromDC", 1},
                         {"USER32.DLL", "LockWindowUpdate", 1},
                         {"USER32.DLL", "GetWindowContextHelpId", 1},
                         {"USER32.DLL", "SetWindowContextHelpId", 2},
                         {"USER32.DLL", "GetClientRect", 2},
                         {"USER32.DLL", "GetWindowRect", 2},
                         {"USER32.DLL", "AdjustWindowRect", 3},
                         {"USER32.DLL", "AdjustWindowRectEx", 4},
                         {"USER32.DLL", "MoveWindow", 6},
                         {"USER32.DLL", "ShowWindow", 2},
                         {"USER32.DLL", "GetSystemMetrics", 1},
                         {"USER32.DLL", "SystemParametersInfoA", 4},
                         {"USER32.DLL", "InvalidateRect", 3},
                         {"USER32.DLL", "RedrawWindow", 4},
                         {"USER32.DLL", "UpdateWindow", 1},
                         {"USER32.DLL", "BeginPaint", 2},
                         {"USER32.DLL", "EndPaint", 2},
                         {"USER32.DLL", "GetUpdateRect", 3},
                         {"USER32.DLL", "ValidateRect", 2},
                         {"USER32.DLL", "GetWindowDC", 1},
                         {"USER32.DLL", "GetDCEx", 3},
                         {"USER32.DLL", "GetUpdateRgn", 3},
                         {"USER32.DLL", "InvalidateRgn", 3},
                         {"USER32.DLL", "ValidateRgn", 2},
                         {"USER32.DLL", "ExcludeUpdateRgn", 2},
                         {"USER32.DLL", "CharToOemA", 2},
                         {"USER32.DLL", "CharToOemBuffA", 3},
                         {"USER32.DLL", "OemToCharA", 2},
                         {"USER32.DLL", "OemToCharBuffA", 3},
                         {"USER32.DLL", "GetWindowWord", 2},
                         {"USER32.DLL", "SetWindowWord", 3},
                         {"USER32.DLL", "CopyIcon", 1},
                         {"USER32.DLL", "AttachThreadInput", 3},
                         {"USER32.DLL", "GetThreadDesktop", 1},
                         {"USER32.DLL", "SetThreadDesktop", 1},
                         {"USER32.DLL", "SetWindowsHookA", 2},
                         {"USER32.DLL", "UnhookWindowsHook", 2},
                         {"USER32.DLL", "GetAncestor", 2},
                         {"USER32.DLL", "GetWindowModuleFileNameA", 3},
                         {"USER32.DLL", "GetKeyboardType", 1},
                         {"USER32.DLL", "BroadcastSystemMessage", 5},
                         {"USER32.DLL", "BroadcastSystemMessageA", 5},
                         {"USER32.DLL", "keybd_event", 4},
                         {"USER32.DLL", "mouse_event", 5},
                         {"USER32.DLL", "TrackMouseEvent", 1},
                         {"USER32.DLL", "GetDlgItemInt", 4},
                         {"USER32.DLL", "PackDDElParam", 3},
                         {"USER32.DLL", "UnpackDDElParam", 4},
                         {"USER32.DLL", "FreeDDElParam", 2},
                         {"USER32.DLL", "ReuseDDElParam", 5},
                         {"USER32.DLL", "SetFocus", 1},
                         {"USER32.DLL", "GetFocus", 0},
                         {"USER32.DLL", "GetForegroundWindow", 0},
                         {"USER32.DLL", "IsIconic", 1},
                         {"USER32.DLL", "IsZoomed", 1},
                         {"USER32.DLL", "CloseWindow", 1},
                         {"USER32.DLL", "OpenIcon", 1},
                         {"USER32.DLL", "SwitchToThisWindow", 2},
                         {"USER32.DLL", "GetActiveWindow", 0},
                         {"USER32.DLL", "SetActiveWindow", 1},
                         {"USER32.DLL", "GetLastActivePopup", 1},
                         {"USER32.DLL", "SetForegroundWindow", 1},
                         {"USER32.DLL", "EnableWindow", 2},
                         {"USER32.DLL", "SetWindowTextA", 2},
                         {"USER32.DLL", "GetWindowTextA", 3},
                         {"USER32.DLL", "GetWindowTextLengthA", 1},
                         {"USER32.DLL", "GetDlgCtrlID", 1},
                         {"USER32.DLL", "SetDlgItemTextA", 3},
                         {"USER32.DLL", "GetDlgItemTextA", 4},
                         {"USER32.DLL", "SendMessageA", 4},
                         {"USER32.DLL", "InSendMessage", 0},
                         {"USER32.DLL", "InSendMessageEx", 1},
                         {"USER32.DLL", "ReplyMessage", 1},
                         {"USER32.DLL", "SendDlgItemMessageA", 5},
                         {"USER32.DLL", "GetMessageA", 4},
                         {"USER32.DLL", "GetMessageTime", 0},
                         {"USER32.DLL", "GetMessagePos", 0},
                         {"USER32.DLL", "GetMessageExtraInfo", 0},
                         {"USER32.DLL", "SetMessageExtraInfo", 1},
                         {"USER32.DLL", "SetWindowsHookExA", 4},
                         {"USER32.DLL", "UnhookWindowsHookEx", 1},
                         {"USER32.DLL", "CallNextHookEx", 4},
                         {"USER32.DLL", "CallMsgFilterA", 2},
                         {"USER32.DLL", "CallMsgFilter", 2},
                         {"USER32.DLL", "PeekMessageA", 5},
                         {"USER32.DLL", "RegisterHotKey", 4},
                         {"USER32.DLL", "DdeInitializeA", 4},
                         {"USER32.DLL", "DdeInitializeW", 4},
                         {"USER32.DLL", "DdeUninitialize", 1},
                         {"USER32.DLL", "DdeCreateStringHandleA", 3},
                         {"USER32.DLL", "DdeCreateStringHandleW", 3},
                         {"USER32.DLL", "DdeFreeStringHandle", 2},
                         {"USER32.DLL", "DdeKeepStringHandle", 2},
                         {"USER32.DLL", "DdeCmpStringHandles", 2},
                         {"USER32.DLL", "DdeQueryStringA", 5},
                         {"USER32.DLL", "DdeQueryStringW", 5},
                         {"USER32.DLL", "DdeGetLastError", 1},
                         {"USER32.DLL", "DdeNameService", 4},
                         {"USER32.DLL", "DdeConnect", 4},
                         {"USER32.DLL", "DdeDisconnect", 1},
                         {"USER32.DLL", "DdeClientTransaction", 8},
                         {"USER32.DLL", "DdeCreateDataHandle", 7},
                         {"USER32.DLL", "DdeGetData", 4},
                         {"USER32.DLL", "DdeAccessData", 2},
                         {"USER32.DLL", "DdeUnaccessData", 1},
                         {"USER32.DLL", "DdeFreeDataHandle", 1},
                         {"USER32.DLL", "UnregisterHotKey", 2},
                         {"USER32.DLL", "GetKeyState", 1},
                         {"USER32.DLL", "GetAsyncKeyState", 1},
                         {"USER32.DLL", "SwapMouseButton", 1},
                         {"USER32.DLL", "GetKeyboardLayout", 1},
                         {"USER32.DLL", "GetKeyboardLayoutList", 2},
                         {"USER32.DLL", "GetKeyboardLayoutNameA", 1},
                         {"USER32.DLL", "ActivateKeyboardLayout", 2},
                         {"USER32.DLL", "LoadKeyboardLayoutA", 2},
                         {"USER32.DLL", "UnloadKeyboardLayout", 1},
                         {"USER32.DLL", "GetKeyboardState", 1},
                         {"USER32.DLL", "SetKeyboardState", 1},
                         {"USER32.DLL", "DispatchMessageA", 1},
                         {"USER32.DLL", "PostMessageA", 4},
                         {"USER32.DLL", "PostThreadMessageA", 4},
                         {"USER32.DLL", "PostThreadMessageW", 4},
                         {"USER32.DLL", "SendNotifyMessageA", 4},
                         {"USER32.DLL", "PostQuitMessage", 1},
                         {"USER32.DLL", "ExitWindowsEx", 2},
                         {"USER32.DLL", "RegisterSystemThread", 2},
                         {"USER32.DLL", "RegisterNetworkCapabilities", 2},
                         {"USER32.DLL", "EnumDisplaySettingsA", 3},
                         {"USER32.DLL", "EnumDisplaySettingsExA", 4},
                         {"USER32.DLL", "EnumDisplayDevicesA", 4},
                         {"USER32.DLL", "SetLastErrorEx", 2},
                         {"ADVAPI32.DLL", "CryptAcquireContextA", 5},
                         {"ADVAPI32.DLL", "CryptReleaseContext", 2},
                         {"ADVAPI32.DLL", "CryptContextAddRef", 3},
                         {"ADVAPI32.DLL", "CryptCreateHash", 5},
                         {"ADVAPI32.DLL", "CryptDecrypt", 6},
                         {"ADVAPI32.DLL", "CryptDeriveKey", 5},
                         {"ADVAPI32.DLL", "CryptDestroyHash", 1},
                         {"ADVAPI32.DLL", "CryptDestroyKey", 1},
                         {"ADVAPI32.DLL", "CryptDuplicateHash", 4},
                         {"ADVAPI32.DLL", "CryptDuplicateKey", 4},
                         {"ADVAPI32.DLL", "CryptEncrypt", 7},
                         {"ADVAPI32.DLL", "CryptEnumProviderTypesA", 6},
                         {"ADVAPI32.DLL", "CryptEnumProvidersA", 6},
                         {"ADVAPI32.DLL", "CryptExportKey", 6},
                         {"ADVAPI32.DLL", "CryptGenKey", 4},
                         {"ADVAPI32.DLL", "CryptGenRandom", 3},
                         {"ADVAPI32.DLL", "CryptGetDefaultProviderA", 5},
                         {"ADVAPI32.DLL", "CryptGetHashParam", 5},
                         {"ADVAPI32.DLL", "CryptGetKeyParam", 5},
                         {"ADVAPI32.DLL", "CryptGetProvParam", 5},
                         {"ADVAPI32.DLL", "CryptGetUserKey", 3},
                         {"ADVAPI32.DLL", "CryptHashData", 4},
                         {"ADVAPI32.DLL", "CryptHashSessionKey", 3},
                         {"ADVAPI32.DLL", "CryptImportKey", 6},
                         {"ADVAPI32.DLL", "CryptSetHashParam", 4},
                         {"ADVAPI32.DLL", "CryptSetKeyParam", 4},
                         {"ADVAPI32.DLL", "CryptSetProvParam", 4},
                         {"ADVAPI32.DLL", "CryptSetProviderA", 2},
                         {"ADVAPI32.DLL", "CryptSetProviderExA", 4},
                         {"ADVAPI32.DLL", "CryptSignHashA", 6},
                         {"ADVAPI32.DLL", "CryptVerifySignatureA", 6},
                         {"USER32.DLL", "SetDebugErrorLevel", 1},
                         {"USER32.DLL", "ChangeDisplaySettingsExA", 5},
                         {"USER32.DLL", "ArrangeIconicWindows", 1},
                         {"USER32.DLL", "TileWindows", 5},
                         {"USER32.DLL", "CascadeWindows", 5},
                         {"USER32.DLL", "ShowOwnedPopups", 2},
                         {"USER32.DLL", "FlashWindow", 2},
                         {"USER32.DLL", "FlashWindowEx", 1},
                         {"USER32.DLL", "ShowWindowAsync", 2},
                         {"USER32.DLL", "AnimateWindow", 3},
                         {"USER32.DLL", "GetWindowInfo", 2},
                         {"USER32.DLL", "GetTitleBarInfo", 2},
                         {"USER32.DLL", "GetScrollBarInfo", 3},
                         {"USER32.DLL", "GetComboBoxInfo", 2},
                         {"USER32.DLL", "GetListBoxInfo", 1},
                         {"USER32.DLL", "GetCursorInfo", 1},
                         {"USER32.DLL", "GetAltTabInfo", 5},
                         {"USER32.DLL", "GetGUIThreadInfo", 2},
                         {"USER32.DLL", "RealGetWindowClass", 3},
                         {"USER32.DLL", "SetSysColors", 3},
                         {"USER32.DLL", "SetSysColorsTemp", 3},
                         {"USER32.DLL", "HiliteMenuItem", 4},
                         {"USER32.DLL", "CreateMDIWindowA", 10},
                         {"USER32.DLL", "DlgDirSelectExA", 4},
                         {"USER32.DLL", "DlgDirSelectComboBoxExA", 4},
                         {"USER32.DLL", "EnumPropsA", 2},
                         {"USER32.DLL", "EnumPropsExA", 3},
                         {"USER32.DLL", "ChangeMenuA", 5},
                         {"USER32.DLL", "AlignRects", 4},
                         {"USER32.DLL", "LoadMenuIndirectA", 1},
                         {"USER32.DLL", "EndTask", 4},
                         {"USER32.DLL", "RegisterDeviceNotificationA", 3},
                         {"USER32.DLL", "UnregisterDeviceNotification", 1},
                         {"USER32.DLL", "DefFrameProcA", 5},
                         {"USER32.DLL", "DefMDIChildProcA", 4},
                         {"USER32.DLL", "TranslateMDISysAccel", 2},
                         {"USER32.DLL", "DdeConnectList", 5},
                         {"USER32.DLL", "DdeQueryNextServer", 2},
                         {"USER32.DLL", "DdeDisconnectList", 1},
                         {"USER32.DLL", "DdeQueryConvInfo", 3},
                         {"USER32.DLL", "DdeSetUserHandle", 3},
                         {"USER32.DLL", "DdeAbandonTransaction", 3},
                         {"USER32.DLL", "DdePostAdvise", 3},
                         {"USER32.DLL", "DlgDirListA", 5},
                         {"USER32.DLL", "DlgDirListComboBoxA", 5},
                         {"USER32.DLL", "DrawStateA", 10},
                         {"USER32.DLL", "GrayStringA", 9},
                         {"USER32.DLL", "TabbedTextOutA", 8},
                         {"USER32.DLL", "GetTabbedTextExtentA", 5},
                         {"USER32.DLL", "CreateAcceleratorTableA", 2},
                         {"USER32.DLL", "CopyAcceleratorTableA", 3},
                         {"USER32.DLL", "DestroyAcceleratorTable", 1},
                         {"USER32.DLL", "ToAscii", 5},
                         {"USER32.DLL", "ToAsciiEx", 6},
                         {"USER32.DLL", "VkKeyScanA", 1},
                         {"USER32.DLL", "VkKeyScanExA", 2},
                         {"USER32.DLL", "OemKeyScan", 1},
                         {"USER32.DLL", "MapVirtualKeyExA", 3},
                         {"USER32.DLL", "TranslateMessage", 1},
                         {"USER32.DLL", "TranslateAcceleratorA", 3},
                         {"USER32.DLL", "IsDialogMessageA", 2},
                         {"USER32.DLL", "GetMenu", 1},
                         {"USER32.DLL", "LoadMenuA", 2},
                         {"USER32.DLL", "SetMenu", 2},
                         {"USER32.DLL", "DrawMenuBar", 1},
                         {"USER32.DLL", "GetSystemMenu", 2},
                         {"USER32.DLL", "GetSubMenu", 2},
                         {"USER32.DLL", "EnableMenuItem", 3},
                         {"USER32.DLL", "CheckMenuItem", 3},
                         {"GDI32.DLL", "GetStockObject", 1},
                         {"GDI32.DLL", "GetObjectA", 3},
                         {"GDI32.DLL", "CreateFontIndirectA", 1},
                         {"GDI32.DLL", "CreateFontA", 14},
                         {"GDI32.DLL", "SelectObject", 2},
                         {"GDI32.DLL", "DeleteObject", 1},
                         {"GDI32.DLL", "GetDeviceCaps", 2},
                         {"GDI32.DLL", "GetTextMetricsA", 2},
                         {"GDI32.DLL", "GetTextFaceA", 3},
                         {"GDI32.DLL", "GetCharWidthA", 4},
                         {"GDI32.DLL", "GetCharWidthW", 4},
                         {"GDI32.DLL", "GetTextCharset", 1},
                         {"GDI32.DLL", "GetTextCharsetInfo", 3},
                         {"GDI32.DLL", "GetFontLanguageInfo", 1},
                         {"GDI32.DLL", "TranslateCharsetInfo", 3},
                         {"GDI32.DLL", "GetCharABCWidthsA", 4},
                         {"GDI32.DLL", "EnumFontsA", 4},
                         {"GDI32.DLL", "EnumFontFamiliesA", 4},
                         {"GDI32.DLL", "EnumFontFamiliesExA", 5},
                         {"GDI32.DLL", "GetTextExtentPointA", 4},
                         {"KERNEL32.DLL", "GetLocaleInfoA", 4},
                         {"KERNEL32.DLL", "RaiseException", 0x40000004u},
                         {"KERNEL32.DLL", "RtlUnwind", 0x40000004u},
                         {"KERNEL32.DLL", "ConvertThreadToFiber", 1},
                         {"KERNEL32.DLL", "CreateFiber", 3},
                         {"KERNEL32.DLL", "DeleteFiber", 1},
                         {"KERNEL32.DLL", "SwitchToFiber", 0x40000001u},
                         {"KERNEL32.DLL", "ContinueDebugEvent", 3},
                         {"KERNEL32.DLL", "DebugActiveProcess", 1},
                         {"KERNEL32.DLL", "DebugBreak", 0x40000000u},
                         {"KERNEL32.DLL", "WaitForDebugEvent", 2},
                         {"KERNEL32.DLL", "_DebugOut", 0x80000002u},
                         {"KERNEL32.DLL", "_DebugPrintf", 0x80000000u},
                         {"KERNEL32.DLL", "dprintf", 0x80000000u},
                         {"KERNEL32.DLL", "FatalAppExitA", 2},
                         {"KERNEL32.DLL", "FatalExit", 0x40000001u},
                         {"KERNEL32.DLL", "Beep", 2},
                         {"KERNEL32.DLL", "GetThreadSelectorEntry", 3},
                         {"KERNEL32.DLL", "GetWriteWatch", 6},
                         {"KERNEL32.DLL", "ResetWriteWatch", 2},
                         {"KERNEL32.DLL", "TlsAllocInternal", 0},
                         {"KERNEL32.DLL", "TlsFreeInternal", 1},
                         {"KERNEL32.DLL", "CreateKernelThread", 6},
                         {"KERNEL32.DLL", "GetProductName", 2},
                         {"KERNEL32.DLL", "K32_NtCreateFile", 11},
                         {"KERNEL32.DLL", "K32_RtlNtStatusToDosError", 1},
                         {"KERNEL32.DLL", "NotifyNLSUserCache", 3},
                         {"KERNEL32.DLL", "ResetNLSUserInfoCache", 0},
                         {"KERNEL32.DLL", "RegisterSysMsgHandler", 5},
                         {"KERNEL32.DLL", "SignalSysMsgHandlers", 4},
                         {"KERNEL32.DLL", "SetMessageWaitingIndicator", 2},
                         {"KERNEL32.DLL", "CancelDeviceWakeupRequest", 1},
                         {"KERNEL32.DLL", "RequestDeviceWakeup", 1},
                         {"KERNEL32.DLL", "RequestWakeupLatency", 1},
                         {"KERNEL32.DLL", "GetDevicePowerState", 2},
                         {"KERNEL32.DLL", "SetSystemPowerState", 2},
                         {"KERNEL32.DLL", "SetUnhandledExceptionFilter", 1},
                         {"KERNEL32.DLL", "UnhandledExceptionFilter", 1},
                         {"COMCTL32.DLL", "PropertySheet", 1},
                         {"COMCTL32.DLL", "PropertySheetA", 1},
                         {"COMCTL32.DLL", "PropertySheetW", 1},
                         {"COMCTL32.DLL", "CreatePropertySheetPage", 1},
                         {"COMCTL32.DLL", "CreatePropertySheetPageA", 1},
                         {"COMCTL32.DLL", "CreatePropertySheetPageW", 1},
                         {"COMCTL32.DLL", "DestroyPropertySheetPage", 1},

                         {"KERNEL32.DLL", "GetDateFormatA", 6},
                         {"KERNEL32.DLL", "GetTimeFormatA", 6},
                         {"KERNEL32.DLL", "EnumDateFormatsA", 3},
                         {"KERNEL32.DLL", "EnumTimeFormatsA", 3},
                         {"KERNEL32.DLL", "GetNumberFormatA", 6},
                         {"KERNEL32.DLL", "GetCurrencyFormatA", 6},
                         {"KERNEL32.DLL", "GetUserDefaultLCID", 0},
                         {"KERNEL32.DLL", "GetSystemDefaultLCID", 0},
                         {"KERNEL32.DLL", "GetSystemDefaultLangID", 0},
                         {"KERNEL32.DLL", "GetUserDefaultLangID", 0},
                         {"KERNEL32.DLL", "GetThreadLocale", 0},
                         {"KERNEL32.DLL", "InvalidateNLSCache", 0},
                         {"KERNEL32.DLL", "GetProfileStringA", 5},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_8", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_12", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_16", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_20", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_24", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_28", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_32", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_36", 0},
                         {"KERNEL32.DLL", "SMapLS_IP_EBP_40", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_8", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_12", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_16", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_20", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_24", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_28", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_32", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_36", 0},
                         {"KERNEL32.DLL", "SUnMapLS_IP_EBP_40", 0},
                         {"KERNEL32.DLL", "AllocLSCallback", 2},
                         {"KERNEL32.DLL", "AllocSLCallback", 2},
                         {"KERNEL32.DLL", "FreeLSCallback", 1},
                         {"KERNEL32.DLL", "FreeSLCallback", 1},
                         {"KERNEL32.DLL", "GetLSCallbackTarget", 1},
                         {"KERNEL32.DLL", "GetLSCallbackTemplate", 1},
                         {"KERNEL32.DLL", "GetSLCallbackTarget", 1},
                         {"KERNEL32.DLL", "GetSLCallbackTemplate", 1},
                         {"KERNEL32.DLL", "IsLSCallback", 1},
                         {"KERNEL32.DLL", "IsSLCallback", 1},
                         {"KERNEL32.DLL", "Callback4", 0x40000000u | 1},
                         {"KERNEL32.DLL", "Callback8", 0x40000000u | 2},
                         {"KERNEL32.DLL", "Callback12", 0x40000000u | 3},
                         {"KERNEL32.DLL", "Callback16", 0x40000000u | 4},
                         {"KERNEL32.DLL", "Callback20", 0x40000000u | 5},
                         {"KERNEL32.DLL", "Callback24", 0x40000000u | 6},
                         {"KERNEL32.DLL", "Callback28", 0x40000000u | 7},
                         {"KERNEL32.DLL", "Callback32", 0x40000000u | 8},
                         {"KERNEL32.DLL", "Callback36", 0x40000000u | 9},
                         {"KERNEL32.DLL", "Callback40", 0x40000000u | 10},
                         {"KERNEL32.DLL", "Callback44", 0x40000000u | 11},
                         {"KERNEL32.DLL", "Callback48", 0x40000000u | 12},
                         {"KERNEL32.DLL", "Callback52", 0x40000000u | 13},
                         {"KERNEL32.DLL", "Callback56", 0x40000000u | 14},
                         {"KERNEL32.DLL", "Callback60", 0x40000000u | 15},
                         {"KERNEL32.DLL", "Callback64", 0x40000000u | 16},
                         {"KERNEL32.DLL", "FT_Exit0", 0x40000000u | 0},
                         {"KERNEL32.DLL", "FT_Exit4", 0x40000000u | 1},
                         {"KERNEL32.DLL", "FT_Exit8", 0x40000000u | 2},
                         {"KERNEL32.DLL", "FT_Exit12", 0x40000000u | 3},
                         {"KERNEL32.DLL", "FT_Exit16", 0x40000000u | 4},
                         {"KERNEL32.DLL", "FT_Exit20", 0x40000000u | 5},
                         {"KERNEL32.DLL", "FT_Exit24", 0x40000000u | 6},
                         {"KERNEL32.DLL", "FT_Exit28", 0x40000000u | 7},
                         {"KERNEL32.DLL", "FT_Exit32", 0x40000000u | 8},
                         {"KERNEL32.DLL", "FT_Exit36", 0x40000000u | 9},
                         {"KERNEL32.DLL", "FT_Exit40", 0x40000000u | 10},
                         {"KERNEL32.DLL", "FT_Exit44", 0x40000000u | 11},
                         {"KERNEL32.DLL", "FT_Exit48", 0x40000000u | 12},
                         {"KERNEL32.DLL", "FT_Exit52", 0x40000000u | 13},
                         {"KERNEL32.DLL", "FT_Exit56", 0x40000000u | 14},
                         {"KERNEL32.DLL", "FT_Prolog", 0x40000000u | 0},
                         {"KERNEL32.DLL", "FT_Thunk", 0x40000000u | 0},
                         {"KERNEL32.DLL", "QT_Thunk", 0x40000000u | 0},
                         {"KERNEL32.DLL", "K32Thk1632Epilog", 0},
                         {"KERNEL32.DLL", "K32Thk1632Prolog", 0},
                         {"KERNEL32.DLL", "MapHInstLS", 0},
                         {"KERNEL32.DLL", "MapHInstLS_PN", 0},
                         {"KERNEL32.DLL", "MapHInstSL", 0},
                         {"KERNEL32.DLL", "MapHInstSL_PN", 0},
                         {"KERNEL32.DLL", "SMapLS", 0},
                         {"KERNEL32.DLL", "SUnMapLS", 0},
                         {"KERNEL32.DLL", "MapHModuleLS", 1},
                         {"KERNEL32.DLL", "MapHModuleSL", 1},
                         {"KERNEL32.DLL", "MapLS", 1},
                         {"KERNEL32.DLL", "MapSL", 1},
                         {"KERNEL32.DLL", "MapSLFix", 1},
                         {"KERNEL32.DLL", "UnMapLS", 1},
                         {"KERNEL32.DLL", "UTUnRegister", 1},
                         {"KERNEL32.DLL", "UnMapSLFixArray", 2},
                         {"KERNEL32.DLL", "UTRegister", 7},
                         {"ADVAPI32.DLL", "GetUserNameA", 2},
                         {"ADVAPI32.DLL", "RegOpenKeyA", 3},
                         {"ADVAPI32.DLL", "RegCreateKeyA", 3},
                         {"ADVAPI32.DLL", "RegSetValueExA", 6},
                         {"ADVAPI32.DLL", "RegQueryValueExA", 6},
                         {"ADVAPI32.DLL", "RegCloseKey", 1},
                         {"ADVAPI32.DLL", "RegCreateKeyExA", 9},
                         {"ADVAPI32.DLL", "RegEnumKeyA", 4},
                         {"ADVAPI32.DLL", "RegEnumKeyExA", 8},
                         {"ADVAPI32.DLL", "RegEnumValueA", 8},
                         {"ADVAPI32.DLL", "RegQueryInfoKeyA", 12},
                         {"ADVAPI32.DLL", "RegFlushKey", 1},
                         {"ADVAPI32.DLL", "RegDeleteKeyA", 2},
                         {"ADVAPI32.DLL", "RegDeleteValueA", 2},
                         {"ADVAPI32.DLL", "RegQueryValueA", 4},
                         {"ADVAPI32.DLL", "RegSetValueA", 5},
                         {"ADVAPI32.DLL", "RegConnectRegistryA", 3},
                         {"ADVAPI32.DLL", "RegNotifyChangeKeyValue", 5},
                         {"SHELL32.DLL", "SHGetSpecialFolderPathA", 4},
                         {"SHELL32.DLL", "SHGetSpecialFolderPathW", 4},
                         {"SHELL32.DLL", "ExtractIconA", 3},
                         {"SHELL32.DLL", "ExtractIconEx", 5},
                         {"SHELL32.DLL", "ExtractIconExA", 5},
                         {"SHELL32.DLL", "ExtractIconW", 3},
                         {"SHELL32.DLL", "InternalExtractIconListA", 3},
                         {"SHELL32.DLL", "InternalExtractIconListW", 3},
                         {"USER32.DLL", "RegisterWindowMessageA", 1},
                         {"SHELL32.DLL", "DragAcceptFiles", 2},
                         {"USER32.DLL", "WinHelpA", 4},
                         {"USER32.DLL", "ScreenToClient", 2},
                         {"USER32.DLL", "ClientToScreen", 2},
                         {"USER32.DLL", "GetCursorPos", 1},
                         {"USER32.DLL", "ChildWindowFromPoint", 3},
                         {"USER32.DLL", "ChildWindowFromPointEx", 4},
                         {"USER32.DLL", "RealChildWindowFromPoint", 3},
                         {"USER32.DLL", "WindowFromPoint", 2},
                         {"USER32.DLL", "SetWindowRgn", 3},
                         {"USER32.DLL", "GetWindowRgn", 2},
                         {"USER32.DLL", "SetCursor", 1},
                         {"USER32.DLL", "GetCursor", 0},
                         {"USER32.DLL", "GetIconInfo", 2},
                         {"USER32.DLL", "ShowCursor", 1},
                         {"USER32.DLL", "ClipCursor", 1},
                         {"USER32.DLL", "GetClipCursor", 1},
                         {"USER32.DLL", "OpenClipboard", 1},
                         {"USER32.DLL", "EmptyClipboard", 0},
                         {"USER32.DLL", "GetClipboardData", 1},
                         {"USER32.DLL", "SetClipboardData", 2},
                         {"USER32.DLL", "CountClipboardFormats", 0},
                         {"USER32.DLL", "EnumClipboardFormats", 1},
                         {"USER32.DLL", "GetPriorityClipboardFormat", 2},
                         {"USER32.DLL", "GetClipboardOwner", 0},
                         {"USER32.DLL", "GetOpenClipboardWindow", 0},
                         {"USER32.DLL", "GetClipboardViewer", 0},
                         {"USER32.DLL", "GetClipboardSequenceNumber", 0},
                         {"USER32.DLL", "SetClipboardViewer", 1},
                         {"USER32.DLL", "ChangeClipboardChain", 2},
                         {"USER32.DLL", "GetClipboardFormatNameA", 3},

                         {"USER32.DLL", "CloseClipboard", 0},
                         {"USER32.DLL", "IsClipboardFormatAvailable", 1},
                         // The exports the export contract still listed as missing on
                         // 23 September 2026 (branch dlls): src/x86_*_completion.cpp.
                         {"USER32.DLL", "IsDialogMessage", 2},
                         {"USER32.DLL", "TranslateAccelerator", 3},
                         {"USER32.DLL", "GetWindowModuleFileNameW", 3},
                         {"USER32.DLL", "InitSharedTable", 0},
                         {"USER32.DLL", "WNDPROC_CALLBACK", 0},
                         {"GDI32.DLL", "FixBrushOrgEx", 4},
                         {"GDI32.DLL", "GetMetaRgn", 2},
                         {"GDI32.DLL", "ByeByeGDI", 1},
                         {"GDI32.DLL", "CreateDIBPatternBrush", 2},
                         {"GDI32.DLL", "CreateDIBPatternBrushPt", 2},
                         {"GDI32.DLL", "CreatePolyPolygonRgn", 4},
                         {"GDI32.DLL", "GetAspectRatioFilterEx", 2},
                         {"GDI32.DLL", "EnumICMProfilesA", 3},
                         {"GDI32.DLL", "UpdateICMRegKeyA", 4},
                         {"WINMM.DLL", "PlaySound", 3},
                         {"WINMM.DLL", "mmioAdvance", 3},
                         {"WINMM.DLL", "mmioFlush", 2},
                         {"WINMM.DLL", "mmioInstallIOProcA", 3},
                         {"WINMM.DLL", "mmioRenameA", 4},
                         {"WINMM.DLL", "mmioSendMessage", 4},
                         {"WINMM.DLL", "mmioSetBuffer", 4},
                         {"WINMM.DLL", "mmioSetInfo", 3},
                         {"WINMM.DLL", "ordinal_1", 2},
                         {"WINMM.DLL", "ordinal_3", 1},
                         {"WINMM.DLL", "ordinal_4", 4},
                         {"WINMM.DLL", "auxGetVolume", 2},
                         {"WINMM.DLL", "auxOutMessage", 4},
                         {"WINMM.DLL", "auxSetVolume", 2},
                         {"WINMM.DLL", "joyConfigChanged", 1},
                         {"WINMM.DLL", "joyGetThreshold", 2},
                         {"WINMM.DLL", "joyReleaseCapture", 1},
                         {"WINMM.DLL", "joySetCapture", 4},
                         {"WINMM.DLL", "joySetThreshold", 2},
                         {"WINMM.DLL", "mciExecute", 1},
                         {"WINMM.DLL", "mciGetCreatorTask", 1},
                         {"WINMM.DLL", "mciGetYieldProc", 2},
                         {"WINMM.DLL", "mciSetYieldProc", 3},
                         {"WINMM.DLL", "midiConnect", 3},
                         {"WINMM.DLL", "midiDisconnect", 3},
                         {"WINMM.DLL", "midiInAddBuffer", 3},
                         {"WINMM.DLL", "midiInGetID", 2},
                         {"WINMM.DLL", "midiInMessage", 4},
                         {"WINMM.DLL", "midiInPrepareHeader", 3},
                         {"WINMM.DLL", "midiInUnprepareHeader", 3},
                         {"WINMM.DLL", "midiOutCacheDrumPatches", 4},
                         {"WINMM.DLL", "midiOutCachePatches", 4},
                         {"WINMM.DLL", "midiOutGetID", 2},
                         {"WINMM.DLL", "waveInGetID", 2},
                         {"ADVAPI32.DLL", "RegLoadKeyA", 3},
                         {"ADVAPI32.DLL", "RegUnLoadKeyA", 2},
                         {"ADVAPI32.DLL", "RegSaveKeyA", 3},
                         {"ADVAPI32.DLL", "RegReplaceKeyA", 4},
                         {"ADVAPI32.DLL", "RegQueryMultipleValuesA", 5},
                         {"ADVAPI32.DLL", "RegRemapPreDefKey", 2},
                         {"USER32.DLL", "SetWinEventHook", 7},
                         {"USER32.DLL", "UnhookWinEvent", 1},
                         {"USER32.DLL", "GetMouseMovePoints", 5},
                         {"COMDLG32.DLL", "GetFileTitleW", 3},
                         // dlls phase 2: the 32-to-16 gates.
                         {"GDI32.DLL", "GetGlyphOutline", 7},
                         {"GDI32.DLL", "GetKerningPairs", 3},
                         {"GDI32.DLL", "CheckColorsInGamut", 4},
                         {"GDI32.DLL", "ColorCorrectPalette", 4},
                         {"GDI32.DLL", "DrawEscape", 4},
                         {"GDI32.DLL", "GdiComment", 3},
                         {"GDI32.DLL", "SetBoundsRect", 3},
                         {"GDI32.DLL", "GetBoundsRect", 3},
                         {"GDI32.DLL", "GetEnhMetaFileA", 1},
                         {"GDI32.DLL", "GetMiterLimit", 2},
                         {"GDI32.DLL", "SetMiterLimit", 3},
                         {"GDI32.DLL", "LineDDA", 6},
                         {"GDI32.DLL", "SetMagicColors", 3},
                         {"GDI32.DLL", "SetMetaRgn", 1},
                         {"USER32.DLL", "AnyPopup", 0},
                         {"USER32.DLL", "CascadeChildWindows", 2},
                         {"USER32.DLL", "TileChildWindows", 2},
                         {"USER32.DLL", "GetKBCodePage", 0},
                         {"USER32.DLL", "GetNextQueueWindow", 2},
                         {"USER32.DLL", "BlockInput", 1},
                         {"USER32.DLL", "SendInput", 3},
                         {"USER32.DLL", "SetSystemCursor", 2},
                         {"USER32.DLL", "IsHungThread", 1},
                         {"USER32.DLL", "PlaySoundEvent", 1},
                         {"USER32.DLL", "HasSystemSleepStarted", 0},
                         {"USER32.DLL", "UserIsSystemResumeAutomatic", 0},
                         {"USER32.DLL", "UserTickleTimer", 1},
                         {"USER32.DLL", "UserSetDeviceHoldState", 2},
                         {"USER32.DLL", "WinOldAppHackoMatic", 1},
                         {"USER32.DLL", "UserSignalProc", 4},
                         {"USER32.DLL", "DragDetect", 3},
                         {"USER32.DLL", "DrawMenuBarTemp", 5},
                         {"USER32.DLL", "SysErrorBox", 5},
                         {"USER32.DLL", "DragObject", 5},
                         {"USER32.DLL", "DdeAddData", 4},
                         {"USER32.DLL", "DdeEnableCallback", 3},
                         // NEO98DX.DLL, the driver below the ARM64 DDRAW.DLL and DSOUND.DLL
                         // (sdk/api/70-neo98dx.api, src/x86_dx_driver.cpp).
                         {"NEO98DX.DLL", "DxDrvVersion", 0},
                         {"NEO98DX.DLL", "DdDrvGetInfo", 2},
                         {"NEO98DX.DLL", "DdDrvGetMode", 3},
                         {"NEO98DX.DLL", "DdDrvSetMode", 3},
                         {"NEO98DX.DLL", "DdDrvRestoreMode", 0},
                         {"NEO98DX.DLL", "DdDrvSetExclusive", 2},
                         {"NEO98DX.DLL", "DdDrvWaitForVerticalBlank", 2},
                         {"NEO98DX.DLL", "DdDrvGetScanLine", 1},
                         {"NEO98DX.DLL", "DdDrvSetGammaRamp", 1},
                         {"NEO98DX.DLL", "DdDrvGetGammaRamp", 1},
                         {"NEO98DX.DLL", "DdDrvCreateSurface", 2},
                         {"NEO98DX.DLL", "DdDrvDestroySurface", 1},
                         {"NEO98DX.DLL", "DdDrvAttach", 3},
                         {"NEO98DX.DLL", "DdDrvLock", 2},
                         {"NEO98DX.DLL", "DdDrvUnlock", 1},
                         {"NEO98DX.DLL", "DdDrvBlt", 2},
                         {"NEO98DX.DLL", "DdDrvFlip", 3},
                         {"NEO98DX.DLL", "DdDrvIsLost", 1},
                         {"NEO98DX.DLL", "DdDrvRestore", 3},
                         {"NEO98DX.DLL", "DdDrvSetPalette", 5},
                         {"NEO98DX.DLL", "DdDrvSetColorKey", 4},
                         {"NEO98DX.DLL", "DdDrvGetDC", 2},
                         {"NEO98DX.DLL", "DdDrvReleaseDC", 1},
                         {"NEO98DX.DLL", "DdDrvGetClipList", 3},
                         {"NEO98DX.DLL", "D3dDrvGetCaps", 2},
                         {"NEO98DX.DLL", "D3dDrvCreateContext", 3},
                         {"NEO98DX.DLL", "D3dDrvDestroyContext", 1},
                         {"NEO98DX.DLL", "D3dDrvRun", 3},
                         {"NEO98DX.DLL", "DsDrvOpen", 0},
                         {"NEO98DX.DLL", "DsDrvGetCaps", 2},
                         {"NEO98DX.DLL", "DsDrvCreateBuffer", 2},
                         {"NEO98DX.DLL", "DsDrvDuplicateBuffer", 2},
                         {"NEO98DX.DLL", "DsDrvDestroyBuffer", 1},
                         {"NEO98DX.DLL", "DsDrvPlay", 2},
                         {"NEO98DX.DLL", "DsDrvStop", 1},
                         {"NEO98DX.DLL", "DsDrvGetPosition", 3},
                         {"NEO98DX.DLL", "DsDrvSetPosition", 2},
                         {"NEO98DX.DLL", "DsDrvGetStatus", 2},
                         {"NEO98DX.DLL", "DsDrvSetNotifications", 3},
                         {"NEO98DX.DLL", "DsDrvSetFormat", 4},
                         {"NEO98DX.DLL", "DsDrvSetFrequency", 2},
                         {"NEO98DX.DLL", "DsDrvSetVolumePan", 3},
                         {"USER32.DLL", "DdeReconnect", 1}};
unsigned Word(const unsigned char *p)
{
    return p[0] | unsigned(p[1]) << 8;
}
void Copy(char *out, const char *in, unsigned capacity)
{
    unsigned n = 0;
    if (in)
        while (n + 1 < capacity && in[n])
        {
            out[n] = in[n];
            ++n;
        }
    if (capacity)
        out[n] = 0;
}
bool Equal(const char *a, const char *b)
{
    while (*a && *b)
    {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return false;
    }
    return *a == *b;
}
void Normalize(char *text)
{
    unsigned out = 0;
    for (unsigned i = 0; text[i]; ++i)
    {
        if (text[i] == '\r' && text[i + 1] == '\n')
            continue;
        text[out++] = text[i];
    }
    text[out] = 0;
}
} // namespace
namespace {unsigned next_process_identity=1,next_thread_identity=1;}
unsigned Application::AllocateThreadIdentity(){return next_thread_identity<65536?next_thread_identity++:0;}
Application::Application(Logger logger) : log(logger)
{
    process_id=next_process_identity++;
    Gdi::SetScreenGuardSink(&Application::ScreenTouched);
}
Application::~Application()
{
    RemoveSessionHooks();
    delete[] host_desktop_mask;
    OpenGLRelease();
    GlideRelease();
    delete[] stub_arguments;
    stub_arguments = nullptr;
    for (char *&slot : strings)
    {
        free(slot);
        slot = nullptr;
    }
    free(menus);
    menus = nullptr;
    menu_count = menu_capacity = 0;
    while (native_modules)
    {
        auto *next = native_modules->next;
        delete native_modules;
        native_modules = next;
    }
    native_module_count = 0;
    FreeToolhelp();
    FreeConsoleState();
    FreeDosVm();
    FreeLzState();
    FreeMultimedia();
    FreeDirectDrawHal();
    FreeDxDriver();
    FreeDxSound();
    for (auto &watch : dci_watches) FreeDciWatch(watch);
    FreeMultimediaTimers();
    FreePipes();
    FreeWsock2();
    OrphanChildren();
    FreeChildHandles();
    FreeChangeNotifications();
    FreeRegistryNotifications();
    FreeWriteWatches();
    FreeKernelOrdinalsB();
    // The named-object tables are session-wide now, so this process has to
    // give its handles back; nothing else drops its references.
    FreeKernelObjectHandles();
    if(user_initialized)User::BindTask(process_id,threads[current_thread].id);
    state = Failed;
    for (unsigned i = 0; i < ThreadCapacity; ++i)
    {
        auto &t = threads[i];
        if (!t.fiber)
            continue;
        current_thread = i;
        if (t.context)
            cpu->Restore(t.context);
        pending = t.pending;
        waiting = false;
        depth = t.depth;
        if (!t.fiber->Finished())
            t.fiber->Resume();
        FreeThreadFiber(t.fiber);
        t.fiber = nullptr;
        // No C++ frame can hold a copy now. ResumeCall interrupted by
        // process termination leaves its captured mark for this owner.
        for (auto &mark : t.call_marks)
        {
            if (mark.context) { DropWin16HostedContext(mark.context); cpu->FreeContext(mark.context); }
            mark = {};
        }
        t.host_frame_count = t.depth = t.pending = 0;
        t.host_waiting = t.host_resume = false;
        if (t.context)
            cpu->FreeContext(t.context);
        t.context = nullptr;
    }
    if (win16_flat_template) { cpu->FreeContext(win16_flat_template); win16_flat_template = nullptr; }
    // Native modal continuations still borrow their PIF/page controllers
    // while the fibers above unwind. Captured callers abandoned by a fault
    // leave their heap controllers for this final reclamation.
    FinishPropertySheet(0);
    DiscardWin16PropertySheets();
    FreePifPages();
    FreePifMgr();
    if(user_initialized){User::BindTask(process_id,threads[0].id);User::ReleaseProcessWindows(process_id);}
    FreeSpecialFolders();
    FreeCabinet();
    FreeShellControl();
    FreeCarets();
    // Every surviving registration of a loaded modal frame can be freed
    // only after all its possible native users above have unwound.
    DiscardSavedMessageBoxes();
    DiscardSavedFileDialogs();
    DiscardSavedWin16FileDialogs();
    DiscardSavedWin16ColorDialogs();
    DiscardSavedWin16FontDialogs();
    DiscardSavedWin16PrintDialogs();
    DiscardSavedWin16HostedLoads();
    // After the fibers above have unwound. A modal dialog is a C++ frame in
    // RunModal, and unwinding it resolves its record once more.
    dialogs.Release();
    FreeClipboard();
    FreeShellProcessReference();
    FreeShellOs();
    FreeShellStopwatch();
    FreeSentMessages();
    FreeWinEvents();
    FreePrintSpool();
    FreePrintHost();
    FreeShellThreads();
    FreeThreadCrt();
    for (unsigned i = 1; i < ModuleCapacity; ++i) DeleteModule(i);
    FreeCommon();
    FreeProperties();
    FreeKernelPaths();
    FreeAtoms();
    FreeDde();
    FreeLocale();
    FreeArguments();
    FreeProcessEnvironment();
    FreeNls();
    FreeSerialUiPages();
    FreeHeaps();
    FreeCommonDialog();
    FreeCritical();
    FreeMappings();
    // Every block of the session arena this process still holds. The host
    // bytes stay alive as long as another process holds them; see
    //
    FreeSessionBlocks();
    FreeImageResources();
    FreeIcons();
    for(auto &list:image_lists)for(auto *item:list.items)delete item;
    if (user_initialized)
    {
        HandOffPointer(); // the pointer's shape must not outlive this table
        User::DetachProcess(process_id);
    }
    FreeGdi();
    // What FreeGdi left alive belongs to another task now; a DIB section
    // among it must not keep its bits in this process.
    Gdi::DetachDibLeases(this);
    delete cpu;
#if defined(__APPLE__) || defined(__linux__)
    free(system_vm_storage);
#else
    delete[] system_vm_storage;
#endif
    delete vga;
    // The DIB sections of the session lose this process; their bits stay
    // with the objects (src/x86_gdi.cpp).
    DropSessionDibs(true, false);
    free(owned_executable); free(owned_font); free(initial_environment);
    while (allocation_pages)
    {
        auto *next = allocation_pages->next;
        delete allocation_pages;
        allocation_pages = next;
    }
}
void Application::Fail(const char *message)
{
    if (state != Failed)
    {
        const State before = state;
        Copy(error, message, sizeof error);
        state = Failed;
        if (log)
        {
            log(error);
            unsigned blocks = 0, bytes = 0;
            for (const auto &b : AllocationBlocks())
                if (b.used)
                {
                    ++blocks;
                    bytes += b.size;
                }
            char detail[256];
            snprintf(detail, sizeof detail,
                     "state thread=%u calls=%u heap_blocks=%u heap_bytes=%u heap_high=%u windows=%u gdi=%u",
                     ThreadId(), api_calls, blocks, bytes, next_alloc - heap_base, User::LiveWindows(),
                     Gdi::LiveObjects());
            log(detail);
            if (HasProcessor())
            {
                unsigned pc = 0, sp = 0, ax = 0, cx = 0, dx = 0;
                cpu->Get(Reg::Eip, pc);
                cpu->Get(Reg::Esp, sp);
                cpu->Get(Reg::Eax, ax);
                cpu->Get(Reg::Ecx, cx);
                cpu->Get(Reg::Edx, dx);
                unsigned cs = 0, ss = 0, ds = 0;
                cpu->Get(Reg::Cs, cs);
                cpu->Get(Reg::Ss, ss);
                cpu->Get(Reg::Ds, ds);
                snprintf(detail, sizeof detail,
                         "guest eip=%08x esp=%08x eax=%08x ecx=%08x edx=%08x cs=%04x ss=%04x ds=%04x", pc, sp, ax,
                         cx, dx, cs, ss, ds);
                log(detail);
                unsigned long long pc64 = 0, sp64 = 0, lr64 = 0;
                unsigned syndrome = 0;
                if (Arm64Process() && Arm64::FaultState(*cpu, pc64, sp64, lr64, syndrome))
                {
                    snprintf(detail, sizeof detail, "arm64 pc=%016llx sp=%016llx lr=%016llx esr=%08x", pc64, sp64,
                             lr64, syndrome);
                    log(detail);
                }
                unsigned stack[6] = {};
                if (cpu->Read(sp, stack, sizeof stack) == CpuError::Ok)
                {
                    snprintf(detail, sizeof detail, "guest stack=%08x %08x %08x %08x %08x %08x",
                             stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
                    log(detail);
                }
            }
        }
        // The report writer runs after the state is final, so it sees the
        // stopped guest. Nothing is written when no host installed a hook,
        // and nothing for a start that only missed a file.
        if (!load_failure)
            NotifyCrash(error);
        // Then the person sees the error box of the system, as after a fault
        // of the program (src/x86_fault_box.inc).
        FailBox(before);
    }
}
// The engine reports the offending address only through this hook. Returning
// false leaves the access unresolved, so emulation still stops with the error
// code Check turns into the failure message.
bool Application::MemoryFaultHook(Cpu &, unsigned type, unsigned address, unsigned size, void *context)
{
    auto &app = *static_cast<Application *>(context);
    // A record of a critical section another process created. Unicorn retries
    // the access once the chunk is mapped (src/x86_critical.cpp).
    if ((type == CpuFault::ReadUnmapped || type == CpuFault::WriteUnmapped) && app.MapCriticalChunk(address))
        return true;
    // The bits of a DIB section another process created (src/x86_gdi.cpp).
    if ((type == CpuFault::ReadUnmapped || type == CpuFault::WriteUnmapped) && app.MapSessionDib(address))
        return true;
    app.memory_fault = {address, size, type, true};
    return false;
}
unsigned Application::DescribeMemoryFault(char *out, unsigned capacity)
{
    if (!out || !capacity || !memory_fault.valid)
        return 0;
    const char *kind = "access";
    switch (memory_fault.type)
    {
    case CpuFault::ReadUnmapped: kind = "read from unmapped"; break;
    case CpuFault::WriteUnmapped: kind = "write to unmapped"; break;
    case CpuFault::FetchUnmapped: kind = "fetch from unmapped"; break;
    case CpuFault::ReadProtected: kind = "read from unreadable"; break;
    case CpuFault::WriteProtected: kind = "write to read-only"; break;
    case CpuFault::FetchProtected: kind = "fetch from non-executable"; break;
    default: break;
    }
    VirtualMemory::Info info = {};
    char where[96];
    if (virtual_memory.Query(memory_fault.address, info))
        snprintf(where, sizeof where, "state=%x protection=%x allocation=%08x+%x", info.state,
                 info.protection, info.allocation, info.size);
    else
        Copy(where, "outside the tracked address space", sizeof where);
    char module[200] = {};
    for (unsigned i = 0; i < module_watermark; ++i)
    {
        const auto &m = modules[i];
        if (!m.pe || !m.handle || memory_fault.address < m.pe->Base() ||
            memory_fault.address - m.pe->Base() >= m.pe->SizeOfImage())
            continue;
        const char *leaf = m.path;
        for (const char *p = m.path; *p; ++p)
            if (*p == '\\' || *p == '/')
                leaf = p + 1;
        snprintf(module, sizeof module, " in %s+0x%x", leaf, memory_fault.address - m.pe->Base());
        break;
    }
    return (unsigned)snprintf(out, capacity, " %s %08x (%u bytes)%s, %s", kind, memory_fault.address,
                              memory_fault.size, module, where);
}
// A range that holds code from the start: a Win16 code segment, a thunk this
// port assembled, a VirtualAlloc the guest asked for with PAGE_EXECUTE. Those
// pages skip the detour through GrantExecute. Each page keeps the read and
// write rights it has; a page the guest may not read stays as it is.
void Application::MarkGuestCode(unsigned base, unsigned size)
{
    if (!size || !execute_on_demand) return;
    const unsigned long long end = (unsigned long long)base + size + 4095;
    const auto stop = (end < 0x100000000ull ? end : 0x100000000ull) & ~4095ull;
    auto permissions_at = [&](unsigned page) -> unsigned {
        if (execute_pages.Test(page) || !virtual_memory.Access(page, 1, false)) return 0;
        return Prot::Read | Prot::Exec | (virtual_memory.Access(page, 1, true) ? Prot::Write : 0);
    };
    // One protection call per contiguous run of equal rights. A call per
    // page splits Unicorn's mapping thousands of times for large allocations.
    for (unsigned long long page = base & ~4095u; page < stop;)
    {
        const unsigned permissions = permissions_at((unsigned)page);
        if (!permissions) { page += 4096; continue; }
        const auto first = page;
        do { page += 4096; }
        while (page < stop && page - first < 0xfffff000ull && permissions_at((unsigned)page) == permissions);
        const unsigned length = (unsigned)(page - first);
        if (cpu->Protect((unsigned)first, length, permissions) == CpuError::Ok)
            execute_pages.Add((unsigned)first, length);
    }
}
// Unicorn refused a fetch from a page that carries no code right. That is not
// a fault of the guest: on the i386 of Win98 every page a process can read it
// can also execute. The port withholds the right only to keep the
// engine's write path fast, so here it hands the right over and lets the run
// go on. A page the guest may not read keeps its access violation.
bool Application::GrantExecute()
{
    if (!memory_fault.valid || memory_fault.type != CpuFault::FetchProtected)
        return false;
    const unsigned page = memory_fault.address & ~4095u;
    // Granting twice would mean the fault has another reason; stop then, so a
    // run ends with a report instead of spinning.
    if (!execute_on_demand || execute_pages.Test(page) || !virtual_memory.Access(page, 1, false))
        return false;
    unsigned permissions = Prot::Read | Prot::Exec;
    if (virtual_memory.Access(page, 1, true))
        permissions |= Prot::Write;
    if (cpu->Protect(page, 4096, permissions) != CpuError::Ok)
        return false;
    execute_pages.Add(page, 4096);
    execute_pages.CountGrant();
    if (log)
    {
        char text[96];
        snprintf(text, sizeof text, "EXECUTE granted page=%08x permissions=%u grants=%u", page,
                 permissions, execute_pages.Grants());
        log(text);
    }
    memory_fault = {};
    return true;
}
// The guest called the window procedure of a system class as a function.
// GetWindowLong(GWL_WNDPROC), GetClassLong(GCL_WNDPROC) and GetClassInfo of
// Win98 answer an address a program may call: the system classes live in
// USER.EXE and USER32 hands out a 32-bit thunk to the 16-bit procedure, so
// `call [old]` in a subclass procedure works as well as CallWindowProc does
// (the setup of Plus! 98 forwards every message of its wizard that way). Here such a procedure is a token in the port's own reservation and
// no code stands there. The fetch from it becomes the call the program meant:
// CallWindowProcA(token, hwnd, message, wParam, lParam) with the return
// address of the program, through the gate of that function.
bool Application::EnterNativeProcedure()
{
    if (!memory_fault.valid ||
        (memory_fault.type != CpuFault::FetchUnmapped && memory_fault.type != CpuFault::FetchProtected))
        return false;
    const unsigned token = Register(Reg::Eip);
    if (token != memory_fault.address || token <= NativeProcedureBase || token >= NativeProcedureEnd || Win16Code())
        return false;
    unsigned gate = 0;
    if (!NativeProcedure("USER32.DLL", "CallWindowProcA", 0, &gate, false) || !gate)
        return false;
    const unsigned esp = Register(Reg::Esp);
    unsigned return_address = 0;
    if (!virtual_memory.Access(esp - 4, 24, true) || !Read(esp, &return_address, 4))
        return false;
    Write32(esp - 4, return_address);
    Write32(esp, token);
    Register(Reg::Esp, esp - 4);
    Register(Reg::Eip, gate);
    memory_fault = {};
    return true;
}
bool Application::CheckFailed(unsigned code)
{
    // Creating an unavailable engine leaves no CPU. Diagnostics must not
    // ask Register to diagnose the missing register handle recursively.
    if (!cpu)
    {
        Fail(CpuErrorText(code));
        return false;
    }
    {
        // A fault inside a connected flat thunk block is the 32-to-16
        // transition we cannot make; say so instead of reporting raw memory.
        char message[256];
        if (FlatThunkFault(message, sizeof message))
        {
            Fail(message);
            return false;
        }
        Copy(message, CpuErrorText(code), sizeof message);
        const unsigned used = (unsigned)strlen(message);
        if (used + 1 < sizeof message)
        {
            if (Win16Process())
                Win16DescribeAddress(message + used, (unsigned)sizeof message - used,
                                     Register(Reg::Cs), Register(Reg::Eip));
            else
                DescribeMemoryFault(message + used, (unsigned)sizeof message - used);
        }
        Fail(message);
        return false;
    }
}
// The i386 of the Win9x era has no execute-disable bit, so every page a
// process can read it can also execute: heap, stack, image data sections and
// the descriptor page alike. Programs rely on it, for instance SHDOC401.DLL,
// which builds a window procedure in the process heap.
// Write protection stays write protection; PAGE_NOACCESS stays unreadable.
unsigned Application::GuestProtection(unsigned p) { return p & Prot::Read ? p | Prot::Exec : p; }
// Unicorn does not have to know what the guest knows. It keeps TLB_NOTDIRTY on
// every page it may execute from and sends each write to such a page through
// its slow store path, so a data page that is executable for nobody's benefit
// costs the guest real time. The engine therefore gets
// the execute right only where code lies: a caller that places code asks for
// Prot::Exec itself, and a page the guest jumps into later gets it from
// GrantExecute. What the guest can read and write does not change, and neither
// does what VirtualQuery answers, because that reads the record of
// virtual_memory, which still carries GuestProtection.
bool Application::execute_on_demand = true;
unsigned Application::HostProtection(unsigned base, unsigned size, unsigned permissions)
{
    permissions &= (unsigned)Prot::All;
    if (!execute_on_demand)
        return GuestProtection(permissions);
    // PAGE_NOACCESS stays unreadable, and an unreadable page is not executable.
    if (!(permissions & Prot::Read))
        return permissions & ~(unsigned)Prot::Exec;
    if (permissions & Prot::Exec)
        execute_pages.Add(base, size);
    return permissions;
}
void Application::KeepExecutable(unsigned base, unsigned size, unsigned permissions)
{
    if (!(permissions & Prot::Read) || (permissions & Prot::Exec) || !execute_pages.Count())
        return;
    const unsigned granted = permissions | Prot::Exec;
    unsigned page = execute_pages.Next(base, size, 0);
    while (page != ExecutePages::Pages)
    {
        const unsigned first = page;
        unsigned next;
        do {
            next = execute_pages.Next(base, size, page + 1);
            if (next != page + 1 || next == ExecutePages::Pages || page - first >= 0xffffe) break;
            page = next;
        } while (true);
        cpu->Protect(first << ExecutePages::PageBits, (page - first + 1) << ExecutePages::PageBits, granted);
        page = next;
    }
}
bool Application::Map(unsigned a, unsigned n, unsigned p)
{
    const unsigned guest = GuestProtection(p);
    if (!virtual_memory.Track(a, n, VirtualMemory::Space::Protection(guest)))
    { Fail("Cannot register native address range"); return false; }
    const auto result = cpu->Map(a, n, HostProtection(a, n, p));
    if (result) virtual_memory.Forget(a, n);
    else KeepExecutable(a, n, p);
    return Check(result);
}
bool Application::Store(unsigned a, const void *p, Pe32::Size n)
{
    // A write inside one page the engine lets the host write in place
    // (Cpu::page_write). Relaxed PE pages still go through the original
    // guest-rights check for native API outputs, on both CPU engines.
    if (const unsigned char *writable = cpu->page_write)
        if (n && n <= 4096 - (a & 4095) && writable[a >> 12] && !write_watch_count &&
            !cpu->ImageDataReadOnly(a))
        {
            memcpy(cpu->memory + a, p, n);
            return true;
        }
    if (n > ~0u || (!loader_writes && !virtual_memory.Access(a, n, true)))
    {
        VirtualMemory::Info info = {};
        char detail[220];
        if (virtual_memory.Query(a & ~4095u, info))
            snprintf(detail, sizeof detail,
                     "Guest write violates page protection at %08x (%u bytes), region %08x+%x state=%x "
                     "protection=%x type=%x in %s!%s",
                     a, (unsigned)n, info.base, info.size, info.state, info.protection, info.type,
                     pending >= Gates && (pending - Gates) / 16 < import_count
                         ? imports[(pending - Gates) / 16].dll : "?",
                     pending >= Gates && (pending - Gates) / 16 < import_count
                         ? imports[(pending - Gates) / 16].name : "?");
        else
            snprintf(detail, sizeof detail,
                     "Guest write violates page protection at %08x (%u bytes), no region", a, (unsigned)n);
        Fail(detail);
        return false;
    }
    unsigned code = cpu->Write(a, p, n);
    if (code)
    {
        char detail[160];
        snprintf(detail, sizeof detail, "Guest write failed address=%08x bytes=%u: %s", a, (unsigned)n,
                 CpuErrorText(code));
        Fail(detail);
        return false;
    }
    if (write_watch_count) MarkWriteWatch(a, (unsigned)n);
    return true;
}
bool Application::Protect(unsigned a, unsigned n, unsigned p)
{
    const unsigned guest = GuestProtection(p);
    if (!Check(cpu->Protect(a, n, HostProtection(a, n, p)))) return false;
    KeepExecutable(a, n, p);
    if (!virtual_memory.SetProtection(a, n, VirtualMemory::Space::Protection(guest)))
    { Fail("Cannot update native page metadata"); return false; }
    return true;
}
bool Application::ReserveImage(unsigned address, unsigned size)
{
    // Unlike VirtualProtect(PAGE_NOACCESS), uncovered PE pages are reserved.
    // The loader commits headers/sections with Protect after this reset.
    return Protect(address, size, 0) && virtual_memory.SetProtection(address, size, 0);
}
unsigned Application::ImageDataPageSize() const
{
    return execute_on_demand && cpu ? cpu->ImageDataPageSize() : 0;
}
bool Application::RelaxImageData(unsigned address, unsigned read_only)
{
    // Reused addresses may have run code before this image was loaded.
    if (execute_pages.Next(address, 16384, 0) != ExecutePages::Pages || write_watch_count)
        return true;
    return Check(cpu->RelaxImageData(address, read_only));
}
bool Application::ReadChecked(unsigned a, void *p, unsigned n)
{
    if (!virtual_memory.Access(a, n, false))
    {
        char detail[160];
        snprintf(detail, sizeof detail, "Guest read violates page protection at %08x (%u bytes)", a, n);
        Fail(detail);
        return false;
    }
    return Check(cpu->Read(a, p, n));
}
// The registers the engine does not hand out in place: EFLAGS, the
// selectors, the x87 words, and every register of an engine without the
// direct access. Writing EIP always comes here, because the engine has to
// leave its current block for it (src/x86_cpu.h).
// A process without a processor (the DOS process) has no
// registers; asking for one is a fault of the port and says so.
unsigned Application::EngineRegister(int r)
{
    unsigned v = 0;
    if (cpu && !HasProcessor()) { Fail("Register access in a process without a processor"); return 0; }
    Check(cpu ? cpu->Get(r, v) : (unsigned)CpuError::Handle);
    return v;
}
void Application::EngineRegister(int r, unsigned v)
{
    if (cpu && !HasProcessor()) { Fail("Register access in a process without a processor"); return; }
    Check(cpu ? cpu->Set(r, v) : (unsigned)CpuError::Handle);
}
unsigned Application::AllocationSize(unsigned address, bool local)
{
    for (auto &b : AllocationBlocks())
        if (b.address == address && b.used)
            return local && b.local_size ? b.local_size : b.size;
    return 0;
}
bool Application::Free(unsigned address)
{
    if (!address)
        return false;
    for (auto &b : AllocationBlocks())
        if (b.used && b.address == address)
        {
            ClipboardAllocationFreed(address);
            b.used = false;
            b.moveable = false;
            b.data = b.locks = b.flags = b.local_size = 0;
            for (;;)
            {
                bool joined = false;
                for (auto &other : AllocationBlocks())
                    if (&other != &b && other.address && !other.used)
                    {
                        if (b.address + b.size == other.address)
                        {
                            b.size += other.size;
                            other = {};
                            joined = true;
                        }
                        else if (other.address + other.size == b.address)
                        {
                            b.address = other.address;
                            b.size += other.size;
                            other = {};
                            joined = true;
                        }
                    }
                if (!joined)
                    break;
            }
            return true;
        }
    return false;
}
unsigned Application::Allocate(unsigned size)
{
    if (size > HeapArenaLimit - 15)
    {
        if (log)
            log("Heap request exceeds arena");
        last_error = 8;
        return 0;
    }
    size = (size + 15) & ~15u;
    if (size < 16)
        size = 16;
    Block *empty = nullptr;
    for (auto &b : AllocationBlocks())
        if (!b.address)
        {
            empty = &b;
            break;
        }
    for (auto &b : AllocationBlocks())
        if (b.address && !b.used && b.size >= size)
        {
            if (b.size > size && empty)
            {
                *empty = {b.address + size, b.size - size, false};
                b.size = size;
            }
            b.used = true;
            return b.address;
        }
    auto acquire_slot = [&]() -> Block *
    {
        for (auto &b : AllocationBlocks()) if (!b.address) return &b;
        auto *page = new (std::nothrow) AllocationPage;
        if (page)
        {
            page->next = allocation_pages;
            allocation_pages = page;
            return page->blocks;
        }
        return nullptr;
    };
    if (!empty) empty = acquire_slot();
    if (empty && size > heap_end - next_alloc)
    {
        GrowHeap(size);
        // Moving to a new segment records the old tail as a free block,
        // possibly in our slot. Keep that record instead of overwriting it.
        if (empty->address) empty = acquire_slot();
    }
    if (!empty || size > heap_end - next_alloc)
    {
        if (log)
            log(!empty ? "Heap metadata exhausted" : "Heap arena exhausted");
        last_error = 8;
        return 0;
    }
    unsigned address = next_alloc;
    next_alloc += size;
    *empty = {address, size, true};
    return address;
}
void Application::HeapFree(unsigned &total, unsigned &largest) const
{
    const unsigned growth = heap_total < HeapArenaLimit ? HeapArenaLimit - heap_total : 0;
    const unsigned tail = heap_end - next_alloc;
    total = tail + growth;
    largest = tail + growth;
    for (const auto &b : AllocationBlocks())
        if (b.address && !b.used)
        {
            total += b.size;
            if (b.size > largest)
                largest = b.size;
        }
}
unsigned Application::String(const char *text)
{
    unsigned n = strlen(text) + 1, p = Allocate(n);
    if (p)
        Store(p, text, n);
    return p;
}
const char *Application::ReadString(unsigned address)
{
    char *out = StringSlot();
    if (!out)
        return "";
    out[0] = 0;
    if (!address)
        return out;
    for (unsigned i = 0; i + 1 < StringCapacity; ++i)
    {
        out[i] = 0;
        if (address > ~0u - i)
        {
            Fail("Guest string address overflow");
            return out;
        }
        if (!Read(address + i, out + i, 1))
            return out;
        if (!out[i])
            return out;
    }
    out[StringCapacity - 1] = 0;
    Fail("Guest string exceeds the staging slot");
    return out;
}
// One of the sixteen staging slots, made on first use.
char *Application::StringSlot()
{
    const unsigned index = next_string++ % 16;
    if (!strings[index])
    {
        // Zeroed: a saved state carries the buffer, and what the last owner
        // of that memory left in it is not the machine's.
        strings[index] = static_cast<char *>(calloc(1, StringCapacity));
        if (!strings[index])
        {
            Fail("Guest string slot could not be allocated");
            return nullptr;
        }
        strings[index][0] = 0;
    }
    return strings[index];
}
unsigned Application::PutString(unsigned address, const char *text, unsigned capacity)
{
    if (!capacity)
        return 0;
    unsigned n = strlen(text);
    if (n >= capacity)
        n = capacity - 1;
    if (n > ~address)
    {
        Fail("Guest output string address overflow");
        return 0;
    }
    if (n)
        Store(address, text, n);
    unsigned char zero = 0;
    Store(address + n, &zero, 1);
    return n;
}
bool Application::AddNativeModule(const char *name)
{
    if (!name || !*name)
        return false;
    auto *entry = new NativeModuleName;
    if (!entry)
        return false;
    Copy(entry->name, name, sizeof entry->name);
    entry->next = native_modules;
    native_modules = entry;
    ++native_module_count;
    return true;
}
bool Application::NativeModule(const char *dll) const
{
    // The route of one DLL. The table is src/dll_routes.h; PLAN.md section 0
    // states the rule. A DLL the table does not name runs as the original as
    // soon as the tree holds it, so nothing here needs an entry per program.
    // Per process, AddNativeModule moves a single DLL back to its native
    // provider (probe and host flag --native-module), so the tests of our
    // implementations still reach them. That override is not limited.
    const char *leaf = dll;
    for (const char *p = dll; p && *p; ++p)
        if (*p == '\\' || *p == '/')
            leaf = p + 1;
    bool forced = false;
    for (auto *entry = native_modules; entry; entry = entry->next)
        if (Equal(dll, entry->name) || Equal(leaf, entry->name))
            forced = true;
    if (!forced && !DllRoutes::Native(leaf))
        return false;
    // A route only holds where a native provider actually exists: specs is
    // the list of names our gates answer to. The OpenGL driver of the display
    // driver has its own list (src/x86_opengl.cpp).
    if (Equal(leaf, OpenGLIcdModule())) return true;
    // GLIDE2X.DLL exists only when the rasterizer draws Glide; otherwise the
    // machine has no 3dfx driver, and the name finds no file.
    if (Equal(leaf, GlideModule())) return GlideAvailable();
    if (Equal(leaf, GlidePageModule())) return GlideAvailable();
    // The driver of the ARM64 DDRAW.DLL and DSOUND.DLL exists for ARM64
    // processes only: it reaches the bytes of the program in place.
    if (Equal(leaf, DxDriverModule())) return Arm64Process();
    for (const auto &spec : specs) if (Equal(leaf, spec.dll)) return true;
    return false;
}
// The first bound slot plus one; session holes are deliberately not indexed.
unsigned Application::FindImport(const char *dll, const char *name)
{
    return imports.Find(dll, name, import_count);
}
bool Application::NativeProcedure(const char *dll, const char *name, unsigned ordinal,
                                  unsigned *address, bool allow_missing)
{
    if (!name && Equal(dll, "NEO98TOUCH.DLL") && ordinal >= 1 && ordinal <= 4) {
        static const char *const names[] = {"Neo98TouchGetCaps", "Neo98TouchRegister", "Neo98TouchUnregister", "Neo98TouchRead"};
        name = names[ordinal - 1];
    }
    if(!name&&Equal(dll,"MSVCRT.DLL")&&(ordinal==573||ordinal==711))name=ordinal==573?"atof":"strtod";
    if(!name&&Equal(dll,"MSVCRT.DLL"))for(const auto&e:Win98CrtOutput::Exports)if(e.ordinal==ordinal){name=e.name;break;}
    if(!name&&Equal(dll,"GDI32.DLL")&&(ordinal==245||ordinal==283))name=ordinal==245?"GetCurrentObject":"GetObjectType";
    if(!name&&Equal(dll,"GDI32.DLL"))for(const auto&e:Win98GdiTextLayout::Exports)if(e.ordinal==ordinal){name=e.name;break;}
    if(!name&&Equal(dll,"USER32.DLL")&&ordinal>=429&&ordinal<=434){static const char*const boxes[]={"MessageBoxA","MessageBoxExA","MessageBoxExW","MessageBoxIndirectA","MessageBoxIndirectW","MessageBoxW"};name=boxes[ordinal-429];}
    if(!name&&Equal(dll,"COMDLG32.DLL")&&(ordinal==110||ordinal==112))name=ordinal==110?"GetOpenFileNameA":"GetSaveFileNameA";
    if(!name&&Equal(dll,"COMDLG32.DLL")&&(ordinal==105||ordinal==117))name=ordinal==105?"CommDlgExtendedError":"PrintDlgA";
    auto &a = *this;
    if (!name && Equal(dll, GlideModule())) name = GlideExportName(ordinal);
    if (!name)
        for (const auto &alias : NativeExportAliases)
            if (alias.ordinal == ordinal && Equal(alias.dll, dll)) { name = alias.name; break; }
    // Every remaining named export of a natively provided DLL, so a lookup by
    // ordinal finds the same entry as a lookup by name.
    if (!name)
        for (const auto &alias : ExportContractAliases)
            if (alias.ordinal == ordinal && alias.name && Equal(alias.dll, dll)) { name = alias.name; break; }
    if(!name&&Equal(dll,"USER32.DLL"))for(const auto&e:Win98Coordinates::Exports)if(e.ordinal==ordinal){name=e.name;break;}
    if(!name&&Equal(dll,"USER32.DLL"))for(const auto&e:Win98Monitors::Exports)if(e.ordinal==ordinal){name=e.name;break;}
    if(!name&&Equal(dll,"USER32.DLL"))
        for(const auto&entry:OriginalWindowTextAnsiExports)if(entry.ordinal==ordinal){name=entry.name;break;}
    if(Equal(dll,"SHLWAPI.DLL") && !name && ordinal==592)name="SHQueryValueExW";
    if(!name&&Equal(dll,"SHLWAPI.DLL"))
        for(const auto&entry:OriginalShellSearchExports)if(entry.ordinal==ordinal){name=entry.name;break;}
    if(!name&&Equal(dll,"SHLWAPI.DLL"))
        for(const auto&entry:OriginalShellPathSyntaxExports)if(entry.ordinal==ordinal){name=entry.name;break;}
    if(!name&&Equal(dll,"SHLWAPI.DLL")){if(ordinal==566)name="SHCreateThread";else if(ordinal==581)name="SHGetThreadRef";else if(ordinal==619)name="SHSetThreadRef";}
    if(!name&&Equal(dll,"SHELL32.DLL")&&ordinal>=281&&ordinal<=283){const char*names[]={"SHGetFileInfo","SHGetFileInfoA","SHGetFileInfoW"};name=names[ordinal-281];}
    if(!name&&Equal(dll,"SHELL32.DLL")&&ordinal==284)name="SHGetInstanceExplorer";
    if (name && strlen(name) >= 128) return false;
    char requested[128];
    Copy(requested, name, sizeof requested);
    if (!name) snprintf(requested, sizeof requested, "ordinal_%u", ordinal);
    if (const unsigned found = FindImport(dll, requested))
        {
            const unsigned i = found - 1;
            if (!allow_missing && imports[i].argc == 0xffffffff)
            {
                // Diagnostic stub mode also answers dynamic lookups so the
                // original delay loader keeps going. Recorded, never ported.
                if (!stub_mode) return false;
                imports[i].argc = 0xfffffffeu;
            }
            // A record the export directory minted becomes a real import the
            // moment the program itself asks for the name.
            if (!building_module_header)
                imports[i].header_only = false;
            *address = TrivialProcedure(imports[i], Gates + i * 16);
            return true;
        }
    if (Equal(dll, "MSVCRT.DLL") && name)
    {
        if (const auto data = a.CrtTimeExport(name))
        {
            *address = data;
            return true;
        }
        if (const auto data = a.LocaleExport(name))
        {
            *address = data;
            return true;
        }
        if (const auto argument_data = a.ArgumentsExport(name))
        {
            *address = argument_data;
            return true;
        }
        if (!strcmp(name, "_adjust_fdiv"))
        {
            *address = a.crt_data + 16;
            return true;
        }
    }
    // A data export of a native provider is not a function and gets no gate.
    // Win98 answers such a name with the address of the block itself, so this
    // port answers with the zeroed block in the module's header page.
    if (const unsigned data = a.ContractDataAddress(dll, name, name ? 0 : ordinal))
    {
        *address = data;
        return true;
    }
    // `requested` and not `name`: an import by ordinal carries the label
    // "ordinal_N", and the session directory has to see the same label that
    // the lookup above searches for.
    const unsigned gate = a.NewImport(dll, requested, 0xffffffff);
    if (!gate)
    {
        a.Fail("Too many imports");
        return false;
    }
    // The session gives the slot, so the record is not always the last one.
    auto &item = a.imports[(gate - Gates) / 16];
    item.header_only = a.building_module_header != 0;
    for (const auto &spec : specs)
        if (Equal(spec.dll, dll) && !strcmp(spec.name, item.name))
        {
            item.argc = spec.argc;
            break;
        }
    if (item.argc == 0xffffffff && Equal(dll, OpenGLIcdModule()))
        item.argc = OpenGLArgumentCount(item.name);
    if (item.argc == 0xffffffff && Equal(dll, GlideModule()))
        item.argc = GlideArgumentCount(item.name);
    if (item.argc == 0xffffffff && Equal(dll, GlidePageModule()))
        item.argc = GlidePageArgumentCount(item.name);
    if (item.argc == 0xffffffff && Equal(dll, "ADVAPI32.DLL"))
        for (const auto &entry : OriginalSecurityUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "ADVAPI32.DLL"))
        for (const auto &entry : OriginalAdvapiUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalCaptionUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal))
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
    if (item.argc == 0xffffffff && Equal(dll, "COMDLG32.DLL"))
        for (const auto &entry : OriginalPrintUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal))
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
    if (item.argc == 0xffffffff && Equal(dll, "KERNEL32.DLL"))
        for (const auto &entry : OriginalKernelUnsupportedExports)
            if ((name && entry.name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal))
            {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalMessageBoxUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalUserUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal))
            {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalImageUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalMenuUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalClassUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalUser32WideUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal))
            {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalCharacterUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "SHELL32.DLL"))
        for (const auto &entry : OriginalShellIconUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal))
            {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);
                break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "GDI32.DLL"))
        for (const auto &entry : OriginalDcUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "GDI32.DLL"))
        for (const auto &entry : OriginalArcUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);break;
            }
    if (item.argc == 0xffffffff && Equal(dll, "USER32.DLL"))
        for (const auto &entry : OriginalKeyboardLayoutUnsupportedExports)
            if ((name && !strcmp(name, entry.name)) || (!name && ordinal == entry.ordinal)) {
                item.argc = 0x20000000u | (entry.encoding << 16) | (entry.encoding & 15);break;
            }
    if(item.argc==0xffffffff && Equal(dll,"GDI32.DLL"))
        for(const auto &entry:OriginalFontUnsupportedExports)
            if((name&&!strcmp(name,entry.name))||(!name&&ordinal==entry.ordinal)){
                item.argc=0x20000000u|(entry.encoding<<16)|(entry.encoding&15);break;
            }
    if(item.argc==0xffffffff && Equal(dll,"GDI32.DLL"))
        for(const auto &entry:OriginalFontWidthUnsupportedExports)
            if((name&&!strcmp(name,entry.name))||(!name&&ordinal==entry.ordinal)){
                item.argc=0x20000000u|(entry.encoding<<16)|(entry.encoding&15);break;
            }
    if(item.argc==0xffffffff && Equal(dll,"USER32.DLL"))
        for(const auto &entry:OriginalWindowTextUnsupportedExports)
            if((name&&!strcmp(name,entry.name))||(!name&&ordinal==entry.ordinal)){
                item.argc=0x20000000u|(entry.encoding<<16)|(entry.encoding&15);break;
            }
    if(item.argc==0xffffffff && Equal(dll,"GDI32.DLL"))
        for(const auto &entry:OriginalGdiMissingUnsupportedExports)
            if((name&&!strcmp(name,entry.name))||(!name&&ordinal==entry.ordinal)){
                item.argc=0x20000000u|(entry.encoding<<16)|(entry.encoding&15);break;
            }
    if(item.argc==0xffffffff && Equal(dll,"WINMM.DLL"))
        for(const auto &entry:OriginalWinmmUnsupportedExports)
            if((name&&!strcmp(name,entry.name))||(!name&&ordinal==entry.ordinal)){
                item.argc=0x20000000u|(entry.encoding<<16)|(entry.encoding&15);break;
            }
    if(item.argc==0xffffffff && Equal(dll,"GDI32.DLL"))
        for(const auto &entry:OriginalFontCreateUnsupportedExports)
            if((name&&!strcmp(name,entry.name))||(!name&&ordinal==entry.ordinal)){
                item.argc=0x20000000u|(entry.encoding<<16)|(entry.encoding&15);break;
            }
    // The export contract of the natively provided DLLs, from the export tables
    // of the pinned originals (tools/export_contract.py).
    // It answers for every export the hand written tables above do not name:
    // an export Win98 itself leaves unimplemented keeps the original contract,
    // and a real function this port lacks gets an address whose first call
    // stops the run with a diagnosis instead of a silent success.
    if (item.argc == 0xffffffff)
        for (const auto &entry : ExportContractOriginalErrors)
            if (Equal(entry.dll, dll) && ((name && entry.name && !strcmp(name, entry.name)) ||
                                          (!name && ordinal == entry.ordinal)))
            {
                item.argc = entry.argc;
                break;
            }
    if (item.argc == 0xffffffff)
        for (const auto &entry : ExportContractMissing)
            if (Equal(entry.dll, dll) && ((name && entry.name && !strcmp(name, entry.name)) ||
                                          (!name && ordinal == entry.ordinal)))
            {
                item.argc = entry.argc;
                break;
            }
    if(Equal(dll,"COMCTL32.DLL")&&!name&&ordinal>=320&&ordinal<=340){
        static const unsigned counts[]={2,1,3,2,3,3,2,1,1,1,2,2,2,2,3,3,2,1,3,6,2};
        item.argc=counts[ordinal-320];
    }
    if (Equal(dll, "COMCTL32.DLL") && !name && ordinal >= 233 && ordinal <= 236)
        item.argc = (ordinal & 1) ? 3 : 2;
    if (!allow_missing && item.argc == 0xffffffff)
    {
        if (!a.stub_mode)
        {
            --a.import_count; // The record was never published; reuse it.
            return false;
        }
        item.argc = 0xfffffffeu;
    }
    // The session keeps the argument contract with the slot, so a process that
    // jumps to this gate without having bound it can still answer.
    Application::PublishSessionGate((gate - Gates) / 16, item.argc);
    *address = TrivialProcedure(item, gate);
    return true;
}
#include "x86_import_table.inc"
unsigned Application::NewImport(const char *dll, const char *name, unsigned argc)
{
    // Exit and Callback are return addresses inside the gate region. Their two
    // slots stay without an import, and Dispatch refuses both addresses:
    // Service ends the thread at Exit, each loop that made a call ends at
    // its Callback.
    static_assert((Exit - Gates) % 16 == 0 && (Callback - Gates) % 16 == 0 && Callback < Gates + GateRegionBytes,
                  "Exit and Callback must be gate slots");
    static_assert((Exit - Gates) / 16 == ExitGateSlot && (Callback - Gates) / 16 == CallbackGateSlot,
                  "the session gate directory reserves the same two slots");
    // The slot comes from the session, so a gate address is the same address
    // in every process.
    const unsigned slot = SessionGateSlot(dll, name);
    if (slot >= ImportCapacity + 2)
        return 0;
    // Every chunk up to this slot exists, because the tables walk densely.
    for (unsigned chunk = 0; chunk <= slot; chunk += ImportTable::ChunkSize)
        if (!imports.Reserve(chunk))
            return 0;
    if (!imports.Reserve(slot))
        return 0;
    // A slot another process filled first is a gate this process has not bound.
    // It keeps a marker name and not the export name: a lookup by name must
    // not find it, or the process would treat the export as unimplemented
    // instead of binding it. A stray jump lands on a named refusal.
    for (unsigned i = import_count; i < slot; ++i)
    {
        auto &hole = imports[i];
        if (hole.dll[0] || hole.name[0])
            continue;
        Copy(hole.dll, "NEO98", sizeof hole.dll);
        Copy(hole.name, "unbound session gate", sizeof hole.name);
        hole.argc = 0xffffffff;
        hole.route = RouteUnbound;
    }
    auto &item = imports[slot];
    Copy(item.dll, dll, sizeof item.dll);
    Copy(item.name, name, sizeof item.name);
    item.argc = argc;
    item.calls = 0;
    item.id = item.route_argument = 0;
    item.route = RouteUnbound;
    item.header_only = false;
    imports.Index(slot);
    if (slot >= import_count)
        import_count = slot + 1;
    // The contract goes with the slot for a process that meets the gate
    // without binding it: DDHELP calls NEO98DD32's DestroySurface for a game
    // that died.
    if (argc != 0xffffffff)
        PublishSessionGate(slot, argc);
    return Gates + 16 * slot;
}
// The slice limit. It must exist before the first block is translated, because
// Unicorn decides per block at translation time whether to call a block hook.
// An engine with a block budget (the group "budget" of
// tools/build_unicorn_host.py) counts the blocks in translated code and calls
// Slice only when the budget is used up; any other calls it before every block.
bool Application::AddSliceHook()
{
    if (cpu->Backend() == CpuBackend::Native)
    {
        budget_slices = false;
        return Check(cpu->SetTimeSliceHook(Slice, this));
    }
    bool counted = true;
    const unsigned error = cpu->SetBudgetHook(Slice, this, counted);
    budget_slices = counted;
    return Check(error);
}
// Sets the budget for the rest of the slice before a run. While the frame of an
// exception handler is open, Slice has to see every block, so the
// budget is one block; that costs what the block hook cost, and only then.
void Application::ArmSlice()
{
    if (!budget_slices)
        return;
    // Blocks vary greatly in cost. Check elapsed time regularly instead of
    // letting a long arithmetic block repeat 50,000 times before yielding.
    constexpr unsigned check_blocks = 256;
    const unsigned remaining = SliceBlocks - slice_blocks;
    slice_armed = ExceptionState().handler_frames && ExceptionState().abandon_from == ~0u ? 1 : (remaining > check_blocks ? check_blocks : remaining);
    cpu->Budget((int)slice_armed - 1);
}
// Unicorn calls this before a translated block: before each one as a block
// hook, before the one that uses up the budget otherwise. The block has not run
// yet and EIP is its first instruction, so a stop here resumes like any other.
// A non-zero count in uc_emu_start would bound the slice too, but Unicorn
// implements it as a hook before every single instruction (uc.c, hook_count_cb).
void Application::Slice(Cpu &cpu, unsigned address, void *context)
{
    auto &a = *static_cast<Application *>(context);
    // Block-hook stops corrupt the output of the Win16 WinZip inflater even
    // when the visible registers survive the yield. Stop these runs at an
    // instruction boundary using Run's quota instead; they remain preemptible.
    if (a.Win16Code()) { a.ArmSlice(); return; }
    // The guest code of a critical section runs as one piece: a thread switch
    // between its test and its change would let a second thread take the same
    // section. It never waits, so the next block after it stops instead.
    if (address - CriticalCodeBase < 4096)
    {
        // With the block budget of the engine the hook comes only when the
        // armed blocks are used up; it has to be armed again, for the one
        // block after this one, or the slice would never end.
        a.slice_blocks += a.slice_armed;
        if (a.budget_slices)
        {
            a.slice_armed = 1;
            cpu.Budget(0);
        }
        return;
    }
    // An exception handler that leaves through a non-local jump never comes
    // back to the mark its frame put on the stack, and the guest can run on
    // for a long time before the loop of that frame would look again. The
    // stop here brings control back to it at the first block above its mark,
    // so the frame ends before the next exception is delivered.
    // A block of the gate region is one of ours, and the return mark of a
    // callback is one of them: a handler that returns normally arrives there
    // with its stack already above the mark, which is not a frame the guest
    // left behind. The gate hook stops the run there anyway.
    if (a.ExceptionState().handler_frames && a.ExceptionState().abandon_from == ~0u &&
        !(address >= Gates && address < Gates + GateRegionBytes))
    {
        const unsigned esp = a.Register(Reg::Esp);
        unsigned left = a.ExceptionState().handler_frames;
        while (left && esp > a.ExceptionState().handler_frame[left - 1])
            --left;
        if (left != a.ExceptionState().handler_frames)
        {
            a.ExceptionState().abandon_from = left;
            cpu.Stop();
            return;
        }
    }
    a.slice_blocks += a.slice_armed;
    if (a.slice_blocks >= SliceBlocks || (a.turn_start && Now() - a.turn_start >= TurnMicroseconds))
    {
        a.slice_stop = true;
        cpu.Stop();
    }
    else
        a.ArmSlice();
}
void Application::Gate(Cpu &cpu, unsigned address, void *context)
{
    auto &a = *static_cast<Application *>(context);
    // A gate of the positive list answers here and lets the engine run the
    // return instruction the gate carries. Every other call, and every call
    // this one hands back, stops the run for Dispatch.
    if (a.FastGate(address))
        return;
    unsigned selector = 0;
    const bool native_flat = cpu.Backend() == CpuBackend::Native && !cpu.Get(Reg::Cs, selector) && !(selector & 4);
    if (direct_gates && (cpu.Backend() == CpuBackend::Fex || native_flat))
    {
        a.DirectGate(address);
        return;
    }
    a.pending = a.GateEntry(address);
    cpu.Stop();
}
bool Application::direct_gates = [] {
    const char *value = getenv("NEO98_DIRECT_GATES");
    return !value || strcmp(value, "0") != 0;
}();
// What Run and Step do after a stop at a gate, done inside the engine: Service
// the call, then the start of the next Step. Where that start would not go
// straight back into the guest, the engine stops, and Run and Step take over
// with the same state they would have had.
void Application::DirectGate(unsigned address)
{
    pending = GateEntry(address);
    if (state == Failed || state == Exited || waiting || pending == Exit || pending == Callback)
    {
        cpu->Stop();
        return;
    }
    // Run: `if (pending && !Service) break;`
    if (!Service())
    {
        direct_break = true;
        cpu->Stop();
        return;
    }
    if (state == Failed || state == Exited || threads[current_thread].done || pending)
    {
        cpu->Stop();
        return;
    }
    // Step, up to the Run of the engine. A sleep the call left behind is
    // slept there, outside the engine.
    if (!budget || Register(Reg::Eip) == 0 || threads[current_thread].sleep_after_call ||
        (state_point_wanted && !depth))
    {
        cpu->Stop();
        return;
    }
    const bool free_call = !slice_stop && Now() - turn_start < TurnMicroseconds;
    slice_stop = false;
    if (!free_call) --budget;
    slice_blocks = 0;
    ArmSlice();
}

// USER callbacks belong to the process runtime, regardless of its image
// format. A NE task uses the same native controls and owner-draw paths.
bool Application::InitializeUserProviders(unsigned version)
{
    user_expected_version = version;
    LoadUserStartSettings();
    LoadDisplayStartMode();
    if (!InitializeClipboard()) { Fail("Native clipboard provider allocation failed"); return false; }
    BindUserProviders();
    User::ConfigureVersion(version);
    return true;
}

// Install compiled providers before restoring fields. Their contexts remain
// serialized guest ownership and are replaced by LoadFields, independently
// for every provider. Do not load settings, allocate clipboard storage or
// configure a version here.
void Application::BindUserProviders()
{
    BindClipboardProviders();
    User::SetCaretProvider(this,[](void*p,unsigned h,unsigned dc){return static_cast<Application*>(p)->DrawCaret(h,dc);},[](void*p,unsigned h){static_cast<Application*>(p)->CaretWindowDestroyed(h);});
    User::SetCaretBracket(this,[](void*p,unsigned h,bool hide){return static_cast<Application*>(p)->CaretPaintBracket(h,hide);});
    // KERNEL ordinal 464 answers a queue handle for a thread that has one.
    // Here every live thread of this process owns the shared native queue.
    User::SetThreadQueueProvider([](void*p,unsigned id){
        auto*app=static_cast<Application*>(p);
        if(!app||!id)return 0u;
        for(auto&t:app->threads)if(t.id==id&&!t.done)return id;
        return 0u;});
    User::SetMetadataProvider(this,[](void*p){auto&a=*static_cast<Application*>(p);for(auto&t:a.threads)if(t.id==User::CurrentThread())return &t.message_metadata;return &a.threads[a.current_thread].message_metadata;},[](void*){return TickCount();},[](void*){return unsigned(Now()/1000);});
    User::SetEditNotify(this,[](void*p,unsigned parent,unsigned command,unsigned control){
        auto&app=*static_cast<Application*>(p);
        if(app.state==Failed||app.state==Exited||!User::IsWindow(parent))return;
        // Host-side inspection/tests can mutate native EDIT outside the guest
        // fiber. Queue those notifications for normal Application dispatch;
        // never enter or suspend an x86 callback on the host caller's stack.
        auto*fiber=app.threads[app.current_thread].fiber;
        if(fiber&&fiber->Active())app.CallWindow(parent,0x111,command,control);
        else User::PostMessage(parent,0x111,command,control);
    });
    User::SetEditWordBreak(this,[](void*p,unsigned procedure,const char*text,unsigned current,unsigned length,unsigned code){
        return static_cast<Application*>(p)->EditWordBreak(procedure,text,current,length,code);
    });
    User::SetCaptureNotify(this,[](void*p,unsigned old,unsigned next){
        auto&app=*static_cast<Application*>(p);if(app.state==Failed||app.state==Exited||!User::IsWindow(old))return;
        auto*fiber=app.threads[app.current_thread].fiber;
        if(fiber&&fiber->Active())app.CallWindow(old,0x215,0,next);
        else User::PostMessage(old,0x215,0,next);
    });
    User::SetWinEventCallback(this,[](void*p,unsigned event,unsigned h,unsigned object,unsigned child,bool window_queue){
        auto&app=*static_cast<Application*>(p);
        if(app.state!=Failed&&app.state!=Exited)app.WinEventNotify(event,h,object,child,window_queue);
    });
    User::SetMenuCallbacks(this,[](void*p,unsigned h,unsigned m,unsigned wp,unsigned lp){return static_cast<Application*>(p)->SendFromNative(h,m,wp,lp);},[](void*p){return static_cast<Application*>(p)->RunMenuLoop();},[](void*p){return static_cast<Application*>(p)->ThreadId();});
    User::SetMenuOwnerCallbacks(this,
        [](void*p,unsigned owner,unsigned id,unsigned data,int*cx,int*cy){static_cast<Application*>(p)->MeasureMenuItem(owner,id,data,cx,cy);},
        [](void*p,unsigned owner,const unsigned*fields){static_cast<Application*>(p)->DrawMenuItem(owner,fields);});
    User::SetPropertyCleanup(this, [](void *p, unsigned hwnd) {
        static_cast<Application *>(p)->DestroyProperties(hwnd);
    });
    User::SetInstanceVersionProvider([](void *p, unsigned instance) {
        return static_cast<Application *>(p)->InstanceVersion(instance);
    });
    User::SetCursorProvider(this, [](void *p, Gdi::HDC dc, int x, int y, unsigned cursor) {
        return static_cast<Application *>(p)->DrawCursorShape(dc, x, y, cursor);
    });
    User::SetFrameCallbacks(this,
        [](void*p,unsigned h,unsigned m,unsigned wp,unsigned lp){return static_cast<Application*>(p)->SendFromNative(h,m,wp,lp);},
        [](void*p,unsigned h,unsigned m,unsigned wp,Gdi::RECT*r){return static_cast<Application*>(p)->FrameTrackRect(h,m,wp,r);},
        [](void*p){return static_cast<Application*>(p)->RunFrameTrackLoop();},
        [](void*p,unsigned h,int x,int y,int w,int t,unsigned f){static_cast<Application*>(p)->FrameTrackCommit(h,x,y,w,t,f);});
    // The caption's icon: the class icon handle resolved to its image, scaled
    // to the caption slot when only the large icon exists.
    User::SetFrameIcon(this, [](void *p, Gdi::HDC dc, int x, int y, unsigned icon, int width, int height) {
        auto *app = static_cast<Application *>(p);
        auto *record = app->FindIcon(icon);
        if (!record || !record->data) return false;
        if ((int)record->data->width == width && (int)record->data->height == height) return record->data->Draw(dc, x, y, 3);
        WinImage::Image scaled;
        if (!scaled.Copy(*record->data, (unsigned)width, (unsigned)height)) return false;
        return scaled.Draw(dc, x, y, 3);
    });
    User::SetIconSizeQuery(this, [](void *p, unsigned icon, int &width, int &height) {
        auto *record = static_cast<Application *>(p)->FindIcon(icon);
        if (!record || !record->data) return false;
        width = (int)record->data->width;
        height = (int)record->data->height;
        return true;
    });
    User::SetDefaultIconQuery(this, [](void *p, unsigned icon) {
        return icon && icon == static_cast<Application *>(p)->default_application_icon;
    });
    User::SetDragIconProvider(this, [](void *p, unsigned hwnd) {
        return static_cast<Application *>(p)->QueryDragIcon(hwnd);
    });
    User::SetShellEvents(this, [](void *p, unsigned code, unsigned hwnd, unsigned parameter) {
        static_cast<Application *>(p)->ShellEvent(code, hwnd, parameter);
    });
    User::SetCommonDraw(this,[](void *p,unsigned h,unsigned dc,const Gdi::RECT &r){static_cast<Application*>(p)->DrawCommon(h,dc,r);});
    User::SetControlCallbacks(
        this,
        [](void *p, unsigned parent, unsigned child, unsigned dc, const Gdi::RECT &rect, unsigned state) {
            static_cast<Application*>(p)->DrawControl(parent, child, dc, rect, state);
        },
        [](void *p, unsigned parent, unsigned child, unsigned dc, unsigned message) {
            return static_cast<Application *>(p)->SendFromNative(parent, message, dc, child);
        });
    User::SetGuestPaint(this, [](void *p, unsigned hwnd) { static_cast<Application *>(p)->SendFromNative(hwnd, 0xf, 0, 0); });
}
bool Application::Start(const void *exe, unsigned length, const unsigned char *font, unsigned font_length,
                        const char *name, const char *command)
{
    if (cpu)
        return false;
    // A 16-bit program is a process of its own loader; PLAN.md step 9 and
    // src/ne_loader.cpp.
    if (Win98Ne::IsImage(exe, length))
        return StartWin16(exe, length, font, font_length, name, command);
    // A DOS program is a process of the DOS machine; src/x86_dos_vm.cpp.
    if (DosVm::IsProgram(name, exe, length))
    {
        const bool started = StartDosVm(font, font_length, name, command);
        if (!started) ReportDosFailure(name, Error());
        return started;
    }
    Copy(program_name, name, sizeof program_name);
    state = Running;
    if (unsigned registry_error=Registry::SystemInitializationError())
    {
        last_error=registry_error;
        Fail("Installed registry configuration could not be loaded");
        return false;
    }
    font_bytes = font;
    font_size = font_length;
    if (!image.Parse(exe, length))
    {
        Fail(image.Error());
        return false;
    }
    if (!image.Entry())
    {
        Fail("EXE has no entrypoint");
        return false;
    }
    // An ARM64 program is a process like any other; what differs is the
    // processor and what stands in the gates. docs/ARM64-APPS.md.
    const bool arm64 = image.Arm64();
    if (arm64 && !Arm64::MachineAvailable())
    {
        Fail("This build has no machine for ARM64 programs");
        return false;
    }
    Files::SetClock(&Application::FileClock);
    if (!ConfigureMemory())
        return false;
#if !defined(__APPLE__) && !defined(__linux__)
    // Unicorn aborts when its code-cache allocation fails, so refuse early.
    if (!neo98_jit_free_slots())
    {
        Fail("No free translation cache slot for another x86 engine");
        return false;
    }
#endif
    if (arm64)
    {
        if (CreateCpu(CpuBackend::Arm64, cpu, "arm64-process") != CpuError::Ok || !cpu)
        {
            cpu = nullptr;
            Fail("The machine for ARM64 programs could not be created (Hypervisor.framework needs the entitlement "
                 "com.apple.security.hypervisor in the signature of the host, and has room for 15 such processes)");
            return false;
        }
        Arm64::Layout layout;
        layout.gates = Gates;
        layout.gate_bytes = GateRegionBytes;
        layout.exit_gate = Exit;
        layout.callback_gate = Callback;
        layout.area = Arm64AreaBase;
        layout.area_bytes = Arm64AreaBytes;
        layout.shadows = Arm64ShadowBase;
        layout.shadow_bytes = Arm64ShadowBytes;
        layout.descriptors = SystemPageBase;
        layout.context = this;
        layout.import_name = [](void *p, unsigned gate, const char *&dll, const char *&name) {
            auto &app = *static_cast<Application *>(p);
            const unsigned slot = (gate - Gates) / 16;
            if (gate < Gates || slot >= app.import_count) return false;
            auto &item = app.imports[slot];
            dll = item.dll;
            name = item.name;
            return item.name[0] != 0;
        };
        layout.is_dialog = [](void *p, unsigned hwnd) { return static_cast<Application *>(p)->FindDialog(hwnd) != nullptr; };
        layout.object_type = [](void *, unsigned handle) { return Gdi::GetObjectType(handle); };
        layout.image_of = [](void *p, unsigned address, unsigned &base) {
            auto &app = *static_cast<Application *>(p);
            for (unsigned i = 0; i < app.module_watermark; ++i)
            {
                const auto &module = app.modules[i];
                if (!module.kind || !module.pe || !module.pe->Arm64() || address < module.handle) continue;
                if (address - module.handle >= module.pe->SizeOfImage()) continue;
                base = module.handle;
                return true;
            }
            return false;
        };
        Arm64::Configure(*cpu, layout);
    }
    else if (!Check(CreateCpu(ProcessCpuBackend(), cpu, "win32-process")))
        return false;
    // FEXCore runs no 16-bit code; the pair gives the 16-bit modules a
    // process hosts a second engine over the same memory.
    if (!arm64 && !cpu->Runs16Bit())
        cpu = CreateDualCpu(cpu);
    if ((!arm64 && !InitializeCpuDescription()) || !InitializeFpu(Win32FpuControl)) return false;
    // The system VM's VGA aperture is readable before DISPDIB is loaded.
    // Hellbender probes it at startup, as confirmed by the Win98 fixture.
    if (!arm64 && (!MapSystemBios() || !EnsureVga())) return false;
    // Win98 maps the image at its preferred base and only then places the
    // process heap in a free part of the private arena. The fixed stack, gate
    // and descriptor ranges go in first so the search cannot pick them.
    if (!image.Load(*this) || !Map(0x0fe00000, 0x200000, 3) || !Map(Gates, GateRegionBytes, 3) ||
        !Map(Win16LdtBase, Win16LdtBytes, 3) || !Map(SystemPageBase, SystemPageBytes, 3) ||
        (arm64 && (!Map(Arm64AreaBase, Arm64AreaBytes, 5) || !Map(Arm64ShadowBase, Arm64ShadowBytes, 3))) || !MapProcessHeap())
        return false;
    // The shared area of the session is mapped in every process of Win98, so
    // this process takes what the session already holds. See
    //
    JoinSession();
    // An x86 process has `hlt` in every gate, an ARM64 process `svc #0; ret`.
    unsigned char trap[65536];
    if (arm64) Arm64::FillGates(trap, sizeof trap);
    else memset(trap, 0xf4, sizeof trap);
    for (unsigned offset = 0; offset < GateRegionBytes; offset += sizeof trap)
        Store(Gates + offset, trap, sizeof trap);
    Protect(Gates, GateRegionBytes, 5);
    const unsigned long long teb = MainTeb;
    const unsigned long long descriptors[] = {
        0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull,
        0xfffull | ((teb & 0xffffff) << 16) | (0x93ull << 40) | (0x40ull << 48) | ((teb >> 24) << 56),
        // Entry four is the LDT of this process. Type 2 is an LDT segment. A
        // 16-bit module this process loads puts its segments in there.
        (unsigned long long)(Win16LdtBytes - 1) | ((unsigned long long)(Win16LdtBase & 0xffffff) << 16) |
            (0x82ull << 40) | ((unsigned long long)(Win16LdtBase >> 24) << 56)};
    Store(SystemPageBase, descriptors, sizeof descriptors);
    DescriptorTable gdt;
    gdt.base = SystemPageBase;
    gdt.limit = (3 + ThreadCapacity) * 8 - 1;
    Check(cpu->SetWide(Reg::Gdtr, &gdt));
    DescriptorTable ldt;
    ldt.base = Win16LdtBase;
    ldt.limit = Win16LdtBytes - 1;
    Check(cpu->SetWide(Reg::Ldtr, &ldt));
    Register(Reg::Cr0, 1);
    Register(Reg::Cs, 8);
    const int segments[] = {Reg::Ds, Reg::Es, Reg::Ss, Reg::Gs};
    for (int reg : segments)
        Register(reg, 16);
    Register(Reg::Fs, 24);
    Write32(teb, 0xffffffff);
    Write32(teb + 4, 0x10000000);
    Write32(teb + 8, 0x0fe00000);
    Write32(teb + 0x18, teb);
    Write32(teb + 0x1c, 0xffff0001); // Win32 TIB flags and released thunk count
    // The Win98 TDB constructor Windows 98 stores TIB+88h, the base of the 80
    // TLS slots, in TIB+2Ch for every thread. Compiled thread-object accesses
    // read that pointer, so static TLS needs nothing else in the TIB.
    Write32(teb + 0x2c, teb + 0x88);
    threads[0].id=AllocateThreadIdentity();if(!threads[0].id){Fail("Global thread identity capacity exhausted");return false;}
    Write32(teb + 0x20,process_id);Write32(teb + 0x24, threads[0].id);
    threads[0].app = this;

    threads[0].keyboard_layout = keyboard_layout_state.entries[0].layout;
    User::SetKeyboardLanguage(keyboard_layout_state.entries[0].layout);
    InitializeThreadControl(0);
    if (!InitializeProcessDatabase()) { Fail("Process database allocation failed"); return false; }
    Write32(teb + 0x30, process_database);
    modules[0].kind = 1;
    modules[0].handle = image.Base();
    modules[0].pe = &image;
    modules[0].bound = true;
    char executable_path[260];
    if (strchr(program_name, '\\') || strchr(program_name, '/') || strchr(program_name, ':'))
    {
        if (files.FullPath(program_name, executable_path)) { Fail("Invalid executable path"); return false; }
    }
    else snprintf(executable_path, sizeof executable_path, "C:\\WINDOWS\\%s", program_name);
    Copy(modules[0].path, executable_path, sizeof modules[0].path);
    command_line = String(command ? command : program_name);
    if(initial_title) startup_info[3]=String(initial_title);
    crt_data = Allocate(64);
    Write32(crt_data, command_line);
    if (!InitializeProcessEnvironment())
    { Fail("Process environment allocation failed"); return false; }
    if (!InitializeArguments())
        return false;
    if (!InitializeNls())
        return false;
    // COMDLG32 allocates its private TLS index in its own DLL_PROCESS_ATTACH
    // (original ), not in every process. InitializeModule does it.
    if (!InitializeThreadCrt())
        return false;
    ++loader_writes;
    binding_process_image = true;
    const bool bound_imports = image.BindImports(*this, Resolve, this);
    binding_process_image = false;
    --loader_writes;
    if (!bound_imports)
    {
        Fail("PE import binding failed");
        return false;
    }
    // Static TLS indices belong to the EXE and to the modules the import graph
    // pulled in. The primary thread needs its blocks before the first DLL entry
    // point and before the EXE entry runs.
    if (!InitializeStaticTls())
    {
        if (state != Failed)
            Fail("Static TLS setup for the primary thread failed");
        return false;
    }
    Check(cpu->SetGateHook(Gates, Gates + GateRegionBytes - 1, Gate, this));
    if (cpu->Backend() == CpuBackend::Native && !trace_calls && !export_census)
        Check(cpu->SetGateCodeExecution(true));
    // Every unmapped or protected guest access passes here first, so the
    // failure message can name the address.
    Check(cpu->SetFaultHook(MemoryFaultHook, this));
    AddSliceHook();
    // Software interrupts of the 32-bit guest. The original NTDLL reaches the
    // kernel through int 2Eh, and without this hook every such instruction
    // became an unhandled CPU exception.
    Check(cpu->SetInterruptHook(GuestInterrupt, this));
    // Port input. The one port a Win98 program of the collection reads is the
    // VGA input status register: the emulation layer of the original DDRAW
    // polls it for the vertical retrace.
    // Port output. The PC speaker is driven with nothing else: a
    // program gives counter 2 of the 8253 a divisor over 42h and 43h and opens
    // the gate in 61h, which is what USER does for MessageBeep too.
    Check(cpu->SetPortHooks(GuestPortInput, GuestPortOutput, this));
    Register(Reg::Esp, 0x0fffe000);
    if (!arm64 && !Win16Process()) InstallThreadStartHandler(teb, 0x0ffff000);
    static_assert(ProcessExitGate == Exit, "the return stub of KERNEL32 leads to the exit gate");
    // Win98 enters a program from BaseProcessStart in KERNEL32, so the return
    // address on the first stack frame lies in that image. Without the header
    // page of KERNEL32 the gate itself stays the return address.
    // The return stub in the header page of KERNEL32 is x86 code; an ARM64
    // program returns into the exit gate itself.
    Write32(0x0fffe000, !arm64 && virtual_memory.Access(Win98KernelBase + ProcessReturnOffset, 5, false)
                            ? Win98KernelBase + ProcessReturnOffset : Exit);
    Register(Reg::Eip, image.Entry());
    Register(Reg::Eflags, 2);
#ifdef NEO98_RUNTIME_CONTENT
    // USER creates its first MS Sans Serif font before the stock objects.
    // Release hosts must have read the installed catalogue by then.
    LoadInstalledFonts();
#endif
    if (!User::Initialize())
    {
        Fail("Native USER initialization failed");
        return false;
    }
    if(!User::AttachProcess(process_id,this)){Fail("Desktop process capacity exhausted");return false;}
    user_initialized = true;User::BindTask(process_id,threads[0].id);
    const auto subsystem = image.SubsystemVersion();
    if (!InitializeUserProviders(((subsystem >> 8) & 0xff00) | (subsystem & 255)))
        return false;
    if (!InstallStockFonts())
        return false;
    const unsigned colors[] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0};
    for (unsigned i = 0; i < 5; ++i)
        stock[i] = Gdi::CreateSolidBrush(colors[i]);
    stock[15] = Gdi::DefaultPalette();
    stock[5] = Gdi::CreateNullBrush();
    stock[6] = Gdi::CreatePen(0,0,0xffffff);
    stock[7] = Gdi::CreatePen(0,0,0);
    stock[8] = Gdi::CreatePen(5,0,0);
    if(!stock[5]||!stock[6]||!stock[7]||!stock[8]){Fail("Original stock pen allocation failed");return false;}
    // A class whose background is a stock brush must not take it along when
    // the class goes.
    for (unsigned i = 0; i <= 5; ++i) User::ProtectStockBrush(stock[i], true);
    LoadDesktopWallpaper();
    if (state == Failed)
        return false;
    if(image.Subsystem()==3 && !(startup_info[11]&0x100)) {
        unsigned unused[1]={};
        if(!ConsoleApi("AllocConsole",unused)){Fail("Console allocation failed");return false;}
    }
    auto &thread = threads[0];
    thread.fiber = NewThreadFiber();
    if (!thread.fiber)
    {
        Fail("Execution fiber allocation failed");
        return false;
    }
    if (!Check(cpu->NewContext(thread.context)) ||
        !Check(cpu->Save(thread.context))) return false;
    if (!thread.fiber->Start([](void *p) { static_cast<Application *>(p)->Run(); }, this))
    {
        Fail("Execution fiber allocation failed");
        return false;
    }
    return true;
}

// Diagnostic export-stub mode. These routines never implement a Windows
// service: they record a missing export and hand the guest a documented
// neutral result so one run can expose the whole remaining dependency chain.
// Nothing recorded here belongs in a dispatch table.
bool Application::AddStubArgumentBytes(const char *dll, const char *name, unsigned bytes)
{
    if (!dll || !name || bytes > 1024 || (bytes & 3))
        return false;
    if (stub_argument_count == stub_argument_capacity)
    {
        const unsigned grown = stub_argument_capacity ? stub_argument_capacity * 2 : 1024;
        auto *table = new StubArgument[grown];
        for (unsigned i = 0; i < stub_argument_count; ++i)
            table[i] = stub_arguments[i];
        delete[] stub_arguments;
        stub_arguments = table;
        stub_argument_capacity = grown;
    }
    auto &entry = stub_arguments[stub_argument_count++];
    Copy(entry.dll, dll, sizeof entry.dll);
    Copy(entry.name, name, sizeof entry.name);
    entry.bytes = bytes;
    return true;
}
bool Application::StubPopBytes(const char *dll, const char *name, unsigned &bytes) const
{
    for (unsigned i = 0; i < stub_argument_count; ++i)
        if (Equal(stub_arguments[i].dll, dll) && !strcmp(stub_arguments[i].name, name))
        {
            bytes = stub_arguments[i].bytes;
            return true;
        }
    return false;
}
Application::StubRecord *Application::StubEntry(const char *dll, const char *name, unsigned kind)
{
    for (unsigned i = 0; i < stub_count; ++i)
        if (stubs[i].kind == kind && Equal(stubs[i].dll, dll) && !strcmp(stubs[i].name, name))
            return &stubs[i];
    if (stub_count == StubCapacity)
        return nullptr;
    auto &record = stubs[stub_count++];
    Copy(record.dll, dll, sizeof record.dll);
    Copy(record.name, name, sizeof record.name);
    record.kind = kind;
    return &record;
}
void Application::NoteStubCall(const char *dll, const char *name, unsigned kind, unsigned eip, unsigned ret,
                               unsigned pop, bool known, const unsigned *args)
{
    ++stub_calls;
    auto *record = StubEntry(dll, name, kind);
    if (record)
    {
        if (!record->calls)
        {
            record->first_call = api_calls;
            record->first_eip = eip;
            record->first_return = ret;
            record->pop_bytes = pop;
            record->pop_known = known;
            for (unsigned i = 0; i < 8 && args; ++i)
                record->first_args[i] = args[i];
        }
        ++record->calls;
        record->last_call = api_calls;
    }
    // Ordered trace so a later crash can be attributed to a preceding stub.
    if (log && stub_logged_calls < 20000)
    {
        ++stub_logged_calls;
        char text[512];
        snprintf(text, sizeof text, "PROBE_STUB_CALL index=%u kind=%u dll=%s name=%s gate=%08x pop=%u known=%u",
                 api_calls, kind, dll, name, eip, pop, known);
        log(text);
    }
}
bool Application::StubDispatch(Import &item)
{
    const unsigned sp = Register(Reg::Esp), ret = Read32(sp), eip = Register(Reg::Eip);
    unsigned args[8] = {};
    Read(sp + 4, args, sizeof args);
    if (!waiting)
        ++api_calls;
    waiting = false;
    ++item.calls;
    unsigned pop = 0;
    const bool known = StubPopBytes(item.dll, item.name, pop);
    NoteStubCall(item.dll, item.name, item.argc == 0xfffffffeu ? StubDynamic : StubImport, eip, ret, pop, known, args);
    // Documented neutral result: EAX = 0, no LastError change, callee-clean by
    // the recorded original argument bytes when known, otherwise caller-clean.
    Register(Reg::Eax, 0);
    Register(Reg::Esp, sp + 4 + pop);
    Register(Reg::Eip, ret);
    pending = 0;
    return true;
}
bool Application::export_census = false;
bool Application::tolerate_missing_exports = false;
// An export Windows 98 has and this port does not. GetProcAddress and the
// import binder hand out its gate, because Win98 hands out an address for it
// and a packed loader ends the process on the first null. The call
// itself is where the gap shows, and it must never look like success.
bool Application::MissingExportCall(Import &item)
{
    const unsigned sp = Register(Reg::Esp), ret = Read32(sp);
    unsigned args[8] = {};
    Read(sp + 4, args, sizeof args);
    const unsigned count = item.argc & 255;
    if (!waiting)
        ++api_calls;
    waiting = false;
    ++item.calls;
    const unsigned ring_token =
        api_ring.Begin(item.dll, item.name, ThreadId(), ret, args, count > 8 ? 8 : count);
    char where[192];
    DescribeCodeAddress(ret, where, sizeof where);
    // The interactive host takes the same answer, for another reason: a desktop
    // session must not end because one program, or Explorer itself, reaches an
    // export the port has no body for yet (MSHTML called GDI32!GetRandomRgn
    // while painting a folder and took the whole shell down). The call is
    // logged once per export with its caller, so it still gets fixed.
    if (export_census || tolerate_missing_exports)
    {
        // Diagnostic measuring mode, never an ordinary run: the
        // answer is the one an unimplemented Win98 export gives, so one start
        // shows every missing call of a program instead of only the first.
        if (log && item.calls == 1)
        {
            char text[512];
            snprintf(text, sizeof text,
                     "%s dll=%s name=%s from=%s args=%08x,%08x,%08x,%08x",
                     export_census ? "PROBE_CENSUS_CALL" : "UNIMPLEMENTED EXPORT answered with error 120:",
                     item.dll, item.name, where, args[0], args[1], args[2], args[3]);
            log(text);
        }
        last_error = 120;
        api_ring.Finish(ring_token, 0);
        Register(Reg::Eax, 0);
        // 255 is the marker of an argument count the original does not tell;
        // the caller then keeps its own stack correction.
        Register(Reg::Esp, sp + 4 + (count == 255 ? 0 : count * 4));
        Register(Reg::Eip, ret);
        pending = 0;
        return true;
    }
    if (stub_mode)
    {
        // The diagnostic stub mode answered these
        // calls before the contract knew their names, and it keeps doing so:
        // EAX zero, last error unchanged, and the argument bytes the original
        // removes when the contract could read them.
        NoteStubCall(item.dll, item.name, StubImport, Register(Reg::Eip), ret,
                     count == 255 ? 0 : count * 4, count != 255, args);
        api_ring.Finish(ring_token, 0);
        Register(Reg::Eax, 0);
        Register(Reg::Esp, sp + 4 + (count == 255 ? 0 : count * 4));
        Register(Reg::Eip, ret);
        pending = 0;
        return true;
    }
    char message[sizeof error];
    if (count == 255)
        snprintf(message, sizeof message, "Unimplemented export %s!%s called from %s",
                 item.dll, item.name, where);
    else
        snprintf(message, sizeof message,
                 "Unimplemented export %s!%s called from %s with %u args %08x,%08x,%08x,%08x",
                 item.dll, item.name, where, count, args[0], args[1], args[2], args[3]);
    Fail(message);
    return false;
}
// The route of an import depends on its DLL and name alone, so the string
// comparisons run once, on the first call through its gate. Dispatch then
// switches on the stored route and the families switch on the stored API ID.
void Application::BindRoute(Import &item)
{
    item.id = ApiId(item.name);
    auto Set = [&item](Route route, unsigned argument = 0) { item.route = route; item.route_argument = argument; };
    if(Equal(item.dll,"SHELL32.DLL")&&(!strcmp(item.name,"ordinal_176")||!strcmp(item.name,"SHGetInstanceExplorer")))Set(RouteShellProcessReference);
    else if(Equal(item.dll,"SHELL32.DLL") && !strcmp(item.name,"ordinal_45"))Set(RoutePathProbe);
    else if(Equal(item.dll,"SHELL32.DLL") && !strncmp(item.name,"ordinal_50",10) && item.name[10]>='5' && item.name[10]<='9' && !item.name[11])Set(RouteShellRegistryOrdinal,500+item.name[10]-'0');
    else if(Equal(item.dll,"SHELL32.DLL") && (!strcmp(item.name,"ordinal_510") || !strcmp(item.name,"ordinal_511")))Set(RouteShellRegistryOrdinal,item.name[10]=='0'?510:511);
    else if(Equal(item.dll,"SHELL32.DLL")&&(!strcmp(item.name,"ordinal_651")||!strcmp(item.name,"ordinal_652")||!strcmp(item.name,"ordinal_654")))Set(RouteCabinet,650+item.name[10]-'0');
    else if(Equal(item.dll,"SHELL32.DLL")&&(!strcmp(item.name,"ordinal_653")||!strcmp(item.name,"ordinal_51")||!strcmp(item.name,"ordinal_40")))Set(RouteShellCommand,!strcmp(item.name,"ordinal_653")?653:!strcmp(item.name,"ordinal_51")?51:40);
    else if(Equal(item.dll,"SHLWAPI.DLL")&&(!strcmp(item.name,"ordinal_3")||!strcmp(item.name,"ordinal_5")))Set(RouteShellCommand,item.name[8]=='3'?3:5);
    else if(Equal(item.dll,"SHELL32.DLL") && (!strcmp(item.name,"SHGetFileInfo") || !strcmp(item.name,"SHGetFileInfoA") || !strcmp(item.name,"SHGetFileInfoW")))Set(RouteShellFileInfo);
    else if(Equal(item.dll,"SHELL32.DLL")&&(!strcmp(item.name,"ordinal_86")||!strcmp(item.name,"ordinal_87")||!strcmp(item.name,"ordinal_88")))Set(RouteShellDragDrop,80+item.name[9]-'0');
    else if(Equal(item.dll,"SHELL32.DLL") && (!strcmp(item.name,"ordinal_660") || !strcmp(item.name,"ordinal_71")))Set(RouteShellIconCache,!strcmp(item.name,"ordinal_660")?660:71);
    else if (Equal(item.dll,"SHELL32.DLL") && (!strcmp(item.name,"ordinal_100") || !strcmp(item.name,"ordinal_244"))) Set(RouteShellPolicy,!strcmp(item.name,"ordinal_100")?100:244);
    else if (Equal(item.dll,"SHELL32.DLL") && !strcmp(item.name,"ordinal_181")) Set(RouteRegisterShell);
    else if(Equal(item.dll,"COMCTL32.DLL")&&!strcmp(item.name,"MenuHelp"))Set(RouteCommonControl,2);
    else if(Equal(item.dll,"COMCTL32.DLL")&&!strncmp(item.name,"ordinal_",8)){
        unsigned ordinal=0;for(const char *p=item.name+8;*p;++p)ordinal=ordinal*10+*p-'0';
        Set(RouteCommonControl,ordinal);
    }else if (Equal(item.dll, "SHLWAPI.DLL"))
    {
        if(!strcmp(item.name,"ordinal_241"))Set(RouteShellStopwatch,241);
        else if(!strcmp(item.name,"ordinal_250"))Set(RouteShellStopwatch,250);
        else if(!strcmp(item.name,"ordinal_83"))Set(RouteShellModule,83);
        else if(!strcmp(item.name,"ordinal_80"))Set(RouteShellModule,80);
        else if(!strcmp(item.name,"ordinal_105"))Set(RouteShellModule,105);
        else if(!strcmp(item.name,"ordinal_437"))Set(RouteShellOs);
        else if(!strcmp(item.name,"ordinal_16")||!strcmp(item.name,"SHCreateThread")||!strcmp(item.name,"SHGetThreadRef")||!strcmp(item.name,"SHSetThreadRef"))Set(RouteShellThread);
        else if(!strcmp(item.name,"SHQueryValueExW"))Set(RouteShellRegistryOrdinal,592);
        else if (!strcmp(item.name, "SHRegOpenUSKeyA") ||
            !strcmp(item.name, "SHRegCreateUSKeyA") ||
            !strcmp(item.name, "SHRegQueryUSValueA") ||
            !strcmp(item.name, "SHRegGetUSValueA") ||
            !strcmp(item.name, "SHRegSetUSValueA") ||
            !strcmp(item.name, "SHRegWriteUSValueA") ||
            !strcmp(item.name, "SHRegDeleteUSValueA") ||
            !strcmp(item.name, "SHRegDeleteEmptyUSKeyA") ||
            !strcmp(item.name, "SHRegCloseUSKey") ||
            !strcmp(item.name, "SHRegEnumUSKeyA") ||
            !strcmp(item.name, "SHRegEnumUSValueA") ||
            !strcmp(item.name, "SHRegQueryInfoUSKeyA") ||
            !strcmp(item.name, "SHGetValueA") ||
            !strcmp(item.name, "SHSetValueA") ||
            !strcmp(item.name, "SHDeleteValueA") ||
            !strcmp(item.name, "SHDeleteEmptyKeyA") ||
            !strcmp(item.name, "SHDeleteOrphanKeyA") ||
            !strcmp(item.name, "SHRegDuplicateHKey") ||
            !strcmp(item.name, "SHQueryValueExA") ||
            !strcmp(item.name, "SHEnumKeyExA") ||
            !strcmp(item.name, "SHEnumValueA") ||
            !strcmp(item.name, "SHQueryInfoKeyA"))
            Set(RouteShellRegistry);
        else if(PathProbeOperation(item.name)>0)Set(RoutePathProbe);
        else if(!strcmp(item.name,"PathFindOnPathA")||!strcmp(item.name,"PathFileExistsA"))Set(RouteShellCommand,!strcmp(item.name,"PathFindOnPathA")?479:471);
        else if(ShellPathSyntaxOperation(item.name)>=0)Set(RouteShellPathSyntax);
        else if(ShellSearchOperation(item.name)>=0)Set(RouteShellSearch);
        else Set(RoutePath);
    }
    else if (Equal(item.dll, "COMCTL32.DLL") &&
             (!strcmp(item.name,"InitCommonControls") || !strcmp(item.name,"InitCommonControlsEx") ||
              !strcmp(item.name,"DllGetVersion"))) Set(RouteCommonInit);
    else if (Equal(item.dll, "NEO98DD32.DLL")) Set(RouteDisplayDriver);   // src/x86_ddhal.cpp
    else if (Equal(item.dll, OpenGLIcdModule())) Set(RouteOpenGL, OpenGLEntry(item.name));   // src/x86_opengl.cpp
    else if (Equal(item.dll, GlideModule())) Set(RouteGlide, GlideEntry(item.name));   // src/x86_glide.cpp
    else if (Equal(item.dll, GlidePageModule())) Set(RouteGlide, GlidePageEntry(item.name));   // src/x86_glide_page.inc
    else if (Equal(item.dll, DxDriverModule())) Set(RouteDxDriver);   // src/x86_dx_driver.cpp
    else if (Equal(item.dll, "GDI32.DLL")) Set(RouteGdi);
    else Set(Equal(item.dll, "MSVCRT.DLL") ? RouteCrt : RouteApi);
    // Whether this export writes or reads the pixels of a DC or a window, and
    // so waits while another thread has the primary surface locked
    // (src/x86_screen_lock.cpp).
    item.screen_pixels = (Equal(item.dll, "USER32.DLL") || Equal(item.dll, "GDI32.DLL")) && ScreenRulesEnabled()
                             ? ScreenPixelRule(ApiName(item.name, item.id)) : 0;
    // WINMM's nameless ordinals share their names with KERNEL32's, and Api
    // switches on the name alone. They get the number of "WINMM.ordinal_n"
    // instead, so KERNEL32 ordinal 1 (VxDCall) never answers for WINMM
    // ordinal 1 (WINMMTILEBUFFER). src/x86_winmm_completion.cpp.
    if (Equal(item.dll, "WINMM.DLL") && !strncmp(item.name, "ordinal_", 8))
    {
        char text[48];
        snprintf(text, sizeof text, "WINMM.%s", item.name);
        item.id = ApiId(text);
    }
}
// A gate of a slot the session created after this process last bound one.
// A shared image carries it: DDRAW.DLL lies in the shared arena with one
// bound import table, so DDHELP runs DDRAW code whose imports the game bound. The table grows to the slot with unbound markers, and Dispatch
// adopts the record of the session as for a hole below import_count.
void Application::ReachSessionGate(unsigned slot)
{
    if (slot < import_count || slot >= session_gate_count || slot >= ImportCapacity + 2)
        return;
    for (unsigned chunk = 0; chunk <= slot; chunk += ImportTable::ChunkSize)
        if (!imports.Reserve(chunk))
            return;
    if (!imports.Reserve(slot))
        return;
    for (unsigned i = import_count; i <= slot; ++i)
    {
        auto &hole = imports[i];
        if (hole.dll[0] || hole.name[0])
            continue;
        Copy(hole.dll, "NEO98", sizeof hole.dll);
        Copy(hole.name, "unbound session gate", sizeof hole.name);
        hole.argc = 0xffffffff;
        hole.route = RouteUnbound;
    }
    import_count = slot + 1;
}
bool Application::Dispatch()
{
    if (pending >= Gates && !((pending - Gates) & 15) && pending != Exit && pending != Callback &&
        pending >= Gates + import_count * 16)
        ReachSessionGate((pending - Gates) / 16);
    if (pending < Gates || pending >= Gates + import_count * 16 || ((pending - Gates) & 15) ||
        pending == Exit || pending == Callback)
    {
        Fail("Invalid API gate");
        return false;
    }
    auto &item = imports[(pending - Gates) / 16];
    // A slot another process bound first. Win98 has one address per function,
    // and a shared section may carry it, so this process takes the record of
    // the session instead of refusing an address it never handed out.
    if (item.argc == 0xffffffff && !strcmp(item.dll, "NEO98") && !strcmp(item.name, "unbound session gate"))
        AdoptSessionGate((pending - Gates) / 16);
    if (item.argc == FlatThunkGate)
        return FlatThunkTransition(item);
    // After the two sentinels above and before the unbound case: 0xffffffff
    // carries this bit too, and an import without any contract is not an
    // export of a native provider that the contract knows.
    if (item.argc != 0xffffffff && item.argc != 0xfffffffeu && (item.argc & MissingExportGate))
        return MissingExportCall(item);
    if (item.argc == 0xffffffff || item.argc == 0xfffffffeu)
    {
        if (stub_mode)
            return StubDispatch(item);
        // Record the call that stops the run, so the report's last ring entry
        // is the missing import and not the call before it.
        api_ring.Begin(item.dll, item.name, ThreadId(), Read32(Register(Reg::Esp)), nullptr, 0);
        char message[256];
        snprintf(message, sizeof message, "Unimplemented API %s!%s", item.dll, item.name);
        Fail(message);
        return false;
    }
    // The return address and the arguments in one read, when the frame
    // lies in one page; a read that fails must fail as two did.
    unsigned sp = Register(Reg::Esp), ret, args[16] = {};
    const unsigned count = item.argc & 255;
    unsigned frame[17];
    if (count <= 16 && (sp & 4095) + (count + 1) * 4 <= 4096 && Read(sp, frame, (count + 1) * 4))
    {
        ret = frame[0];
        memcpy(args, frame + 1, count * 4);
    }
    else
    {
        ret = Read32(sp);
        Read(sp + 4, args, count * 4);
    }
    unsigned ring_token = 0;
    bool ring_active = false;
    const int regs[] = {Reg::Ebx, Reg::Esi, Reg::Edi, Reg::Ebp};
    unsigned saved[4];
    for (unsigned i = 0; i < 4; ++i)
        saved[i] = Register(regs[i]);
    if (!waiting)
    {
        ++api_calls;
        ++slow_gate_calls;
        ring_token = api_ring.Begin<true>(item.dll, item.name, ThreadId(), ret, args, count);
        ring_active = true;
        if (!item.calls++ && log)
            log(item.name);
        if (log && (!strcmp(item.name, "TextOutA") || !strcmp(item.name, "ExtTextOutA")) &&
            ((trace_calls && api_calls >= trace_from) || getenv("NEO98_TRACE_TEXT")))
        {
            char text[320];
            // Text painted by original controls is also useful to host UI
            // tests. The character count need not include a trailing NUL.
            const bool text_out = !strcmp(item.name, "TextOutA");
            const bool extended_text_out = !strcmp(item.name, "ExtTextOutA");
            if (text_out || (extended_text_out && !(args[3] & 0x10)))
            {
                const unsigned pointer = args[text_out ? 3 : 5], length = args[text_out ? 4 : 6];
                char painted[256] = {};
                const unsigned n = length < sizeof painted - 1 ? length : sizeof painted - 1;
                if (n && virtual_memory.Access(pointer, n, false) && Read(pointer, painted, n))
                {
                    for (unsigned i = 0; i < n; ++i)
                        if ((unsigned char)painted[i] < 32) painted[i] = ' ';
                    snprintf(text, sizeof text, "APITEXT %u %s", api_calls, painted);
                    log(text);
                }
            }
        }
        if (log && trace_calls && api_calls >= trace_from)
        {
            char text[320];
            int used = snprintf(text, sizeof text, "APITRACE %u %s!%s ret=%08x thread=%u args=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
                     api_calls, item.dll, item.name, ret, ThreadId(), args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
            // Calls with more than eight arguments (BitBlt, StretchBlt,
            // CreateFontA) carry the rest after the first eight.
            for (unsigned i = 8; i < count && i < 16 && used > 0 && used < int(sizeof text) - 10; ++i)
                used += snprintf(text + used, sizeof text - used, ",%08x", args[i]);
            log(text);
            if (!strcmp(item.name, "CreateFontIndirectA") && args[0])
            {
                unsigned char lf[60] = {};
                if (Read(args[0], lf, sizeof lf))
                {
                    const int height = (int)(lf[0] | lf[1] << 8 | lf[2] << 16 | lf[3] << 24), weight = (int)(lf[16] | lf[17] << 8 | lf[18] << 16 | lf[19] << 24);
                    lf[59] = 0;
                    snprintf(text, sizeof text, "APIFONT %u height=%d weight=%d charset=%u face=%s", api_calls, height, weight, lf[23], (const char *)lf + 28);
                    log(text);
                }
            }
        }
    }
    waiting = false;
    LoadError();
    // The host asked for another desktop size or for a window in the
    // foreground. Either runs on a thread that is about
    // to ask for messages, where a broadcast of another process would reach
    // it as well.
    // The import names the module as the program wrote it ("USER32.dll" in
    // Explorer), so the comparison ignores case.
    if ((HostRequestPending() || host_desktop_pending || (host_icon_wanted && !host_icon_tried)) && !strcasecmp(item.dll, "USER32.DLL") &&
        (!strcmp(item.name, "GetMessageA") || !strcmp(item.name, "PeekMessageA") ||
         !strcmp(item.name, "GetMessageW") || !strcmp(item.name, "PeekMessageW") ||
         !strcmp(item.name, "WaitMessage") || !strcmp(item.name, "MsgWaitForMultipleObjects")))
        ServeHostRequests();
    const unsigned left_before = ExceptionState().abandoned_callbacks;
    unsigned value;
    if (item.argc & 0x20000000u)
    {
        last_error = 120;
        value = Win98::UnsupportedResult((item.argc >> 16) & 255);
        // Recorded for the diagnostic inventory only; the behaviour is the
        // established original Win98 return and is unchanged by stub mode.
        if (stub_mode)
            NoteStubCall(item.dll, item.name, StubOriginalError, Register(Reg::Eip), ret,
                         count * 4, true, args);
    }
    else
    {
        if (item.route == RouteUnbound)
        {
            BindRoute(item);
            // The first call of an import binds its route and only then can
            // say whether its gate may carry its own return. The engine is at
            // rest here, so the gate and its translated block may change.
            ArmFastGate(item, pending);
        }
        // A call that writes or reads the screen's pixels waits while another
        // thread has the primary surface locked; nothing else does
        // (src/x86_screen_lock.cpp). The wait leaves `waiting` set and the call undone.
        const ApiName name(item.name, item.id);
        // A thread out of a saved state that stood in this call: the call
        // is not made again, its frames are entered where they were.
        if (threads[current_thread].host_resume)
            value = ResumeHostCall();
        else
        {
        // A call a saved state can take up again says so to its handler.
        if (!item.tail_gate) item.tail_gate = TailGate(item.dll, item.name, item.id, false) ? 2 : 1;
        threads[current_thread].tail = item.tail_gate == 2 ? TailFresh : TailNone;
        if (item.screen_pixels && (User::ScreenLockActive() || threads[current_thread].screen_wait) &&
            ScreenWait(item.screen_pixels, args))
            value = 0;
        else
        switch (item.route)
        {
        case RouteShellProcessReference: value = ShellProcessReferenceApi(name, args); break;
        case RoutePathProbe: value = PathProbeApi(name, args); break;
        case RouteShellRegistryOrdinal: value = ShellRegistryOrdinalApi(item.route_argument, args); break;
        case RouteCabinet: value = CabinetApi(item.route_argument, args); break;
        case RouteShellCommand: value = ShellCommandApi(item.route_argument, args); break;
        case RouteShellFileInfo: value = ShellFileInfoApi(name, args); break;
        case RouteShellDragDrop: value = ShellDragDropApi(item.route_argument, args); break;
        case RouteShellIconCache: value = ShellIconCacheApi(item.route_argument, args); break;
        case RouteShellPolicy: value = ShellPolicyApi(item.route_argument, args); break;
        case RouteRegisterShell: value = RegisterShell(args[0], args[1]); break;
        case RouteCommonControl: value = CommonControlApi(item.route_argument, args); break;
        case RouteShellStopwatch: value = ShellStopwatchApi(item.route_argument); break;
        case RouteShellModule: value = ShellModuleApi(item.route_argument, args); break;
        case RouteShellOs: value = ShellOsApi(args[0]); break;
        case RouteShellThread: value = ShellThreadApi(name, args); break;
        case RouteShellRegistry: value = ShellRegistryApi(name, args); break;
        case RouteShellPathSyntax: value = ShellPathSyntaxApi(name, args); break;
        case RouteShellSearch: value = ShellSearchApi(name, args); break;
        case RoutePath: value = PathApi(name, args); break;
        case RouteCommonInit: value = CommonInitApi(name, args); break;
        case RouteDisplayDriver: value = DisplayDriverApi(name, args); break;
        case RouteOpenGL: value = OpenGLApi(item.route_argument, args); break;
        case RouteGlide: value = GlideApi(item.route_argument, args); break;
        case RouteDxDriver: value = DxDriverApi(name, args); break;
        case RouteGdi: value = GdiApi(name, args); break;
        case RouteCrt: value = CrtApi(name, args); break;
        default: value = Api(name, args); break;
        }
        threads[current_thread].tail = TailNone;
        }
    }
    if (state != Failed) StoreError();
    if (state == Failed || waiting)
        return false;
    // A callback of this call was left: the guest returned through the mark
    // of an outer call, or a handler jumped out of it, and stands above this
    // gate's frame now (see Call). Win98 never comes back into this function
    // either. Its return would overwrite the registers the guest continues
    // with, so the gate stays unfinished and the guest goes on where it
    // stands; at a Callback gate that is the next loop out.
    if (ExceptionState().abandoned_callbacks != left_before && Register(Reg::Esp) != sp)
    {
        if (ring_active)
            api_ring.Finish(ring_token, value);
        pending = 0;
        return true;
    }
    if (item.argc & 0x40000000u)
    {
        pending = 0;
        return true;
    }
    for (unsigned i = 0; i < 4; ++i)
        Register(regs[i], saved[i]);
    if (log && trace_calls && api_calls >= trace_from)
    {
        char text[160];
        snprintf(text, sizeof text, "APIRET %u %s value=%08x", api_calls, item.name, value);
        log(text);
        if (!strncmp(item.name, "GetTextExtentPoint", 18) && args[3])
        {
            unsigned size[2] = {};
            if (Read(args[3], size, 8)) { snprintf(text, sizeof text, "APISIZE %u cx=%u cy=%u chars=%u", api_calls, size[0], size[1], args[2]); log(text); }
        }
        if ((!strcmp(item.name, "GetClientRect") || !strcmp(item.name, "GetWindowRect")) && args[1])
        {
            int rect[4] = {};
            if (Read(args[1], rect, 16)) { snprintf(text, sizeof text, "APIRECT %u %d,%d,%d,%d hwnd=%08x", api_calls, rect[0], rect[1], rect[2], rect[3], args[0]); log(text); }
        }
        if (!strcmp(item.name, "GetMenuItemInfoA") && args[3])
        {
            unsigned mii[12] = {};
            if (Read(args[3], mii, 48)) { snprintf(text, sizeof text, "APIMII %u menu=%08x item=%08x bypos=%u size=%u mask=%08x type=%08x state=%08x id=%08x sub=%08x data=%08x text=%08x", api_calls, args[0], args[1], args[2], mii[0], mii[1], mii[2], mii[3], mii[4], mii[5], mii[8], mii[9]); log(text); }
        }
        if (!strcmp(item.name, "DrawTextA") && args[3])
        {
            int rect[4] = {};
            if (Read(args[3], rect, 16)) { snprintf(text, sizeof text, "APIRECT %u %d,%d,%d,%d flags=%08x", api_calls, rect[0], rect[1], rect[2], rect[3], args[4]); log(text); }
        }
    }
    if (ring_active)
        api_ring.Finish(ring_token, value);
    auto &resumed=threads[current_thread];
    if(resumed.context_redirect){
        Register(Reg::Eax,(resumed.context_redirect&2)?resumed.redirect_eax:value);
        Register(Reg::Esp,resumed.redirect_esp);Register(Reg::Eip,resumed.redirect_eip);
        resumed.context_redirect=0;
    }else{
        Register(Reg::Eax, value);
        Register(Reg::Esp, sp + 4 + ((item.argc & 0x80000000u) ? 0 : count * 4));
        Register(Reg::Eip, ret);
    }
    pending = 0;
    return true;
}
void Application::Suspend(State reason)
{
    // A handler of the fast gate list must never get here: the engine is in
    // the middle of a translated block, and the fiber this would leave could
    // not come back to it. The list is wrong if this ever fires.
    if (in_fast_gate)
    {
        in_fast_gate = false;
        Fail("Fast gate handler suspended the thread");
        return;
    }
    auto &t = threads[current_thread];
    // Owner-draw can itself yield. Keep this API's wait separate from a
    // nested rendering callback so a Sleep deadline or event is not lost.
    const unsigned block = t.block, handle = t.wait_handle;
    const auto multiwait=t.multiwait;
    const auto deadline = t.deadline;
    const unsigned message_window=t.message_window, message_minimum=t.message_minimum, message_maximum=t.message_maximum;
    t.block = 0;
    t.deadline = 0;
    // Games often poll PeekMessage forever and never enter an idle wait.
    // Present their completed GDI work at scheduler boundaries as well.
    Application *root=this;while(root->parent_process)root=root->parent_process;
    // Serve paints only at message waits when other queued work is exhausted,
    // on the owning thread. Block codes 0/1 are GetMessage, 7 covers
    // WaitMessage/MsgWaitForMultipleObjects, and 16 is WaitForInputIdle.
    // Kernel-object, loader and section waits neither paint nor dispatch sent
    // messages.
    if (reason == Idle && state != Failed && state != Exited &&
        (block == 0 || block == 1 || block == 7 || block == 16))
        User::ServeControlPaints(); // also inside an open paint,
    if (state == Failed || state == Exited)
        return;
    // A polling thread may never enter a message wait. Publish its screen
    // writes at the display cadence even if another window still owes a
    // WM_PAINT: that window must not hold back completed video frames.
    const bool animation=reason==Running&&(User::Dirty() || (vga && vga->active))&&Now()-root->last_compose>=16667ull;
    // Composition makes no guest callbacks and needs no guest thread or
    // critical-section exception. An open paint does not stall presentation:
    // drawing already reaches framebuffer memory, so even a long WM_PAINT
    // becomes visible incrementally.
    if ((reason == Idle || animation) && state != Failed && state != Exited && block != 5 && block != 6)
        ComposeIfQuiet();
    if (state == Failed || state == Exited)
        return;
    t.block = block ? block : (reason == Idle ? 1 : 0);
    t.wait_handle = handle;
    t.multiwait=multiwait;
    t.deadline = deadline;
    t.message_window=message_window; t.message_minimum=message_minimum; t.message_maximum=message_maximum;
    t.pending = pending;
    t.waiting = waiting;
    t.depth = depth;
    t.last_error = last_error;
    Check(cpu->Save(t.context));
    state = reason;
    // A thread that has the primary surface locked and waits for messages
    // lets the threads that wait for the screen draw until it comes back
    // (src/user_screen.inc, ParkScreen). In Win98 such a wait runs in
    // USER.EXE under the Win16 scheduler, which gives the Win16Mutex up; a
    // holder that sends to a thread waiting for the screen would hang
    // otherwise.
    const bool message_wait = reason == Idle && (block == 0 || block == 1 || block == 16 || block == 7 || block == 8);
    const bool parked = message_wait && User::ScreenLockActive() && User::ParkScreen(t.id, true);
    // A gate served from inside the engine may yield here with its Run open
    // (DirectGate); the engine keeps that frame for this fiber.
    void *const engine_frame = cpu->DetachFrame();
    t.fiber->Yield();
    cpu->AttachFrame(engine_frame);
    if (parked) User::ParkScreen(t.id, false);
    // Resume sent calls on this actual receiver fiber at USER message waits.
    // Kernel object/loader/critical waits intentionally do not dispatch them.
    // Message waits of every kind service sent messages: GetMessage (1), a
    // send reply wait (8), MsgWaitForMultipleObjects/WaitMessage (7) and the
    // input-idle wait (16), the way USER dispatches them inside its wait loops.
    // Sleep keeps block code 3 and must not dispatch: KERNEL32!Sleep is a
    // kernel wait, and tools/multiwait_fixture.c pins that (check 17970).
    // WaitForInputIdle belongs here because USER32!WaitForInputIdle
    // delegates to the 16-bit implementation. The whole wait runs in
    // USER.EXE under the Win16 scheduler and the waiting task keeps its
    // queue. A task that waits there still answers an inter-task send
    // (USER). Kernel object, loader and
    // critical-section waits intentionally do not dispatch them.
    if(reason==Idle&&(block==0||block==1||block==16||block==7||block==8))ProcessSentMessages();
}
// Win98 gives a runnable thread a quantum of time (VMM, 20 ms by default)
// and switches when it is used up or the thread waits; an API call that
// returns without waiting does not end it. Counting every call as a unit of
// the turn switched threads after eight calls, in the middle of what the
// program runs as one piece: Super Collapse's paint thread still had its back
// buffer selected into a memory DC when the drawing thread, woken by
// LeaveCriticalSection, tried to select it and got NULL. A call now
// costs a unit only after TurnMicroseconds of the turn; a used-up slice of
// blocks always does. The turn stays short of the Win98 quantum because the
// host composes a frame between pumps.
bool Application::Step()
{
    // The sleep a call left for the time after its return (PaceFrame).
    if (const unsigned long long until = threads[current_thread].sleep_after_call)
    {
        auto &thread = threads[current_thread];
        thread.sleep_after_call = 0;
        if (Now() < until)
        {
            thread.block = 3;
            thread.deadline = until;
            Suspend(Idle);
            if (state == Failed || state == Exited || threads[current_thread].done)
                return false;
        }
    }
    const bool free_call = budget && !slice_stop && Now() - turn_start < TurnMicroseconds;
    slice_stop = false;
    if (!budget)
        Suspend(Running);
    if (state == Failed || state == Exited || threads[current_thread].done)
        return false;
    if (!free_call) --budget;
    pending = 0;
    const unsigned eip = Register(Reg::Eip);
    // uc_emu_start stops as soon as it reaches its "until" address, and that
    // address is zero here. A guest EIP of zero therefore returns success
    // without executing a single instruction, so the thread stays runnable,
    // the scheduler keeps handing it slices and the application reports
    // Running forever without any API call. Win98 does not run there either:
    // the first page is reserved, and a call through a null pointer raises
    // 0xC0000005 at once. Report that stop instead of spinning.
    // A 16-bit segment supplies the base, so an offset of zero is an ordinary
    // entry point there: the first procedure of a code segment sits
    // and WINVER's dialog procedure is exactly that. The stop address moves
    // out of the way for those processes instead.
    const bool segmented = Win16Code();
    const unsigned instructions = Win16InstructionSlice() ? 50000 : 0;
    if (!eip && !segmented)
    {
        char message[128];
        snprintf(message, sizeof message, "Guest execution at address 0 esp=%08x", Register(Reg::Esp));
        Fail(message);
        return false;
    }
    const unsigned until = segmented ? 0xfffffffeu : 0u;
    slice_blocks = 0;
    ArmSlice();
    ++cpu_runs;
    unsigned code = cpu->Run(eip, until, instructions);
    --cpu_runs;
    // The processor of an ARM64 process names what the result code cannot:
    // a function the signature table does not describe.
    if (code && Arm64Process())
        if (const char *detail = Arm64::ErrorDetail(*cpu))
        {
            Fail(detail);
            return false;
        }
    // The guest jumped into a page this port had not made executable yet. That
    // is no fault of the guest : hand the right over
    // and carry on at the address the engine stopped at, which is the target of
    // the jump. The loop is bounded because GrantExecute refuses a page it has
    // already granted.
    // A fetch from the token of a native window procedure is a call of that
    // procedure (EnterNativeProcedure); the run goes on in its gate.
    while ((code == CpuError::FetchProtected && GrantExecute()) ||
           ((code == CpuError::FetchUnmapped || code == CpuError::FetchProtected) &&
            (EnterNativeProcedure() || EnterExceptionGuard())))
    {
        slice_blocks = 0;
        ArmSlice();
        ++cpu_runs;
        code = cpu->Run(Register(Reg::Eip), until, instructions);
        --cpu_runs;
    }
    if (instructions && !code && !pending) slice_stop = true;
    // A stop inside a hook of the processor (an interrupt of the guest)
    // shows its error box here, where the processor has returned.
    if (fail_box_deferred && !cpu_runs && state == Failed)
    {
        const State before = fail_box_before;
        fail_box_deferred = false;
        FailBox(before);
    }
    // A processor fault is an exception of the guest, not a stop of the port.
    if (GuestFault(code))
        return state != Failed && state != Exited;
    // The exception ended the process (the error box of Win98): that
    // is its end, not a stop of the port.
    if (code && state == Exited)
        return false;
    return Check(code);
}
// A thread routine returns into the start code of its thread, and that code
// ends the thread with the routine's result: KERNEL32 hands EAX to ExitThread,
// which sends DLL_THREAD_DETACH and then signals the thread handle; the initial
// thread ends the process. Here the return address of every routine is the
// Exit gate (src/x86_threads.cpp, CreateThread). The routine can reach it from
// any loop that runs guest code, not only from Run: an exception handler that
// jumps out of RaiseException, or out of a window procedure that a native
// SendMessage or CallWindowProc called, leaves that loop behind on the host
// stack, and the guest runs on inside it until its routine returns.
// Every such loop hands a gate to Service, so the thread ends here and
// Service answers false. The loops see the ended thread and unwind to Run,
// which sends the detach notices.
bool Application::Service()
{
    if (pending == Exit)
    {
        pending = 0;
        unsigned code = Register(Reg::Eax);
        if (current_thread)
        {
            ThreadApi("ExitThread", &code);
            return false;
        }
        exit_code = code;
        TerminateModules();
        if (state != Failed) state = Exited;
        if (user_initialized) User::DropPointerLevel(process_id, 0); // USER,
        if (user_initialized) User::DestroyProcessWindows(process_id); // USER
        if (user_initialized) HandOffPointer(); //
        FreePipes();
        return false;
    }
    for (;;)
    {
        if (Dispatch())
            return true;
        if (state == Failed || state == Exited || !waiting)
            return false;
        Suspend(Idle);
        if (state == Failed || state == Exited)
            return false;
    }
}
Application::State Application::Pump(unsigned slices)
{
    Files::ServiceCachePages();
    if (!parent_process && state != Failed && state != Exited) StartCloseProgramRequest();
    PumpChildren(slices ? slices : 1);
    struct Binding{unsigned p=User::CurrentProcess(),t=User::CurrentThread();~Binding(){User::BindTask(p,t);}} binding;
    User::BindTask(process_id,ThreadId());
    if (state == Failed || state == Exited)
    {
        Win16SoundRelease(); // an ended task does not keep the speaker
        CommPump(); // nor a serial port of the host
        return state;
    }
    UpdateLaunchFeedback();
    CaretPulse();
    ScreenSaverTick();
    // MMSOUND.DRV note queues run on the mixer clock. Copy their threshold
    // status into application memory here.
    Win16SoundServe();
    // Buffers of an active camera stream (src/win16_capture.cpp).
    Win16CaptureServe();
    // Audio sources need a clock even without a host audio device. The host
    // audio thread advances an open speaker generator; otherwise advance the
    // mixer here, as for wave devices. Playing CD audio and DirectSound
    // driver buffers need the same clock.
    if (Sound::Speaker::Clocked() || CdAudio::Clocked() || DsoundVxdClocked())
        Sound::Shared().Advance(Now());
    // Apply DSOUND.DLL source changes and service due position notifications
    // for its software mixer.
    if (DsoundVxdMixing()) DsoundVxdServe();
    if (dx_sound) DxSoundServe();
    unsigned remaining = slices ? slices : 1;
    for (unsigned attempt = 0; attempt < 256 && remaining; ++attempt)
    {
        Files::ServiceCachePages();
        ScanThreadPriorities();
        unsigned index = ThreadCapacity;
        for (unsigned n = 0; n < ThreadCapacity; ++n)
        {
            unsigned i = (scheduler_cursor + n) % ThreadCapacity;
            if (Runnable(i) && (index == ThreadCapacity ||
                threads[i].control_priority.effective > threads[index].control_priority.effective))
                index = i;
        }
        if (index == ThreadCapacity)
        {
            state = Idle;
            return state;
        }
        auto &t = threads[index];
        current_thread = index;User::BindTask(process_id,t.id);
        scheduler_cursor = (index + 1) % ThreadCapacity;
        Check(cpu->Restore(t.context));
        if (state == Failed)
            return state;
        pending = t.pending;
        waiting = t.waiting;
        depth = t.depth;
        last_error = t.last_error;
        t.block = 0;
        t.deadline = 0;
        budget = remaining > 8 ? 8 : remaining;
        turn_start = Now(); slice_stop = false;
        unsigned allowance = budget;
        state = Running;
        const auto quantum_began = Now();
        t.fiber->Resume();
        AccountThreadQuantum(index, quantum_began);
        remaining -= allowance - budget;
        if (state == Failed || state == Exited)
            return state;
        if (t.fiber->Finished() && !t.done)
        {
            t.done = true;
            t.result = Register(Reg::Eax);
        }
        if (!budget && remaining == 0)
        {
            state = Running;
            return state;
        }
    }
    state = Running;
    return state;
}
// Windows draw directly into the shared screen. Render publishes these bytes
// and paints USER's own decorations; guest paint callbacks run separately on
// their owner threads. Publish when work settles, or at the display cadence
// while threads keep running. The old per-window compositor's 100/500 ms wait
// for pending paints would freeze unrelated screen writes.
// Whether any other thread of any process still has work of its own. A thread
// that only waits for messages and is runnable because the screen is dirty is
// not work: asking that question here would ask it about this composition, so
// compose_probe takes the screen out of the answer while it is being computed.
bool Application::compose_probe = false;
bool Application::ComposeQuiet()
{
    const bool saved = compose_probe;
    compose_probe = true;
    Application *root = this;
    while (root->parent_process) root = root->parent_process;
    bool quiet = !root->AnyRunnableExcept(this, current_thread);
    for (unsigned slot = 0; quiet && slot < ChildCapacity; ++slot)
    {
        bool done = false;
        if (auto *child = root->ChildApplication(slot, done); child && !done && child->AnyRunnableExcept(this, current_thread)) quiet = false;
    }
    compose_probe = saved;
    return quiet;
}
// Whether a composition would really run right now. Runnable asks before it
// wakes a thread that waits for messages: without this the thread is runnable
// again the moment ComposeIfQuiet declines, and the scheduler spins through it
// without dispatching a single call. Two processes in that state keep each
// other from ever being quiet.
bool Application::ComposeDue()
{
    if (compose_probe) return false;
    if (ComposeQuiet()) return true;
    Application *root = this;
    while (root->parent_process) root = root->parent_process;
    return Now() - root->last_compose >= 16667ull;
}
void Application::ComposeIfQuiet()
{
    Application *root = this;
    while (root->parent_process) root = root->parent_process;
    const bool quiet = ComposeQuiet();
    const auto now = Now();
    if (!quiet && now - root->last_compose < 16667ull) return;
    if (log && trace_calls && api_calls >= trace_from)
    {
        unsigned first = 0;
        const unsigned pending = User::PendingPaintWindows(&first);
        char text[120];
        snprintf(text, sizeof text, "COMPOSE %u quiet=%d pending=%u first=%08x thread=%u", api_calls, quiet ? 1 : 0, pending, first, ThreadId());
        log(text);
    }
    if (VgaScanout()) { root->last_compose = now; return; }
    // Resolve one remaining invalidation before yielding. Render does not
    // dispatch guest callbacks; controls still paint on their owning thread.
    bool painted = User::Render();
    if (painted && User::Dirty()) User::Render();
    // Applications in their own display mode often write directly through a
    // surface lock. Scanout must publish those pixels even without USER
    // painting.
    if (!painted) painted = User::ScanoutScreen();
    if (painted || quiet) root->last_compose = now;
}
void Application::Run()
{
    // A thread out of a saved state has started long ago: it goes on in its
    // routine (a native one begins its loop again, which keeps nothing on
    // the stack between two turns) or in the loop below, with the call it
    // stood in still pending (src/x86_state_machine.cpp).
    const bool resumed = threads[current_thread].state_resumed;
    threads[current_thread].state_resumed = false;
    if (resumed)
    {
        if (!current_thread && threads[0].host_frame_count && threads[0].host_frames[0].kind == HostDosUiRoot)
        { ResumeDosVm(); return; }
        if (SavedCloseProgramThread(current_thread)) { ResumeCloseProgramDialog(); return; }
        if (!depth && Register(Reg::Eip) == 0x71000f20) RunMultimediaTimerThread();
        else if (!ResumeThread()) return RunEnd();
    }
    else
    {
    // KERNEL32's system UI thread is not an application DLL thread. It must
    // remain independent of a loader lock held by a stopped application.
    if (current_thread && Register(Reg::Eip) == CloseProgramThread)
    {
        CloseProgramDialog();
        const unsigned result = 0;
        ThreadApi("ExitThread", &result);
        EndThreadStaticTls(current_thread);
        return;
    }
    if (!LockLoader()) return;
    if (!current_thread)
    {
        if (!InitializeModule(0) && state != Failed) Fail("DLL process initialization failed");
    }
    else NotifyModules(2);
    UnlockLoader();
    if (current_thread && state != Failed && state != Exited &&
        !threads[current_thread].done && threads[current_thread].crt_start_data)
        RunCrtThread();
    if(current_thread&&state!=Failed&&state!=Exited&&!threads[current_thread].done&&Register(Reg::Eip)==0x71000f10)RunShellThread();
    if(current_thread&&state!=Failed&&state!=Exited&&!threads[current_thread].done&&Register(Reg::Eip)==0x71000f20)RunMultimediaTimerThread();
    if(current_thread&&state!=Failed&&state!=Exited&&!threads[current_thread].done&&Register(Reg::Eip)==0x71000f30)RunWin16MmThread();
    }
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        if (!Step())
            break;
        if (direct_break)
        {
            direct_break = false;
            break;
        }
        if (pending == Callback)
        {
            Fail("Unexpected callback return");
            break;
        }
        if (pending && !Service())
            break;
        if (state_point_wanted && !threads[current_thread].done && state != Failed && state != Exited)
        {
            // The turn ends here, so the host gets to look at the machine.
            budget = 0;
            threads[current_thread].block = 0;
            Suspend(Running);
        }
    }
    RunEnd();
}
// The end of a thread's routine: what Run does after its loop.
void Application::RunEnd()
{
    if (current_thread && state != Failed && state != Exited)
    {
        const bool done = threads[current_thread].done;
        threads[current_thread].done = false;
        if (LockLoader())
        {
            NotifyModules(3);
            // Native MSVCRT detach follows the guest DLL notifications, so a
            // DLL can still use its PTD while its own detach callback runs.
            DetachCrtThread();
            UnlockLoader();
        }
        // Windows 98 frees a module's static TLS block right after that module's
        // DLL_THREAD_DETACH. The port frees all of them after the last
        // notification, so a detaching DLL can still read another one's block.
        EndThreadStaticTls(current_thread);
        threads[current_thread].done = done;
        if(caret.owner&&User::WindowThread(caret.owner)==ThreadId())CaretWindowDestroyed(caret.owner);
        DetachClipboardThread();
        AbandonMutexes(ThreadId());
        User::ReleaseWindowUpdateLock(process_id,ThreadId());
        User::DropScreenThread(process_id,ThreadId()); // a Lock of the primary ends with its thread
        User::DropPointerLevel(process_id,ThreadId()); // USER,
        RemoveThreadHooks(ThreadId());
        // The first thread is the process: its hooks for the session end with it.
        if (!current_thread) RemoveSessionHooks();
    }
}
unsigned Application::ButtonProcedure(unsigned h, unsigned m, unsigned wp, unsigned lp)
{
    const bool fresh = threads[current_thread].tail == TailFresh;
    const unsigned tail = TakeTail();
    // Only the parent notification is resumable. State changes and capture
    // release must finish before recording the button's zero return value.
    auto notify = [&](unsigned parent, unsigned command) {
        const bool kept = (fresh || tail) && PushHostFrame(HostButtonCommand);
        if (kept) GiveTail(TailRecord);
        Send(parent, 0x111, command, h);
        if (kept) { GiveTail(TailNone); PopHostFrame(); }
    };
    // The BUTTON class procedure behind token 0x71000030. The pushed state
    // travels as BM_SETSTATE through the window's own procedure chain, the way
    // USER's button does it: Explorer's Start button subclass intercepts that
    // message, notifies the tray on the push and swallows the release while
    // the Start menu is open. Painting stays with USER's paint pass.
    const int cx = (short)(lp & 0xffff), cy = (short)(lp >> 16);
    // USER converts BS_USERBUTTON on modern windows at NCCREATE.
    if(m==0x81&&User::ModernWindow(h)&&(User::Style(h)&15)==8)
        User::SetStyle(h,User::Style(h)&~8u);
    auto paint_now=[&]{
        if(!User::IsWindow(h)||!User::IsVisible(h))return;
        if(User::ScreenLockedElsewhere(process_id,ThreadId())){
            User::InvalidateRect(h,nullptr,false);
            return;
        }
        if(const auto dc=User::GetDC(h)){
            User::PaintButton(h,dc);
            User::DrawingChanged(dc);
            User::ReleaseDC(h,dc);
        }
    };
    if (m == 8 && User::IsButton(h) && User::ButtonTracking(h, false))
    {
        Send(h, 0xf3, 0, 0);
        if (User::ThreadCapture(User::WindowThread(h)) == h) User::CaptureOperation(9, h);
    }
    if (m == 0x215 && User::IsButton(h))
    {
        if (User::ButtonTracking(h,false)) Send(h, 0xf3, 0, 0);
        return 0;
    }
    if (m == 0x201 && User::IsButton(h))
    {
        if (!User::IsEnabled(h)) return 0;
        User::ButtonTracking(h,true);
        User::BeginControlCapture(h);
        // USER takes capture before setting focus. This also
        // applies to messages sent directly, without the input dispatcher.
        SetFocus(h);
        if(!User::IsWindow(h)||state==Failed||state==Exited)return 0;
        Send(h, 0xf3, 1, 0);
        return 0;
    }
    if (m == 0x200 && User::IsButton(h) && User::ThreadCapture(User::WindowThread(h)) == h)
    {
        Send(h, 0xf3, User::ButtonInside(h, cx, cy) ? 1 : 0, 0);
        return 0;
    }
    if (m == 0x202 && User::IsButton(h) && User::ThreadCapture(User::WindowThread(h)) == h)
    {
        const bool inside = User::ButtonInside(h, cx, cy);
        Send(h, 0xf3, 0, 0);
        // USER changes automatic checks through BM_SETCHECK
        // before releasing capture. Subclasses see the change and its paint.
        if(inside&&User::IsWindow(h)&&User::IsEnabled(h)){
            const unsigned type=User::Style(h)&15;
            if(type==3||type==6){
                const unsigned check=Send(h,0xf0,0,0)&3;
                if(User::IsWindow(h))Send(h,0xf1,type==3?!check:(check+1)%3,0);
            }else if(type==9){
                const unsigned parent=User::Parent(h);
                unsigned next=h;
                for(unsigned n=0;next&&n<User::WindowCapacity;++n){
                    if(Send(next,0x87,0,0)&0x40)Send(next,0xf1,next==h,0);
                    if(!User::IsWindow(h)||state==Failed||state==Exited)return 0;
                    next=User::NextDialogItem(parent,next,false,true);
                    if(next==h)break;
                }
            }
        }
        // USER clears the capture/mouse-tracking bits before release;
        // WM_CAPTURECHANGED must not send a second BM_SETSTATE(FALSE).
        User::ButtonTracking(h,false);
        User::CaptureOperation(9, h);
        if (inside && User::IsWindow(h) && User::IsEnabled(h)) {
            const unsigned parent=User::Parent(h);
            if(parent)notify(parent,User::ControlID(h)&65535);
        }
        return 0;
    }
    // A check box and an automatic check box take '+' and '=' to check and
    // '-' to clear. WM_GETDLGCODE asks for them as characters when the MSG of
    // IsDialogMessage is a WM_CHAR with one of them; WM_CHAR
    // does nothing when the check already stands so, sets an automatic check
    // box itself and tells the parent BN_CLICKED.
    const unsigned kind = User::IsButton(h) ? (User::Style(h) & 15) : 0;
    if(m==7||m==8){
        paint_now();
        if(!User::IsWindow(h)||state==Failed||state==Exited)return 0;
        const unsigned parent=User::Parent(h),id=User::ControlID(h)&65535;
        if(parent&&(User::Style(h)&0x4000))Send(parent,0x111,id|((m==7?6u:7u)<<16),h);
        // USER: focusing an unchecked radio not in its mouse
        // capture transition notifies BN_CLICKED, without changing the check.
        if(m==7&&(kind==4||kind==9)&&User::IsWindow(h)&&
           !(User::ControlMessage(h,0xf2,0,0)&0x63)&&parent)
            Send(parent,0x111,id,h);
        return 0;
    }
    if(m==0xf1){
        const unsigned previous=User::ControlMessage(h,0xf0,0,0);
        const unsigned result=User::ControlMessage(h,m,wp,lp);
        if(previous!=User::ControlMessage(h,0xf0,0,0))paint_now();
        return result;
    }
    if(m==0xf3){const unsigned result=User::ControlMessage(h,m,wp,lp);paint_now();return result;}
    if(m==0x30){User::ControlMessage(h,m,wp,lp);if(lp&&User::IsVisible(h))User::InvalidateRect(h,nullptr,true);return 0;}
    const bool sign = wp == '+' || wp == '=' || wp == '-';
    if (m == 0x87 && (kind == 2 || kind == 3) && sign && lp)
    {
        unsigned msg[4] = {};
        if (Read(lp, msg, sizeof msg) && msg[1] == 0x102)
            return 0x2080;
    }
    if (m == 0x102 && (kind == 2 || kind == 3) && sign)
    {
        const unsigned check = wp == '-' ? 0 : 1;
        if ((Send(h, 0xf0, 0, 0) & 3) == check || !User::IsWindow(h)) return 0;
        if (kind == 3) Send(h, 0xf1, check, 0);
        const unsigned parent = User::Parent(h);
        if (parent && User::IsWindow(h) && state != Failed && state != Exited)
            Send(parent, 0x111, User::ControlID(h) & 0xffffu, h);
        return 0;
    }
    if (m == 0xf5 || (m == 0x102 && wp == ' '))
    {
        const bool kept = (fresh || tail) && PushHostFrame(HostButtonCommand);
        if (kept) GiveTail(TailRecord);
        User::ControlMessage(h, m, wp, lp);
        if (kept) { GiveTail(TailNone); PopHostFrame(); }
        return 0;
    }
    if (m == 0x30 || m == 0x31 || m == 0x87 || (m >= 0xf0 && m <= 0xf7) || (m == 0x102 && wp == ' '))
        return User::ControlMessage(h, m, wp, lp);
    if (m == 0xf) {
        User::Paint paint;
        const auto dc=wp?wp:BeginPaintWindow(h,paint);
        if(dc){User::PaintButton(h,dc);User::DrawingChanged(dc);if(!wp)User::EndPaint(h,paint);}
        return 0;
    }
    if (m == 0x14) {
        // USER only erases BS_OWNERDRAW, using the parent's
        // CTLCOLOR brush. Parentless buttons have no erase recipient.
        if(kind==11&&wp&&User::Parent(h)){
            Gdi::RECT area{};User::GetClientRect(h,&area);
            const unsigned brush=Send(User::Parent(h),0x135,wp,h);
            if(User::IsWindow(h)&&brush)Gdi::FillRect(wp,&area,brush);
        }
        return 1;
    }
    return DefaultWindowMessage(h, m, wp, lp);
}
unsigned Application::Call(unsigned procedure, const unsigned *arguments, unsigned count)
{
    return Call(procedure, arguments, count, nullptr);
}
unsigned Application::Call(unsigned procedure, const unsigned *arguments, unsigned count, CallRegisters *registers, bool *frame_abandoned)
{
    if (frame_abandoned) *frame_abandoned = false;
    // Whether this callback is the last thing its caller does (GiveTail).
    const unsigned tail = TakeTail();
    if(User::CurrentProcess()&&User::CurrentProcess()!=process_id){last_error=50;return 0;}
    // A window procedure of the 16-bit side. PLAN.md step 9, src/win16_user.cpp.
    if (Win16IsMark(procedure) && count == 4)
    {
        GiveTail(tail);
        return Win16Deliver(procedure, arguments[0], arguments[1], arguments[2], arguments[3]);
    }
    // A hook procedure of the 16-bit side: code, wParam, lParam.
    if (Win16IsMark(procedure) && count == 3)
        return Win16HookCallback(procedure, arguments[0], arguments[1], arguments[2]);
    // An AbortProc(hdc, code) of the 16-bit side, set with GDI.381 or the
    // escape SETABORTPROC; QUERYABORT calls it.
    if (Win16IsMark(procedure) && count == 2)
    {
        const unsigned push[] = {arguments[0] & 0xffff, arguments[1] & 0xffff};
        return Win16CallPascal(Win16MarkPointer(procedure), push, 2, ~0u) & 0xffff;
    }
    if(procedure==0x71000006&&count==4)return ComboBoxMessage(arguments[0],arguments[1],arguments[2],arguments[3]);
    if(procedure==0x71000102&&count==4)return TooltipMessage(arguments[0],arguments[1],arguments[2],arguments[3]);
    if(procedure==0x71000108&&count==4)return TrackbarMessage(arguments[0],arguments[1],arguments[2],arguments[3]);
    if(procedure==0x71000109&&count==4)return UpDownMessage(arguments[0],arguments[1],arguments[2],arguments[3]);
    if(procedure==0x71000189&&count==4)return UpDownBuddyMessage(arguments[0],arguments[1],arguments[2],arguments[3]);
    if(procedure==0x71000106&&count==4)return TabMessage(arguments[0],arguments[1],arguments[2],arguments[3]);
    if(procedure==0x71000100&&count==4)return ToolbarMessage(arguments[0],arguments[1],arguments[2],arguments[3]);
    if (UnsupportedCommonProcedure(procedure)) return 0;
    if (++depth > 32)
    {
        Fail("Window callback nesting exceeded");
        --depth;
        return 0;
    }
    // The 16-bit code of a task is on the CPU and the procedure is flat: a
    // hook or window procedure out of a Win32 DLL in the process of a 16-bit
    // program (a hook DLL of the session). It runs on the Win32 stack
    // of the thread, as the generic thunks do; the state of the task comes
    // back whole afterwards. Nothing happens in a flat context.
    struct TaskFlat
    {
        Application &app;
        CpuContext *saved = nullptr;
        unsigned top = 0;
        bool entered;
        explicit TaskFlat(Application &a) : app(a) { entered = app.Win16FlatEnter(saved, top); }
        ~TaskFlat() { app.Win16FlatLeave(saved, top); }
    } task_flat(*this);
    if (!task_flat.entered)
    {
        --depth;
        return 0;
    }
    // The buffer of this frame belongs to this thread and this depth and stays
    // allocated for the life of the thread. The pair is unique while the frame
    // lives: the depth counter is per thread, and a suspended callback keeps
    // its depth until it returns.
    CpuContext *context = nullptr;
    if (!Check(cpu->NewContext(context)))
    {
        --depth;
        return 0;
    }
    Check(cpu->Save(context));
    // 16-bit code of a hosted library is on the CPU (a printer driver called
    // a gate that calls back into 32-bit code): run the callback on the flat
    // context the hosted call came from. src/x86_win16_hosted.cpp.
    struct HostedFlat
    {
        Application &app;
        unsigned low = 0;
        bool switched = false;
        explicit HostedFlat(Application &a) : app(a) { switched = app.Win16HostedFlat(low); }
        ~HostedFlat() { if (switched) app.Win16HostedResume(low); }
    } hosted_flat(*this);
    // x86 exception handlers track their dispatcher frames separately.
    // ARM64 can also leave a system callback through the SEH walker.
    const bool handler = ExceptionState().calling_exception_handler && ExceptionState().handler_frames < 32;
    ExceptionState().calling_exception_handler = false;
    const unsigned frame_slot = ExceptionState().handler_frames;
    unsigned old_pending = pending;
    bool old_waiting = waiting;
    const unsigned old_window=threads[current_thread].message_window, old_minimum=threads[current_thread].message_minimum, old_maximum=threads[current_thread].message_maximum;
    waiting = false;
    unsigned sp = Register(Reg::Esp) - (count + 1) * 4 - 32;
    Write32(sp, Callback);
    Store(sp + 4, arguments, count * 4);
    Register(Reg::Esp, sp);
    Register(Reg::Eip, procedure);
    if (registers)
    {
        // A 32-bit target of a 16-to-32 flat thunk (C16ThkSL01) reads the
        // 16-bit arguments through EBX and leaves with `mov esp,ebp / ret`.
        Register(Reg::Ebx, registers->ebx);
        if (registers->frame)
            Register(Reg::Ebp, sp);
    }
    if (handler)
        ExceptionState().handler_frame[ExceptionState().handler_frames++] = sp;
    // A saved state takes this frame up again only when nothing of it is
    // left to do but to put the registers back (Application::ResumeCall).
    const bool plain = !registers && !handler && !task_flat.saved && !hosted_flat.switched && !Win16Process();
    threads[current_thread].call_marks[depth] = {sp, count * 4, old_pending, context, plain ? tail : 0u,
                                                 threads[current_thread].host_frame_count, 0, 0,
                                                 reinterpret_cast<unsigned long long>(__builtin_frame_address(0))};
    ++callbacks;
    bool returned = false, abandoned = false, left = false;
    const unsigned initial_calls = api_calls;
    while (state != Failed && state != Exited && !threads[current_thread].done)
    {
        if (!Step())
            break;
        // ARM64's unwinder stops at a discarded callback boundary before
        // running the outer continuation. Leave this native frame first.
        // A native procedure can already stand on Callback while Service
        // has cleared pending; its next Step still has to consume the mark.
        if (Arm64Process() && Register(Reg::Esp) > sp && pending != Callback && Register(Reg::Eip) != Callback)
        {
            abandoned = true;
            break;
        }
        if (direct_break)
        {
            direct_break = false;
            break;
        }
        if (pending == Callback)
        {
            // The mark of an outer call: the guest has left this frame. An
            // exception handler that jumps out of RaiseException never comes
            // back to its dispatcher, and the code it continues in can return
            // through the mark of a window procedure or of a thread routine
            // that called it. Win98 leaves the kernel frames below the handler
            // on the stack; here this loop ends without restoring anything,
            // and the gates between it and the owner of the mark are not
            // finished (Dispatch). The owner meets its mark again.
            if (OuterCallMark(Register(Reg::Esp)))
            {
                abandoned = left = true;
                break;
            }
            returned = true;
            break;
        }
        if (pending && !Service())
            break;
        // The guest popped past the return mark this call put on its stack
        // without coming back through it. Win98 has the same case: an
        // exception handler that leaves through RtlUnwind and a non-local
        // jump never returns to the dispatcher, and the kernel frame that
        // called it is simply left behind on the same stack. Here the frame is
        // a loop of the host, so it has to end by itself; otherwise every such
        // handler keeps a callback level and the run stops at the nesting
        // limit after 32 of them. The guest state stays as it is: it
        // belongs to the path the handler continued on, not to the call this
        // frame made.
        if ((handler && (ExceptionState().abandon_from <= frame_slot || Register(Reg::Esp) > sp)) ||
            (Arm64Process() && Register(Reg::Esp) > sp && pending != Callback && Register(Reg::Eip) != Callback))
        {
            abandoned = true;
            break;
        }
    }
    if (handler && ExceptionState().handler_frames > frame_slot)
        ExceptionState().handler_frames = frame_slot;
    if (frame_abandoned) *frame_abandoned = abandoned;
    if (abandoned)
    {
        ++ExceptionState().abandoned_callbacks;
        if (log)
        {
            char text[160];
            snprintf(text, sizeof text,
                     "%s procedure=%08x eip=%08x esp=%08x mark=%08x call=%u",
                     left ? "Callback frame left through an outer mark" : "SEH handler frame abandoned",
                     procedure, Register(Reg::Eip), Register(Reg::Esp), sp, api_calls);
            log(text);
        }
        if (ExceptionState().abandon_from >= frame_slot)
            ExceptionState().abandon_from = ~0u;
    }
    unsigned result = Register(Reg::Eax);
    if (registers)
    {
        registers->ecx = Register(Reg::Ecx);
        registers->edx = Register(Reg::Edx);
    }
    // The guest may have set the error word itself on the way.
    if (state != Failed && state != Exited) LoadError();
    if (!returned && !abandoned && state != Failed && state != Exited && !threads[current_thread].done)
    {
        char reason[256];
        snprintf(reason, sizeof reason, "Callback interrupted procedure=%08x eip=%08x thread=%u calls=%u",
                 procedure, Register(Reg::Eip), ThreadId(), api_calls - initial_calls);
        Fail(reason);
    }
    if (!abandoned)
        Check(cpu->Restore(context));
    cpu->FreeContext(context);
    // An abandoned frame leaves no gate behind. Restoring the gate this call
    // was made from would run its service a second time, against a guest that
    // has already moved on.
    pending = abandoned ? 0 : old_pending;
    waiting = old_waiting;
    threads[current_thread].message_window=old_window; threads[current_thread].message_minimum=old_minimum; threads[current_thread].message_maximum=old_maximum;
    threads[current_thread].call_marks[depth] = {};
    --depth;
    return result;
}
// Whether a return through the Callback gate with this stack pointer is the
// return of an outer open Call of the running thread: the mark sits below the
// stack pointer by the return address and at most the argument bytes the
// callee pops. A callee that pops more than its arguments matches no mark and
// stays the return of the innermost call, as before.
bool Application::OuterCallMark(unsigned esp) const
{
    const auto &marks = threads[current_thread].call_marks;
    if (depth >= 33)
        return false;
    const auto &own = marks[depth];
    if (own.sp && esp - own.sp - 4 <= own.bytes)
        return false;
    for (unsigned d = depth; d-- > 1;)
        if (marks[d].sp && esp - marks[d].sp - 4 <= marks[d].bytes)
            return true;
    return false;
}
int Application::EditWordBreak(unsigned procedure,const char*text,unsigned current,unsigned length,unsigned code)
{
    if(Win16IsMark(procedure))return Win16WordBreakCallback(procedure,text,current,length,code);
    if(!procedure||!text||length>65535)return int(current);
    const unsigned copy=Allocate(length+1);
    if(!copy)return int(current);
    Store(copy,text,length);const unsigned char zero=0;Store(copy+length,&zero,1);
    const unsigned arguments[]={copy,current,length,code};
    const int result=(int)Call(procedure,arguments,4);
    Free(copy);
    return result;
}
// Whether the error box of an unhandled exception of this process is open and
// hwnd is not part of it (src/x86_fault_box.inc).
bool FaultBoxSilences(const Application *app, unsigned hwnd);
unsigned Application::CallWindow(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    if((User::WindowProcess(hwnd)&&User::WindowProcess(hwnd)!=process_id)||(User::CurrentProcess()&&User::CurrentProcess()!=process_id)){last_error=50;return 0;}
    // A program that stands in the error box of Win98 runs no window
    // procedure any more: its windows keep their last picture until Close
    // ends it. A procedure that ran here faulted again on the next message
    // and opened a box for each.
    // What USER needs of the window (the frame, giving up the activation)
    // comes from the default procedure.
    const unsigned tail = TakeTail();
    if (FaultBoxSilences(this, hwnd))
    {
        if (message == 0xf) { User::ValidateRect(hwnd, nullptr); return 0; }
        if (message == 0x14) return 0;
        return DefaultWindowMessage(hwnd, message, wp, lp);
    }
    RelayTooltipInput(hwnd,message,wp,lp);
    if (dispatching_posted) dispatching_posted = false; // only the dispatched call itself, not what it sends
    else { CallWndProcHook(hwnd, message, wp, lp); if (state == Failed || state == Exited) return 0; }
    unsigned procedure = User::Procedure(hwnd), arguments[] = {hwnd, message, wp, lp};
    if((procedure>=0x71000001&&procedure<=0x71000004)||procedure==0x71000006||procedure==0x7100000a)return CommonMessage(hwnd,message,wp,lp,procedure);
    auto *dialog = FindDialog(hwnd);
    // The current window procedure owns dispatch, including an installed guest
    // subclass. The dialog procedure runs only through the default dialog proc.
    if (dialog && (!procedure || procedure == 0x71000020))
    {
        GiveTail(tail);
        return DialogProcedure(hwnd, message, wp, lp);
    }
    if (procedure == 0x71000030 || ((!procedure || procedure == 0x71000020) && User::IsButton(hwnd))) { GiveTail(tail); return ButtonProcedure(hwnd,message,wp,lp); }
    if (procedure == 0x71000031) { User::AdoptLabel(hwnd); return StaticProcedure(hwnd,message,wp,lp); }
    if (procedure == 0x71000020) return User::IsLabel(hwnd) ? StaticProcedure(hwnd,message,wp,lp) : DefaultWindowMessage(hwnd,message,wp,lp);
    if(procedure==0x71000021)return Edit(hwnd,message,wp,lp);
    if(procedure==0x71000040){GiveTail(tail);return DosVmWindowMessage(hwnd,message,wp,lp);} // the tty class, src/x86_dos_vm.cpp
    if(procedure==0x71000046)return Win16ColorBoxMessage(hwnd,message,wp,lp); // COMMDLG, src/win16_choose_color.inc
    if(procedure==0x7100004b||procedure==0x7100004c)return Win16PrintFieldMessage(hwnd,message,wp,lp,procedure==0x7100004c); // COMMDLG, src/win16_print_dialog.inc
    if(procedure==MdiClientProcedure)return MdiClientMessage(hwnd,message,wp,lp);
    if(procedure==DdeFrameProcedure)return DdeFrameMessage(hwnd,message,wp,lp);
    if (procedure)
    {
        GiveTail(tail);
        return Call(procedure, arguments, 4);
    }
    return dialog ? DialogDefault(hwnd, message, wp, lp) : DefaultWindowMessage(hwnd, message, wp, lp);
}
void Application::EnqueueInput(const User::InputPacket &p)
{
    if(auto*target=static_cast<Application*>(User::InputContext(p));target&&target!=this){target->EnqueueInput(p);return;}
    if (state == Failed || state == Exited)
        return;
    // The cursor belongs to the interactive session, but consumers such as
    // mouse_event and tooltip timers also keep this process-local snapshot.
    // A packet forwarded from the shell must update the receiving process just
    // as a packet injected directly into that process does.
    if (p.kind == 2) { mouse_x = p.x; mouse_y = p.y; }
    // Consecutive motion samples have no intervening button/key transition.
    if (input_count && p.kind == 2 && !p.changed)
    {
        auto &last = input_packets[(input_head + input_count - 1) % 256];
        if (last.kind == 2 && !last.changed && last.buttons == p.buttons)
        {
            const bool moved = last.moved;
            last = p;
            last.moved |= moved;
            return;
        }
    }
    if (input_count == 256)
    {
        Fail("Native input queue capacity exceeded");
        return;
    }
    input_packets[(input_head + input_count++) % 256] = p;
}
// The thread that routes this process's hardware input. Win98 hands a hardware
// event to the queue of the thread that owns the target window and wakes it.
// Native USER has one queue per process, so one thread routes: the owner of
// the process's foreground window, otherwise the last such owner, otherwise
// the first thread. Allegro programs create their window on a thread of its
// own that waits in MsgWaitForMultipleObjects, while the first thread never
// retrieves a message; routing only there left that window without input
// for good.
// VGA input status register one, port 3DAh: bit 3 is the vertical retrace, bit
// 0 says the beam is outside the visible area. DDRAW BAAD1B4D and BAAD18E2
// poll the retrace bit for WaitForVerticalBlank, GetVerticalBlankStatus and a
// waiting Flip; without an answer a program that paces itself by the retrace
// ran as fast as the emulation allowed (Moorhuhn drew several hundred frames a
// second and its menu animation raced). The raster is the wall clock at
// RefreshHertz with a retrace of about a millisecond, the share a CRT spends
// there. Every other port answers FFh, the open bus.
unsigned Application::GuestPortInput(Cpu &, unsigned port, unsigned size, void *context)
{
    auto &a = *static_cast<Application *>(context);
    const unsigned open_bus = size >= 4 ? 0xffffffffu : size == 2 ? 0xffffu : 0xffu;
    // Port B of the system board, 61h. Bits 0 and 1 are the gate of counter 2
    // and the amplifier of the speaker, and they read back what was written:
    // every program that switches the speaker on reads the port first and puts
    // the two bits back afterwards. Bit 4 is the
    // refresh toggle and bit 5 the output of counter 2; a program that counts a
    // delay with either of them needs both to move.
    if (port == 0x61)
    {
        const unsigned latch = Sound::Speaker::PortB();
        // 15,085 microseconds is one DRAM refresh period, the half wave of the
        // toggle on an AT.
        const unsigned refresh = (a.Now() / 15) & 1 ? 0x10u : 0u;
        return latch | refresh | (Sound::Speaker::Output(a.Now()) ? 0x20u : 0u);
    }
    if (a.vga && port >= 0x3b0 && port <= 0x3df)
    {
        const unsigned long long period = 1000000ull / RefreshHertz;
        const unsigned retrace = a.Now() % period < RetraceMicroseconds ? 9 : 0;
        unsigned value = 0;
        for (unsigned i = 0; i < size && i < 4; ++i) value |= a.vga->Input(port + i, retrace) << (i * 8);
        return value;
    }
    if (port != 0x3da) return open_bus;
    const unsigned long long period = 1000000ull / RefreshHertz, phase = a.Now() % period;
    return phase < RetraceMicroseconds ? 0x09u : 0x00u;
}
// Every OUT of the guest. VGA registers and counter 2 of the 8253 listen over
// 42h and 43h and port B over 61h. A wider access writes the bytes in
// their own order, as the bus does.
void Application::GuestPortOutput(Cpu &, unsigned port, unsigned size, unsigned value, void *context)
{
    auto &a = *static_cast<Application *>(context);
    if (a.vga)
        for (unsigned i = 0; i < size && i < 4; ++i) a.vga->Output(port + i, value >> (i * 8));
    for (unsigned i = 0; i < size && i < 4; ++i)
        Sound::Speaker::Write(port + (unsigned)i, (unsigned char)((value >> (i * 8)) & 0xff));
}
unsigned Application::InputThread()
{
    // A captured mouse belongs to its window's thread even when a different
    // thread of this process owns the foreground window. COMCTL32's taskbar
    // tabs retrieve WM_LBUTTONUP in a nested PeekMessage loop while Progman
    // is foreground; picking Progman's thread strands that release forever.
    const unsigned captured = User::ProcessCapture(process_id);
    const unsigned foreground = captured && User::WindowProcess(captured) == process_id
                                    ? captured : User::GetForegroundWindow();
    if (foreground && User::IsWindow(foreground) &&
        (!User::WindowProcess(foreground) || User::WindowProcess(foreground) == process_id))
    {
        const unsigned owner = User::WindowThread(foreground);
        for (unsigned i = 0; owner && i < ThreadCapacity; ++i)
            if (threads[i].id == owner && !threads[i].done) { input_thread = i; break; }
    }
    // The remembered thread only serves while it can take input: it runs, or
    // it waits for messages (GetMessage 1, WaitMessage and
    // MsgWaitForMultipleObjects 7). A thread that waits for an event or sleeps
    // would hold the input of the whole process back; every process of the
    // desktop then stood in its message wait and nothing reacted any more
    // (the "system hangs" reports of 21 September).
    const auto serves = [&](unsigned i) {
        if (i >= ThreadCapacity || threads[i].done || !threads[i].fiber) return false;
        return i == current_thread || threads[i].block == 0 || threads[i].block == 1 || threads[i].block == 7;
    };
    if (!serves(input_thread)) input_thread = 0;
    return input_thread;
}
// Input this process can route now: packets wait, and the next one is not a
// transition held back behind a press or key the program has not read yet
// (User::HardwareInputUnread).
bool Application::InputPending() const
{
    if (!input_count) return false;
    const auto &next = input_packets[input_head];
    const bool transition = next.kind != 2 || next.changed;
    return !transition || !User::HardwareInputUnread(process_id);
}
// The packet a retrieval with this filter routes next, as an offset from the
// head, or -1. The head goes first when it may be routed (InputPending) and
// the filter takes it; motion is routed for every retrieval. A filtered
// retrieval then looks further: USER leaves an event the filter does not take
// where it is and takes the first later button or key event that matches,
// against the capture and focus of this moment (User::InputPacketMatches).
// Win98's thread input queue holds all of them, and a PeekMessage for
// WM_KEYFIRST..WM_KEYLAST finds the key behind two unread button messages
// (tools/keys_fixture.c). The key-hold behavior applies to the
// unfiltered retrieval only, which never skips: an event a filter asks for by
// window and message is routed at once, as USER does when it is read.
int Application::MatchingInput(unsigned hwnd, unsigned minimum, unsigned maximum) const
{
    if (!input_count) return -1;
    const auto &head = input_packets[input_head];
    if (InputPending() && ((head.kind == 2 && !head.changed) || User::InputPacketMatches(head, hwnd, minimum, maximum)))
        return 0;
    if (!hwnd && !minimum && !maximum) return -1;
    for (unsigned n = 0; n < input_count; ++n)
    {
        const auto &p = input_packets[(input_head + n) % 256];
        const bool transition = p.kind == 1 || p.kind == 3 || (p.kind == 2 && p.changed);
        if (transition && User::InputPacketMatches(p, hwnd, minimum, maximum)) return int(n);
    }
    return -1;
}
// Routes one packet: the head, or a later one a filtered retrieval takes
// (MatchingInput); the packets before it keep their order.
void Application::RoutePacket(unsigned offset)
{
    if (offset >= input_count) return;
    const auto packet = input_packets[(input_head + offset) % 256];
    for (unsigned n = offset; n > 0; --n)
        input_packets[(input_head + n) % 256] = input_packets[(input_head + n - 1) % 256];
    input_head = (input_head + 1) % 256;
    --input_count;
    // Pop before calling USER: callbacks may yield or enter a nested message loop.
    User::RouteInput(packet, this,
                     [](void *p, unsigned h, bool focus) { static_cast<Application *>(p)->MouseFocus(h, focus); },
                     [](void *p, unsigned h, unsigned hit, unsigned message) {
                         return static_cast<Application *>(p)->MouseActivate(h, hit, message);
                     });
}
void Application::DrainInput()
{
    // Native USER still has one queue per process, serviced by one GUI fiber.
    if (current_thread != InputThread())
        return;
    for (unsigned n = 0; n < 256 && InputPending() && state != Failed && state != Exited; ++n)
    {
        const auto packet = input_packets[input_head];
        RoutePacket(0);
        // USER routes a hardware event when the application retrieves
        // it, against the capture and focus of that moment. A button or key
        // transition therefore ends this drain: the program handles it, and the
        // next retrieval routes what follows (a toolbar takes the capture on
        // the press and must see the release).
        if (packet.kind != 2 || packet.changed)
            break;
    }
}
void Application::RawKey(unsigned key, unsigned character, bool down, unsigned scan, bool extended)
{
    // A paused machine has no keyboard and no mouse; the
    // host settles the keys that changed meanwhile when it goes on.
    if (MachinePaused()) return;
    NoteHardwareInput(); // the screen saver's idle time
    // BlockInput keeps hardware input away; keybd_event, mouse_event and
    // SendInput pass (src/x86_user32_gates.cpp).
    if (User::InputBlocked() && !injected_input)
        return;
    auto packet=User::CaptureKey(key, character, down, scan, extended);
    // Win98 keybd_event keeps explicitly supplied left/right modifier VKs.
    // Host keyboard events still use the generic VK for window messages.
    if(injected_input && key>=0xa0 && key<=0xa5)packet.key=key;
    unsigned device_scan = scan;
    if (!device_scan) { const unsigned args[] = {key, 0}; device_scan = KeyboardGameApi("MapVirtualKeyA", args); }
    if (DinputKeyboard((device_scan & 0x7f) | (extended ? 0x80 : 0), down)) return;
    // Console processes may also own GUI windows. Route to their focused
    // window when present; a console allocation alone does not own input.
    // A focus that belongs to another process is that process's input, so the
    // packet goes to the queue below and reaches its owner; swallowing it into
    // this console would lose a keystroke Win98 delivers.
    if(console && !User::WindowProcess(User::GetFocus()))
    {
        // Registered thread hotkeys precede console delivery, including a
        // console process that owns no GUI window.
        if(!User::HotKeyInput(packet))ConsoleKey(key,character,down,scan,extended);
        return;
    }
    EnqueueInput(packet);
}
void Application::KeyboardModifiers(unsigned mask)
{
    static const unsigned vk[] = {0xa2, 0xa0, 0xa4, 0x5b, 0xa3, 0xa1, 0xa5, 0x5c};
    static const unsigned scans[] = {0x1d, 0x2a, 0x38, 0x5b, 0x1d, 0x36, 0x38, 0x5c};
    for (unsigned i = 0; i < 8; ++i)
        if (User::PhysicalKeyDown(vk[i]) != bool(mask & (1u << i)))
            RawKey(vk[i], 0, mask & (1u << i), scans[i], i == 3 || i == 4 || i == 6 || i == 7);
}
// Key types a character: USER translates the key with the layout
// (TranslateMessage), so the key and the shift state are the ones
// VkKeyScan of the installed layout names for it. ALT goes down around the
// key when the layout needs it (AltGr is CONTROL+ALT).
void Application::Key(unsigned key, unsigned character, bool shift, bool control)
{
    bool alt = false;
    const unsigned named = key;
    if (character)
    {
        const unsigned scanned = KeyScan(character);
        if (scanned != 0xffff)
        {
            key = scanned & 255;
            shift = scanned & 0x100;
            control = scanned & 0x200;
            alt = scanned & 0x400;
        }
    }
    typed_named_key = named;
    typed_key = key;
    User::SetModifiers(shift, control);
    if (alt) RawKey(0xa4, 0, true, 0x38, false);
    RawKey(key, character, true);
    if (alt) RawKey(0xa4, 0, false, 0x38, false);
}
void Application::KeyUp(unsigned key, bool shift, bool control)
{
    if (typed_key && key == typed_named_key) key = typed_key;
    User::SetModifiers(shift, control);
    RawKey(key, 0, false);
}
namespace
{
void (*host_cursor_move)(void *, int, int) = nullptr;
void *host_cursor_context = nullptr; // of the host (src/state_rules/state.rules)
} // namespace
void Application::SetHostCursorProvider(void *context, void (*move)(void *, int, int))
{
    host_cursor_context = context;
    host_cursor_move = move;
}
// A guest that moves the pointer itself. The host pointer follows, so the next
// movement the host reports is a step from here and not a distance from the
// place the user's hand happens to be.
void Application::MoveHostCursor(int x, int y)
{
    if (host_cursor_move)
        host_cursor_move(host_cursor_context, x, y);
}
void Application::MouseButtons(int x, int y, unsigned buttons, const Gdi::POINT *motion)
{
    if (MachinePaused()) return;
    NoteHardwareInput();
    if (User::InputBlocked() && !injected_input)
        return;
    // DINPUT.VXD sees every packet of VMD first. A program that holds the
    // mouse exclusively takes the movement away from Windows: the pointer
    // stays and no window gets a mouse message (src/x86_dinput_vxd.cpp).
    if (!injected_input && !dinput_warping && DinputPointer(x, y, buttons, motion)) return;
    // USER: the position the mouse driver reports stays inside the
    // ClipCursor rectangle.
    User::ConfinePointer(x, y);
    mouse_x = x;
    mouse_y = y;
    auto packet = User::CaptureMouse(x, y, buttons, SystemTime());
    // SetCursorPos also requests a mouse message at an unchanged position;
    // the newly exposed window/tool under that position must see the move.
    if (dinput_warping) packet.moved = true;
    EnqueueInput(packet);
}
void Application::Mouse(int x, int y, bool down, bool up)
{
    unsigned buttons = User::PhysicalMouseButtons();
    if (down)
        buttons |= 1;
    if (up)
        buttons &= ~1u;
    MouseButtons(x, y, buttons);
}
void Application::MouseWheel(int steps)
{
    // One USB/host notch is WHEEL_DELTA. Split large bursts rather than
    // overflowing the signed WORD carried by WM_MOUSEWHEEL.
    while (steps)
    {
        const int part = steps > 273 ? 273 : steps < -273 ? -273 : steps;
        MouseScroll(part * 120, 0, 0);
        steps -= part;
    }
}
// A step of the wheel or the trackpad in units of WHEEL_DELTA (120 a notch),
// up and down and sideways, with the flags of User::ScrollFine
// (User::RouteWheel).
void Application::MouseScroll(int units, int sideways, unsigned flags)
{
    if (MachinePaused()) return;
    NoteHardwareInput();
    if (User::InputBlocked() && !injected_input)
        return;
    // A program that reads the mouse of DirectInput gets the Z axis in the
    // same units, the small steps as they are.
    if (units && !injected_input && DinputWheel(units)) return;
    if (units > 32760) units = 32760;
    if (units < -32760) units = -32760;
    if (units || sideways)
        EnqueueInput(User::CaptureScroll(units, sideways, flags, TickCount()));
}
unsigned Application::Send(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned result = SendDispatch(hwnd, message, wp, lp);
    if (log && trace_calls && api_calls >= trace_from)
    {
        char text[160];
        snprintf(text, sizeof text, "APISEND %u hwnd=%08x msg=%08x wp=%08x lp=%08x -> %08x thread=%u", api_calls, hwnd, message, wp, lp, result, ThreadId());
        log(text);
        if ((message == 0x463 || message == 0x453) && lp) { unsigned size[2] = {}; if (Read(lp, size, 8)) { snprintf(text, sizeof text, "APISENDSIZE %u msg=%08x cx=%u cy=%u", api_calls, message, size[0], size[1]); log(text); } }
        if ((message == 0x1007 || message == 0x102e) && lp) { unsigned item[8] = {}; if (Read(lp, item, 32)) { const char *item_text = (item[0] & 1) && item[5] && item[5] != 0xffffffffu ? (virtual_memory.Access(item[5], 1, false) ? ReadString(item[5]) : "<unreadable>") : "<callback>"; snprintf(text, sizeof text, "APILVITEM %u msg=%08x mask=%08x item=%u image=%d text=%s", api_calls, message, item[0], item[1], (int)item[7], item_text); log(text); } }
        if (message == 0x4e && lp) { unsigned n[12] = {}; if (Read(lp, n, 24)) { Read(lp, n, 48); const char *answer = (int)n[2] == -150 && (n[3] & 1) && n[8] && n[8] != 0xffffffffu && virtual_memory.Access(n[8], 1, false) ? ReadString(n[8]) : ""; snprintf(text, sizeof text, "APINOTIFY %u from=%08x id=%08x code=%d extra=%08x,%08x,%08x image=%d text=%s", api_calls, n[0], n[1], (int)n[2], n[3], n[4], n[5], (int)n[10], answer); log(text); } }
    }
    return result;
}
unsigned Application::SendDispatch(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    // The tail position goes on to the procedure of the window, and only
    // on the straight way there.
    const unsigned tail = TakeTail();
    if(User::WindowProcess(hwnd)&&User::WindowProcess(hwnd)!=process_id){GiveTail(tail);return SendCrossProcess(hwnd,message,wp,lp);}
    if(User::IsWindow(hwnd)&&User::WindowThread(hwnd)&&Win98SendMessage::CrossTask(User::WindowThread(hwnd),ThreadId())){GiveTail(tail);return SendCrossThread(hwnd,message,wp,lp);}
    if(message==0xc||message==0xd||message==0xe){GiveTail(tail);return CallWindow(hwnd,message,wp,lp);}
    if(User::CommonControl(hwnd)==6||User::Procedure(hwnd)==0x71000189){GiveTail(tail);return CallWindow(hwnd,message,wp,lp);}
    // An EDIT superclass or subclass owns its window procedure just like
    // any other window. Only the native procedure may bypass CallWindow.
    if (User::IsEdit(hwnd) && (!User::Procedure(hwnd)||User::Procedure(hwnd)==0x71000021))
        return Edit(hwnd, message, wp, lp);
    GiveTail(tail);
    return CallWindow(hwnd, message, wp, lp);
}

unsigned Application::ParseMenu(const unsigned char *data, unsigned length, unsigned &position,
                                unsigned level,bool extended)
{
    if (level > 8)
    {
        Fail("Menu template nesting exceeded");
        return 0;
    }
    unsigned menu = User::CreateMenu();
    if (!menu)
    {
        Fail("Menu capacity exceeded");
        return 0;
    }
    // A menu template has no item count of its own: it ends at the item with
    // MF_END. The ceiling is the one Win98 has, the item array of a menu in the
    // 64-KB USER heap.
    for (unsigned item = 0; item < User::MenuItemCapacity; ++item)
    {
        if(extended)position=(position+3)&~3u;
        if (position > length || (extended?14u:2u)>length-position)
        {
            Fail("Truncated menu flags");
            return 0;
        }
        unsigned flags = 0;
        unsigned id = 0;
        if(extended){
            flags=(Word(data+position)|(Word(data+position+2)<<16))|(Word(data+position+4)|(Word(data+position+6)<<16));id=Word(data+position+8)|(Word(data+position+10)<<16);
            unsigned info=Word(data+position+12);flags|=(info&1?16:0)|(info&128);position+=14;
        }else{
        flags=Word(data+position);position+=2;
        if (!(flags & 16))
        {
            if (position + 2 > length)
            {
                Fail("Truncated menu ID");
                return 0;
            }
            id = Word(data + position);
            position += 2;
        }
        }
        // USER: in a standard template, flag 4 is the old help
        // marker and becomes MFT_RIGHTJUSTIFY (4000h), never MF_BITMAP.
        if (!extended && (flags & 4))
            flags = (flags & ~4u) | 0x4000;
        char label[96];
        unsigned n = 0;
        for (;;)
        {
            if (position + 2 > length || n >= 95)
            {
                Fail("Invalid menu label");
                return 0;
            }
            unsigned ch = Word(data + position);
            position += 2;
            if (!ch)
                break;
            label[n++] = ch < 256 ? ch : '?';
        }
        label[n] = 0;
        if (!n && !(flags & 16))
            flags |= 0x800;
        if(extended&&(flags&16)){position=(position+3)&~3u;if(position>length||4>length-position){Fail("Truncated menu help ID");return 0;}position+=4;}
        unsigned child = flags & 16 ? ParseMenu(data, length, position, level + 1,extended) : 0;
        if (state == Failed)
            return 0;
        // MF_END (0x80) only closes the template level; kept in the item it would
        // read as MFS_HILITE and draw the last bar item selected.
        const unsigned item_flags = flags & ~0x80u;
        if (!(extended && child ? User::AppendMenuEx(menu, item_flags, id, label, child) : User::AppendMenu(menu, item_flags, id, label, child)))
        {
            char text[224];
            snprintf(text, sizeof text,
                     "Menu template item rejected: menu %04x level %u item %u flags %04x id %04x submenu %04x "
                     "label \"%s\"; the menu holds %u of %u items and the run holds %u menus",
                     menu, level, item, item_flags, id, child, label, User::MenuItemCount(menu),
                     User::MenuItemCapacity, User::MenuCount());
            Fail(text);
            return 0;
        }
        if (flags & 128)
            return menu;
    }
    Fail("Unterminated menu template");
    return 0;
}
unsigned Application::LoadNamedMenu(const char *name, unsigned module)
{
    if (!name || !name[0]) return 0;
    if (!module) module = image.Base();
    const Pe32::Image *source = &image;
    if (module != image.Base())
    {
        const unsigned index = ModuleIndex(module);
        if (index == ModuleCapacity || !modules[index].pe) return 0;
        source = modules[index].pe;
    }
    // FindResourceA upper-cases a name before it looks; the directory keeps
    // names in capitals.
    unsigned short wide[64];
    unsigned n = 0;
    for (; name[n] && n < 63; ++n)
        wide[n] = (unsigned short)(name[n] >= 'a' && name[n] <= 'z' ? name[n] - 32 : (unsigned char)name[n]);
    Pe32::Image::ResourceKey type, key;
    type.id = 4;
    key.name = wide;
    key.length = n;
    Pe32::Image::ResourceEntry entry;
    unsigned error = 0;
    bool found = source->FindResource(type, key, 0, entry, &error);
    if (!found && error == 1815)
        found = source->FindResource(type, key, 0x409, entry, &error) || source->FindResource(type, key, ~0u, entry, &error);
    if (!found || entry.size < 4 || entry.size > 1024 * 1024)
    {
        last_error = found ? 1812 : (error ? error : 1814);
        return 0;
    }
    auto *data = static_cast<unsigned char *>(malloc(entry.size));
    if (!data || !Read(source->Base() + entry.data, data, entry.size))
    {
        free(data);
        return 0;
    }
    unsigned handle = 0;
    if (Word(data) > 1 || (Word(data) == 1 && (entry.size < 8 || Word(data + 2) < 4)))
        Fail("Unsupported menu template version");
    else
    {
        unsigned position = 4 + Word(data + 2);
        handle = ParseMenu(data, entry.size, position, 0, Word(data) == 1);
    }
    free(data);
    return handle;
}
unsigned Application::LoadMenu(unsigned id, unsigned module, bool shared)
{
    if (!module) module = image.Base();
    const Pe32::Image *source = &image;
    if (module != image.Base())
    {
        const unsigned index = ModuleIndex(module);
        if (index == ModuleCapacity || !modules[index].pe) return 0;
        source = modules[index].pe;
    }
    // DestroyWindow destroys a window's menu, so a dialog's shared entry can
    // name a menu that is gone; the next dialog then loads the resource anew.
    unsigned stale = menu_count;
    for (unsigned i = 0; shared && i < menu_count; ++i)
        if (menus[i].id == id && menus[i].module == module)
        {
            if (User::IsMenu(menus[i].handle)) return menus[i].handle;
            stale = i;
            break;
        }
    unsigned length = 0;
    const auto *data = source->Resource(4, id, &length);
    if (!data)
        return 0;
    if (length < 4 || Word(data)>1 || (Word(data)==1&&(length<8||Word(data+2)<4)))
    {
        Fail("Unsupported menu template version");
        return 0;
    }
    if (shared && stale < menu_count)
    {
        unsigned position = 4 + Word(data + 2), handle = ParseMenu(data, length, position, 0, Word(data) == 1);
        if (handle) menus[stale].handle = handle;
        return handle;
    }
    if (shared && menu_count == menu_capacity)
    {
        // Grow the list instead of refusing the resource. Win98 has no count
        // here; the ceiling is the 1.023 menus the provider hands out.
        const unsigned capacity = menu_capacity ? menu_capacity * 2 : 32;
        auto *grown = static_cast<Menu *>(realloc(menus, capacity * sizeof(Menu)));
        if (!grown)
        {
            last_error = 8;
            return 0;
        }
        menus = grown;
        menu_capacity = capacity;
    }
    unsigned position = 4 + Word(data + 2), handle = ParseMenu(data, length, position, 0,Word(data)==1);
    if (shared && handle) menus[menu_count++] = {id, handle, module};
    return handle;
}
// SPDX-License-Identifier: GPL-2.0-or-later
// Continuation of the creation notifications. Every address is a guest
// allocation; the call's host arguments are no longer needed here.
unsigned Application::ContinueCreateWindow(bool resumed)
{
    auto &t = threads[current_thread];
    const unsigned at = resumed ? t.host_resume_at++ : t.host_frame_count - 1;
    auto &frame = t.host_frames[at];
    const unsigned hwnd = frame.a;
    unsigned answer = resumed ? ResumeInner() : 0;
    auto phase = [&](unsigned n) { frame.d = (frame.d & ~255u) | n; };
    auto call = [&](unsigned n, unsigned target, unsigned message, unsigned wp, unsigned lp) {
        phase(n);
        GiveTail(TailRecord);
        return CallWindow(target, message, wp, lp);
    };
    auto finish = [&](unsigned result) { PopHostFrame(); return result; };
    auto failed = [&]() {
        if (User::IsWindow(hwnd))
        {
            answer = call(4, hwnd, 0x82, 0, 0);
            User::DestroyWindow(hwnd);
        }
        Free(frame.b);
        return finish(0);
    };
    while (state != Failed && state != Exited)
        switch (frame.d & 255u)
        {
        case 0:
            answer = call(1, hwnd, 0x81, 0, frame.b);
            break;
        case 1:
        {
            if (!answer || !User::IsWindow(hwnd)) return failed();
            Gdi::RECT window{};
            User::GetWindowRect(hwnd, &window);
            frame.c = Allocate(16);
            phase(2);
            if (frame.c)
            {
                Store(frame.c, &window, 16);
                answer = call(2, hwnd, 0x83, 0, frame.c);
            }
            break;
        }
        case 2:
            if (frame.c)
            {
                Gdi::RECT client{};
                if (Read(frame.c, &client, sizeof client)) User::ApplyClientRect(hwnd, client);
                Free(frame.c);
            }
            frame.c = 0;
            if (!User::IsWindow(hwnd)) { Free(frame.b); return finish(0); }
            answer = call(3, hwnd, 1, 0, frame.b);
            break;
        case 3:
        {
            if (answer == ~0u || !User::IsWindow(hwnd)) return failed();
            const unsigned id = frame.data[1];
            Free(frame.b);
            frame.b = id;
            phase(5);
            // The event provider can run several callbacks. It keeps no
            // continuation of its own, so this short stage stays transient.
            if (User::WinEventListening()) WinEventNotify(0x8000, hwnd, 0, 0, false);
            break;
        }
        case 4:
            if (User::IsWindow(hwnd)) User::DestroyWindow(hwnd);
            Free(frame.b);
            return finish(0);
        case 5:
        {
            if (!User::IsWindow(hwnd)) return finish(0);
            frame.data[0] = User::Style(hwnd);
            if (!(frame.data[0] & 0xc0000000u))
            {
                User::MarkSizeMove(hwnd);
                phase(7);
            }
            else
            {
                Gdi::RECT rect{};
                User::GetClientRect(hwnd, &rect);
                answer = call(6, hwnd, 5, 0, ((unsigned(rect.bottom) & 65535u) << 16) |
                                                  (unsigned(rect.right) & 65535u));
            }
            break;
        }
        case 6:
        {
            if (!User::IsWindow(hwnd)) return finish(0);
            Gdi::POINT origin{};
            User::ScreenToClient(hwnd, &origin);
            origin = {-origin.x, -origin.y};
            if (const unsigned parent = User::Parent(hwnd)) User::ScreenToClient(parent, &origin);
            answer = call(7, hwnd, 3, 0, ((unsigned(origin.y) & 65535u) << 16) |
                                              (unsigned(origin.x) & 65535u));
            break;
        }
        case 7:
        {
            if (!User::IsWindow(hwnd)) return finish(0);
            phase(8);
            InitializeCreatedWindowState(hwnd);
            if (!User::IsWindow(hwnd)) return finish(0);
            if ((frame.data[0] & 0xc0000000u) == 0x40000000u && !(User::ExtendedStyle(hwnd) & 4u))
                if (const unsigned parent = User::Parent(hwnd); parent && parent != User::GetDesktopWindow())
                    answer = call(8, parent, 0x210, 1u | ((frame.b & 65535u) << 16), hwnd);
            break;
        }
        case 8:
            if (!User::IsWindow(hwnd)) return finish(0);
            phase(9);
            FinishCreatedWindow(hwnd, frame.d & 256u, frame.d >> 16);
            break;
        case 9:
            return finish(User::IsWindow(hwnd) ? hwnd : 0);
        case 10:
        case 11:
        {
            if (!User::IsWindow(hwnd)) return finish(0);
            const bool update = (frame.d & 255u) == 10;
            phase(update ? 11 : 7);
            if (const unsigned parent = User::Parent(hwnd))
                answer = call(update ? 11 : 7, parent, 0x111,
                              (frame.b & 65535u) | (update ? 0x04000000u : 0x03000000u), hwnd);
            break;
        }
        default:
            Fail("Invalid creation phase in a saved state");
            return finish(0);
        }
    return finish(0);
}

unsigned Application::CreateWindow(const unsigned *a, bool mdi_structure)
{
    // CreateWindowEx(WS_EX_MDICHILD) synthesizes an MDICREATESTRUCT, including
    // the caller's unmodified geometry and lParam. Keep it with CREATESTRUCT
    // until all creation callbacks, including suspended callbacks, finish.
    unsigned mdi[9] = {a[1], a[2], a[10], a[4], a[5], a[6], a[7], a[3], a[11]};
    unsigned adjusted[12], ordinary[12];
    if (a[8] == ~2u && !(a[3] & 0x40000000u))
    {
        memcpy(ordinary, a, sizeof ordinary); ordinary[8] = 0; a = ordinary;
    }
    const bool wrap_mdi = !mdi_structure && (a[0] & 0x40) && MdiRecord(a[8], false);
    if (wrap_mdi)
    {
        memcpy(adjusted, a, sizeof adjusted);
        unsigned geometry[9];
        memcpy(geometry, mdi, sizeof geometry);
        MdiChildGeometry(a[8], geometry);
        if ((User::Style(a[8]) & 1) && (a[3] & 0x80000000u)) return 0;
        adjusted[3] = User::Style(a[8]) & 1 ? a[3] | 0x44000000u
            : (a[3] & ~0x80000000u) | 0x54cf0000u;
        for (unsigned i = 0; i < 4; ++i) adjusted[4+i] = geometry[3+i];
        unsigned children[User::WindowCapacity];
        adjusted[9] = MdiRecord(a[8], false)->first_id + MdiChildren(a[8], children, User::WindowCapacity);
        a = adjusted;
    }
    const unsigned tail = threads[current_thread].tail;
    const bool gate = TakeFresh();
    const char *name = "";
    if (a[1] < 65536)
    {
        char *buffer=StringSlot();if(!buffer)return 0;
        User::ClassAtomName(a[1],buffer,8192);name=buffer;
    }
    else
        name = ReadString(a[1]);
    if (log)
    {
        char trace[256];
        snprintf(trace, sizeof trace, "CreateWindow class=%s style=%08x parent=%x x=%d y=%d w=%d h=%d", name,
                 a[3], a[8], (int)a[4], (int)a[5], (int)a[6], (int)a[7]);
        log(trace);
    }
    if ((a[3] & 0xc0000000u) != 0x40000000u && a[9] && !User::IsMenu(a[9]))
    { last_error = 1401; return 0; }
    unsigned window_menu=a[9];
    if(!window_menu&&!(a[3]&0x40000000)){
        unsigned fields[12]={48};
        if(GetNativeClassInfo(a[10],name,fields)&&fields[9])window_menu=LoadClassMenu(fields[9],a[10]);
    }
    // USER clears WS_VISIBLE for the creation and shows the window at
    // the end through ShowWindow; the procedure sees the bit only from then.
    const bool show = a[3] & 0x10000000;
    char mdi_class_name[8192];
    if (show && (a[0] & 0x40))
        if (const auto *record = MdiRecord(a[8], false))
        {
            // USER restores the previous maximized child
            // before allocating the new child. Only a new maximized child
            // brackets that restoration with WM_SETREDRAW.
            const unsigned old = record->maximized;
            if (User::IsWindow(old))
            {
                // Nested guest callbacks may reuse ReadString's slots.
                snprintf(mdi_class_name, sizeof mdi_class_name, "%s", name);
                name = mdi_class_name;
                const bool redraw = a[3] & 0x01000000u;
                if (redraw) Send(old, 0xb, 0, 0);
                // supplies visible=1 even while WM_SETREDRAW has
                // temporarily cleared WS_VISIBLE; do not show it early.
                if (User::IsWindow(old) && !CbtEvent(1, old, 1) && User::IsWindow(old))
                    ShowTransition(old, 1, User::Style(old), true);
                if (redraw && User::IsWindow(old)) Send(old, 0xb, 1, 0);
                if (state == Failed || state == Exited || !User::IsWindow(a[8])) return 0;
            }
        }
    unsigned hwnd = User::CreateWindow(name, ReadString(a[2]), a[3] & ~0x10000000u, (int)a[4], (int)a[5], (int)a[6],
                                       (int)a[7], a[8], window_menu, a[11], ThreadId(),a[10]);
    if (!hwnd)
    {
        if(window_menu&&!a[9])User::DestroyMenu(window_menu);
        last_error = User::CreateWindowError();
        return 0;
    }
    if(window_menu&&!a[9])User::OwnClassMenu(hwnd,window_menu);
    if (!a[8])
        main_window = hwnd;
    User::SetExtendedStyle(hwnd,a[0]);
    // USER, WM_NCCREATE of STATIC: SS_SUNKEN, SS_ETCHEDHORZ and
    // SS_ETCHEDVERT carry WS_EX_STATICEDGE (the separator of a wizard).
    if (User::IsLabel(hwnd) && ((a[3] & 0x1000) || (a[3] & 0x1f) == 0x10 || (a[3] & 0x1f) == 0x11))
        User::SetExtendedStyle(hwnd, a[0] | 0x20000);
    unsigned create_values[] = {a[11], a[10], a[9], a[8], a[7], a[6], a[5], a[4], a[3], a[2], a[1], a[0]};
    create_values[8] = User::Style(hwnd) | (a[3] & 0x10000000u);
    const bool edge = (a[0] & 1) || (!(a[0] & 0x20000u) && (a[3] & 0x00440000u));
    create_values[11] = (a[0] & ~0x100u) | (edge ? 0x100u : 0u);
    for (unsigned i = 4; i <= 7; ++i)
        if (create_values[i] != 0x80000000u) create_values[i] = unsigned(short(create_values[i]));
    // For an ordinary child/popup Win98 keeps x/cx's CW_USEDEFAULT marker
    // in CREATESTRUCT, while the paired y/cy fields have already become zero.
    if ((a[3] & 0xc0000000u) && !wrap_mdi && !mdi_structure)
    {
        if (a[4] == 0x80000000u) create_values[6] = 0;
        if (a[6] == 0x80000000u) create_values[4] = 0;
    }
    unsigned create = Allocate(sizeof create_values + (wrap_mdi ? sizeof mdi : 0));
    if(!create){User::DestroyWindow(hwnd);return 0;}
    if (wrap_mdi)
    {
        create_values[0] = create + sizeof create_values;
        Store(create_values[0], mdi, sizeof mdi);
    }
    Store(create, create_values, sizeof create_values);
    if(!CbtCreate(hwnd,create))
    {
        // USER -> 1E1A sends WM_NCDESTROY after creation veto,
        // without a second HCBT_DESTROYWND or WM_DESTROY notification.
        if(User::IsWindow(hwnd))CbtDestroyWindow(hwnd,true);
        Free(create);return 0;
    }
    // The CBT creation hook precedes WM_GETMINMAXINFO. Its geometry is
    // constrained afterwards; a veto never enters the min/max callback.
    if ((a[3] & 0x00040000u) || !(a[3] & 0xc0000000u))
    {
        Gdi::RECT bounds{};
        User::SwapCreationBounds(hwnd, bounds);
        Win98MinMax::Info limits{};
        if (!QueryMinMax(hwnd, limits)) {
            if (User::IsWindow(hwnd)) User::DestroyWindow(hwnd);
            Free(create);return 0;
        }
        const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
        auto constrain = [](int value, int minimum, int maximum) {
            if (maximum > 0 && value > maximum) value = maximum;
            if (value < minimum) value = minimum;
            return value;
        };
        bounds.right = bounds.left + constrain(width, limits.min_track.x, limits.max_track.x);
        bounds.bottom = bounds.top + constrain(height, limits.min_track.y, limits.max_track.y);
        User::SwapCreationBounds(hwnd, bounds);
    }
    // USER materializes CS_NOCLOSE's system menu during
    // creation and removes the separator and SC_CLOSE. GetSystemMenu(TRUE)
    // later discards that copy; its replacement is the ordinary menu even
    // if the class still has CS_NOCLOSE.
    if ((User::Style(hwnd) & 0x80000u) && (User::ClassAccess(hwnd, -26, 0, 2) & 0x200u))
    {
        const unsigned arguments[] = {hwnd, 0};
        if (const unsigned menu = WindowApi("GetSystemMenu", arguments))
        {
            User::RemoveMenu(menu, 5, true, false);
            User::RemoveMenu(menu, 0xf060, false, false);
        }
    }
    // Win98 retains WINDOWEDGE on captioned windows even with STATICEDGE.
    if ((a[3] & 0xc00000u) == 0xc00000u)
        User::SetStyleWords(hwnd, User::ExtendedStyle(hwnd) | 0x100u, true);
    // The CBT hook sees the normalized window style. Creation messages
    // retain the caller's style, except when that hook explicitly changed it.
    if (!wrap_mdi && !mdi_structure && Read32(create + 32) == create_values[8])
        Write32(create + 32, a[3]);
    // the show command is SW_SHOW, or the y argument when an
    // overlapped window's x is CW_USEDEFAULT (0x80000000 on the Win32 side)
    // and y is not.
    unsigned show_command = 5;
    if (!(a[3] & 0xc0000000) && a[4] == 0x80000000u && a[5] != 0x80000000u)
        show_command = a[5] & 0xffff;
    if (User::IsEdit(hwnd))
    {
        // The built-in edit has already initialized its storage, but still
        // follows USER's parent notification and visibility stages.
        Free(create);
        const bool notify_text = !(User::Style(hwnd) & 4u) && User::TextLength(hwnd);
        if ((gate || tail == TailRecord) && PushHostFrame(HostCreateWindow, hwnd, a[9], 0,
                (notify_text ? 10u : 7u) | (show ? 256u : 0u) | (show_command << 16)))
        {
            threads[current_thread].host_frames[threads[current_thread].host_frame_count - 1].data[0] = User::Style(hwnd);
            return ContinueCreateWindow(false);
        }
        if (notify_text)
        {
            const unsigned codes[] = {0x04000000u, 0x03000000u};
            for (const unsigned code : codes)
                if (User::IsWindow(hwnd))
                    if (const unsigned parent = User::Parent(hwnd))
                        CallWindow(parent, 0x111, (a[9] & 65535u) | code, hwnd);
        }
        InitializeCreatedWindowState(hwnd);
        if (!User::IsWindow(hwnd)) return 0;
        if ((User::Style(hwnd) & 0xc0000000u) == 0x40000000u && !(User::ExtendedStyle(hwnd) & 4u))
            if (const unsigned parent = User::Parent(hwnd); parent && parent != User::GetDesktopWindow())
                CallWindow(parent, 0x210, 1u | ((a[9] & 65535u) << 16), hwnd);
        if (User::IsWindow(hwnd)) FinishCreatedWindow(hwnd, show, show_command);
        return User::IsWindow(hwnd) ? hwnd : 0;
    }
    if ((gate || tail == TailRecord) && PushHostFrame(HostCreateWindow, hwnd, create, 0,
                                                     (show ? 256u : 0u) | (show_command << 16)))
    {
        // WM_PARENTNOTIFY uses the original child id, even if a callback
        // changed a field of the CREATESTRUCT handed to it.
        threads[current_thread].host_frames[threads[current_thread].host_frame_count - 1].data[1] = a[9];
        return ContinueCreateWindow(false);
    }
    if (!CallWindow(hwnd, 0x81, 0, create) || !User::IsWindow(hwnd))
    {
        if (User::IsWindow(hwnd)) { CallWindow(hwnd, 0x82); User::DestroyWindow(hwnd); }
        Free(create);
        return 0;
    }
    {
        // USER: WM_NCCALCSIZE with the window rectangle before
        // WM_CREATE; the client comes from the answer.
        Gdi::RECT window = {};
        User::GetWindowRect(hwnd, &window);
        const unsigned block = Allocate(16);
        if (block)
        {
            Store(block, &window, 16);
            CallWindow(hwnd, 0x83, 0, block);
            if (Read(block, &window, 16)) User::ApplyClientRect(hwnd, window);
            Free(block);
        }
        if (!User::IsWindow(hwnd)) { Free(create); return 0; }
    }
    if (CallWindow(hwnd, 1, 0, create) == 0xffffffff || !User::IsWindow(hwnd))
    {
        if (User::IsWindow(hwnd)) { CallWindow(hwnd, 0x82); User::DestroyWindow(hwnd); }
        Free(create);
        return 0;
    }
    Free(create);
    // EVENT_OBJECT_CREATE once WM_CREATE has let the window live.
    if (User::WinEventListening())
    {
        WinEventNotify(0x8000, hwnd, 0, 0, false);
        if (!User::IsWindow(hwnd)) return 0;
    }
    const unsigned style = User::Style(hwnd);
    if (!(style & 0xc0000000))
        User::MarkSizeMove(hwnd); // an overlapped window learns its size when shown
    else
    {
        // WM_SIZE, then WM_MOVE with the client origin relative
        // to the parent's client.
        Gdi::RECT rect = {};
        User::GetClientRect(hwnd, &rect);
        CallWindow(hwnd, 5, 0, (unsigned(rect.bottom) & 65535) << 16 | (unsigned(rect.right) & 65535));
        if (!User::IsWindow(hwnd)) return 0;
        Gdi::POINT origin = {0, 0};
        User::ScreenToClient(hwnd, &origin);
        origin = {-origin.x, -origin.y};
        if (const unsigned parent = User::Parent(hwnd)) User::ScreenToClient(parent, &origin);
        CallWindow(hwnd, 3, 0, (unsigned(origin.y) & 65535) << 16 | (unsigned(origin.x) & 65535));
        if (!User::IsWindow(hwnd)) return 0;
    }
    InitializeCreatedWindowState(hwnd);
    if (!User::IsWindow(hwnd)) return 0;
    // WM_PARENTNOTIFY follows the initial min/max transition, before the visibility stage, unless WS_EX_NOPARENTNOTIFY.
    if ((style & 0xc0000000) == 0x40000000 && !(User::ExtendedStyle(hwnd) & 4))
        if (const unsigned parent = User::Parent(hwnd); parent && parent != User::GetDesktopWindow())
        {
            CallWindow(parent, 0x210, 1 | ((a[9] & 65535) << 16), hwnd);
            if (!User::IsWindow(hwnd)) return 0;
        }
    FinishCreatedWindow(hwnd, show, show_command);
    return User::IsWindow(hwnd) ? hwnd : 0;
}
void Application::InitializeCreatedWindowState(unsigned hwnd)
{
    if (!(User::ExtendedStyle(hwnd) & 0x40)) User::CompleteCreation(hwnd);
    const unsigned style = User::Style(hwnd);
    // Initial minimized/maximized states also position hidden windows. Keep
    // the caller's rectangle as the normal placement before changing it.
    if (style & 0x21000000u)
    {
        const bool minimized = style & 0x20000000u;
        User::SetStyleWords(hwnd, style & ~0x21000000u, false);
        // Initial state changes notify CBT after clearing the creation style.
        // A veto leaves the normal rectangle but does not cancel creation or
        // the subsequent visibility change (native create_minmax probe).
        const bool veto = CbtEvent(1, hwnd, minimized ? 7 : 3) != 0;
        if (state == Failed || !User::IsWindow(hwnd)) return;
        if (!veto)
        {
            Gdi::RECT target{};
            const unsigned after = minimized ? User::MinimizeInsertAfter(hwnd) : 0;
            if (!User::SetShowState(hwnd, minimized ? 6 : 3, target)) return;
            if (!minimized)
            {
                Win98MinMax::Info info{};
                if (!QueryMinMax(hwnd, info)) return;
                target = {info.max_position.x, info.max_position.y,
                          info.max_position.x + info.max_size.x, info.max_position.y + info.max_size.y};
            }
            unsigned position[1][7] = {{hwnd, after, unsigned(target.left), unsigned(target.top),
                unsigned(target.right - target.left), unsigned(target.bottom - target.top),
                minimized ? (0x8130u | (after ? 0u : 4u)) : 0x8030u}};
            PositionWindows(1, position);
            if (!User::IsWindow(hwnd)) return;
        }
    }
}
void Application::FinishCreatedWindow(unsigned hwnd, bool show, unsigned command)
{
    if (show) ShowWindow(hwnd, command);
    if (User::IsWindow(hwnd) && (User::ExtendedStyle(hwnd) & 0x40))
        if (auto *record = MdiRecord(User::Parent(hwnd), false))
        {
            if (++record->created > 0x7ffe) record->created = 0;
            if (User::Style(hwnd) & 0x01000000u) record->maximized = hwnd;
            if ((User::Style(hwnd) & 0x10000000u) && User::IsEnabled(hwnd))
            {
                // Win98 publishes the menu refresh through MDICLIENT's
                // procedure, then positions the visible child with activation.
                // WM_CHILDACTIVATE invokes DefMDIChildProc and its nested
                // NOACTIVATE positioning; a direct MdiActivate skips both
                // observable calls (Wine msg.c:3065).
                const unsigned client = record->hwnd;
                Send(client, 0x234, 0, 0);
                if (!User::IsWindow(hwnd) || !User::IsWindow(client) || User::Parent(hwnd) != client) return;
                unsigned position[1][7] = {{hwnd, 0, 0, 0, 0, 0, 0x43}};
                PositionWindows(1, position);
                User::CompleteCreation(hwnd);
                // USER finishes a maximized child's creation
                // with a frame recalculation after child activation.
                if (User::IsWindow(hwnd) && (User::Style(hwnd) & 0x01000000u))
                {
                    // USER inserts the system-menu buttons only
                    // after creation's child activation has returned.
                    if (auto *current = MdiRecord(client, false)) MdiComposeMenu(*current);
                    const unsigned frame = User::Parent(client);
                    if (User::IsWindow(frame))
                    {
                        unsigned frame_position[1][7] = {{frame, 0, 0, 0, 0, 0, 0x37}};
                        PositionWindows(1, frame_position);
                    }
                }
            }
            else MdiRefreshMenu(*record);
        }
    User::CompleteCreation(hwnd);
}
unsigned Application::GetText(unsigned hwnd, unsigned output, unsigned capacity)
{
    // The staging is the size of the text the window holds, because an edit
    // control of this port holds as much as its limit allows. The
    // converted copy can be twice as long, one carriage return per line break.
    const unsigned bytes = User::TextLength(hwnd) + 1;
    auto *text = new char[bytes];
    auto *converted = new char[bytes * 2];
    User::GetWindowText(hwnd, text, bytes);
    unsigned n = 0;
    for (unsigned i = 0; text[i]; ++i)
    {
        if (User::IsEdit(hwnd) && text[i] == '\n')
            converted[n++] = '\r';
        converted[n++] = text[i];
    }
    converted[n] = 0;
    const unsigned result = PutString(output, converted, capacity);
    delete[] text;
    delete[] converted;
    return result;
}
// The keys of a dialog in the EDIT bodies of USER.EXE. Answers true when the
// key is done with and the text is not to see it.
bool Application::EditDialogKey(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    // 1 for CTRL, 2 for SHIFT, 3 for both.
    const unsigned modifiers = ((User::GetKeyState(0x11) & 0x8000u) ? 1u : 0u) |
                               ((User::GetKeyState(0x10) & 0x8000u) ? 2u : 0u);
    const unsigned style = User::Style(hwnd);
    if (!(style & 4u))
    {
        // The single-line character body has no case for TAB and beeps as
        // for every control character it does not name.
        if (message == 0x102 && wp == 9)
        {
            const unsigned type = 0;
            Api("MessageBeep", &type);
            return true;
        }
        return false;
    }
    const bool dialog = User::EditInDialog(hwnd);
    const unsigned parent = User::Parent(hwnd);
    if (message == 0x102)
    {
        // ESC is never text; in a dialog TAB is not either, and
        // ENTER only with ES_WANTRETURN, unless CTRL alone is down.
        if (wp == 27)
            return true;
        if (dialog && modifiers != 1 && (wp == 9 || (wp == 13 && !(style & 0x1000u))))
            return true;
        return false;
    }
    // WM_KEYDOWN.
    if (wp == 9)
    {
        // CTRL+TAB types a TAB even in a dialog (Windows 98 calls the character
        // body); otherwise the dialog moves the focus on, backwards with
        // SHIFT alone.
        if (modifiers == 1)
        {
            User::EditMessage(hwnd, 0x102, 9, lp);
            return true;
        }
        if (dialog && parent)
            Send(parent, 0x28, modifiers == 2 ? 1 : 0, 0);
        return dialog;
    }
    if (wp == 13)
    {
        // without ES_WANTRETURN ENTER goes to the default button
        // of the dialog. WM_NEXTDLGCTL gives it the focus; if the edit has
        // lost it, the button gets the ENTER posted.
        if (!dialog || modifiers == 1 || (style & 0x1000u) || !parent)
            return false;
        const unsigned id = Send(parent, 0x400, 0, 0) & 0xffffu;
        if (!id || state == Failed || state == Exited || !User::IsWindow(parent))
            return true;
        const unsigned button = User::GetDlgItem(parent, id);
        if (!button)
            return true;
        Send(parent, 0x28, button, 1);
        if (state != Failed && state != Exited && User::IsWindow(button) && User::GetFocus() != hwnd)
            User::PostMessage(button, 0x100, 13, 0);
        return true;
    }
    if (wp == 27 && dialog)
    {
        // ESC closes the dialog.
        if (parent)
            User::PostMessage(parent, 0x10, 0, 0);
        return true;
    }
    return false;
}
unsigned Application::Edit(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    // A superclass reaches the native edit procedure through CallWindowProc.
    // Allocate its edit state on WM_NCCREATE, as for the built-in class.
    if(message==0x81&&!User::IsEdit(hwnd)){
        unsigned create[12]={};
        if(!lp||!Read(lp,create,sizeof create)||!User::AdoptEdit(hwnd,ReadString(create[9])))return 0;
    }
    if(message==7||message==8)EditCaret(hwnd,message==7);
    if (message == 0xa1)
        return DefaultWindowMessage(hwnd, message, wp, lp);
    if (message == 0x20a)
    {
        // WM_MOUSEWHEEL. The multiline edit control of Win98 scrolls by the
        // lines of SPI_GETWHEELSCROLLLINES a notch, as EM_LINESCROLL does;
        // a single line has nothing to scroll and leaves the wheel to
        // DefWindowProc, which hands it to the parent.
        if (!(User::Style(hwnd) & 4u))
            return DefaultWindowMessage(hwnd, message, wp, lp);
        // A delta below a notch scrolls its share of the lines (the steps
        // of a trackpad are whole lines, User::RouteWheel).
        const int lines = WheelLines(hwnd, (int)(short)(wp >> 16));
        if (lines)
            Edit(hwnd, 0xb6, 0, (unsigned)-lines);
        return 0;
    }
    if (message == 0x87)
    {
        // WM_GETDLGCODE, single-line body USER: DLGC_WANTCHARS,
        // DLGC_HASSETSEL and DLGC_WANTARROWS (89h). With the MSG of
        // IsDialogMessage it adds DLGC_WANTALLKEYS (4) for Enter and Esc while
        // the popup of its dropdown combo box is down and for
        // ALT+Backspace, which undoes. The multiline body
        // answers 8Dh always; asked with a MSG it marks itself as standing in
        // a dialog and then handles Tab, Enter and Esc of the
        // dialog itself, below.
        if (User::Style(hwnd) & 4u)
        {
            if (lp)
                User::EditInDialog(hwnd, true);
            return 0x8d;
        }
        unsigned code = 0x89, msg[4] = {};
        if (lp && Read(lp, msg, sizeof msg) && !(User::Style(hwnd) & 4u))
        {
            if (msg[1] == 0x100 && (wp == 13 || wp == 27))
            {
                const unsigned parent = User::Parent(hwnd);
                char klass[16]{};
                if (parent && (User::Style(parent) & 2u) &&
                    User::GetClassName(parent, klass, sizeof klass) && !strcasecmp(klass, "COMBOBOX") &&
                    Send(parent, 0x157, 0, 0))
                    code = 0x8d;
            }
            else if (msg[1] == 0x106 && (msg[3] & 0x20000000u) && wp == 8)
                code = 0x8d;
        }
        return code;
    }
    if (ComboEditKey(hwnd, message, wp, lp)) return 1;
    if ((message == 0x100 || message == 0x102) && EditDialogKey(hwnd, message, wp, lp))
        return 1;
    if (message == 0xc)
    {
        // The text of an edit control is as large as the control lets it be,
        // so the staging is the size of the text and not a fixed buffer

        const char *source = ReadString(lp);
        if (state == Failed)
            return 0;
        auto *text = new char[strlen(source) + 1];
        Copy(text, source, strlen(source) + 1);
        Normalize(text);
        const unsigned result = User::SetWindowText(hwnd, text);
        delete[] text;
        if (result) User::EditMessage(hwnd, 0xc, 0, 0);
        return result;
    }
    if (message == 0xd)
        return GetText(hwnd, lp, wp);
    if (message == 0xe)
    {
        const unsigned bytes = User::TextLength(hwnd) + 1;
        auto *text = new char[bytes];
        User::GetWindowText(hwnd, text, bytes);
        unsigned n = strlen(text);
        for (unsigned i = 0; text[i]; ++i)
            if (text[i] == '\n')
                ++n;
        delete[] text;
        return n;
    }
    if (message == 0xbd)
    {
        EditHandle *record = nullptr;
        for (auto &entry : edit_handles)
            if (entry.hwnd == hwnd)
            {
                record = &entry;
                break;
            }
        if (!record)
            for (auto &entry : edit_handles)
                if (!entry.hwnd)
                {
                    entry.hwnd = hwnd;
                    record = &entry;
                    break;
                }
        if (!record)
        {
            Fail("EDIT buffer table full");
            return 0;
        }
        unsigned required = Edit(hwnd, 0xe, 0, 0) + 1;
        if (AllocationSize(record->memory) < required)
            record->memory = Allocate(required > 4096 ? required : 4096);
        if (!record->memory)
            return 0;
        GetText(hwnd, record->memory, required);
        return record->memory;
    }
    if (message == 0xbc)
    {
        const char *source = ReadString(wp);
        if (state == Failed)
            return 0;
        auto *text = new char[strlen(source) + 1];
        Copy(text, source, strlen(source) + 1);
        Normalize(text);
        bool result = User::SetWindowText(hwnd, text);
        delete[] text;
        for (auto &entry : edit_handles)
            if (entry.hwnd == hwnd || !entry.hwnd)
            {
                entry = {hwnd, wp};
                break;
            }
        return result;
    }
    // EM_LINEFROMCHAR and EM_LINELENGTH take a character index, EM_LINEINDEX
    // answers one. The wire counts a line break as two characters, the native
    // store as one. A line length needs no correction: both bodies of the
    // original stop before the break.
    if (message == 0xc9 || message == 0xc1 || message == 0xbb)
    {
        char text[4096];
        User::GetWindowText(hwnd, text, sizeof text);
        if (message != 0xbb)
        {
            unsigned index = wp;
            if (wp != ~0u)
            {
                unsigned wire = 0;
                index = 0;
                while (text[index])
                {
                    unsigned width = text[index] == '\n' ? 2 : 1;
                    if (wp - wire < width)
                        break;
                    wire += width;
                    ++index;
                }
            }
            return User::EditMessage(hwnd, message, index, 0);
        }
        unsigned index = User::EditMessage(hwnd, message, wp, 0), wire = index;
        if (index != ~0u)
            for (unsigned i = 0; i < index; ++i)
                if (text[i] == '\n')
                    ++wire;
        return wire;
    }
    if (message == 0xd6 || message == 0xd7)
    {
        const unsigned size = User::TextLength(hwnd) + 1;
        auto *text = new char[size];
        if (!text) return ~0u;
        User::GetWindowText(hwnd, text, size);
        unsigned answer;
        if (message == 0xd6) {
            unsigned index = 0, wire = 0;
            while (text[index] && wire < wp) {
                const unsigned bytes = text[index] == '\n' ? 2 : 1;
                if (wp - wire < bytes) break;
                wire += bytes; ++index;
            }
            answer = User::EditMessage(hwnd, message, index, lp);
        } else {
            answer = User::EditMessage(hwnd, message, wp, lp);
            unsigned index = answer & 65535, wire = index;
            for (unsigned i = 0; i < index && text[i]; ++i) if (text[i] == '\n') ++wire;
            answer = (answer & 0xffff0000u) | (wire & 65535);
        }
        delete[] text;
        return answer;
    }
    if (message == 0xb0 || message == 0xb1)
    {
        unsigned start = 0, end = 0;
        char text[4096];
        User::GetWindowText(hwnd, text, sizeof text);
        User::EditSelection(hwnd, &start, &end);
        if (message == 0xb0)
        {
            unsigned a = start, b = end;
            for (unsigned i = 0; text[i]; ++i)
                if (text[i] == '\n')
                {
                    if (i < start)
                        ++a;
                    if (i < end)
                        ++b;
                }
            if (wp)
                Write32(wp, a);
            if (lp)
                Write32(lp, b);
            return b << 16 | (a & 65535);
        }
        auto index = [&](unsigned value) {
            if (value == 0xffffffff)
                return value;
            unsigned wire = 0, native = 0;
            while (text[native] && wire < value)
            {
                wire += text[native] == '\n' ? 2 : 1;
                ++native;
            }
            return native;
        };
        bool changed = User::SetEditSelection(hwnd, index(wp), index(lp));
        if (changed && log)
        {
            unsigned first = 0, last = 0;
            User::EditSelection(hwnd, &first, &last);
            char trace[96];
            snprintf(trace, sizeof trace, "EDIT selection hwnd=%u start=%u end=%u", hwnd, first, last);
            log(trace);
        }
        return changed;
    }
    if (message == 0xb2)
    {
        Gdi::RECT rect{};
        return lp && User::GetEditFormat(hwnd, rect) && Store(lp, &rect, sizeof rect);
    }
    if (message == 0xb3 || message == 0xb4)
    {
        Gdi::RECT rect{};
        // Win98 SE ignores a NULL rectangle: the formatting rectangle stays
        // as it was (edit_default_rect contract).
        if (lp && Read(lp, &rect, sizeof rect)) User::SetEditFormat(hwnd, rect, message == 0xb3);
        return 0;
    }
    if (message == 0xc2)
    {
        char text[8192];
        Copy(text, ReadString(lp), sizeof text);
        Normalize(text);
        return User::ReplaceEditSelection(hwnd, text, strlen(text), wp != 0);
    }
    if (message == 0xc4)
    {
        if (!lp) return 0;
        unsigned short capacity = 0; if (!Read(lp, &capacity, sizeof capacity)) return 0;
        const unsigned line = wp, count = User::EditMessage(hwnd, 0xba, 0, 0);
        if (line >= count) return 0;
        const unsigned start = User::EditMessage(hwnd, 0xbb, line, 0);
        const unsigned length = User::EditMessage(hwnd, 0xc1, start, 0);
        char text[4096]; User::GetWindowText(hwnd, text, sizeof text);
        const unsigned copied = length < capacity ? length : capacity;
        return Store(lp, text + start, copied) ? copied : 0;
    }
    if (message == 0xcb)
    {
        if (!wp) return User::SetEditTabStops(hwnd, nullptr, 0);
        if (wp > 32 || !lp) return 0;
        int stops[32] = {};
        if (!Read(lp, stops, wp * sizeof *stops)) return 0;
        return User::SetEditTabStops(hwnd, stops, wp);
    }
    if (message == 0xd0 && lp && Win16Process()) lp = Win16Mark(lp);
    if (message == 0xd1) {
        const unsigned answer=User::EditMessage(hwnd,message,wp,lp);
        return Win16Process()?Win16ProcedurePointer(answer):answer;
    }
    if(message==0xc5||message==0xd0||message==0xd5)return User::EditMessage(hwnd,message,wp,lp);
    if (message == 0xc8)
    {
        if (wp)
            Fail("Unsupported EDIT line-format or limit option");
        return 0;
    }
    // In USER.EXE, an EDIT that gains or loses the focus
    // tells its parent with EN_SETFOCUS (100h) and EN_KILLFOCUS (200h), after its own caret and selection work. A ComboBox
    // runs its focus helper on EN_SETFOCUS.
    if ((message == 7 || message == 8) && (User::Style(hwnd) & 0x40000000u))
    {
        if(message==7 && !(User::Style(hwnd)&4u) && User::IsVisible(hwnd)) User::AskEditColors(hwnd);
        if(!User::IsWindow(hwnd)||state==Failed||state==Exited)return 0;
        const unsigned answer = User::EditMessage(hwnd, message, wp, lp);
        const unsigned parent = User::Parent(hwnd);
        if (parent && User::IsWindow(parent) && state != Failed && state != Exited)
            Send(parent, 0x111, (User::ControlID(hwnd) & 0xffffu) | (message == 7 ? 0x100u : 0x200u) << 16, hwnd);
        return answer;
    }
    if(message==0x201 || message==0x203) {
        const bool multiline=User::Style(hwnd)&4u;
        // The single-line press checks WS_DISABLED; the multiline press
        // and double-click handlers still select when called directly.
        if(message==0x201 && !multiline && !User::IsEnabled(hwnd))return 0;
        if(multiline || message==0x203) User::BeginControlCapture(hwnd);
        if(message==0x201)SetFocus(hwnd);
        if(!User::IsWindow(hwnd)||state==Failed||state==Exited)return 0;
        if(!multiline && message==0x201)User::BeginControlCapture(hwnd);
        if(!User::IsWindow(hwnd)||state==Failed||state==Exited)return 0;
        return User::EditMessage(hwnd,message,wp,lp);
    }
    if(message==0x202) {
        if(User::ThreadCapture(User::WindowThread(hwnd))==hwnd)User::CaptureOperation(9,hwnd);
        return 0;
    }
    // The EDIT procedure hands WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP and
    // WM_SYSCHAR to DefWindowProc, which opens the menu
    // of the window from the keyboard.
    if (message == 0x101 || message == 0x104 || message == 0x105 || message == 0x106)
    {
        const unsigned answer = User::EditMessage(hwnd, message, wp, lp);
        DefaultKeyMessage(hwnd, message, wp, lp);
        return answer;
    }
    switch (message)
    {
    case 7:
    case 8:
    case 0xb:
    case 0x30:
    case 0x31:
    case 0xb5:
    case 0xb6:
    case 0xb7:
    case 0xb8:
    case 0xb9:
    case 0xba:
    case 0xbe:
    case 0xc6:
    case 0xc7:
    case 0xcd:
    case 0xcc:
    case 0xd2:
    // EM_SETREADONLY; USER.EXE.
    case 0xcf:
    case 0xce:
    case 0x100:
    case 0x102:
    case 0x114:
    case 0x115:
    // Selection moves belong to the EDIT procedure, including sent messages.
    case 0x200:
    case 0x201:
    case 0x202:
    case 0x204:
    case 0x205:
    case 0x207:
    case 0x208:
    case 0x300:
    case 0x301:
    case 0x302:
    case 0x303:
    case 0x304:
        return User::EditMessage(hwnd, message, wp, lp);
    default:
        // The callable EDIT class now receives lifecycle and clipboard-owner
        // notifications formerly sent to the default window provider. Keep
        // generic system messages there. Only unported EM_* still stops.
        // The Windows 98 EDIT procedure
        // dispatches with a chain of compares and lets every message it does
        // not name fall through: Windows 98 reads bit 0 of byte +36h of the edit
        // record and picks the single-line or multiline body; both end in DefWindowProc.

        // There is no upper bound and no failing branch, so a message at or
        // above WM_USER belongs to DefWindowProc as well. A Delphi TMemo
        // sends its control notification 0xBE10 that way.
        if (message >= 0x400 || !(message >= 0xb0 && message <= 0xdf))
            return DefaultWindowMessage(hwnd, message, wp, lp);
        // EM_SETMARGINS/EM_GETMARGINS: the native edit keeps its fixed inset;
        // COMCTL32's ComboBoxEx sets margins on the edit it wraps.
        if (message == 0xd3 || message == 0xd4) return User::EditMessage(hwnd,message,wp,lp);
        if (message == 0xd5) return Edit(hwnd, 0xc5, wp, lp); // EM_SETLIMITTEXT is EM_LIMITTEXT
        char diagnostic[128];
        snprintf(diagnostic, sizeof diagnostic, "Unsupported EDIT message %x", message);
        Fail(diagnostic);
        return 0;
    }
}

#include "x86_scroll_family.inc"
unsigned Application::Api(ApiName name, const unsigned *a)
{
    // The first call of a Win16 gate is what the 16-bit program asked for. If
    // it draws on the screen while another thread has the primary surface
    // locked, the gate waits and is entered again (src/x86_screen_lock.cpp);
    // Dispatch does the same for a 32-bit import.
    if (win16_screen_check)
    {
        win16_screen_check = false;
        if (User::ScreenLockActive() || threads[current_thread].screen_wait)
            if (const unsigned rule = ScreenRulesEnabled() ? ScreenPixelRule(name) : 0; rule && ScreenWait(rule, a))
                return 0;
    }
    // One switch over the API ID. A case that does not
    // return leaves the switch and reaches the prefix rules and Fail below.
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    API(Neo98TouchGetCaps) {
        if (a[0] != NEO98_TOUCH_VERSION || !a[1] || a[2] < sizeof(NEO98_TOUCH_CAPS)) { last_error = 87; return 0; }
        const NEO98_TOUCH_CAPS caps = {sizeof caps, NEO98_TOUCH_VERSION, NEO98_TOUCH_MAX_CONTACTS, NEO98_TOUCH_CAP_MULTITOUCH |
            (User::HasTouchscreen() ? NEO98_TOUCH_CAP_TOUCHSCREEN_PRESENT : 0u)};
        return Store(a[1], &caps, sizeof caps);
    }
    API(Neo98TouchRegister) {
        if (a[2]) { last_error = 87; return 0; }
        if (!User::TouchRegister(a[0], a[1])) { last_error = 87; return 0; }
        return 1;
    }
    API(Neo98TouchUnregister) {
        if (!User::TouchUnregister(a[0])) { last_error = 87; return 0; }
        return 1;
    }
    API(Neo98TouchRead) {
        if (!a[1] || !a[2] || a[2] > NEO98_TOUCH_QUEUE_SIZE) { last_error = 87; return ~0u; }
        NEO98_TOUCH_EVENT events[NEO98_TOUCH_QUEUE_SIZE] = {};
        const int count = User::TouchPeek(a[0], events, a[2]);
        if (count < 0) { last_error = 5; return ~0u; }
        if (count && !Store(a[1], events, unsigned(count) * sizeof events[0])) { last_error = 998; return ~0u; }
        User::TouchConsume(a[0], unsigned(count));
        return unsigned(count);
    }
    API(__NEO98_SerialUiPageCallback)  return SerialUiPageCallback(a[1],a[2]);
    // The exports of the export contract that were still missing on 23
    // September 2026 (branch dlls), one family function per DLL.
    API(IsDialogMessage) API(TranslateAccelerator) API(GetWindowModuleFileNameW)
    API(InitSharedTable) API(WNDPROC_CALLBACK) API(GetMouseMovePoints)
        return User32CompletionApi(name, a);
    API(PlaySound)
        return WinmmCompletionApi(name, a);
    API_CASE("WINMM.ordinal_1") API_CASE("WINMM.ordinal_3") API_CASE("WINMM.ordinal_4")
    API(auxGetVolume) API(auxOutMessage) API(auxSetVolume) API(joyConfigChanged) API(joyGetThreshold)
    API(joyReleaseCapture) API(joySetCapture) API(joySetThreshold) API(mciExecute) API(mciGetCreatorTask)
    API(mciGetYieldProc) API(mciSetYieldProc) API(midiConnect) API(midiDisconnect) API(midiInAddBuffer)
    API(midiInGetID) API(midiInMessage) API(midiInPrepareHeader) API(midiInUnprepareHeader)
    API(midiOutCacheDrumPatches) API(midiOutCachePatches) API(midiOutGetID)
        return WinmmCompletionApi(name, a);
    API(waveInGetID) return WaveInApi(name, a);
    API(mmioAdvance) API(mmioFlush) API(mmioInstallIOProcA) API(mmioRenameA) API(mmioSendMessage)
    API(mmioSetBuffer) API(mmioSetInfo) API(__NEO98_MmioDosIoProc) API(__NEO98_MmioMemIoProc)
        return MmioApi(name, a);
    API(__NEO98_CryptVerifyImage) return CryptoVerifyImageGate(a);
    API(__NEO98_CryptReturnWindow) return CryptoReturnWindowGate(a);
    // Original COMCTL32 is a bare RET; class setup is part of loading.
    API(InitCommonControls)  return CommonInitApi(name,a);
    // Pipes, mailslots, VxD devices and Winsock handle services:
    // Handles of the 0x66 namespace and "\\.\" device names belong to that provider.
    // COMn behind \\.\ belongs to the serial provider, every other \\.\ name to the device family.
    API(CreateFileA)
        // The original string probe faults on NULL; the empty path has its
        // own ERROR_BAD_PATHNAME result before disposition validation.
        if(!a[0]){last_error=87;return ~0u;}
        if(!*ReadString(a[0])){last_error=161;return ~0u;}
        if(a[0]){const char *path=ReadString(a[0]);char device[8]{};
            if(strlen(path)<sizeof device){for(unsigned i=0;path[i];++i)device[i]=path[i]>='a'&&path[i]<='z'?path[i]-32:path[i];
                if(!strcmp(device,"CONIN$")||!strcmp(device,"CONOUT$"))return OpenConsoleFile(a);}}
        if(LptPort(a[0]))return LptApi(name,a);
        if(CommPort(a[0]))return CommApi(name,a);
        if(KernelPipeDeviceName(a[0]))return KernelPipesApi(name,a);
        return FileApi(name, a);
    API(CloseHandle)
    {
        unsigned result;
        if(LptHandle(a[0]))result=LptApi(name,a);
        else if((a[0]&0xff000000)==0x66000000||KernelSocketHandle(a[0]))result=KernelPipesApi(name,a);
        else if((a[0]&0xff000000)==0x67000000)result=CommApi(name,a);
        else if((a[0]&0xff000000)==0x63000000)result=PipeApi(name,a);
        else if((a[0]&0xff000000)==0x62000000)result=ConsoleApi(name,a);
        else result=files.Owns(a[0]) ? FileApi(name,a) : ThreadApi(name,a);
        // explicitly clears LastError after removing a real handle.
        // The process/thread pseudo-handles return earlier.
        if(result && a[0]!=0x7fffffffu && a[0]!=0xfffffffeu)last_error=0;
        return result;
    }
    API(ReadFile)
        if(LptHandle(a[0]))return LptApi(name,a);
        if((a[0]&0xff000000)==0x66000000||KernelSocketHandle(a[0]))return KernelPipesApi(name,a);
        if((a[0]&0xff000000)==0x67000000)return CommApi(name,a);
        if((a[0]&0xff000000)==0x63000000)return PipeApi(name,a);
        return (a[0]&0xff000000)==0x62000000?ConsoleApi(name,a):FileApi(name, a);
    API(WriteFile)
        if(LptHandle(a[0]))return LptApi(name,a);
        if((a[0]&0xff000000)==0x66000000||KernelSocketHandle(a[0]))return KernelPipesApi(name,a);
        if((a[0]&0xff000000)==0x67000000)return CommApi(name,a);
        if((a[0]&0xff000000)==0x63000000)return PipeApi(name,a);
        return (a[0]&0xff000000)==0x62000000?ConsoleApi(name,a):FileApi(name, a);
    API(GetFileType)
        if(LptHandle(a[0]))return LptApi(name,a);
        if((a[0]&0xff000000)==0x66000000||KernelSocketHandle(a[0]))return KernelPipesApi(name,a);
        if((a[0]&0xff000000)==0x67000000)return CommApi(name,a);
        if((a[0]&0xff000000)==0x63000000)return PipeApi(name,a);
        return (a[0]&0xff000000)==0x62000000?ConsoleApi(name,a):FileApi(name, a);
    API(CallNamedPipeA) return KernelPipesApi(name,a);
    API(WaitNamedPipeA) return KernelPipesApi(name,a);
    API(TransactNamedPipe) return KernelPipesApi(name,a);
    API(GetNamedPipeHandleStateA) return KernelPipesApi(name,a);
    API(SetNamedPipeHandleState) return KernelPipesApi(name,a);
    API(GetNamedPipeInfo) return KernelPipesApi(name,a);
    API(CreateMailslotA) return KernelPipesApi(name,a);
    API(GetMailslotInfo) return KernelPipesApi(name,a);
    API(SetMailslotInfo) return KernelPipesApi(name,a);
    API(DeviceIoControl) return KernelPipesApi(name,a);
    API(DefineDosDeviceA) return KernelPipesApi(name,a);
    API(QueryDosDeviceA) return KernelPipesApi(name,a);
    API(OpenVxDHandle) return KernelPipesApi(name,a);
    API(CreateSocketHandle) return KernelPipesApi(name,a);
    API(GetQueuedCompletionStatus) return KernelPipesApi(name,a);
    API(GetHandleContext) return KernelPipesApi(name,a);
    API(SetHandleContext) return KernelPipesApi(name,a);
    API(CloseSystemHandle) return KernelPipesApi(name,a);
    API(SetScrollInfo)return ScrollApi(name,a);
    API(GetScrollInfo)return ScrollApi(name,a);
    API(SetScrollPos)return ScrollApi(name,a);
    API(GetScrollPos)return ScrollApi(name,a);
    API(SetScrollRange)return ScrollApi(name,a);
    API(GetScrollRange)return ScrollApi(name,a);
    API(ShowScrollBar)return ScrollApi(name,a);
    API(EnableScrollBar)return ScrollApi(name,a);

    // Original USER32 : OR EAX,1; RET 4. No queue resize on Win32.
    API(SetMessageQueue) return Register(Reg::Eax)|1u;
    // This platform has no wave device yet. Playback fails; stopping is harmless.
    API(MapVirtualKeyA) return KeyboardGameApi(name,a);
    API(GetKeyNameTextA) return KeyboardGameApi(name,a);
    API(SetCursorPos) return KeyboardGameApi(name,a);
    API(ChangeDisplaySettingsA) return KeyboardGameApi(name,a);
    API(mmioStringToFOURCCA) return MmioApi(name,a);
    API(mmioOpenA) return MmioApi(name,a);
    API(mmioClose) return MmioApi(name,a);
    API(mmioRead) return MmioApi(name,a);
    API(mmioSeek) return MmioApi(name,a);
    API(mmioDescend) return MmioApi(name,a);
    API(mmioAscend) return MmioApi(name,a);
    API(mciSendCommandA) return MciSendCommand(name,a);
    API(waveOutOpen) return WaveOutApi(name,a);
    API(waveOutGetDevCapsA) return WaveOutApi(name,a);
    API(waveOutGetPosition) return WaveOutApi(name,a);
    API(waveOutUnprepareHeader) return WaveOutApi(name,a);
    API(waveOutPrepareHeader) return WaveOutApi(name,a);
    API(waveOutClose) return WaveOutApi(name,a);
    API(waveOutReset) return WaveOutApi(name,a);
    API(waveOutWrite) return WaveOutApi(name,a);
    API(waveOutRestart) return WaveOutApi(name,a);
    API(waveOutPause) return WaveOutApi(name,a);
    API(waveOutBreakLoop) return WaveOutApi(name,a);
    API(waveOutGetPitch) return WaveOutApi(name,a);
    API(waveOutSetPitch) return WaveOutApi(name,a);
    API(waveOutGetPlaybackRate) return WaveOutApi(name,a);
    API(waveOutSetPlaybackRate) return WaveOutApi(name,a);
    API(SetFileApisToANSI) return KernelPathApi(name,a);
    API(SetFileApisToOEM) return KernelPathApi(name,a);
    API(lstrlen) return Api("lstrlenA",a);
    API(lstrcpy) return Api("lstrcpyA",a);
    API(lstrcpyn) return Api("lstrcpynA",a);
    API(lstrcat) return Api("lstrcatA",a);
    API(lstrcmp) return Api("lstrcmpA",a);
    API(lstrcmpi) return Api("lstrcmpiA",a);
    API(sndPlaySoundA) return SoundApi(name,a);
    API(waveOutGetNumDevs) return WaveOutApi(name,a);
    API(waveInGetNumDevs) return WaveInApi(name,a);
    API(timeBeginPeriod) return MultimediaTimerApi(name,a);
    API(timeEndPeriod) return MultimediaTimerApi(name,a);
    API(timeGetDevCaps) return MultimediaTimerApi(name,a);
    API(timeSetEvent) return MultimediaTimerApi(name,a);
    API(timeKillEvent) return MultimediaTimerApi(name,a);
    API(midiOutGetNumDevs) return MidiOutApi(name,a);
    API(midiInGetNumDevs) return MultimediaDeviceApi(name,a);
    API(mixerGetNumDevs) return MixerApi(name,a);
    API(auxGetNumDevs) return MultimediaDeviceApi(name,a);
    API(joyGetNumDevs) return MultimediaDeviceApi(name,a);
    API(waveInGetDevCapsA) return WaveInApi(name,a);
    API(midiOutGetDevCapsA) return MidiOutApi(name,a);
    API(midiInGetDevCapsA) return MultimediaDeviceApi(name,a);
    API(mixerGetDevCapsA) return MixerApi(name,a);
    API(auxGetDevCapsA) return MultimediaDeviceApi(name,a);
    API(joyGetDevCapsA) return MultimediaDeviceApi(name,a);
    API(joyGetPos) return MultimediaDeviceApi(name,a);
    API(joyGetPosEx) return MultimediaDeviceApi(name,a);
    API(waveOutGetVolume) return WaveOutApi(name,a);
    API(waveOutSetVolume) return WaveOutApi(name,a);
    API(midiOutGetVolume) return MidiOutApi(name,a);
    API(midiOutSetVolume) return MidiOutApi(name,a);
    API(waveOutGetID) return WaveOutApi(name,a);
    API(mixerGetID) return MixerApi(name,a);
    API(mciGetErrorStringA) return MultimediaDeviceApi(name,a);
    API(waveOutGetErrorTextA) return MultimediaDeviceApi(name,a);
    API(midiOutGetErrorTextA) return MultimediaDeviceApi(name,a);
    API(waveInGetErrorTextA) return MultimediaDeviceApi(name,a);
    API(midiInGetErrorTextA) return MultimediaDeviceApi(name,a);
    API(DriverCallback) return CensusImportApi(name,a);
    API(waveInOpen) return WaveInApi(name,a);
    API(midiOutOpen) return MidiOutApi(name,a);
    API(midiInOpen) return MultimediaDeferredApi(name,a);
    API(midiStreamOpen) return MidiOutApi(name,a);
    API(mixerOpen) return MixerApi(name,a);
    API(waveInClose) return WaveInApi(name,a);
    API(waveInStart) return WaveInApi(name,a);
    API(waveInStop) return WaveInApi(name,a);
    API(waveInReset) return WaveInApi(name,a);
    API(waveInAddBuffer) return WaveInApi(name,a);
    API(waveInPrepareHeader) return WaveInApi(name,a);
    API(waveInUnprepareHeader) return WaveInApi(name,a);
    API(waveInGetPosition) return WaveInApi(name,a);
    API(waveInMessage) return WaveInApi(name,a);
    API(waveOutMessage) return WaveOutApi(name,a);
    API(midiOutMessage) return MidiOutApi(name,a);
    API(midiOutClose) return MidiOutApi(name,a);
    API(midiOutReset) return MidiOutApi(name,a);
    API(midiOutShortMsg) return MidiOutApi(name,a);
    API(midiOutLongMsg) return MidiOutApi(name,a);
    API(midiOutPrepareHeader) return MidiOutApi(name,a);
    API(midiOutUnprepareHeader) return MidiOutApi(name,a);
    API(midiInClose) return MultimediaDeferredApi(name,a);
    API(midiInStart) return MultimediaDeferredApi(name,a);
    API(midiInStop) return MultimediaDeferredApi(name,a);
    API(midiInReset) return MultimediaDeferredApi(name,a);
    API(midiStreamClose) return MidiOutApi(name,a);
    API(midiStreamOut) return MidiOutApi(name,a);
    API(midiStreamPause) return MidiOutApi(name,a);
    API(midiStreamRestart) return MidiOutApi(name,a);
    API(midiStreamStop) return MidiOutApi(name,a);
    API(midiStreamProperty) return MidiOutApi(name,a);
    API(midiStreamPosition) return MidiOutApi(name,a);
    API(mixerClose) return MixerApi(name,a);
    API(mixerMessage) return MixerApi(name,a);
    API(mixerGetLineInfoA) return MixerApi(name,a);
    API(mixerGetLineControlsA) return MixerApi(name,a);
    API(mixerGetControlDetailsA) return MixerApi(name,a);
    API(mixerSetControlDetails) return MixerApi(name,a);
    API(PlaySoundA) return SoundApi(name,a);
    API(mciSendStringA) return MciSendString(name,a);
    API(mciGetDeviceIDA) return MciDeviceIdentifier(name,a);
    API(OpenDriver) API(OpenDriverA) API(DrvOpen) API(DrvOpenA) return InstallableDriverApi(name,a);
    API(CloseDriver) API(DrvClose) return InstallableDriverApi(name,a);
    API(SendDriverMessage) API(DrvSendMessage) return InstallableDriverApi(name,a);
    API(DefDriverProc) API(DrvDefDriverProc) return MultimediaDeferredApi(name,a);
    API(GetDriverModuleHandle) API(DrvGetModuleHandle) API(GetDriverFlags)
        return InstallableDriverApi(name,a);
    API(mmioWrite) return MmioApi(name,a);
    API(mmioCreateChunk) return MmioApi(name,a);
    API(mmioGetInfo) return MmioApi(name,a);
    API(ThunkConnect32) return FlatThunkApi(name, a);
    API(ordinal_41) return LegacyThunkConnect(a);
    API(ordinal_20) return LegacyFileHandleApi(name,a);
    API(ordinal_21) return LegacyFileHandleApi(name,a);
    API(ordinal_22) return LegacyFileHandleApi(name,a);
    // Ordinal-only KERNEL32 exports 1..60.
    API(ordinal_1) API(ordinal_2) API(ordinal_3) API(ordinal_4) API(ordinal_5) API(ordinal_6) API(ordinal_7) API(ordinal_8)
    API(ordinal_9) API(ordinal_10) API(ordinal_11) API(ordinal_12) API(ordinal_13) API(ordinal_14) API(ordinal_15) API(ordinal_16)
    API(ordinal_17) API(ordinal_19) API(ordinal_23) API(ordinal_24) API(ordinal_25) API(ordinal_26) API(ordinal_27) API(ordinal_28)
    API(ordinal_29) API(ordinal_30) API(ordinal_31) API(ordinal_32) API(ordinal_33) API(ordinal_34) API(ordinal_38) API(ordinal_39)
    API(ordinal_40) API(ordinal_42) API(ordinal_43) API(ordinal_44) API(ordinal_45) API(ordinal_46) API(ordinal_47) API(ordinal_48)
    API(ordinal_49) API(ordinal_51) API(ordinal_52) API(ordinal_53) API(ordinal_54) API(ordinal_55) API(ordinal_56) API(ordinal_57)
    API(ordinal_58) API(ordinal_59) API(ordinal_60)
    {
        unsigned ordinal=0;for(const char *p=name+8;*p>='0'&&*p<='9';++p)ordinal=ordinal*10+unsigned(*p-'0');
        if(ordinal>=1&&ordinal<=9)return VxDCallApi(a);
        if(ordinal==17)return UnimpStubApi();
        if(ordinal==38||ordinal==39)return MappedBufferApi(ordinal==38);
        if(ordinal==40||ordinal==45||ordinal==51)return ThunkTransitionStop(ordinal);
        if((ordinal>=10&&ordinal<=16)||ordinal==19||ordinal==23||(ordinal>=24&&ordinal<=34)||(ordinal>=42&&ordinal<=49&&ordinal!=45)||(ordinal>=52&&ordinal<=60))return KernelOrdinalApi(ordinal,a);
        break;
    }
    // Report a missing startup library only after the entire load attempt,
    // not on each loader retry. WinExec and LoadModule return a value below
    // 32; CreateProcess returns zero.
    API(WinExec) { const unsigned r = LegacyProcessApi(name, a); if (r < 32) ReportMissingModule(); return r; }
    API(LoadModule) { const unsigned r = LegacyProcessApi(name, a); if (r < 32) ReportMissingModule(); return r; }
    API(CreateProcessA) { const unsigned r = ProcessApi(name, a); if (!r) ReportMissingModule(); return r; }
    API(GetExitCodeProcess) return ProcessApi(name, a);
    API(CreateThread) return ThreadApi(name, a);
    API(CreateMutexA) return MutexApi(name, a);
    API(OpenMutexA) return MutexApi(name, a);
    API(ReleaseMutex) return MutexApi(name, a);
    API(CreateSemaphoreA) return SemaphoreApi(name, a);
    API(OpenSemaphoreA) return SemaphoreApi(name, a);
    API(ReleaseSemaphore) return SemaphoreApi(name, a);
    API(CreateFileMappingA) return MappingApi(name, a);
    API(OpenFileMappingA) return MappingApi(name, a);
    API(MapViewOfFile) return MappingApi(name, a);
    API(MapViewOfFileEx) return MappingApi(name, a);
    API(UnmapViewOfFile) return MappingApi(name, a);
    API(TlsAlloc) return TlsApi(name, a);
    API(TlsFree) return TlsApi(name, a);
    API(TlsGetValue) return TlsApi(name, a);
    API(TlsSetValue) return TlsApi(name, a);
    API(MakeCriticalSectionGlobal) return CriticalApi(name, a);
    API(InitializeCriticalSection) return CriticalApi(name, a);
    API(InitializeCriticalSectionAndSpinCount) return CriticalApi(name, a);
    API(DeleteCriticalSection) return CriticalApi(name, a);
    API(ReinitializeCriticalSection) return CriticalApi(name, a);
    API(EnterCriticalSection) return CriticalApi(name, a);
    API(LeaveCriticalSection) return CriticalApi(name, a);
    API(CreateWaitableTimerA) return ThreadApi(name,a);
    API(OpenWaitableTimerA) return ThreadApi(name,a);
    API(SetWaitableTimer) return WaitableTimerApi(name,a);
    API(CancelWaitableTimer) return WaitableTimerApi(name,a);
    API(CreateEventA) return ThreadApi(name, a);
    API(OpenEventA) return ThreadApi(name, a);
    API(SetEvent) return ThreadApi(name, a);
    API(ResetEvent) return ThreadApi(name, a);
    // KERNEL32 serial family; ahead of the generic handle dispatch.
    API(ClearCommBreak) return CommApi(name, a);
    API(ClearCommError) return CommApi(name, a);
    API(CommConfigDialogA) return CommApi(name, a);
    API(EscapeCommFunction) return CommApi(name, a);
    API(GetCommConfig) return CommApi(name, a);
    API(GetCommMask) return CommApi(name, a);
    API(GetCommModemStatus) return CommApi(name, a);
    API(GetCommProperties) return CommApi(name, a);
    API(GetCommState) return CommApi(name, a);
    API(GetCommTimeouts) return CommApi(name, a);
    API(GetDefaultCommConfigA) return CommApi(name, a);
    API(PurgeComm) return CommApi(name, a);
    API(SetCommBreak) return CommApi(name, a);
    API(SetCommConfig) return CommApi(name, a);
    API(SetCommMask) return CommApi(name, a);
    API(SetCommState) return CommApi(name, a);
    API(SetCommTimeouts) return CommApi(name, a);
    API(SetDefaultCommConfigA) return CommApi(name, a);
    API(SetupComm) return CommApi(name, a);
    API(TransmitCommChar) return CommApi(name, a);
    API(WaitCommEvent) return CommApi(name, a);
    // Serial device handles : COM names and the 0x67 handle prefix.
    API(GetOverlappedResult)
        if((a[0]&0xff000000)==0x67000000)return CommApi(name,a);
        return FileExtendedApi(name, a);
    API(DuplicateHandle)
        if((a[1]&0xff000000)==0x63000000)return DuplicatePipe(a);
        return ThreadApi(name, a);
    API(WaitForSingleObject) return ThreadApi(name, a);
    API(WaitForMultipleObjects) API(WaitForMultipleObjectsEx) API(MsgWaitForMultipleObjects) API(MsgWaitForMultipleObjectsEx) API(WaitMessage) return MultiwaitApi(name,a);
    API(Sleep) return ThreadApi(name, a);
    API(SuspendThread) return ThreadControlApi(name, a);
    API(GetThreadPriority) return ThreadControlApi(name, a);
    API(SetThreadPriority) return ThreadControlApi(name, a);
    API(GetPriorityClass) return ThreadControlApi(name, a);
    API(SetPriorityClass) return ThreadControlApi(name, a);
    API(SetThreadAffinityMask) return ThreadControlApi(name, a);
    API(ResumeThread) return ThreadApi(name, a);
    API(GetCurrentThreadId) return ThreadApi(name, a);
    API(GetCurrentThread) return ThreadApi(name, a);
    API(LCMapStringA) return LcMapStringApi(a);
    API(SearchPathA) return SearchPathApi(a);
    API(OpenProcess) return OpenProcessApi(a);
    API(ordinal_35) return Win16ModuleApi(35,a);
    API(ordinal_36) return Win16ModuleApi(36,a);
    API(ordinal_37) return Win16ModuleApi(37,a);
    API(GetCurrentProcessId) return ThreadApi(name, a);
    API(GetProcessVersion) return ThreadApi(name, a);
    API(GetVersionExA) return ThreadApi(name, a);
    API(GetCurrentProcess) return 0x7fffffff; // Original KERNEL32.
    API(AllocConsole) return ConsoleApi(name, a);
    API(FreeConsole) return ConsoleApi(name, a);
    API(CreateConsoleScreenBuffer) return ConsoleApi(name, a);
    API(GetConsoleCP) return ConsoleApi(name, a);
    API(GetConsoleOutputCP) return ConsoleApi(name, a);
    API(GetConsoleMode) return ConsoleApi(name, a);
    API(SetConsoleMode) return ConsoleApi(name, a);
    API(GetConsoleCursorInfo) return ConsoleApi(name, a);
    API(SetConsoleCursorInfo) return ConsoleApi(name, a);
    API(GetConsoleScreenBufferInfo) return ConsoleApi(name, a);
    API(GetConsoleTitleA) return ConsoleApi(name, a);
    API(SetConsoleTitleA) return ConsoleApi(name, a);
    API(GetLargestConsoleWindowSize) return ConsoleApi(name, a);
    API(GetNumberOfConsoleInputEvents) return ConsoleApi(name, a);
    API(GetNumberOfConsoleMouseButtons) return ConsoleApi(name, a);
    API(SetConsoleActiveScreenBuffer) return ConsoleApi(name, a);
    API(SetConsoleCursorPosition) return ConsoleApi(name, a);
    API(SetConsoleScreenBufferSize) return ConsoleApi(name, a);
    API(SetConsoleTextAttribute) return ConsoleApi(name, a);
    API(SetConsoleWindowInfo) return ConsoleApi(name, a);
    API(FillConsoleOutputAttribute) return ConsoleApi(name, a);
    API(FillConsoleOutputCharacterA) return ConsoleApi(name, a);
    API(FlushConsoleInputBuffer) return ConsoleApi(name, a);
    API(PeekConsoleInputA) return ConsoleApi(name, a);
    API(ReadConsoleA) return ConsoleApi(name, a);
    API(ReadConsoleInputA) return ConsoleApi(name, a);
    API(ReadConsoleOutputA) return ConsoleApi(name, a);
    API(ReadConsoleOutputAttribute) return ConsoleApi(name, a);
    API(ReadConsoleOutputCharacterA) return ConsoleApi(name, a);
    API(ScrollConsoleScreenBufferA) return ConsoleApi(name, a);
    API(WriteConsoleA) return ConsoleApi(name, a);
    API(WriteConsoleInputA) return ConsoleApi(name, a);
    API(WriteConsoleOutputA) return ConsoleApi(name, a);
    API(WriteConsoleOutputAttribute) return ConsoleApi(name, a);
    API(WriteConsoleOutputCharacterA) return ConsoleApi(name, a);
    API(SetConsoleCtrlHandler) return ConsoleApi(name, a);
    API(GenerateConsoleCtrlEvent) return ConsoleApi(name, a);
    API(SetStdHandle) return ConsoleApi(name, a);
    API(SleepEx) return KernelAsyncApi(name, a);
    API(WaitForSingleObjectEx) return KernelAsyncApi(name, a);
    API(QueueUserAPC) return KernelAsyncApi(name, a);
    API(TerminateThread) return KernelAsyncApi(name, a);
    API(TerminateProcess) return KernelAsyncApi(name, a);
    API(FreeLibraryAndExitThread) return KernelAsyncApi(name, a);
    API(PulseEvent) return KernelAsyncApi(name, a);
    API(UninitializeCriticalSection) return KernelAsyncApi(name, a);
    API(GetThreadContext) return KernelAsyncApi(name, a);
    API(SetThreadContext) return KernelAsyncApi(name, a);
    API(CreateToolhelp32Snapshot) return ToolhelpApi(name, a);
    API(Process32First) return ToolhelpApi(name, a);
    API(Process32Next) return ToolhelpApi(name, a);
    API(Thread32First) return ToolhelpApi(name, a);
    API(Thread32Next) return ToolhelpApi(name, a);
    API(Module32First) return ToolhelpApi(name, a);
    API(Module32Next) return ToolhelpApi(name, a);
    API(Heap32ListFirst) return ToolhelpApi(name, a);
    API(Heap32ListNext) return ToolhelpApi(name, a);
    API(Heap32First) return ToolhelpApi(name, a);
    API(Heap32Next) return ToolhelpApi(name, a);
    API(Toolhelp32ReadProcessMemory) return ToolhelpApi(name, a);
    API(LocalCompact) return HeapExtendedApi(name, a);
    API(LocalFlags) return HeapExtendedApi(name, a);
    API(LocalHandle) return HeapExtendedApi(name, a);
    API(LocalShrink) return HeapExtendedApi(name, a);
    API(GlobalHandle) return HeapExtendedApi(name, a);
    API(GlobalFix) return HeapExtendedApi(name, a);
    API(GlobalUnfix) return HeapExtendedApi(name, a);
    API(GlobalWire) return HeapExtendedApi(name, a);
    API(GlobalUnWire) return HeapExtendedApi(name, a);
    API(MoveFileExA) return FileExtendedApi(name, a);
    API(OpenFile) return FileExtendedApi(name, a);
    API(LockFile) return FileExtendedApi(name, a);
    API(UnlockFile) return FileExtendedApi(name, a);
    API(FlushViewOfFile) return MappingApi(name, a);
    API(ReadFileEx) return FileExtendedApi(name, a);
    API(WriteFileEx) return FileExtendedApi(name, a);
    API(CancelIo) return FileExtendedApi(name, a);
    API(SetVolumeLabelA) return FileExtendedApi(name, a);
    API(GetCPInfoExA) return NlsExtendedApi(name, a);
    API(IsValidCodePage) return NlsExtendedApi(name, a);
    API(IsValidLocale) return NlsExtendedApi(name, a);
    API(ConvertDefaultLocale) return NlsExtendedApi(name, a);
    API(EnumSystemCodePagesA) return NlsExtendedApi(name, a);
    API(EnumSystemLocalesA) return NlsExtendedApi(name, a);
    API(VerLanguageNameA) return NlsExtendedApi(name, a);
    API(EnumCalendarInfoA) return NlsExtendedApi(name, a);
    API(EnumCalendarInfoExA) return NlsExtendedApi(name, a);
    API(GetCalendarInfoA) return NlsExtendedApi(name, a);
    API(SetCalendarInfoA) return NlsExtendedApi(name, a);
    API(EnumDateFormatsExA) return NlsExtendedApi(name, a);
    API(SetLocaleInfoA) return NlsExtendedApi(name, a);
    API(GetErrorMode) return KernelMiscApi(name, a);
    API(GetProcessFlags) return KernelMiscApi(name, a);
    API(RegisterServiceProcess) return KernelMiscApi(name, a);
    API(GetSystemPowerStatus) return KernelMiscApi(name, a);
    API(IsSystemResumeAutomatic) return KernelMiscApi(name, a);
    API(SetThreadExecutionState) return KernelMiscApi(name, a);
    API(BuildCommDCBA) return CommParseApi(name, a);
    API(BuildCommDCBAndTimeoutsA) return CommParseApi(name, a);
    API(GetSystemInfo) return ProcessMemoryApi(name, a);
    API(ReadProcessMemory) return ProcessMemoryApi(name, a);
    API(WriteProcessMemory) return ProcessMemoryApi(name, a);
    API(VirtualQueryEx) return ProcessMemoryApi(name, a);
    API(VirtualProtectEx) return ProcessMemoryApi(name, a);
    API(FlushInstructionCache) return ProcessMemoryApi(name, a);
    API(GetProcessAffinityMask) return ProcessMemoryApi(name, a);
    API(IsDebuggerPresent) return ProcessMemoryApi(name, a);
    API(GetCommandLineW) return ProcessMemoryApi(name, a);
    API(VirtualUnlock) return ProcessMemoryApi(name, a);
    API(GlobalMemoryStatus) return MemoryStatusApi(a);
    API(VirtualAlloc) return MemoryApi(name, a);
    API(InterlockedExchange) return MemoryApi(name, a);
    API(InterlockedCompareExchange) return MemoryApi(name, a);
    API(InterlockedIncrement) return MemoryApi(name, a);
    API(InterlockedDecrement) return MemoryApi(name, a);
    API(InterlockedExchangeAdd) return MemoryApi(name, a);
    API(VirtualFree) return MemoryApi(name, a);
    API(VirtualProtect) return MemoryApi(name, a);
    API(VirtualQuery) return MemoryApi(name, a);
    API(IsBadCodePtr) return PointerProbeApi(name, a);
    API(IsBadHugeReadPtr) return PointerProbeApi(name, a);
    API(IsBadHugeWritePtr) return PointerProbeApi(name, a);
    API(IsBadReadPtr) return PointerProbeApi(name, a);
    API(IsBadStringPtrA) return PointerProbeApi(name, a);
    API(IsBadStringPtrW) return PointerProbeApi(name, a);
    API(IsBadWritePtr) return PointerProbeApi(name, a);
    API(GetExitCodeThread) return ThreadApi(name, a);
    API(ExitThread) return ThreadApi(name, a);
    API(SetLastError) return ThreadApi(name, a);
    // USER32: the first argument goes into SetLastError, the error
    // type NT would read is dropped, and the export returns nothing.
    API(SetLastErrorEx) return ThreadApi("SetLastError", a);
    // USER32 writes the level into the USER thread information block
    // and does nothing else with it; no other original export reads it back.
    API(SetDebugErrorLevel) { debug_error_level = a[0]; return 0; }
    API(ordinal_99) return TimezoneApi(name, a);
    API(ordinal_61) API(ordinal_62) API(ordinal_63) API(ordinal_64) API(ordinal_65) API(ordinal_66) API(ordinal_67) API(ordinal_68)
    API(ordinal_69) API(ordinal_70) API(ordinal_71) API(ordinal_72) API(ordinal_73) API(ordinal_74) API(ordinal_75) API(ordinal_76)
    API(ordinal_77) API(ordinal_78) API(ordinal_79) API(ordinal_80) API(ordinal_81) API(ordinal_82) API(ordinal_83) API(ordinal_84)
    API(ordinal_85) API(ordinal_86) API(ordinal_87) API(ordinal_88) API(ordinal_89) API(ordinal_90) API(ordinal_91) API(ordinal_92)
    API(ordinal_93) API(ordinal_94) API(ordinal_95) API(ordinal_96) API(ordinal_97) API(ordinal_98) API(ordinal_100) API(ordinal_101)
    API(ordinal_102) API(ordinal_103) API(ordinal_104) API(ordinal_105) API(ordinal_106) API(ordinal_107) API(ordinal_108) API(ordinal_109)
    API(ordinal_110) API(ordinal_111) API(ordinal_112) API(ordinal_113) API(ordinal_114) API(ordinal_115) API(ordinal_116) API(ordinal_117)
    API(ordinal_119) API(ordinal_120) API(ordinal_121) API(ordinal_122)
    {
        // Ordinal-only KERNEL32 exports 61..122 except 99 and 118.
        unsigned o = 0;
        for (const char *p = name + 8; *p >= '0' && *p <= '9'; ++p) o = o * 10 + unsigned(*p - '0');
        if ((o >= 61 && o <= 98) || (o >= 100 && o <= 117) || (o >= 119 && o <= 122)) return KernelOrdinalsBApi(o, a);
        break;
    }
    API(GetDaylightFlag) return TimezoneApi(name,a);
    API(SetDaylightFlag) return TimezoneApi(name,a);
    API(GetTimeZoneInformation) return TimezoneApi(name, a);
    API(SetTimeZoneInformation) return TimezoneApi(name, a);
    API(GetLocalTime) return TimezoneApi(name, a);
    API(GetSystemTimeAsFileTime) return TimeApi(name, a);
    API(GetSystemTime) return TimezoneApi(name, a);
    API(SetLocalTime) return TimezoneApi(name, a);
    API(SetSystemTime) return TimezoneApi(name, a);
    API(SystemTimeToFileTime) return TimeApi(name, a);
    API(FileTimeToSystemTime) return TimeApi(name, a);
    API(CompareFileTime) return TimeApi(name, a);
    API(DosDateTimeToFileTime) return TimeApi(name, a);
    API(FileTimeToDosDateTime) return TimeApi(name, a);
    API(FileTimeToLocalFileTime) return TimeApi(name, a);
    API(LocalFileTimeToFileTime) return TimeApi(name, a);
    API(QueryPerformanceCounter) return TimeApi(name,a);
    API(QueryPerformanceFrequency) return TimeApi(name,a);
    API(GetTickCount) return ThreadApi(name, a);
    // timeGetTime refreshes system time before reading, giving millisecond
    // resolution; GetTickCount advances at timer interrupts.
    API(timeGetTime) return SystemTime();
    // WINMM answers the MMSYSTEM version 3.10 and nothing else.
    API(mmsystemGetVersion) return 0x30a;
    // WINMM : below twelve bytes, or on a buffer the pointer probe
    // rejects, MMSYSERR_INVALPARAM. Otherwise wType becomes TIME_MS and the
    // millisecond field the count the timer VxD keeps, which is the same count
    // timeGetTime answers; the original returns zero either way after that.
    API(timeGetSystemTime)
    {
        if (a[1] < 12 || !a[0] || !virtual_memory.Access(a[0], a[1], true)) return 11;
        Write32(a[0], 1);
        Write32(a[0] + 4, SystemTime());
        return 0;
    }
    API(FindTextA) return FindApi(a[0], false);
    API(ReplaceTextA) return FindApi(a[0], true);
    API(GetEnvironmentStrings) API(GetEnvironmentStringsA) API(FreeEnvironmentStringsA) API(GetEnvironmentVariableA) API(SetEnvironmentVariableA) API(ExpandEnvironmentStringsA)
        return EnvironmentApi(name, a);
    API(GetACP) return NlsApi(312, a);
    API(GetOEMCP) return NlsApi(406, a);
    API(GetCPInfo) return NlsApi(318, a);
    API(GetStringTypeA) return StringTypeApi(a);
    API(GetStringTypeExA) return StringTypeApi(a);
    API(IsDBCSLeadByte) return NlsApi(537, a);
    API(IsDBCSLeadByteEx) return NlsApi(538, a);
    API(MultiByteToWideChar) return NlsApi(594, a);
    API(WideCharToMultiByte) return NlsApi(811, a);
    API(GetFileTitleA) return PathApi(name, a);
    API(CommDlgExtendedError) return CommonDialogError();
    API(PrintDlgA) return PrintDialogApi(name,a);
    API(GetOpenFileNameA) API(GetSaveFileNameA) return FileDialogApi(name,a);
    API(FormatMessageA)return FormatMessage(a);
    API(GetVolumeInformationA) return VolumeInformationApi(name,a);
    API(GetDriveTypeA) return VolumeInformationApi(name,a);
    API(GetDiskFreeSpaceA) return VolumeInformationApi(name,a);
    API(GetDiskFreeSpaceExA) return VolumeInformationApi(name,a);
    API(GetLogicalDrives) return VolumeInformationApi(name,a);
    API(GetLogicalDriveStringsA) return VolumeInformationApi(name,a);
    API(CreatePipe) return PipeApi(name,a);
    API(PeekNamedPipe) return PipeApi(name,a);
    API(SetFilePointer) return FileApi(name, a);
    API(SetEndOfFile) return LptHandle(a[0]) ? LptApi(name, a) : FileApi(name, a);
    API(GetFileSize) return FileApi(name, a);
    API(GetFileTime) return FileTimeApi(name,a);
    API(SetFileTime) return FileTimeApi(name,a);
    API(FindFirstChangeNotificationA) return ChangeNotificationApi(name, a);
    API(FindFirstChangeNotificationW) return ChangeNotificationApi(name, a);
    API(FindNextChangeNotification) return ChangeNotificationApi(name, a);
    // Ordinal 281 is CloseHandle's own body; the object type decides.
    API(FindCloseChangeNotification) return Api("CloseHandle", a);
    API(FlushFileBuffers)
        if((a[0]&0xff000000)==0x67000000)return CommApi(name,a);
        return FileApi(name, a);
    API(CopyFileA) return FileApi(name, a);
    API(CreateDirectoryExA) return FileApi(name, a);
    API(GetFileAttributesExA) return FileApi(name, a);
    API(GetFileInformationByHandle) return FileApi(name, a);
    API(GetFileAttributesA) return FileApi(name, a);
    API(SetFileAttributesA) return FileApi(name, a);
    API(DeleteFileA) return FileApi(name, a);
    API(CreateDirectoryA) return FileApi(name, a);
    API(RemoveDirectoryA) return FileApi(name, a);
    API(MoveFileA) return FileApi(name, a);
    API(FindFirstFileA) return FileApi(name, a);
    API(FindNextFileA) return FileApi(name, a);
    API(FindClose) return FileApi(name, a);
    API(AreFileApisANSI) return KernelPathApi(name, a);
    API(GetFullPathNameA) return KernelPathApi(name, a);
    API(GetCurrentDirectoryA) return KernelPathApi(name, a);
    API(SetCurrentDirectoryA) return KernelPathApi(name, a);
    API(GetWindowsDirectoryA) return FileApi(name, a);
    API(GetSystemDirectoryA) return FileApi(name, a);
    API(GetTempPathA) return KernelPathApi(name, a);
    API(GetShortPathNameA) return KernelPathApi(name, a);
    API(GetLongPathNameA) return KernelPathApi(name, a);
    API(GetTempFileNameA) return KernelPathApi(name, a);
    API(_lopen) return FileApi(name, a);
    API(_lcreat) return FileApi(name, a);
    API(_lclose) return FileApi(name, a);
    API(_lread) return FileApi(name, a);
    API(_lwrite) return FileApi(name, a);
    API(_hread) return FileApi(name, a);
    API(_hwrite) return FileApi(name, a);
    API(_llseek) return FileApi(name, a);

    API(FindWindowA){
        const char *klass=nullptr;
        if(a[0]){if(a[0]<65536){char *buffer=StringSlot();if(!buffer||!User::ClassAtomName(a[0],buffer,8192))return 0;klass=buffer;}
            else klass=ReadString(a[0]);}
        const char *title=a[1]?ReadString(a[1]):nullptr;return User::FindWindow(klass,title);
    }
    // USER32, ordinal 212. reaches it with two leading
    // zeros, so FindWindowA is this call with no parent and no predecessor.
    API(FindWindowExA){
        const char *klass=nullptr;
        if(a[2]){if(a[2]<65536){char *buffer=StringSlot();if(!buffer||!User::ClassAtomName(a[2],buffer,8192))return 0;klass=buffer;}
            else klass=ReadString(a[2]);}
        const char *title=a[3]?ReadString(a[3]):nullptr;
        return User::FindWindowEx(a[0],a[1],klass,title);
    }
    API(GetClassNameA)return ClassApi(name,a);
    // Windows 98 has no wide window procedures. Return FALSE for every handle
    // without validation or a LastError change.
    API(IsWindowUnicode) return 0;
    // USER32 narrows the HDC for USER's desktop painter.
    API(PaintDesktop) return PaintDesktop(a[0]);
    API(IsWindowVisible)return User::IsVisible(a[0]);
    API(IsWindowEnabled)return User::IsEnabled(a[0]);
    API(GetSysColor)
    {
        // The same classic table the implemented frame-control body reads.
        const unsigned *colors = Win98Frame::SystemColors();
        // USER32 answers 0 for an index below 0 or from 29 on and
        // does not touch the last error.
        if (a[0] >= Win98Frame::ColorCount)
            return 0;
        unsigned c = colors[a[0]];
        return Gdi::RGB(c >> 16, c >> 8, c);
    }
    API(GetSysColorBrush)
    {
        if (a[0] >= Win98Frame::ColorCount)
            return 0;
        if (!system_brushes[a[0]])
        {
            system_brushes[a[0]] = Gdi::CreateSolidBrush(Api("GetSysColor", a));
            // A program may register the handle as the background brush of a
            // class (CALC does). USER frees the brush of a class with the
            // class, but a brush of GetSysColorBrush is USER's own and stays:
            // without this the end of that program took the brush from every
            // other one, and no dialog erased its surface any more.
            User::ProtectStockBrush(system_brushes[a[0]], true);
        }
        return system_brushes[a[0]];
    }
    API(SetTextColor) return Gdi::SetTextColor(a[0], a[1]);
    API(SetBkColor) return Gdi::SetBkColor(a[0], a[1]);
    API(DrawFrameControl)
    {
        Gdi::RECT r={};
        if(!Read(a[1],&r,sizeof r))return 0;
        const bool result=Win98Frame::DrawFrameControl(a[0],&r,a[2],a[3]);
        Store(a[1],&r,sizeof r);User::DrawingChanged(a[0]);return result;
    }
    API(DrawEdge)
    {
        Gdi::RECT r = {};
        Read(a[1], &r, sizeof r);
        bool result = Win98Draw::DrawEdge(a[0], &r, a[2], a[3]);
        Store(a[1], &r, sizeof r);
        User::DrawingChanged(a[0]);
        return result;
    }
    API(OffsetRect)
    {
        Win98::Rect r = {};
        Read(a[0], &r, sizeof r);
        bool result = Win98::OffsetRect(&r, a[1], a[2]);
        Store(a[0], &r, sizeof r);
        return result;
    }
    API(DrawCaption) return DrawCaptionApi(name,a);
    API(DrawCaptionTempA) return DrawCaptionApi(name,a);
    API(DrawTextA) return DrawTextApi(name,a);
    API(DrawTextExA) return DrawTextApi(name,a);
    API(SetWindowPos) API(BeginDeferWindowPos) API(DeferWindowPos) API(EndDeferWindowPos)
        return PositionApi(name, a);
    API(GetWindowPlacement)
    {
        Gdi::RECT r = {};
        if (!User::NormalWindowRect(a[0], &r)) return 0;
        // Win98 returns FALSE for inaccessible buffers and wrong lengths,
        // preserving LastError. Probe without raising a guest memory fault.
        if (!virtual_memory.Access(a[1], 44, false) ||
            !virtual_memory.Access(a[1], 44, true) || Read32(a[1]) != 44) return 0;
        Gdi::POINT minimum{-1, -1}, maximum{-1, -1};
        User::PlacementPoints(a[0], minimum, maximum);
        unsigned placement[] = {44,
                                0,
                                User::Style(a[0]) & 0x20000000 ? 2u : User::Style(a[0]) & 0x01000000 ? 3u : 1u,
                                unsigned(minimum.x),
                                unsigned(minimum.y),
                                unsigned(maximum.x),
                                unsigned(maximum.y),
                                (unsigned)r.left,
                                (unsigned)r.top,
                                (unsigned)r.right,
                                (unsigned)r.bottom};
        Store(a[1], placement, sizeof placement);
        return 1;
    }
    API(SetWindowPlacement)
    {
        unsigned p[11] = {};
        Read(a[1], p, sizeof p);
        if (p[0] != 44)
        {
            last_error = 87;
            return 0;
        }
        if (p[1] || p[2] > 9)
        {
            last_error = 120;
            return 0;
        }
        // USER32 copies each DWORD of WINDOWPLACEMENT
        // to a WORD before USER.371 consumes it. Paint can restore a saved
        // rectangle beginning at 80000000h; on Win98 that coordinate is zero.
        Gdi::RECT normal={short(p[7]),short(p[8]),short(p[9]),short(p[10])};
        if(!User::SetNormalWindowRect(a[0],normal)){last_error=87;return 0;}
        ShowWindow(a[0],p[2]);return User::IsWindow(a[0]);
    }
    API(MapWindowPoints) return CoordinatesApi(name,a);
    API(MessageBeep)
    {
        // A type of -1 beeps the PC speaker. Every other type plays the sound
        // registered for the event and falls back to the same beep when there
        // is none. On Windows 98 the beep is a 748 Hz tone on the speaker.
        // NEO98 carries no registered event sounds, so every type ends at that
        // beep, and the answer is one whatever happened.
        // The length of the delay loop depends on the machine; it was a few
        // milliseconds on a fast Pentium and about forty on a 386, and this
        // port takes the upper figure so the tone is audible at all.
        Sound::Speaker::Tone(748, 40);
        return 1;
    }
    API(GlobalSize) API(GlobalFlags) API(GlobalReAlloc) return LocalMemoryApi(name, a);
    API(GlobalCompact) return heap_end - next_alloc;
    API(CreateDialogIndirectParamA) return IndirectDialogApi(name,a);
    API(DialogBoxIndirectParamA) return IndirectDialogApi(name,a);
    API(CreateDialogParamA) return CreateDialog(a[0], a[1], a[2], a[3], a[4]);
    API(DialogBoxParamA)
    {
        // The handler of the gate, with nothing after the loop: a saved
        // state can take the call up again inside it (ResumeHostCall).
        const bool gate = TakeFresh();
        unsigned owner = a[2];
        if (owner == User::GetDesktopWindow()) owner = 0;
        if (owner && !User::IsWindow(owner)) { last_error = 1400; return ~0u; }
        while ((User::Style(owner) & 0xc0000000u) == 0x40000000u)
        {
            const unsigned parent = User::Parent(owner);
            if (!parent) break;
            owner = parent;
        }
        // Match the indirect entry point: USER disables the owner before
        // the dialog's creation hook, not only when its modal loop starts.
        const bool restore = owner && User::IsEnabled(owner);
        if (owner) { const unsigned enable[] = {owner, 0}; Api("EnableWindow", enable); }
        const bool kept = gate && PushHostFrame(HostDialogBox, restore, owner, 1, 1);
        if (kept) GiveTail(TailRecord);
        unsigned h = CreateDialog(a[0], a[1], owner, a[3], a[4], true);
        if (auto *dialog = FindDialog(h)) dialog->restore_owner = restore;
        if (kept) threads[current_thread].host_frames[threads[current_thread].host_frame_count - 1].d = 0;
        const unsigned result = h ? RunModal(h, kept) : 0xffffffff;
        if (kept) PopHostFrame();
        if (restore && User::IsWindow(owner)) { const unsigned enable[] = {owner, 1}; Api("EnableWindow", enable); }
        return result;
    }
    API(EndDialog)
    {
        EndDialog(a[0], a[1]);
        return 1;
    }
    API(GetDlgItem) return User::GetDlgItem(a[0], a[1]);
    API(GetClassLongA) return ClassApi(name,a);
    API(SendMessageTimeoutA) return WindowApi(name,a);
    API(GetSystemMenu) return WindowApi(name,a);
    API(GetDesktopWindow) return WindowApi(name,a);
    API(GetWindow)
        if ((a[1] == 2 || a[1] == 3) && CurrentHookType() == 5 && User::CreationPending(a[0])) return 0;
        return WindowApi(name,a);
    API(GetTopWindow) return WindowApi(name,a);
    API(GetCapture) return User::CaptureOperation(0);
    API(SetCapture) return User::CaptureOperation(1,a[0]);
    API(ReleaseCapture) return User::CaptureOperation(2);
    API(BringWindowToTop) return ParentApi(name,a);
    API(SetParent) return ParentApi(name,a);
    API(GetWindowThreadProcessId) return WindowApi(name,a);
    API(EnumWindows) return WindowApi(name,a);
    API(EnumChildWindows) return WindowApi(name,a);
    API(EnumThreadWindows) return WindowApi(name,a);
    API(GetParent)
    {
        // USER reads the high style byte at window+33h. A window that is
        // neither WS_CHILD nor WS_POPUP answers zero, a WS_CHILD
        // without WS_POPUP answers the parent at window+8, everything
        // else answers the owner at window+0Ch. USER32 keeps the same
        // rule. The older answer here, parent and otherwise owner, is the rule
        // of GWL_HWNDPARENT, not of GetParent; it moved every form of
        // the Visual Basic runtime off the left edge, because the runtime
        // positions a form against what GetParent names.
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        const unsigned style = User::Style(a[0]);
        if (!(style & 0xc0000000u)) return 0;
        if ((style & 0xc0000000u) == 0x40000000u) return User::WindowNode(a[0]).parent;
        return User::Owner(a[0]);
    }
    API(IsChild)
    {
        unsigned h = User::Parent(a[1]);
        for (unsigned i = 0; h && i < 192; ++i)
        {
            if (h == a[0])
                return 1;
            h = User::Parent(h);
        }
        return 0;
    }
    API(CheckDlgButton)
    {
        User::ControlMessage(User::GetDlgItem(a[0], a[1]), 0xf1, a[2], 0);
        return 1;
    }
    API(IsDlgButtonChecked) return User::ControlMessage(User::GetDlgItem(a[0], a[1]), 0xf0, 0, 0);
    API(CheckRadioButton)
    {
        if (a[2] - a[1] > 192)
            return 0;
        for (unsigned id = a[1]; id <= a[2]; ++id)
            User::ControlMessage(User::GetDlgItem(a[0], id), 0xf1, id == a[3], 0);
        return 1;
    }
    API(SetDlgItemInt)
    {
        char text[32];
        if (a[3])
            snprintf(text, sizeof text, "%d", (int)a[2]);
        else
            snprintf(text, sizeof text, "%u", a[2]);
        return User::SetWindowText(User::GetDlgItem(a[0], a[1]), text);
    }
    API(MapDialogRect)
    {
        auto *d = FindDialog(a[0]);
        if (!d)
            return 0;
        Gdi::RECT r = {};
        Read(a[1], &r, sizeof r);
        Win98Dialog::MapRect(&r, d->xunit, d->yunit);
        Store(a[1], &r, sizeof r);
        return 1;
    }
    API(GetDialogBaseUnits) return (13 << 16) | 6;
    API(GetNextDlgTabItem) return User::NextDialogItem(a[0], a[1], a[2] != 0);
    API(GetNextDlgGroupItem) return User::NextDialogItem(a[0], a[1], a[2] != 0, true);
    API(DefDlgProcA) return DialogProcedure(a[0], a[1], a[2], a[3]);
    API(MessageBoxA) API(MessageBoxExA) API(MessageBoxW) API(MessageBoxExW) API(MessageBoxIndirectA) API(MessageBoxIndirectW) return MessageBoxApi(name,a);
    API(GetWindowLongA)
    {
        if (!User::IsWindow(a[0])) return 0;
        if (a[1] == 4)
            if (auto *dialog = FindDialog(a[0]))
                return dialog->procedure;
        if((int)a[1]>=0||(int)a[1]==-21){unsigned value=0;User::WindowLong(a[0],a[1],value,false);return value;}
        switch ((int)a[1])
        {
        case -4:
            // A native BUTTON reports its class procedure as a token, so a
            // subclass can hand messages back to it through CallWindowProc.
            if ((!User::Procedure(a[0]) || User::Procedure(a[0]) == 0x71000020) && User::IsButton(a[0])) return 0x71000030;
            return User::Procedure(a[0]);
        case -16:
            return User::Style(a[0]);
        case -20:
            return User::ExtendedStyle(a[0]);
        case -12:
            return User::ControlID(a[0]);
        case -8:
            // GWL_HWNDPARENT. USER compares the window's parent with the
            // desktop: a top-level window answers with its owner, a child with
            // its parent. NEO98 leaves the parent of a top-level window at zero,
            // so both zero and the explicit desktop mean an owner query.
            return User::Parent(a[0]) && User::Parent(a[0]) != User::GetDesktopWindow()
                ? User::Parent(a[0]) : User::Owner(a[0]);
        case -6:
            // GWL_HINSTANCE is the instance the window was created with; a window made without one keeps the old answer.
            if (const unsigned instance = User::WindowInstance(a[0]))
                return instance;
            return image.Base();
        default:
            return 0;
        }
    }
    API(GetStdHandle) return ConsoleApi(name,a);
    API(SetHandleCount) return Win98::HandleCount(a[0]);
    API(GetVersion) return Win98::Version(0);
    API(HeapCreate) return HeapApi(name, a);
    API(HeapDestroy) return HeapApi(name, a);
    API(GetProcessHeap) return HeapApi(name, a);
    API(HeapAlloc) return HeapApi(name, a);
    API(HeapReAlloc) return HeapApi(name, a);
    API(HeapFree) return HeapApi(name, a);
    API(HeapSize) return HeapApi(name, a);
    API(HeapValidate) return HeapApi(name, a);

    API(GetComputerNameA) return ComputerNameApi(name,a);
    API(SetComputerNameA) return ComputerNameApi(name,a);
    API(GetCommandLineA) return command_line;
    API(GetStartupInfoA) return LegacyProcessApi(name,a);
    API(OutputDebugStringA)
    {
        // Original KERNEL32 hands the text to the debugger, if any. NEO98 keeps
        // it in the log; there is nothing else to do.
        if (log) { char text[600]; snprintf(text, sizeof text, "OutputDebugString %s", ReadString(a[0])); log(text); }
        return 0;
    }
    API(FindResourceW) return ResourceApi(name, a);
    API(FindResourceExW) return ResourceApi(name, a);
    API(EnumResourceTypesA) return ResourceApi(name, a);
    API(EnumResourceTypesW) return ResourceApi(name, a);
    API(EnumResourceNamesA) return ResourceApi(name, a);
    API(EnumResourceNamesW) return ResourceApi(name, a);
    API(FindResourceA) return ResourceApi(name, a);
    API(EnumResourceLanguagesA) return ResourceApi(name, a);
    API(EnumResourceLanguagesW) return ResourceApi(name, a);
    API(FindResourceExA) return ResourceApi(name, a);
    API(LoadResource) return ResourceApi(name, a);
    API(LockResource) return ResourceApi(name, a);
    API(FreeResource) return ResourceApi(name, a);
    API(SizeofResource) return ResourceApi(name, a);
    API(GetModuleHandleA) return ModuleApi(name, a);
    API(GetModuleFileNameA) return ModuleApi(name, a);
    API(GetProcAddress) return ModuleApi(name, a);
    API(LoadLibraryA) return ModuleApi(name, a);
    API(LoadLibraryExA) return ModuleApi(name, a);
    API(FreeLibrary) return ModuleApi(name, a);
    API(DisableThreadLibraryCalls) return ModuleApi(name, a);
    API(ExitProcess)
    {
        exit_code = a[0];
        if (user_initialized) HandForegroundOn();
        TerminateModules();
        if (state != Failed) state = Exited;
        // The process's queues are ending. Release senders waiting for
        // replies now, rather than delaying until process cleanup.
        CancelSendLifetimes();
        // the queues of the process and their activation state.
        if (user_initialized) for (auto &t : threads) if (t.fiber) User::ForgetThreadQueue(t.id);
        // USER: with the queue goes its ShowCursor level.
        if (user_initialized) User::DropPointerLevel(process_id, 0);
        // USER: the windows of the ending queues go now,
        // not when the process is taken down.
        if (user_initialized) User::DestroyProcessWindows(process_id);
        if (user_initialized) HandOffPointer(); //
        FreePipes();
        return 0;
    }
    API(GetLastError) return last_error;
    API(SetErrorMode) return Win98::SetErrorMode(process_error_mode, a[0],
        [&](unsigned mode) { legacy_error_mode = mode; });
    API(LocalAlloc) API(GlobalAlloc) API(LocalLock) API(GlobalLock)
    API(LocalUnlock) API(GlobalUnlock) API(LocalFree) API(GlobalFree)
        return LocalMemoryApi(name, a);
    API(ordinal_18)  return ProcessDwordApi(a);
    API(LocalSize) return LocalMemoryApi(name, a);
    API(ConvertToGlobalHandle)
        return LocalHandleApi(name, a);
    API(LocalReAlloc) return LocalMemoryApi(name, a);
    API(RtlFillMemory) API(RtlZeroMemory)  {
        unsigned char buffer[1024];
        memset(buffer, API_IS(name,"RtlZeroMemory") ? 0 : a[2] & 255, sizeof buffer);
        // KERNEL32's own exception frame catches a bad pointer and sets 87.
        // Keep writes before the fault, but do not invoke Store's fatal path.
        // Zero length must not dereference even an invalid destination.
        for (unsigned offset = 0; offset < a[1];) {
            const unsigned words = a[1] & ~3u;
            unsigned bytes = offset < words ? words - offset : 1;
            if (bytes > sizeof buffer) bytes = sizeof buffer;
            const unsigned address = a[0] + offset;
            if (offset < words) {
                const unsigned page_words = (4096 - (address & 4095)) & ~3u;
                if (bytes > page_words) bytes = page_words ? page_words : 4;
            }
            if ((unsigned long long)a[0] + offset + bytes > 0x100000000ull ||
                !virtual_memory.Access(address, bytes, true) || cpu->Write(address, buffer, bytes))
            { last_error = 87; return 0; }
            if (write_watch_count) MarkWriteWatch(address, bytes);
            offset += bytes;
        }
        return 0; // These exports return void.
    }
    API(RtlMoveMemory)
    {
        // KERNEL32: forward except for a higher destination within
        // the source range. It copies dwords first, then the remaining bytes.
        unsigned char buffer[4096];
        const bool forward = a[0] <= a[1] || (unsigned long long)a[1] + a[2] < a[0];
        const unsigned words = a[2] & ~3u;
        for (unsigned done = 0; done < a[2];)
        {
            unsigned n = done < words ? words - done : 1;
            if (n > sizeof buffer) n = sizeof buffer;
            // Stop at either page boundary so a later fault does not erase
            // the partial copy performed by the original routine.
            const unsigned source = a[1] + (forward ? done : a[2] - done - 1);
            const unsigned target = a[0] + (forward ? done : a[2] - done - 1);
            const unsigned source_bytes = forward ? 4096 - (source & 4095) : (source & 4095) + 1;
            const unsigned target_bytes = forward ? 4096 - (target & 4095) : (target & 4095) + 1;
            if (done < words) {
                unsigned page_words = (source_bytes < target_bytes ? source_bytes : target_bytes) & ~3u;
                if (!page_words) page_words = 4;
                if (n > page_words) n = page_words;
            }
            const unsigned offset = forward ? done : a[2] - done - n;
            if ((unsigned long long)a[1] + offset + n > 0x100000000ull ||
                (unsigned long long)a[0] + offset + n > 0x100000000ull ||
                !virtual_memory.Access(a[1] + offset, n, false) ||
                !virtual_memory.Access(a[0] + offset, n, true) ||
                cpu->Read(a[1] + offset, buffer, n) || cpu->Write(a[0] + offset, buffer, n))
            { last_error = 87; return 0; }
            if (write_watch_count) MarkWriteWatch(a[0] + offset, n);
            done += n;
        }
        return a[0];
    }
    API(MulDiv)
    {
        long long product = (long long)(int)a[0] * (int)a[1], denominator = (int)a[2];
        if (!denominator)
            return -1;
        unsigned long long p = product < 0 ? -product : product,
                           d = denominator < 0 ? -denominator : denominator;
        long long result = (p + d / 2) / d;
        if ((product < 0) != (denominator < 0))
            result = -result;
        return result < -2147483648ll || result > 2147483647ll ? 0xffffffff : (unsigned)result;
    }
    API(lstrlenA)
    {
        // KERNEL32 catches invalid string accesses in its own exception frame.
        // Probe without Read, whose failure stops the application.
        if (!a[0]) return 0;
        for (unsigned length = 0; length < ~a[0]; ++length)
        {
            unsigned char c = 0;
            if (!virtual_memory.Access(a[0] + length, 1, false) ||
                cpu->Read(a[0] + length, &c, 1)) return 0;
            if (!c) return length;
        }
        return 0;
    }
    API(lstrlenW) return WideStringApi(name, a);
    API(lstrcpyW) return WideStringApi(name, a);
    API(lstrcatW) return WideStringApi(name, a);
    API(lstrcpynW) return WideStringApi(name, a);
    API(lstrcpyA) API(lstrcpynA)
    {
        // KERNEL32 / scan the source, then copy with
        // forward DWORD/byte moves. lstrcpy includes the terminating NUL;
        // lstrcpyn copies at most count-1 bytes and writes a separate NUL.
        // Their SEH wrappers return NULL / ERROR_INVALID_PARAMETER on an
        // access fault. DemoShield queries a value with a null destination.
        // ReadString cannot stage these sources: its 64 KiB slot also
        // truncates DSETUP32's multilingual EULA.
        const bool bounded = API_IS(name, "lstrcpynA");
        auto fault = [&]() -> unsigned { last_error = 87; return 0; };
        unsigned length = 0;
        for (;;)
        {
            unsigned char value;
            if (!virtual_memory.Access(a[1] + length, 1, false) ||
                !Read(a[1] + length, &value, 1)) return fault();
            ++length;
            if (!value) break;
            if (!length) return fault();
        }
        unsigned count = bounded && a[2] < length ? a[2] : length;
        if (!count) return a[0];
        if (bounded) --count;
        for (unsigned offset = 0; offset < count;)
        {
            const unsigned bytes = count - offset >= 4 ? 4 : 1;
            unsigned value = 0;
            if (!virtual_memory.Access(a[1] + offset, bytes, false) ||
                !Read(a[1] + offset, &value, bytes)) return fault();
            // A DWORD store spanning a page boundary can change the writable
            // prefix before the original kernel's SEH wrapper catches it.
            for (unsigned copied = 0; copied < bytes;)
            {
                const unsigned address = a[0] + offset + copied;
                unsigned part = 4096 - (address & 4095);
                if (part > bytes - copied) part = bytes - copied;
                if (!virtual_memory.Access(address, part, true) ||
                    !Store(address, (unsigned char *)&value + copied, part)) return fault();
                copied += part;
            }
            offset += bytes;
        }
        if (bounded)
        {
            const unsigned char zero = 0;
            if (!virtual_memory.Access(a[0] + count, 1, true) ||
                !Store(a[0] + count, &zero, 1)) return fault();
        }
        return a[0];
    }
    API(lstrcatA)
    {
        const unsigned length_args[]={a[0]};
        const unsigned length=Api("lstrlenA",length_args);
        if(a[0]>~0u-length)return 0;
        const unsigned copy_args[]={a[0]+length,a[1]};
        return Api("lstrcpyA",copy_args)?a[0]:0;
    }
    API(lstrcmpA)
    API(lstrcmpiA)
    {
        // KERNEL32/: compare directly from guest memory.
        // Valid strings have no staging-buffer length limit. The SBCS
        // insensitive branch folds ASCII to uppercase before comparison.
        const bool insensitive=API_IS(name,"lstrcmpiA");
        if(!a[0]||!a[1])return a[0]?1:a[1]?~0u:0;
        unsigned x=a[0],y=a[1];
        for (;;) {
            unsigned char left=0,right=0;
            if(!Read(x,&left,1)||!Read(y,&right,1))return 0;
            if(insensitive){
                if(left>='a'&&left<='z')left-=32;
                if(right>='a'&&right<='z')right-=32;
            }
            if(left!=right)return left<right?-1:1;
            if(!left)return 0;
            ++x;++y;
        }
    }
    API(CompareStringA) return CompareStringApi(a);
    API(CharLowerA) return CharacterCaseApi(name,a);
    API(CharUpperA) return CharacterCaseApi(name,a);
    API(CharLowerBuffA) return CharacterCaseApi(name,a);
    API(CharUpperBuffA) return CharacterCaseApi(name,a);
    API(IsCharLowerA) return CharacterApi(0, a);
    API(IsCharUpperA) return CharacterApi(1, a);
    API(IsCharAlphaNumericA) return CharacterApi(2, a);
    API(IsCharAlphaA) return CharacterApi(3, a);
    API(CharNextA) return CharacterApi(4, a);
    API(CharPrevA) return CharacterApi(5, a);
    API(CharNextExA) return CharacterApi(6, a);
    API(CharPrevExA) return CharacterApi(7, a);
    API(LoadStringA)
    {
        unsigned length = 0;
        const unsigned module = a[0] ? ModuleIndex(a[0], true) : 0;
        // The number decides which block of sixteen strings is read, and a
        // report that does not carry it cannot say whether a program asked for
        // a string it does not have.
        if (log)
        {
            char trace[128];
            snprintf(trace, sizeof trace, "LoadString instance=%x id=%u block=%u capacity=%u", a[0], a[1],
                     (a[1] & 65535) / 16 + 1, a[3]);
            log(trace);
        }
        if (module >= ModuleCapacity || !modules[module].pe) return 0;
        const auto *data = modules[module].pe->Resource(6, (a[1] & 65535) / 16 + 1, &length);
        // USER reads the block out of the mapped image, as LoadResource hands
        // it out. A packed program has its string tables there and not in the
        // file: UPX keeps them compressed and its stub rebuilds them before
        // the program starts (Start.exe of KIDS200, whose message box had a
        // button without a caption).
        unsigned live = 0;
        {
            const unsigned kept = last_error;
            if (LockLoader())
            {
                ResourceModule mapped;
                Pe32::Image::ResourceKey type, id;
                Pe32::Image::ResourceEntry entry;
                unsigned error = 0;
                type.id = 6;
                id.id = (a[1] & 65535) / 16 + 1;
                if (ResolveResourceModule(a[0], mapped) && !mapped.datafile &&
                    (mapped.pe->FindResource(type, id, 0, entry, &error) ||
                     mapped.pe->FindResource(type, id, 0x409, entry, &error) ||
                     mapped.pe->FindResource(type, id, ~0u, entry, &error)))
                {
                    const unsigned address = mapped.Address(entry.data);
                    if (address && entry.size && virtual_memory.Access(address, entry.size, false))
                    {
                        live = address;
                        length = entry.size;
                    }
                }
                UnlockLoader();
            }
            last_error = kept;
        }
        if (!data && !live)
            return 0;
        const auto word = [&](unsigned offset) -> unsigned {
            if (!live) return Word(data + offset);
            unsigned short value = 0;
            Read(live + offset, &value, 2);
            return value;
        };
        unsigned at = 0;
        for (unsigned i = 0; i < 16; ++i)
        {
            if (at + 2 > length)
                return 0;
            unsigned n = word(at);
            at += 2;
            if (n > (length - at) / 2)
                return 0;
            if (i == a[1] % 16)
            {
                char text[8192];
                if (n >= sizeof text)
                {
                    Fail("String resource too long");
                    return 0;
                }
                for (unsigned j = 0; j < n; ++j)
                {
                    unsigned ch = word(at + j * 2);
                    text[j] = AnsiNarrow(ch);
                }
                text[n] = 0;
                return PutString(a[2], text, a[3]);
            }
            at += n * 2;
        }
        return 0;
    }
    API(LoadAcceleratorsA)
    {
        // USER loads RT_ACCELERATOR from the supplied instance, including a
        // DLL. Keep its module-qualified resource handle for copy/translation.
        const unsigned find[] = {a[0], a[1], 9};
        const unsigned handle = ResourceApi("FindResourceA", find);
        for (auto &resource : resource_handles)
            if (handle && resource.id == handle && resource.size && resource.size % 8 == 0) {
                resource.loaded = resource.accelerator = true;
                return handle;
            }
        return 0;
    }
    API(CopyImage) return ImageApi(name,a);
    // USER32 ordinal 444 : with no WinEvent hook installed the export
    // returns immediately (win98_win_event.h); otherwise the body
    // goes to USER (src/x86_win_event.cpp).
    API(NotifyWinEvent) return Win98WinEvent::Notifies(WinEventHooksInstalled()) ? WinEventApi(name, a) : 0u;
    API(SetWinEventHook) API(UnhookWinEvent) return WinEventApi(name, a);
    API(GetFileTitleW) return Comdlg32CompletionApi(name, a);
    API(AnyPopup) API(CascadeChildWindows) API(TileChildWindows) API(GetKBCodePage) API(GetNextQueueWindow)
    API(BlockInput) API(SendInput) API(SetSystemCursor) API(SwapMouseButton) API(IsHungThread)
    API(PlaySoundEvent) API(HasSystemSleepStarted) API(UserIsSystemResumeAutomatic) API(UserTickleTimer)
    API(UserSetDeviceHoldState) API(WinOldAppHackoMatic) API(UserSignalProc) API(DragDetect)
    API(DrawMenuBarTemp) API(SysErrorBox) API(DragObject)
        return User32GateApi(name, a);
    API(LoadIconA) API(LoadImageA) API(DestroyIcon) API(DrawIcon) API(DrawIconEx)
    API(ImageList_Create) API(ImageList_Destroy) API(ImageList_Draw) API(ImageList_GetIconSize) API(ImageList_GetImageCount) API(ImageList_Remove)
    API(ImageList_ReplaceIcon) API(ImageList_SetBkColor) API(ImageList_SetIconSize)
        return ImageApi(name,a);
    API(LoadCursorA) return ImageApi(name,a);
    API(LoadBitmapA) return ImageApi(name,a);
    API(LoadCursorFromFileA) return ImageApi(name,a);
    API(LookupIconIdFromDirectory) return ImageApi(name,a);
    API(LookupIconIdFromDirectoryEx) return ImageApi(name,a);
    API(CreateIconFromResource) return ImageApi(name,a);
    API(CreateIconFromResourceEx) return ImageApi(name,a);
    API(CreateIconIndirect) API(CreateIcon) API(CreateCursor) return ImageApi(name,a);
    API(DestroyCursor) return ImageApi(name,a);
    API(RegisterClassExA) return ClassApi(name,a);
    API(RegisterClassA) return ClassApi(name,a);
    API(SetClassWord) return ClassApi(name,a);
    API(GetClassWord) return ClassApi(name,a);
    API(SetClassLongA) return ClassApi(name,a);
    API(UnregisterClassA) return ClassApi(name,a);
    API(GetClassInfoExA) return ClassApi(name,a);
    API(GetClassInfoA) return ClassApi(name,a);
    API(CreateWindowExA) return CreateWindow(a);
    API(DestroyWindow) return CbtDestroyWindow(a[0],false);
    API(DefWindowProcA) return DefaultWindowMessage(a[0],a[1],a[2],a[3]);
    API(SetWindowLongA)
    {
        if (a[0] == User::GetDesktopWindow()) { last_error = 5; return 0; }
        if (!User::IsWindow(a[0])) return 0;
        if ((int)a[1] == -6) return User::SetWindowInstance(a[0], a[2]);
        if (a[1] == 4)
            if (auto *dialog = FindDialog(a[0]))
            {
                unsigned old = dialog->procedure;
                dialog->procedure = a[2];
                return old;
            }
        if((int)a[1]>=0||(int)a[1]==-21){unsigned value=a[2];if(!User::WindowLong(a[0],a[1],value,true))return 0;return value;}
        if ((int)a[1] == -4)
        {
            if (!a[2]) return User::Procedure(a[0]);
            const unsigned old = User::SetProcedure(a[0], a[2] == 0x71000030 ? 0 : a[2]);
            return (!old || old == 0x71000020) && User::IsButton(a[0]) ? 0x71000030 : old;
        }
        if ((int)a[1] == -12)
        {
            // GWL_ID. USER keeps the child identifier in the same word as the
            // menu handle of a top-level window, and SetWindowLong answers the
            // previous value. The original SHELL32 renumbers its view window
            // this way when it puts SHELLDLL_DefView into a COMDLG32 dialog.
            const unsigned old = User::ControlID(a[0]);
            if (!User::SetControlID(a[0], a[2])) { last_error = 1400; return 0; }
            return old;
        }
        if ((int)a[1] == -8)
        {
            // GWL_HWNDPARENT. USER writes the field at window+3Ch: the
            // owner when the structural parent is the desktop, and otherwise
            // the parent. Changing WS_CHILD alone does not change that choice.
            if (!User::IsWindow(a[0])) return 0;
            const unsigned old = User::Parent(a[0]) && User::Parent(a[0]) != User::GetDesktopWindow()
                ? User::Parent(a[0]) : User::Owner(a[0]);
            if (User::SetOwnerField(a[0], a[2])) return old;
            const unsigned parent[2] = {a[0], a[2]};
            return ParentApi("SetParent", parent) ? old : 0;
        }
        if ((int)a[1] == -16 || (int)a[1] == -20)
        {
            // USER's SetWindowLong for GWL_STYLE/GWL_EXSTYLE is : it
            // writes the two style words and rechecks no geometry. The frame
            // follows at the next SetWindowPos with SWP_FRAMECHANGED. The
            // messages WM_STYLECHANGING and WM_STYLECHANGED come
            // and only for a window with bit 40h at window+2Eh, that is a
            // window of a module that expects version 4.0 or higher;
            // jumps over the SendMessage for every other one, so no Win16
            // window sees them. COMCTL32 keeps its cached style current from
            // exactly that message, which is why a modern window still gets it.
            const bool extended = (int)a[1] == -20;
            const unsigned old = extended ? User::ExtendedStyle(a[0]) : User::Style(a[0]);
            if (!User::ModernWindow(a[0]))
            {
                User::SetStyleWords(a[0], a[2], extended);
                return old;
            }
            unsigned structure[2] = {old, a[2]};
            const unsigned block = Allocate(8);
            if (!block) return 0;
            Store(block, structure, 8);
            Send(a[0], 0x7c, a[1], block);
            Read(block, structure, 8);
            User::SetStyleWords(a[0], structure[1], extended);
            structure[1] = extended ? User::ExtendedStyle(a[0]) : User::Style(a[0]);
            Store(block, structure, 8);
            Send(a[0], 0x7d, a[1], block);
            Free(block);
            return old;
        }
        return 0;
    }
    API(MonitorFromPoint) return MonitorsApi(name,a);
    API(MonitorFromRect) return MonitorsApi(name,a);
    API(MonitorFromWindow) return MonitorsApi(name,a);
    API(GetMonitorInfoA) return MonitorsApi(name,a);
    API(GetMonitorInfoW) return MonitorsApi(name,a);
    API(EnumDisplayMonitors) return MonitorsApi(name,a);
    API(EnumDisplayDevicesA) return MonitorsApi(name,a);
    API(GetClientRect) return CoordinatesApi(name,a);
    API(GetWindowRect) return CoordinatesApi(name,a);
    API(AdjustWindowRect) API(AdjustWindowRectEx)
    {
        Gdi::RECT r;
        if (!Read(a[0], &r, sizeof r))
            return 0;
        const auto result = User::AdjustWindowRect(&r, a[1], a[2], API_IS(name,"AdjustWindowRectEx") ? a[3] : 0);
        return Store(a[0], &r, sizeof r) ? result : 0;
    }
    // The drawing families of GDI32 are routed by the import table; these
    // cases give Api the same names, because the 16-bit GDI gates of
    // src/win16_user.cpp reach them through Api.
    API(CreateHatchBrush) API(GetDCOrgEx) API(GetObjectType)
    API(CreateSolidBrush) API(CreateBrushIndirect) API(PatBlt) API(BitBlt) API(CombineRgn) API(CreateCompatibleDC)
    API(CreateRectRgn) API(CreateRectRgnIndirect) API(DeleteDC) API(RectVisible) API(SetRectRgn) API(ExtTextOutA)
    API(TextOutA) API(LineTo) API(MoveToEx) API(SetBkMode) API(SetTextAlign) API(GetTextExtentPoint32A)
    API(CreateDiscardableBitmap) API(CreateCompatibleBitmap) API(Rectangle) API(Ellipse)
    API(CreatePen) API(CreatePalette) API(CreateDCA) API(CreateDIBitmap) API(SelectPalette)
    API(RealizePalette) API(SetBitmapBits) API(GetBitmapBits) API(GetPixel) API(SetPixel)
    API(GetClipBox) API(GetMapMode) API(SetBrushOrgEx) API(LPtoDP) API(DPtoLP) API(CreatePatternBrush) API(CreateBitmap)
    API(SelectClipRgn) API(RestoreDC) API(SaveDC) API(StretchBlt) API(SetWindowOrgEx)
    API(SetWindowExtEx) API(SetViewportOrgEx) API(SetViewportExtEx) API(SetStretchBltMode)
    API(SetROP2) API(SetMapMode) API(StretchDIBits) API(CreateICA) API(GetNearestColor) API(FloodFill)
    API(SetDIBitsToDevice) API(GetDIBits) API(SetDIBits) API(GetViewportOrgEx) API(GetPaletteEntries) API(GetSystemPaletteEntries) API(UnrealizeObject)
    API(SetSystemPaletteUse) API(GetSystemPaletteUse) API(AnimatePalette) API(ResizePalette)
    API(GetNearestPaletteIndex) API(UpdateColors) API(SetPaletteEntries)
    API(GetROP2) API(PtInRegion) API(GetWindowOrgEx) API(GetWindowExtEx) API(GetViewportExtEx)
    API(OffsetWindowOrgEx) API(OffsetViewportOrgEx) API(GetTextFaceA)
    API(ExcludeClipRect) API(IntersectClipRect) API(GetBkColor) API(GetBkMode) API(GetTextColor)
    API(EnumFontsA) API(EnumFontFamiliesA) API(EnumFontFamiliesExA) API(GetTextCharset) API(Pie) API(Chord)
    API(StartDocA) API(EndDoc) API(StartPage) API(EndPage) API(AbortDoc) API(SetAbortProc)
    // The GDI32 names of the gates of src/win16_gdi_more.inc, the pass over
    // the whole export table of GDI.EXE.
    API(AbortPath) API(Arc) API(BeginPath) API(CloseFigure) API(CreateBitmapIndirect) API(CreateEllipticRgn)
    API(CreateEllipticRgnIndirect) API(CreateHalftonePalette) API(CreatePolyPolygonRgn) API(CreatePolygonRgn)
    API(CreateRoundRectRgn) API(EndPath) API(EqualRgn) API(ExtCreatePen) API(ExtFloodFill) API(ExtSelectClipRgn)
    API(FillPath) API(FillRgn) API(FlattenPath) API(FrameRgn) API(GetArcDirection) API(GetAspectRatioFilterEx)
    API(GetBitmapDimensionEx) API(GetBoundsRect) API(GetBrushOrgEx) API(GetCharABCWidthsA) API(GetCharWidthA)
    API(GetCurrentPositionEx) API(GetDIBColorTable) API(GetFontData) API(GetFontLanguageInfo)
    API(GetGlyphOutlineA) API(GetKerningPairsA) API(GetMiterLimit) API(GetPolyFillMode) API(GetRandomRgn)
    API(GetRasterizerCaps) API(GetRgnBox) API(GetStretchBltMode) API(GetTextAlign) API(GetTextCharacterExtra)
    API(InvertRgn) API(OffsetClipRgn) API(OffsetRgn) API(PaintRgn) API(PathToRegion) API(PolyBezier)
    API(PolyBezierTo) API(PolyPolygon) API(Polygon) API(Polyline) API(PtVisible) API(RectInRegion)
    API(RemoveFontResourceA) API(ResetDCA) API(RoundRect) API(ScaleViewportExtEx) API(ScaleWindowExtEx)
    API(SelectClipPath) API(SetArcDirection) API(SetBitmapDimensionEx) API(SetBoundsRect) API(SetDIBColorTable)
    API(SetMagicColors) API(SetMapperFlags) API(SetMiterLimit) API(SetPolyFillMode) API(SetTextCharacterExtra)
    API(SetTextJustification) API(StrokeAndFillPath) API(StrokePath) API(WidenPath)
    return GdiApi(name, a);
    // The 16-bit gate of GDI.38 reaches the escape group through Api.
    API(Escape) return GdiApi(name, a);
    API(FrameRect) return GdiApi(name, a);
    API(InvertRect) return GdiApi(name, a);
    API(FillRect) return GdiApi(name, a);
    API(DrawFocusRect) return GdiApi(name, a);
    API(GetDC) return User::GetDC(a[0]);
    API(WindowFromDC) return User::WindowFromDC(a[0]);
    API(LockWindowUpdate) return User::LockWindowUpdate(a[0]);
    API(GetWindowContextHelpId) return WindowApi(name,a);
    API(SetWindowContextHelpId) return WindowApi(name,a);
    API(ReleaseDC) return User::ReleaseDC(a[0], a[1]);
    API(ScrollDC) return ScrollDcApi(name, a);
    API(ScrollWindowEx) API(ScrollWindow)
    {
        // USER maps ScrollWindow onto ScrollWindowEx with
        // SW_INVALIDATE|SW_ERASE, plus SW_SCROLLCHILDREN without a scroll
        // rectangle; is the worker.
        const bool ex = name[12] == 'E';
        const unsigned flags = ex ? a[7] : (a[3] ? 6u : 7u);
        const unsigned scroll_at = a[3], clip_at = a[4], region = ex ? a[5] : 0, update_at = ex ? a[6] : 0;
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        if (region && Gdi::GetObjectType(region)!=8) { last_error=6; return 0; }
        Gdi::RECT scroll = {}, clip = {}, update = {};
        if (scroll_at && !Read(scroll_at, &scroll, 16)) return 0;
        if (clip_at && !Read(clip_at, &clip, 16)) return 0;
        const int dx = (int)a[1], dy = (int)a[2];
        const int result = User::ScrollWindowArea(a[0], dx, dy, scroll_at ? &scroll : nullptr, clip_at ? &clip : nullptr, &update, flags & 2, flags & 4, region, flags & 1);
        if ((flags & 1) && !scroll_at && (dx || dy))
        {
            // USER5 reports WM_MOVE after moving the child records, but only
            // when no explicit scroll rectangle was supplied.
            unsigned child = User::GetWindow(a[0], 5);
            for (unsigned n = 0; child && n < 192; ++n)
            {
                const unsigned next = User::GetWindow(child, 2);
                {
                        Gdi::POINT client{};
                        User::ScreenToClient(child,&client);
                        client.x=-client.x;client.y=-client.y;
                        User::ScreenToClient(a[0],&client);
                        SendFromNative(child,3,0,(unsigned short)client.x|(unsigned((unsigned short)client.y)<<16));
                }
                child = next;
            }
        }
        if (update_at) Store(update_at, &update, 16);
        // Win98 leaves the region-complexity value intact for ScrollWindow too.
        // Without invalidation or either output, the worker only needs a box.
        return result>1 && !(flags&2) && !region && !update_at ? 2 : result;
    }
    API(MoveWindow)
    {
        // USER: SetWindowPos with SWP_NOZORDER|SWP_NOACTIVATE, plus
        // SWP_NOREDRAW when the caller declines the repaint.
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        unsigned position[1][7] = {{a[0], 0, a[1], a[2], a[3], a[4], a[5] ? 0x14u : 0x1cu}};
        return PositionWindows(1, position);
    }
    API(ShowWindow) return ShowWindow(a[0],a[1]);
    API(SetFocus) return SetFocus(a[0]);
    API(GetFocus)
    {
        const unsigned focus = User::ThreadActivation(ThreadId()).focus;
        // The foreground handoff keeps the old queue's record internally
        // until its callbacks return. It is not the caller's input focus.
        return User::WindowThread(focus) == ThreadId() ? focus : 0;
    }
    API(GetForegroundWindow)
    {
        const unsigned active = User::ForegroundActiveWindow();
        return User::WindowThread(active) == User::ForegroundQueue() ? active : 0;
    }
    API(GetActiveWindow) return ActivationApi(name,a);
    API(SetActiveWindow) return ActivationApi(name,a);
    API(SetForegroundWindow) return ActivationApi(name,a);
    API(SwitchToThisWindow) return ActivationApi(name,a);
    API(GetLastActivePopup) return ActivationApi(name,a);
    API(IsIconic) return ActivationApi(name,a);
    API(IsZoomed) return ActivationApi(name,a);
    API(CloseWindow) return ActivationApi(name,a);
    API(OpenIcon) return ActivationApi(name,a);
    API(EnableWindow)
    {
        // USER. Disabling first sends WM_CANCELMODE and takes the focus
        // away when this very window holds it; the bit changes; a real change
        // then reports WM_ENABLE. The answer is the previous disabled state.
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        const bool was_disabled = !User::IsEnabled(a[0]);
        bool changed;
        if (!a[1])
        {
            Send(a[0], 0x1f, 0, 0);
            if (!User::IsWindow(a[0])) return was_disabled;
            if (User::GetFocus() == a[0]) SetFocus(0);
            if (!User::IsWindow(a[0])) return was_disabled;
            User::EnableWindow(a[0], false);
            changed = !was_disabled;
        }
        else
        {
            User::EnableWindow(a[0], true);
            changed = was_disabled;
        }
        if (changed) Send(a[0], 0xa, a[1] ? 1 : 0, 0);
        return was_disabled;
    }
    API(SystemParametersInfoA) return SystemParameters(a);
    API(GetSystemMetrics) return SystemMetric(a[0]);
    API(InvalidateRect)
    {
        Gdi::RECT rect = {};
        // USER hands a null window to, which drops the rect
        // and calls RedrawWindow on the desktop with RDW_INVALIDATE |
        // RDW_ERASE | RDW_ALLCHILDREN | RDW_ERASENOW (285h), whatever the
        // erase argument says: every window, whole, erased before the call
        // returns.
        if (!a[0])
        {
            const auto immediate = [](void *p, unsigned hwnd, unsigned kind, unsigned parameter) {
                static_cast<Application *>(p)->RedrawImmediate(hwnd, kind, parameter);
            };
            return User::RedrawWindow(0, nullptr, 0, 0x285, this, immediate);
        }
        if (a[1]&&!Read(a[1], &rect, sizeof rect))return 0;
        return User::InvalidateRect(a[0], a[1] ? &rect : nullptr, a[2] != 0);
    }
    API(RedrawWindow)
    {
        Gdi::RECT rect = {};
        if (a[1] && !Read(a[1], &rect, sizeof rect)) return 0;
        // The original's immediate paths run on the caller's thread:
        // delivers WM_PAINT, the erase. Both are adapter work here.
        const auto immediate = [](void *p, unsigned hwnd, unsigned kind, unsigned parameter) {
            static_cast<Application *>(p)->RedrawImmediate(hwnd, kind, parameter);
        };
        const bool result = User::RedrawWindow(a[0], a[1] ? &rect : nullptr, a[2], a[3], this, immediate);
        if (!result && User::RedrawError()) last_error = 87;
        return result;
    }
    API(UpdateWindow) return PaintApi(name,a);
    API(BeginPaint) return PaintApi(name,a);
    API(EndPaint) return PaintApi(name,a);
    API(GetUpdateRect) return PaintApi(name,a);
    API(ValidateRect) return PaintApi(name,a);
    // USER: a null window goes to the worker as a null window,
    // which takes the desktop, so GetWindowDC(NULL) is a DC on the whole
    // screen.
    API(GetWindowDC) return a[0] ? WindowDeviceContext(a[0], 0, 0x10001) : User::GetWindowDC(0);
    API(GetDCEx) return WindowDeviceContext(a[0], a[1], a[2]);
    API(GetUpdateRgn) return UpdateRegionApi(name,a);
    API(InvalidateRgn) return UpdateRegionApi(name,a);
    API(ValidateRgn) return UpdateRegionApi(name,a);
    API(ExcludeUpdateRgn) return UpdateRegionApi(name,a);
    API(CharToOemA) return UserOemStringApi(name,a);
    API(CharToOemBuffA) return UserOemStringApi(name,a);
    API(OemToCharA) return UserOemStringApi(name,a);
    API(OemToCharBuffA) return UserOemStringApi(name,a);
    API(GetWindowWord) return User32ImportApi(name,a);
    API(SetWindowWord) return User32ImportApi(name,a);
    API(CopyIcon) return User32ImportApi(name,a);
    API(AttachThreadInput) return User32ImportApi(name,a);
    API(GetThreadDesktop) return User32ImportApi(name,a);
    API(SetThreadDesktop) return User32ImportApi(name,a);
    API(SetWindowsHookA) return User32ImportApi(name,a);
    API(UnhookWindowsHook) return User32ImportApi(name,a);
    API(GetAncestor) return User32ImportApi(name,a);
    API(GetWindowModuleFileNameA) return User32ImportApi(name,a);
    API(GetKeyboardType) return User32ImportApi(name,a);
    API(BroadcastSystemMessage) API(BroadcastSystemMessageA) return User32ImportApi(name,a);
    API(keybd_event) return User32ImportApi(name,a);
    API(mouse_event) return User32ImportApi(name,a);
    API(TrackMouseEvent) return User32ImportApi(name,a);
    API(GetDlgItemInt) return User32ImportApi(name,a);
    API(ChangeMenuA) return User32ImportApi(name,a);
    API(AlignRects) return User32ImportApi(name,a);
    API(LoadMenuIndirectA) return User32ImportApi(name,a);
    API(RegisterDeviceNotificationA) return User32ImportApi(name,a);
    API(UnregisterDeviceNotification) return User32ImportApi(name,a);
    API(EndTask) API(ExitWindowsEx) API(RegisterSystemThread) API(RegisterNetworkCapabilities)
        API(EnumDisplaySettingsA) API(EnumDisplaySettingsExA) API(ChangeDisplaySettingsExA) return UserSessionApi(name,a);
    API(ArrangeIconicWindows) API(TileWindows) API(CascadeWindows)
        API(ShowOwnedPopups) API(FlashWindow) API(FlashWindowEx)
        API(ShowWindowAsync) return ArrangeApi(name,a);
    API(AnimateWindow) API(SetSysColors) API(SetSysColorsTemp) API(HiliteMenuItem)
        API(CreateMDIWindowA) API(DlgDirSelectExA) API(DlgDirSelectComboBoxExA)
        API(EnumPropsA) API(EnumPropsExA) return WindowExportApi(name,a);
    API(GetWindowInfo) API(GetTitleBarInfo) API(GetScrollBarInfo)
        API(GetComboBoxInfo) API(GetListBoxInfo) API(GetCursorInfo)
        API(GetAltTabInfo) API(GetGUIThreadInfo) API(RealGetWindowClass)
        return WindowInfoApi(name,a);
    API(DefFrameProcA) API(DefMDIChildProcA) API(TranslateMDISysAccel) return MdiApi(name,a);
    API(DlgDirListA) API(DlgDirListComboBoxA) return DlgDirApi(name,a);
    API(DrawStateA) API(GrayStringA) return DrawStateApi(name,a);
    API(TabbedTextOutA) API(GetTabbedTextExtentA) return TabbedTextApi(name,a);
    API(CreateAcceleratorTableA) API(CopyAcceleratorTableA) API(DestroyAcceleratorTable)
        return AcceleratorApi(name,a);
    API(ToAscii) API(ToAsciiEx) return KeyboardTranslateApi(name,a);
    API(VkKeyScanA) return KeyboardTranslateApi(name,a);
    API(VkKeyScanExA) return KeyboardTranslateApi(name,a);
    API(OemKeyScan) return KeyboardTranslateApi(name,a);
    API(MapVirtualKeyExA) return KeyboardTranslateApi(name,a);
    API(PackDDElParam) return DdeParameterApi(name,a);
    API(UnpackDDElParam) return DdeParameterApi(name,a);
    API(FreeDDElParam) return DdeParameterApi(name,a);
    API(ReuseDDElParam) return DdeParameterApi(name,a);
    API(SetWindowTextA) return WindowTextApi(name,a);
    API(GetWindowTextA) return WindowTextApi(name,a);
    API(GetWindowTextLengthA) return WindowTextApi(name,a);
    API(SetDlgItemTextA) return WindowTextApi(name,a);
    API(GetDlgItemTextA) return WindowTextApi(name,a);
    API(GetDlgCtrlID) return User::ControlID(a[0]);
    API(SubtractRect) API(UnionRect) API(IntersectRect)
    {
        Gdi::RECT first = {}, second = {}, out = {};
        Read(a[1], &first, sizeof first);
        Read(a[2], &second, sizeof second);
        unsigned result = API_IS(name,"SubtractRect") ? Win98::SubtractRect(&out, &first, &second)
                          : API_IS(name,"UnionRect")  ? Win98::UnionRect(&out, &first, &second)
                                                        : Win98::IntersectRect(&out, &first, &second);
        Store(a[0], &out, sizeof out);
        return result;
    }
    API(SetRect)
    {
        Gdi::RECT r = {};
        unsigned result = Win98::SetRect(&r, a[1], a[2], a[3], a[4]);
        Store(a[0], &r, sizeof r);
        return result;
    }
    API(SetRectEmpty)
    {
        Gdi::RECT r = {};
        unsigned result = Win98::SetRectEmpty(&r);
        Store(a[0], &r, sizeof r);
        return result;
    }
    API(CopyRect)
    {
        Gdi::RECT r = {};
        Read(a[1], &r, sizeof r);
        Store(a[0], &r, sizeof r);
        return 1;
    }
    API(IsRectEmpty)
    {
        Gdi::RECT r = {};
        Read(a[0], &r, sizeof r);
        return Win98::IsRectEmpty(&r);
    }
    API(PtInRect)
    {
        Gdi::RECT r = {};
        Read(a[0], &r, sizeof r);
        return Win98::PtInRect(&r, a[1], a[2]);
    }
    API(InflateRect)
    {
        Gdi::RECT r = {};
        Read(a[0], &r, sizeof r);
        unsigned result = Win98::InflateRect(&r, a[1], a[2]);
        Store(a[0], &r, sizeof r);
        return result;
    }
    API(EqualRect)
    {
        Gdi::RECT left = {}, right = {};
        Read(a[0], &left, sizeof left);
        Read(a[1], &right, sizeof right);
        return Win98::EqualRect(&left, &right);
    }
    API(IsWindow) return User::IsWindow(a[0]);
    API(InSendMessage) return SentMessageApi(name,a);
    API(InSendMessageEx) return SentMessageApi(name,a);
    API(ReplyMessage) return SentMessageApi(name,a);
    // HWND_BROADCAST: USER32 narrows a window handle to USER's 16-bit word,
    // so the 0FFFFh of the SDK headers and a 32-bit -1 are the same handle
    // and both take the recipient walk of USER. The Jugglor launcher
    // of Golden Tower posts its start message to 0FFFFh.
    API(SendMessageA)
    {
        if (Win98Broadcast::IsBroadcast(a[0])) return BroadcastMessage(Win98Broadcast::Send, a[1], a[2], a[3]);
        TailOfGate(TailResult);
        return Send(a[0], a[1], a[2], a[3]);
    }
    API(SendNotifyMessageA) return Win98Broadcast::IsBroadcast(a[0])
                                       ? BroadcastMessage(Win98Broadcast::Notify, a[1], a[2], a[3])
                                       : SendNotify(a[0], a[1], a[2], a[3]);
    // A native control has no guest class procedure, so GetWindowLong(GWL_WNDPROC)
    // answers zero before a subclass installs itself. Explorer's Start button
    // keeps that zero and calls back through it; the native default owns it.
    API(CallWindowProcA){if(a[0]==MdiClientProcedure)return MdiClientMessage(a[1],a[2],a[3],a[4]);if(!a[0])return DefaultWindowMessage(a[1],a[2],a[3],a[4]);if(a[0]==0x71000030)User::AdoptButton(a[1]);if(a[0]==0x71000030||(a[0]==0x71000020&&User::IsButton(a[1])))return ButtonProcedure(a[1],a[2],a[3],a[4]);if(a[0]==0x71000031){User::AdoptLabel(a[1]);return StaticProcedure(a[1],a[2],a[3],a[4]);}if(a[0]==0x71000020&&User::IsLabel(a[1]))return StaticProcedure(a[1],a[2],a[3],a[4]);if(a[0]==0x71000020){TailOfGate(TailResult);return DialogProcedure(a[1],a[2],a[3],a[4]);}if(a[0]==0x71000021)return Edit(a[1],a[2],a[3],a[4]);if(a[0]==0x71000010)return FindProcedure(a[1],a[2],a[3],a[4]);if((a[0]>=0x71000001&&a[0]<=0x71000004)||a[0]==0x71000006||a[0]==0x7100000a)return CommonMessage(a[1],a[2],a[3],a[4],a[0]);TailOfGate(TailResult);return Call(a[0],a+1,4);}
    API(SendDlgItemMessageA) return Send(User::GetDlgItem(a[0], a[1]), a[2], a[3], a[4]);
    API(GetMessageTime) return User::MessageMetadata(0);
    API(GetMessagePos) return User::MessageMetadata(1);
    API(GetMessageExtraInfo) return User::MessageMetadata(2);
    API(SetMessageExtraInfo) return User::MessageMetadata(3,a[0]);
    // USER32 and 325Ah enter the FT_UsrF2Thk block with thunk indices
    // 1Fh and 1Eh; the 16-bit sides are in USER.EXE. Neither
    // the thunklet nor the body sets a Win32 error code, and 1409 does not
    // occur anywhere in this USER.EXE or USER32.DLL.
    API(RegisterHotKey) return User::RegisterHotKey(a[0], a[1], a[2], a[3]);
    API(DdeAccessData) API(DdeClientTransaction) API(DdeCmpStringHandles) API(DdeConnect) API(DdeCreateDataHandle) API(DdeCreateStringHandleA)
    API(DdeCreateStringHandleW) API(DdeDisconnect) API(DdeFreeDataHandle) API(DdeFreeStringHandle) API(DdeGetData) API(DdeGetLastError)
    API(DdeInitializeA) API(DdeInitializeW) API(DdeKeepStringHandle) API(DdeNameService) API(DdeQueryStringA) API(DdeQueryStringW)
    API(DdeUnaccessData) API(DdeUninitialize) API(DdeAddData) API(DdeEnableCallback) API(DdeReconnect)
        return DdeApi(name, a);
    API(UnregisterHotKey) return User::UnregisterHotKey(a[0], a[1]);
    API(PostThreadMessageA)
    {
        // USER answers FALSE and sets no error code; 5A4h does not
        // appear in the local binary. The adapter reports the documented
        // ERROR_INVALID_THREAD_ID anyway, because callers here expect it.
        const bool result = User::PostThreadMessage(a[0], a[1], a[2], a[3]);
        if (!result) last_error = 1444;
        return result;
    }
    API(PostThreadMessageW)
    {
        // USER32 shares for the wide entries: KERNEL32 ordinal 17
        // sets ERROR_CALL_NOT_IMPLEMENTED and answers zero.
        (void)a;
        last_error = Win98PostThreadMessage::WideError;
        return Win98PostThreadMessage::WideResult;
    }
    API(PostMessageA)
    {
        if (!Win98PostThreadMessage::MessageAllowed(a[1])) return 0;
        return Win98Broadcast::IsBroadcast(a[0])
                   ? BroadcastMessage(Win98Broadcast::Post, a[1], a[2], a[3])
                   : User::PostMessage(a[0], a[1], a[2], a[3]);
    }
    API(wsprintfA) return Printf(a[0], a[1], Register(Reg::Esp) + 12);
    API(wvsprintfA) return Printf(a[0], a[1], a[2]);
    API(DrawAnimatedRects)
    {
        last_error = 120;
        if (log)
            log("DrawAnimatedRects unavailable; returned FALSE");
        return 0;
    }
    API(CreateCaret) return CaretApi(name,a);
    API(DestroyCaret) return CaretApi(name,a);
    API(ShowCaret) return CaretApi(name,a);
    API(HideCaret) return CaretApi(name,a);
    API(SetCaretPos) return CaretApi(name,a);
    API(GetCaretPos) return CaretApi(name,a);
    API(SetCaretBlinkTime) return CaretApi(name,a);
    API(GetCaretBlinkTime) return CaretApi(name,a);
    API(SetTimer) return TimerApi(name, a);
    API(KillTimer) return TimerApi(name, a);
    API(PostQuitMessage) {User::PostQuitMessage(a[0]);return 0;}
    API(SetWindowsHookExA) return HookApi(name, a);
    API(UnhookWindowsHookEx) return HookApi(name, a);
    API(CallNextHookEx) return HookApi(name, a);
    API(CallMsgFilterA) return HookApi(name, a);
    API(CallMsgFilter) return HookApi(name, a);
    API(SendMessageCallbackA)
    {
        // USER32 SendMessageCallback: the message is delivered, then the
        // caller's SENDASYNCPROC(hwnd, msg, data, result) runs. The original
        // answers a same-thread send synchronously and a cross-thread one from
        // the reply; both end in the callback on the sender's thread, which is
        // what happens here once the synchronous send returns.
        if (!User::IsWindow(a[0])) { last_error = 1400; return 0; }
        const unsigned result = Send(a[0], a[1], a[2], a[3]);
        if (state == Failed || state == Exited) return 0;
        if (a[4]) { unsigned arguments[] = {a[0], a[1], a[5], result}; Call(a[4], arguments, 4); }
        return 1;
    }
    API(WaitForInputIdle) return WaitForInputIdleApi(a);
    API(GetQueueStatus) return QueueStatusApi(name,a);
    API(GetInputState) return QueueStatusApi(name,a);
    API(GetMessageA) API(PeekMessageA)
    {
        const bool fresh = TakeFresh();
        input_idle = true; // WaitForInputIdle: the process has reached its message loop
        unsigned settings = 0;
        bool recorded = false;
        if (fresh && HasDosSettingsRequests())
        {
            settings = Allocate(24);
            if (settings)
            {
                unsigned args[6] = {API_IS(name, "PeekMessageA") ? 1u : 0u, a[0], a[1], a[2], a[3], 0};
                if (args[0]) args[5] = a[4];
                Store(settings, args, sizeof args);
                recorded = PushHostFrame(HostSettingsApi, settings);
            }
        }
        ServeOpenRequests(recorded); // a file the person opened in the Finder (src/x86_host_open.cpp)
        if (recorded) PopHostFrame();
        if (settings) Free(settings);
        ServeHostClipboard(); // what the person copied on the Mac (src/host_clipboard.h)
        if (state == Failed || state == Exited) return 0;
        ProcessSentMessages();
        ProcessWinEvents(); // out-of-context WinEvents wait in the queue too (src/x86_win_event.cpp)
        if (const unsigned posted = User::TakeQueueActivation(ThreadId()))
            if (User::IsWindow(posted) && User::ForegroundQueue() == ThreadId())
                ActivateInQueue(ThreadId(), posted, 2);
        if (User::TakeQueueLeave(ThreadId())) LeaveForeground();
        {
            unsigned clicked = 0; bool take_focus = false;
            if (User::TakeQueueMouseActivation(ThreadId(), clicked, take_focus) && User::IsWindow(clicked))
                MouseFocus(clicked, take_focus);
        }
        User::NoteQueueRead(ThreadId(), Now());   // IsHungThread, USER
        if (state == Failed || state == Exited)
            return 0;
        WaitQueueStatus(current_thread,true,User::FilterWakeMask(a[2],a[3]));
        // PM_NOREMOVE leaves the old move in USER's queue. Refresh it from
        // consecutive motion packets before selecting a message; transitions
        // still wait until retrieval so capture can change on a button press.
        if(current_thread==InputThread()&&User::MouseMovePending()&&!User::HardwareInputUnread(process_id))
            for(unsigned n=0;n<256&&input_count&&state!=Failed&&state!=Exited;++n){
                const auto &packet=input_packets[input_head];
                if(packet.kind!=2||packet.changed)break;
                RoutePacket(0);
            }
        User::Message message = {};
        bool get = API_IS(name,"GetMessageA");
        bool found = User::PeekMessage(&message, a[1], a[2], a[3], get || (a[4] & 1), current_thread != InputThread() || !InputPending());
        // A filtered retrieval must search past hardware transitions that
        // produced no matching message. Stop as soon as one matches so the
        // caller can change capture/focus before the next transition routes.
        for (int next; !found && current_thread == InputThread() && state != Failed && state != Exited &&
                       (next = MatchingInput(a[1], a[2], a[3])) >= 0;)
        {
            RoutePacket(unsigned(next));
            found = User::PeekMessage(&message, a[1], a[2], a[3], get || (a[4] & 1), current_thread != InputThread() || !InputPending());
        }
        if (state == Failed || state == Exited) return 0;
        // Paint requests have lower priority than posted or hardware
        // messages. When retrieval finds no work or only paint work, service
        // native controls here so color and owner-draw callbacks run on their
        // owning thread.
        if ((!found || message.message == 0x0f) &&
            User::ServeControlPaints() && !found)
        {
            if (state == Failed || state == Exited) return 0;
            found = User::PeekMessage(&message, a[1], a[2], a[3], get || (a[4] & 1), current_thread != InputThread() || !InputPending());
        }
        if (state == Failed || state == Exited) return 0;
        if (!found){
            found = TimerMessage(message, a[1], a[2], a[3], get || (a[4] & 1));
            if(found)Win98MessageMetadata::Posted(threads[current_thread].message_metadata,{message.time,Win98MessageMetadata::Pack(message.x,message.y),0},1);
        }
        // A keyboard or mouse hook that answers non-zero discards the input
        // message; the retrieval then goes on with the next one.
        while (found && InputHook(message, get || (a[4] & 1)))
        {
            if (state == Failed || state == Exited) return 0;
            if (!get && !(a[4] & 1))
                User::PeekMessage(&message, message.hwnd, message.message, message.message, true, current_thread != InputThread() || !InputPending());
            found = User::PeekMessage(&message, a[1], a[2], a[3], get || (a[4] & 1), current_thread != InputThread() || !InputPending());
        }
        if (state == Failed || state == Exited) return 0;
        if (found)
        {
            Store(a[0], &message, sizeof message);
            MessageHook(3, 0, get || (a[4] & 1), a[0]);
            if (state == Failed || !Read(a[0], &message, sizeof message))
                return 0;
        }
        if (get)
        {
            auto &thread = threads[current_thread];
            thread.message_window = found ? 0 : a[1];
            thread.message_minimum = found ? 0 : a[2];
            thread.message_maximum = found ? 0 : a[3];
            waiting = !found;
        }
        return found && (!get || message.message != 0x12);
    }
    API(GetKeyState) return User::GetKeyState(a[0]);
    API(GetAsyncKeyState) return User::GetAsyncKeyState(a[0]);
    API(GetKeyboardLayout) API(GetKeyboardLayoutList) API(GetKeyboardLayoutNameA) API(ActivateKeyboardLayout) API(LoadKeyboardLayoutA) API(UnloadKeyboardLayout)
        return KeyboardLayoutApi(name,a);
    API(GetKeyboardState) API(SetKeyboardState)
    {
        unsigned char buffer[256] = {};
        const bool set = API_IS(name,"SetKeyboardState");
        if (set && !Read(a[0],buffer,sizeof buffer)) return 0;
        const auto result = User::KeyboardState(buffer,set);
        return !set && !Store(a[0],buffer,sizeof buffer) ? 0 : result;
    }
    API(TranslateMessage)
    {
        // USER32.DLL answers 0 for a NULL MSG before it reads it.
        if (!a[0])
            return 0;
        User::Message m = {};
        Read(a[0], &m, sizeof m);
        return User::TranslateMessage(&m);
    }
    API(DispatchMessageA)
    {
        User::Message m = {};
        Read(a[0], &m, sizeof m);
        if(m.message==0x118&&m.lparam==User::MouseHoverTimerProcedure){
            User::DispatchMouseHoverTimer(m.hwnd,m.wparam);return 0;
        }
        // USER hands SetTimer a procedure of its own, so the tick of a
        // flashing caption never reaches the program's window procedure.
        if (m.message == 0x113 && m.lparam == FlashTimerProcedure)
        {
            FlashTick(m.hwnd);
            return 0;
        }
        if (m.message == 0x113 && m.lparam)
        {
            unsigned args[] = {m.hwnd, m.message, m.wparam, TickCount()};
            TailOfGate(TailZero);
            Call(m.lparam, args, 4);
            return 0;
        }
        // A dispatched message is not a sent one: WH_CALLWNDPROC stays out.
        if (m.message == 0xf) return DispatchPaint(m.hwnd, m.wparam, m.lparam);
        const bool outer = dispatching_posted;
        dispatching_posted = true;
        TailOfGate(TailResult);
        const unsigned answer = Send(m.hwnd, m.message, m.wparam, m.lparam);
        dispatching_posted = outer;
        return answer;
    }
    API(TranslateAcceleratorA)
    {
        User::Message m = {};
        Read(a[2], &m, sizeof m);
        if (m.message != 0x100 && m.message != 0x102 && m.message != 0x104 && m.message != 0x106)
            return 0;
        // A table CreateAcceleratorTableA built is in the native store; one
        // LoadAcceleratorsA returned is still the resource. Windows 98 walks both
        // the same way and stops after the entry whose fVirt carries bit 80h.
        const Win98Accelerators::Entry *created = nullptr;
        unsigned count = 0;
        if (AcceleratorTableEntries(a[1], created, count))
        {
            for (unsigned i = 0; i < count; ++i)
            {
                if (Win98Accelerators::Matches(created[i].virt, created[i].key, m.message,
                                               m.wparam, User::Modifiers()))
                {
                    CallWindow(a[0], 0x111, created[i].command | 0x10000);
                    return 1;
                }
                if (created[i].virt & Win98Accelerators::FlagLast) break;
            }
            return 0;
        }
        unsigned length = 0;
        const unsigned address = AcceleratorResource(a[1], length);
        const auto *data = address ? nullptr : image.Resource(9, a[1], &length);
        if (!address && !data)
            return 0;
        for (unsigned i = 0; i + 8 <= length; i += 8)
        {
            unsigned short wire[4]{};
            if (address && !Read(address + i, wire, sizeof wire)) return 0;
            unsigned flags = address ? wire[0] : Word(data + i),
                     key = address ? wire[1] : Word(data + i + 2),
                     command = address ? wire[2] : Word(data + i + 4);
            if (Win98Accelerators::Matches(flags, key, m.message, m.wparam, User::Modifiers()))
            {
                CallWindow(a[0], 0x111, command | 0x10000);
                return 1;
            }
        }
        return 0;
    }
    API(IsDialogMessageA)
    {
        User::Message message = {};
        if (!Read(a[1], &message, sizeof message))
            return 0;
        const auto result = DialogMessage(a[0], message);
        return Store(a[1], &message, sizeof message) ? result : 0;
    }
    API(LoadMenuA)
    {
        if (a[1] < 65536) return LoadMenu(a[1], a[0], false);
        // "#123" is a number; anything else names the resource.
        const char *text = ReadString(a[1]);
        if (!text) return 0;
        if (text[0] == '#')
        {
            unsigned id = 0;
            for (const char *p = text + 1; *p >= '0' && *p <= '9'; ++p) id = id * 10 + unsigned(*p - '0');
            return id && id < 65536 ? LoadMenu(id, a[0], false) : 0;
        }
        return LoadNamedMenu(text, a[0]);
    }
    API(SetMenu)
    {
        // USER stores the menu and, unless minimized, redraws the frame
        // through SetWindowPos with SWP_FRAMECHANGED.
        // Keep the previous client rectangle until WM_NCCALCSIZE. Eager
        // layout hides the size change from WM_WINDOWPOSCHANGED.
        if(!User::SetMenu(a[0],a[1],false))return 0;
        if(!(User::Style(a[0])&0x20000000)){unsigned position[1][7]={{a[0],0,0,0,0,0,0x37}};PositionWindows(1,position);}
        return 1;
    }
    API(DrawMenuBar)
    {
        if (!User::IsWindow(a[0])) return 0;
        // USER uses the same frame-change helper as SetMenu.
        if ((User::Style(a[0]) & 0xc0000000u) != 0x40000000u)
        {
            unsigned position[1][7] = {{a[0], 0, 0, 0, 0, 0, 0x37}};
            PositionWindows(1, position);
        }
        return User::IsWindow(a[0]) ? User::DrawMenuBar(a[0]) : 0;
    }
    API(GetMenu) return User::ControlID(a[0]);
    API(GetSubMenu) return User::GetSubMenu(a[0], a[1]);
    API(DeleteMenu) return User::RemoveMenu(a[0], a[1], a[2] & 0x400, true);
    API(RemoveMenu) return User::RemoveMenu(a[0], a[1], a[2] & 0x400, false);
    API(DestroyMenu) return User::DestroyMenu(a[0]);
    API(GetDoubleClickTime) return User::GetDoubleClickTime();
    API(SetDoubleClickTime)
    {
        User::SetDoubleClickTime(a[0]);
        return 1;
    }
    API(GetMenuBarInfo) API(TrackPopupMenu) API(TrackPopupMenuEx) API(EndMenu) API(GetMenuItemRect) API(MenuItemFromPoint) return MenuTrackingApi(name,a);
    API(SetMenuDefaultItem) return User::SetMenuDefault(a[0], a[1], a[2] != 0);
    API(CreateMenu) return User::CreateMenu();
    API(CreatePopupMenu) return User::CreateMenu(true);
    API(AppendMenuA)
    {
        unsigned args[] = {a[0], ~0u, a[1] | 0x400, a[2], a[3]};
        return Api("InsertMenuA", args);
    }
    API(InsertMenuA) API(ModifyMenuA)
    {
        const char *text = (a[2] & 0x904) ? reinterpret_cast<const char *>(static_cast<__UINTPTR_TYPE__>(a[4])) : a[4] ? ReadString(a[4]) : nullptr;
        if (state == Failed) return 0;
        return API_IS(name,"InsertMenuA") ? User::InsertMenu(a[0],a[1],a[2],a[3],text) : User::ModifyMenu(a[0],a[1],a[2],a[3],text);
    }
    API(GetMenuItemInfoA) API(SetMenuItemInfoA) API(InsertMenuItemA)
    {
        unsigned p[12]={}; if(!Read(a[3],p,4))return 0;
        if(p[0]!=44 && p[0]!=48)return 0;
        if(!Read(a[3],p,p[0]))return 0;
        bool get=API_IS(name,"GetMenuItemInfoA");
        if(get){Win98Menu::PrepareGet32(p);if((p[1]&0x10)&&!Store(a[3]+4,p+1,4))return 0;}
        auto info=Win98Menu::From32(p); unsigned pointer=p[9],capacity=info.length;
        char *buffer=nullptr;
        if(get && (info.mask&0x40) && capacity){
            // The original probes the caller's string buffer only when the
            // item has text. A bitmap item may carry an unusable pointer and
            // still succeeds; leave it unwritable for the menu core then.
            if(virtual_memory.Access(pointer,capacity,true)){
                buffer=static_cast<char *>(malloc(65536));if(!buffer){last_error=8;return 0;}
                memset(buffer,0,65536);
            }
            info.text=reinterpret_cast<Win98Menu::P>(buffer);
        }else if(!get && (info.mask&0x40) && pointer){
            info.text=reinterpret_cast<Win98Menu::P>(ReadString(pointer));if(state==Failed)return 0;
        }
        bool result=User::MenuItemInfo(a[0],a[1],a[2],info,get?0:API_IS(name,"SetMenuItemInfoA")?1:2);
        if(get){
            if(buffer && result){unsigned copied=info.length;if(copied>=65536)result=false;else if(!Store(pointer,buffer,copied+1))result=false;}
            // USER's mapped string buffer has offset zero in its 16-bit
            // selector. The bitmap MIIM_TYPE copy-back uses that offset as
            // cch, not the low word of the caller's flat Win32 address.
            info.text=(info.mask&0x10)&&info.bitmap?0:pointer;
            if(!Win98Menu::Publish32(a[3],info,[&](unsigned at,unsigned&value){return Read(at,&value,4);},[&](unsigned at,unsigned value){return Store(at,&value,4);}))result=false;
        }
        free(buffer);return result;
    }
    API(IsMenu) return User::IsMenu(a[0]);
    API(GetMenuState) return User::MenuState(a[0],a[1],a[2]);
    API(GetMenuDefaultItem) return User::MenuDefault(a[0],a[1],a[2]);
    API(CheckMenuRadioItem) return User::MenuRadio(a[0],a[1],a[2],a[3],a[4]);
    API(GetMenuContextHelpId) return User::MenuContextHelp(a[0],0,false);
    API(SetMenuContextHelpId) return User::MenuContextHelp(a[0],a[1],true);
    API(SetMenuItemBitmaps) {Win98Menu::Info i;i.mask=8;i.unchecked=a[3];i.checked=a[4];return User::MenuItemInfo(a[0],a[1],a[2]&0x400,i,1);}
    API(GetMenuCheckMarkDimensions) return User::MenuCheckMarkSize();
    API(GetMenuInfo) API(SetMenuInfo)  {
        Win98Menu::MenuInfo info{};if(!Read(a[1],&info,4)||info.size!=28)return 0;
        if(!Read(a[1],&info,28))return 0;bool set=API_IS(name,"SetMenuInfo");
        bool result=User::MenuDetails(a[0],info,set);if(result&&!set)result=Store(a[1],&info,28);return result;
    }
    API(GetMenuStringA) {
        Win98Menu::Info i;i.mask=0x40;i.length=static_cast<unsigned short>(a[3]);
        char *buffer=nullptr;if(i.length){unsigned char zero=0;if(!Store(a[2],&zero,1))return 0;buffer=static_cast<char*>(malloc(65536));if(!buffer){last_error=8;return 0;}memset(buffer,0,65536);i.text=reinterpret_cast<Win98Menu::P>(buffer);}
        bool result=User::MenuItemInfo(a[0],a[1],a[4]&0x400,i,0);
        if(buffer&&result)result=Store(a[2],buffer,unsigned(i.length)+1);free(buffer);return result?i.length:0;
    }
    API(GetMenuItemID) return User::MenuItemID(a[0], a[1]);
    API(GetMenuItemCount) return User::MenuItemCount(a[0]);
    API(EnableMenuItem) return User::SetMenuFlags(a[0], a[1], a[2], (a[2] & 0x400) != 0, 3);
    API(CheckMenuItem) return User::SetMenuFlags(a[0], a[1], a[2], (a[2] & 0x400) != 0, 8);
    API(GetStockObject)
    {
        if (a[0] < 18)
            return stock[a[0]];
        // Win98 has stock indices 0..17; newer/invalid indices return NULL.
        return 0;
    }
    API(GetObjectA) return GdiApi(name,a);
    // Metafiles live in GdiApi. Win32 imports reach it through routing; Win16
    // gates call it by name (src/win16_metafile.cpp).
    API(CreateMetaFileA) API(CloseMetaFile) API(DeleteMetaFile) API(CopyMetaFileA)
    API(GetMetaFileA) API(GetMetaFileBitsEx) API(SetMetaFileBitsEx) API(PlayMetaFile)
        return GdiApi(name,a);
    API(CreateFontA) return FontCreateApi(name,a);
    API(CreateFontIndirectA) return FontCreateApi(name,a);
    API(SelectObject) {
        if (Gdi::GetObjectType(a[1]) == 8) return GdiApi("SelectClipRgn", a);
        return PublicGdiObject(Gdi::SelectObject(a[0], a[1]));
    }
    API(DeleteObject)
    {
        for (auto brush : system_brushes)
            if (brush && brush == a[0])
                return 0;
        for (auto value : stock)
            if (value && value == a[0])
                return 1; // Stock objects survive DeleteObject.
        bool result = Gdi::DeleteObject(a[0]);
        if (result && !Gdi::ObjectExists(a[0]))
        {
            for (auto &f : fonts) if (f.handle == a[0]) f.handle = 0;
            for (auto &object : gdi_objects) if (object.handle == a[0]) object = {};
        }
        return result;
    }
    API(GetDeviceCaps) return Gdi::DeviceCaps(a[0],a[1]);
    API(GetTextMetricsA) return GdiApi(name,a);
    API(GetTextExtentPointA)
    {
        if (a[2] > 8191)
        {
            Fail("Text extent input too long");
            return 0;
        }
        char text[8192];
        Read(a[1], text, a[2]);
        Gdi::SIZE s = {};
        bool ok = Gdi::GetTextExtentPoint32A(a[0], text, a[2], &s);
        Store(a[3], &s, sizeof s);
        return ok;
    }
    API(PathGetArgsA) return ShellPathSyntaxApi(name,a);
    API(PathGetArgsW) return ShellPathSyntaxApi(name,a);
    API(PathRemoveArgsA) return ShellPathSyntaxApi(name,a);
    API(PathRemoveArgsW) return ShellPathSyntaxApi(name,a);
    API(PathQuoteSpacesA) return ShellPathSyntaxApi(name,a);
    API(PathQuoteSpacesW) return ShellPathSyntaxApi(name,a);
    API(PathUnquoteSpacesA) return ShellPathSyntaxApi(name,a);
    API(PathUnquoteSpacesW) return ShellPathSyntaxApi(name,a);
    API(PathRemoveBlanksA) return ShellPathSyntaxApi(name,a);
    API(PathRemoveBlanksW) return ShellPathSyntaxApi(name,a);
    API(PathFindExtensionA) return ShellPathSyntaxApi(name,a);
    API(PathFindExtensionW) return ShellPathSyntaxApi(name,a);
    API(PathFindFileNameA) return ShellPathSyntaxApi(name,a);
    API(PathFindFileNameW) return ShellPathSyntaxApi(name,a);
    API(PathFindNextComponentA) return ShellPathSyntaxApi(name,a);
    API(PathFindNextComponentW) return ShellPathSyntaxApi(name,a);
    API(PathAddExtensionA) return ShellPathSyntaxApi(name,a);
    API(PathAddExtensionW) return ShellPathSyntaxApi(name,a);
    API(PathRenameExtensionA) return ShellPathSyntaxApi(name,a);
    API(PathRenameExtensionW) return ShellPathSyntaxApi(name,a);
    API(PathRemoveExtensionA) return ShellPathSyntaxApi(name,a);
    API(PathRemoveExtensionW) return ShellPathSyntaxApi(name,a);
    API(PathGetDriveNumberA) return ShellPathSyntaxApi(name,a);
    API(PathGetDriveNumberW) return ShellPathSyntaxApi(name,a);
    API(SHGetSettings) return CabinetApi(290,a);
    API(Control_RunDLL) return ShellControlApi(name,a);
    API(Control_RunDLLA) return ShellControlApi(name,a);
    API(ShellExecuteA) return ShellExecuteApi(name,a);
    API(ShellExecuteExA) return ShellExecuteApi(name,a);
    API(PathFindOnPathA) return ShellCommandApi(479,a);
    API(PathFileExistsA) API(PathFileExistsW) API(PathIsDirectoryA) API(PathIsDirectoryW)
    API(PathIsDirectoryEmptyA) API(PathIsDirectoryEmptyW) API(PathIsUNCServerA) API(PathIsUNCServerW)
    API(PathIsUNCServerShareA) API(PathIsUNCServerShareW)
        return PathProbeApi(name,a);
    API(PathIsFileSpecA) return ShellPathSyntaxApi(name,a);
    API(PathIsFileSpecW) return ShellPathSyntaxApi(name,a);
    API(PathParseIconLocationA) return ShellPathSyntaxApi(name,a);
    API(PathParseIconLocationW) return ShellPathSyntaxApi(name,a);
    API(PathGetCharTypeA) return ShellPathSyntaxApi(name,a);
    API(PathGetCharTypeW) return ShellPathSyntaxApi(name,a);
    API(StrCpyW) return ShellCopyApi(name,a);
    API(StrCpyNW) return ShellCopyApi(name,a);
    API(StrCatW) return ShellCopyApi(name,a);
    API(StrCatBuffA) return ShellCopyApi(name,a);
    API(StrCatBuffW) return ShellCopyApi(name,a);
    API(StrNCatA) return ShellCopyApi(name,a);
    API(StrNCatW) return ShellCopyApi(name,a);
    API(StrDupA) return ShellCopyApi(name,a);
    API(StrDupW) return ShellCopyApi(name,a);
    API(SHStrDupA) return ShellCopyApi(name,a);
    API(SHStrDupW) return ShellCopyApi(name,a);
    API(StrToIntA) return ShellNumberApi(name,a);
    API(StrToIntW) return ShellNumberApi(name,a);
    API(StrToIntExA) return ShellNumberApi(name,a);
    API(StrToIntExW) return ShellNumberApi(name,a);
    API(StrCmpW) return ShellCompareApi(name,a);
    API(StrCmpIW) return ShellCompareApi(name,a);
    API(StrCmpNW) return ShellCompareApi(name,a);
    API(StrCmpNIW) return ShellCompareApi(name,a);
    API(StrCmpNA) return ShellCompareApi(name,a);
    API(StrCmpNIA) return ShellCompareApi(name,a);
    API(ChrCmpIA) return ShellCompareApi(name,a);
    API(ChrCmpIW) return ShellCompareApi(name,a);
    API(StrChrA) return ShellSearchApi(name,a);
    API(StrChrW) return ShellSearchApi(name,a);
    API(StrRChrA) return ShellSearchApi(name,a);
    API(StrRChrW) return ShellSearchApi(name,a);
    API(StrPBrkA) return ShellSearchApi(name,a);
    API(StrPBrkW) return ShellSearchApi(name,a);
    API(StrSpnA) return ShellSearchApi(name,a);
    API(StrSpnW) return ShellSearchApi(name,a);
    API(StrCSpnA) return ShellSearchApi(name,a);
    API(StrCSpnW) return ShellSearchApi(name,a);
    API(GetProfileIntA) return ProfileApi(name,a);
    API(GetPrivateProfileIntA) return ProfileApi(name,a);
    API(GetProfileStringA) return ProfileApi(name,a);
    API(GetPrivateProfileStringA) return ProfileApi(name,a);
    API(GetProfileSectionA) return ProfileApi(name,a);
    API(GetPrivateProfileSectionA) return ProfileApi(name,a);
    API(GetPrivateProfileSectionNamesA) return ProfileApi(name,a);
    API(WriteProfileStringA) return ProfileApi(name,a);
    API(WritePrivateProfileStringA) return ProfileApi(name,a);
    API(WriteProfileSectionA) return ProfileApi(name,a);
    API(WritePrivateProfileSectionA) return ProfileApi(name,a);
    API(GetPrivateProfileStructA) return ProfileApi(name,a);
    API(WritePrivateProfileStructA) return ProfileApi(name,a);
    API(GetLocaleInfoA) return LocaleInfo(a);
    API(RaiseException) return ExceptionApi(name, a);
    API(RtlUnwind) return ExceptionApi(name, a);
    API(DebugBreak) return ExceptionApi(name, a);
    API(ConvertThreadToFiber) return MiscFamilyApi(name, a);
    API(CreateFiber) return MiscFamilyApi(name, a);
    API(DeleteFiber) return MiscFamilyApi(name, a);
    API(SwitchToFiber) return MiscFamilyApi(name, a);
    API(ContinueDebugEvent) return MiscFamilyApi(name, a);
    API(DebugActiveProcess) return MiscFamilyApi(name, a);
    API(WaitForDebugEvent) return MiscFamilyApi(name, a);
    API(_DebugOut) return MiscFamilyApi(name, a);
    API(_DebugPrintf) return MiscFamilyApi(name, a);
    API(dprintf) return MiscFamilyApi(name, a);
    API(FatalAppExitA) return MiscFamilyApi(name, a);
    API(FatalExit) return MiscFamilyApi(name, a);
    API(Beep) return MiscFamilyApi(name, a);
    API(GetThreadSelectorEntry) return MiscFamilyApi(name, a);
    API(GetWriteWatch) return MiscFamilyApi(name, a);
    API(ResetWriteWatch) return MiscFamilyApi(name, a);
    API(TlsAllocInternal) return MiscFamilyApi(name, a);
    API(TlsFreeInternal) return MiscFamilyApi(name, a);
    API(CreateKernelThread) return MiscFamilyApi(name, a);
    API(GetProductName) return MiscFamilyApi(name, a);
    API(K32_NtCreateFile) return MiscFamilyApi(name, a);
    API(K32_RtlNtStatusToDosError) return MiscFamilyApi(name, a);
    API(NotifyNLSUserCache) return MiscFamilyApi(name, a);
    API(ResetNLSUserInfoCache) return MiscFamilyApi(name, a);
    API(RegisterSysMsgHandler) return MiscFamilyApi(name, a);
    API(SignalSysMsgHandlers) return MiscFamilyApi(name, a);
    API(SetMessageWaitingIndicator) return MiscFamilyApi(name, a);
    API(CancelDeviceWakeupRequest) return MiscFamilyApi(name, a);
    API(RequestDeviceWakeup) return MiscFamilyApi(name, a);
    API(RequestWakeupLatency) return MiscFamilyApi(name, a);
    API(GetDevicePowerState) return MiscFamilyApi(name, a);
    API(SetSystemPowerState) return MiscFamilyApi(name, a);
    API(SetUnhandledExceptionFilter) return ExceptionApi(name, a);
    API(UnhandledExceptionFilter) return ExceptionApi(name, a);
    API(PropertySheet) API(PropertySheetA) API(PropertySheetW) return PropertySheetApi(name,a);
    API(CreatePropertySheetPage) return PropertyPageApi(name, a);
    API(CreatePropertySheetPageA) return PropertyPageApi(name, a);
    API(CreatePropertySheetPageW) return PropertyPageApi(name, a);
    API(DestroyPropertySheetPage) return PropertyPageApi(name, a);

    API(GetDateFormatA) return DateTimeFormatApi(name, a);
    API(GetTimeFormatA) return DateTimeFormatApi(name, a);
    API(EnumDateFormatsA) return DateTimeFormatApi(name, a);
    API(EnumTimeFormatsA) return DateTimeFormatApi(name, a);
    API(GetNumberFormatA) return NumberFormatApi(name,a);
    API(GetCurrencyFormatA) return NumberFormatApi(name,a);
    API(GetUserDefaultLCID) return UserDefaultLocale();
    API(GetSystemDefaultLCID) return DefaultLocaleApi(name);
    API(GetSystemDefaultLangID) return DefaultLocaleApi(name);
    API(GetUserDefaultLangID) return DefaultLocaleApi(name);
    API(GetThreadLocale) return DefaultLocaleApi(name);
    API(InvalidateNLSCache) return DefaultLocaleApi(name);
    API(RegCreateKeyA) API(RegCreateKeyExA) API(RegDeleteKeyA) API(RegDeleteValueA) API(RegEnumKeyA) API(RegEnumKeyExA)
    API(RegEnumValueA) API(RegOpenKeyA) API(RegOpenKeyExA) API(RegQueryInfoKeyA) API(RegQueryValueA) API(RegQueryValueExA)
    API(RegSetValueA) API(RegSetValueExA) API(RegCloseKey) API(RegFlushKey)
        return RegistryApi(name, a);
    API(RegConnectRegistryA) API(RegNotifyChangeKeyValue) return AdvapiRegistryApi(name, a);
    API(RegLoadKeyA) API(RegUnLoadKeyA) API(RegSaveKeyA) API(RegReplaceKeyA) API(RegQueryMultipleValuesA)
    API(RegRemapPreDefKey) return RegistryHiveApi(name, a);
    API(CryptAcquireContextA) API(CryptReleaseContext) API(CryptContextAddRef) API(CryptCreateHash)
    API(CryptDecrypt) API(CryptDeriveKey) API(CryptDestroyHash) API(CryptDestroyKey)
    API(CryptDuplicateHash) API(CryptDuplicateKey) API(CryptEncrypt) API(CryptEnumProviderTypesA)
    API(CryptEnumProvidersA) API(CryptExportKey) API(CryptGenKey) API(CryptGenRandom)
    API(CryptGetDefaultProviderA) API(CryptGetHashParam) API(CryptGetKeyParam)
    API(CryptGetProvParam) API(CryptGetUserKey) API(CryptHashData) API(CryptHashSessionKey)
    API(CryptImportKey) API(CryptSetHashParam) API(CryptSetKeyParam) API(CryptSetProvParam)
    API(CryptSetProviderA) API(CryptSetProviderExA) API(CryptSignHashA)
    API(CryptVerifySignatureA) return AdvapiCryptoApi(name, a);
    API(WinHelpA) return WinHelpApi(a);
    API(ExtractIconA) API(ExtractIconEx) API(ExtractIconExA) API(ExtractIconW) API(InternalExtractIconListA) API(InternalExtractIconListW)
        return ShellIconApi(name,a);
    API(SHGetSpecialFolderPathA) return SpecialFolderApi(name,a);
    API(SHGetSpecialFolderPathW) return SpecialFolderApi(name,a);
    API(GetPropA) return PropertyApi(name, a);
    API(SetPropA) return PropertyApi(name, a);
    API(RemovePropA) return PropertyApi(name, a);
    API(GlobalAddAtomA) return GlobalAtomApi(name, a);
    API(GlobalFindAtomA) return GlobalAtomApi(name, a);
    API(GlobalDeleteAtom) return GlobalAtomApi(name, a);
    API(GlobalGetAtomNameA) return GlobalAtomApi(name, a);
    API(GlobalAddAtomW) return GlobalAtomApi(name, a);
    API(GlobalFindAtomW) return GlobalAtomApi(name, a);
    API(GlobalGetAtomNameW) return GlobalAtomApi(name, a);
    API(RegisterClipboardFormatA) return RegisterAtom(a[0]);
    API(GetShellWindow) return ShellWindow();
    API(SetShellWindow) return SetShellWindow(a[0]);
    API(AddAtomA) return AtomApi(name,a);
    API(FindAtomA) return AtomApi(name,a);
    API(DeleteAtom) return AtomApi(name,a);
    API(GetAtomNameA) return AtomApi(name,a);
    API(InitAtomTable) return AtomApi(name,a);
    API(RegisterWindowMessageA) return RegisterAtom(a[0]);
    API(DragAcceptFiles)
    {
        if (User::IsWindow(a[0]))
            drag_accept[(a[0] & 255) - 1] = a[1] != 0;
        return 0;
    }
    API(ScreenToClient) return CoordinatesApi(name,a);
    API(ClientToScreen) return CoordinatesApi(name,a);
    API(GetCursorPos)
    {
        Gdi::POINT point;
        User::CursorPosition(&point);
        return Store(a[0], &point, sizeof point);
    }
    API(ChildWindowFromPoint) return WindowApi(name,a);
    API(ChildWindowFromPointEx) return WindowApi(name,a);
    API(RealChildWindowFromPoint) return WindowApi(name,a);
    API(WindowFromPoint) return WindowApi(name,a);
    API(SetWindowRgn) return WindowApi(name,a);
    API(GetWindowRgn) return WindowApi(name,a);
    API(ShowCursor)
    {
        // USER answers the level of the caller's queue.
        cursor_count += a[0] ? 1u : ~0u;
        return unsigned(User::ShowPointerCount(a[0] != 0));
    }
    API(SetCursor)
    {
        unsigned old = CurrentCursor();
        threads[current_thread].cursor_handle = a[0];
        cursor = a[0];
        User::SetPointerShape(a[0]);
        return old;
    }
    API(ClipCursor)
    {
        if (!a[0]) return User::ClipPointer(nullptr);
        int r[4];
        if (!Read(a[0], r, sizeof r)) { last_error = 998; return 0; }
        const Gdi::RECT want = {r[0], r[1], r[2], r[3]};
        int x = -1, y = -1;
        const unsigned okay = User::ClipPointer(&want, &x, &y);
        // The pointer the clip moves reaches the windows as a mouse move in
        // the input queue, as SetCursorPos does; the program itself is no
        // hand on the mouse for DINPUT.
        if (x >= 0)
        {
            dinput_warping = true;
            MouseButtons(x, y, User::PhysicalMouseButtons());
            dinput_warping = false;
        }
        return okay;
    }
    API(GetClipCursor)
    {
        Gdi::RECT r = {};
        if (!a[0] || !User::PointerClip(&r)) { last_error = 998; return 0; }
        const int values[4] = {r.left, r.top, r.right, r.bottom};
        return Store(a[0], values, sizeof values) ? 1 : 0;
    }
    // USER, six instructions: the WORD at offset 0x38 of the current
    // thread's USER info record, which is the field the SETCURSOR worker at
    // Windows 98 writes. Keep that queue field separate from the compositor's
    // current physical shape and process-launch feedback.
    API(GetCursor) return CurrentCursor();
    API(GetIconInfo) return ImageApi(name, a);
    API(GetUserNameA) return UserNameApi(a);
    API(SMapLS_IP_EBP_8) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_12) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_16) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_20) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_24) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_28) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_32) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_36) return SegmentMapApi(name, a);
    API(SMapLS_IP_EBP_40) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_8) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_12) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_16) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_20) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_24) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_28) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_32) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_36) return SegmentMapApi(name, a);
    API(SUnMapLS_IP_EBP_40) return SegmentMapApi(name, a);
    API(AllocLSCallback) return ThunkBridgeApi(name, a);
    API(AllocSLCallback) return ThunkBridgeApi(name, a);
    API(FreeLSCallback) return ThunkBridgeApi(name, a);
    API(FreeSLCallback) return ThunkBridgeApi(name, a);
    API(GetLSCallbackTarget) return ThunkBridgeApi(name, a);
    API(GetLSCallbackTemplate) return ThunkBridgeApi(name, a);
    API(GetSLCallbackTarget) return ThunkBridgeApi(name, a);
    API(GetSLCallbackTemplate) return ThunkBridgeApi(name, a);
    API(IsLSCallback) return ThunkBridgeApi(name, a);
    API(IsSLCallback) return ThunkBridgeApi(name, a);
    API(Callback4) return ThunkBridgeApi(name, a);
    API(Callback8) return ThunkBridgeApi(name, a);
    API(Callback12) return ThunkBridgeApi(name, a);
    API(Callback16) return ThunkBridgeApi(name, a);
    API(Callback20) return ThunkBridgeApi(name, a);
    API(Callback24) return ThunkBridgeApi(name, a);
    API(Callback28) return ThunkBridgeApi(name, a);
    API(Callback32) return ThunkBridgeApi(name, a);
    API(Callback36) return ThunkBridgeApi(name, a);
    API(Callback40) return ThunkBridgeApi(name, a);
    API(Callback44) return ThunkBridgeApi(name, a);
    API(Callback48) return ThunkBridgeApi(name, a);
    API(Callback52) return ThunkBridgeApi(name, a);
    API(Callback56) return ThunkBridgeApi(name, a);
    API(Callback60) return ThunkBridgeApi(name, a);
    API(Callback64) return ThunkBridgeApi(name, a);
    API(FT_Exit0) return ThunkBridgeApi(name, a);
    API(FT_Exit4) return ThunkBridgeApi(name, a);
    API(FT_Exit8) return ThunkBridgeApi(name, a);
    API(FT_Exit12) return ThunkBridgeApi(name, a);
    API(FT_Exit16) return ThunkBridgeApi(name, a);
    API(FT_Exit20) return ThunkBridgeApi(name, a);
    API(FT_Exit24) return ThunkBridgeApi(name, a);
    API(FT_Exit28) return ThunkBridgeApi(name, a);
    API(FT_Exit32) return ThunkBridgeApi(name, a);
    API(FT_Exit36) return ThunkBridgeApi(name, a);
    API(FT_Exit40) return ThunkBridgeApi(name, a);
    API(FT_Exit44) return ThunkBridgeApi(name, a);
    API(FT_Exit48) return ThunkBridgeApi(name, a);
    API(FT_Exit52) return ThunkBridgeApi(name, a);
    API(FT_Exit56) return ThunkBridgeApi(name, a);
    API(FT_Prolog) return ThunkBridgeApi(name, a);
    API(FT_Thunk) return ThunkBridgeApi(name, a);
    API(QT_Thunk) return ThunkBridgeApi(name, a);
    API(K32Thk1632Epilog) return ThunkBridgeApi(name, a);
    API(K32Thk1632Prolog) return ThunkBridgeApi(name, a);
    API(MapHInstLS) return ThunkBridgeApi(name, a);
    API(MapHInstLS_PN) return ThunkBridgeApi(name, a);
    API(MapHInstSL) return ThunkBridgeApi(name, a);
    API(MapHInstSL_PN) return ThunkBridgeApi(name, a);
    API(SMapLS) return ThunkBridgeApi(name, a);
    API(SUnMapLS) return ThunkBridgeApi(name, a);
    API(MapHModuleLS) return ThunkBridgeApi(name, a);
    API(MapHModuleSL) return ThunkBridgeApi(name, a);
    API(MapLS) return ThunkBridgeApi(name, a);
    API(MapSL) return ThunkBridgeApi(name, a);
    API(MapSLFix) return ThunkBridgeApi(name, a);
    API(UnMapLS) return ThunkBridgeApi(name, a);
    API(UTUnRegister) return ThunkBridgeApi(name, a);
    API(UnMapSLFixArray) return ThunkBridgeApi(name, a);
    API(UTRegister) return ThunkBridgeApi(name, a);
    API(OpenClipboard) return ClipboardApi(name, a);
    API(CloseClipboard) return ClipboardApi(name, a);
    API(EmptyClipboard) return ClipboardApi(name, a);
    API(GetClipboardData) return ClipboardApi(name, a);
    API(SetClipboardData) return ClipboardApi(name, a);
    API(CountClipboardFormats) return ClipboardApi(name, a);
    API(EnumClipboardFormats) return ClipboardApi(name, a);
    API(IsClipboardFormatAvailable) return ClipboardApi(name, a);
    API(GetPriorityClipboardFormat) return ClipboardApi(name, a);
    API(GetClipboardOwner) return ClipboardApi(name, a);
    API(GetOpenClipboardWindow) return ClipboardApi(name, a);
    API(GetClipboardViewer) return ClipboardApi(name, a);
    API(GetClipboardSequenceNumber) return ClipboardApi(name, a);
    API(SetClipboardViewer) return ClipboardApi(name, a);
    API(ChangeClipboardChain) return ClipboardApi(name, a);
    API(GetClipboardFormatNameA) return ClipboardApi(name, a);
    default:
        break;
    }
#undef API
    // Names without a case of their own. The prefix rules are the ones the
    // former if-chain had; every name of these families that specs knows has
    // a case above, so this path only serves a name added to specs alone.
    if (!strncmp(name, "ImageList_", 10)) return ImageApi(name, a);
    if (!strncmp(name, "Dde", 3)) return DdeApi(name, a);
    if (PathProbeOperation(name) > 0) return PathProbeApi(name, a);
    if (!strncmp(name, "RegOpen", 7) || !strncmp(name, "RegCreateKey", 12) || !strncmp(name, "RegEnum", 7) ||
        !strncmp(name, "RegQuery", 8) || !strncmp(name, "RegSetValue", 11) || !strncmp(name, "RegDelete", 9))
        return RegistryApi(name, a);
    Fail(name);
    return 0;
}
} // namespace X86

#include "x86_fast_gate.inc"

#include "x86_process_children.inc"

#include "x86_process_legacy.inc"

#include "x86_shell_control.inc"

#include "x86_exceptions.inc"

#include "x86_fault_exceptions.inc"
#include "x86_fault_box.inc"

#include "x86_draw_text.inc"

#include "x86_shell_threads.inc"

#include "x86_shell_process_reference.inc"
#include "x86_shell_os.inc"

#include "x86_draw_caption.inc"

#include "x86_send_message.inc"

#include "x86_shell_modules.inc"

#include "x86_shell_stopwatch.inc"

#include "x86_kernel_fibers_debug.inc"
#include "x86_kernel_thunks.inc"
#include "win98_comm_parse.h"
#include "win98_comm.h"
#include "x86_kernel_comm.inc"
