// SPDX-License-Identifier: GPL-2.0-or-later
// DDRAW16 connects the DirectDraw display driver, DIB engine, and Win16 thunk
// catalogue. Catalogue ordinals are target indices plus one. DirectDrawCreate
// checks DIB-engine capabilities and the device signature, then queries DCI
// support. A driver without DCI or a HAL uses software emulation. Unsupported
// surface and hardware operations return the corresponding failure.
#pragma once
namespace Win98DDraw16
{
using U = unsigned;
// The HRESULTs the module itself produces, read out of its own code.
enum : U
{
    // Mode X entries without a mode in effect.
    Generic = 0x80004005u,
    // a width or height the Mode X table does not carry.
    InvalidMode = 0x88760078u,
    // a size the register programming does not reach.
    UnsupportedMode = 0x8876024eu,
    // the HEL blit without a frame buffer selector.
    BlitRefused = 0xffffu,
    // The DDHAL callbacks answer with this when the driver did not do the work.
    DriverNotHandled = 0,
};
// GDI escape numbers used, the same pair DCIMAN uses.
enum : U
{
    QueryEscSupport = 8,
    DciCommand = 0x0c03,
    // GetDeviceCaps index 94, GDIINFO dpCaps1, and its DIB engine bit.
    Caps1 = 94,
    DibEngine = 0x10,
};
// Windows 98 checks width and height against two fixed lists before it touches any
// register. Anything else answers DDERR_INVALIDMODE without asking the
// hardware, so this much of Mode X is portable.
inline bool ModeXWidth(U width) { return width == 320 || width == 360; }
inline bool ModeXHeight(U height)
{
    return height == 175 || height == 200 || height == 240 || height == 350 || height == 400 ||
           height == 480;
}
// SetModeX(width, height, useBios). Without the VGA registers of
// PLAN.md step 12 the size check is all that can be answered honestly: a size
// the table does not carry is DDERR_INVALIDMODE, and a size it does carry
// cannot be set, which is DDERR_UNSUPPORTEDMODE.
inline U SetModeX(U width, U height)
{
    if (!ModeXWidth(width) || !ModeXHeight(height))
        return InvalidMode;
    return UnsupportedMode;
}
// What needs. The second half of its test reads the 16-bit PDEVICE of
// the display DC, which has no counterpart in this port; the first half is the
// question the caller actually asks, and the display driver answers it.
struct Display
{
    virtual ~Display() = default;
    // GetDC(0), GetDeviceCaps(hdc, 94), ReleaseDC.
    virtual U Caps1Of() = 0;
    // Whether this port's display driver is the DIB engine. NEO98's whole
    // drawing lower edge is the DIB engine port, so this is a fact about the
    // port, not a guess about a foreign driver.
    virtual bool DibEngineDriver() = 0;
};
// Takes no arguments. Returns one for a DIB engine based display.
inline U DibEngineDisplay(Display &display)
{
    return (display.Caps1Of() & DibEngine) || display.DibEngineDriver() ? 1u : 0u;
}
// What needs, the same three steps DCIMAN's provider takes.
struct DciProbe
{
    virtual ~DciProbe() = default;
    virtual U CreateDisplayDC() = 0;
    virtual U QueryDciCommand(U hdc) = 0;
    virtual void DeleteDC(U hdc) = 0;
};
// Takes no arguments. Returns the display driver's HDC when it supports
// DCICOMMAND, zero otherwise. The caller deletes the DC afterwards.
inline U OpenDciDisplay(DciProbe &probe)
{
    const U hdc = probe.CreateDisplayDC();
    if (!hdc)
        return 0; //
    if (probe.QueryDciCommand(hdc))
        return hdc; //
    probe.DeleteDC(hdc);
    return 0; //
}
} // namespace Win98DDraw16
