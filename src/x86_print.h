// SPDX-License-Identifier: GPL-2.0-or-later
// The records of the printer half of GDI.EXE, shared by src/x86_print_driver.cpp
// (the DC and the DDI), src/x86_print_spool.cpp (the job API and the client of
// SPOOL32) and src/win16_print.cpp (the gates of the driver).
#pragma once
#include "win16_machine.h"
namespace X86
{
namespace Print
{
// DDI ordinals of a Windows 3.1 and later device driver.
enum : unsigned
{
    DdiBitBlt = 1, DdiColorInfo = 2, DdiControl = 3, DdiDisable = 4, DdiEnable = 5, DdiEnumDFonts = 6,
    DdiEnumObj = 7, DdiOutput = 8, DdiPixel = 9, DdiRealizeObject = 10, DdiStrBlt = 11, DdiScanLR = 12,
    DdiDeviceMode = 13, DdiExtTextOut = 14, DdiGetCharWidth = 15, DdiDeviceBitmap = 16, DdiFastBorder = 17,
    DdiSetAttribute = 18, DdiDibBlt = 19, DdiCreateDibitmap = 20, DdiDibToDevice = 21, DdiStretchBlt = 27,
    DdiStretchDib = 28, DdiEntries = 0x22,
    DdiExtDeviceMode = 90, DdiDeviceCapabilities = 91, DdiAdvancedSetupDialog = 93,
    DdiExtDeviceModePropSheet = 95
};
} // namespace Print

// A block of the 16-bit global heap the driver can address: its handle, its
// selector and its flat address.
struct Application::PrintBlock
{
    unsigned handle = 0, selector = 0, memory = 0, size = 0;
    unsigned Far(unsigned offset = 0) const { return selector ? selector << 16 | offset : 0; }
};
// The driver block: one per loaded driver.
struct Application::PrintDriver
{
    unsigned module = 0, instance = 0, references = 0;
    char name[16] = {};
    unsigned entry[Print::DdiEntries + 1] = {};
    unsigned extended_mode = 0, capabilities = 0;
};
// One printer DC.
struct Application::PrintDc
{
    Application *owner = nullptr;
    unsigned hdc = 0;
    PrintDriver *driver = nullptr;
    bool information = false;
    char device[64] = {}, port[128] = {};
    PrintBlock gdiinfo, pdevice, strings, devmode, scratch;
    // GDI's document state of the DC: the job of OpenJob (DC+D8h), the
    // abort procedure of SetAbortProc (DC+D0h) and the bits of DC+FEh to
    // DC+101h.
    unsigned job = 0, abort_procedure = 0, document = 0;
    bool in_call = false; // DC+FEh bit 10h: inside a document call
    // The drawing half (src/x86_print_draw.cpp): the argument block, the
    // DRAWMODE, the physical pen, brush and font with the TEXTXFORM, and the
    // logical state each was realized from.
    PrintBlock work, drawmode, pen, brush, font, xform;
    unsigned pen_key[4] = {}, brush_key[5] = {}, font_key[16] = {};
    // The DC's default font, GDI's DEVICE_DEFAULT_FONT (see PrinterCreateDC).
    unsigned default_font = 0;
};
// The records EnumDFonts is filling right now (src/x86_print_draw.cpp).
struct Application::PrintEnumSink
{
    PrintDc *dc = nullptr;
    unsigned char *rows = nullptr;
    unsigned capacity = 0, count = 0;
};
struct Application::PrintHost
{
    PrintDriver drivers[4];
    PrintDc *dcs[16] = {};
};

} // namespace X86
