// SPDX-License-Identifier: GPL-2.0-or-later
// DCIMAN communicates with the display driver through QUERYESCSUPPORT and
// DCICOMMAND. DCIOpenProvider reads profile settings, creates a display DC,
// and checks DCI support; failure returns zero. Offscreen, overlay, and
// enumeration requests return -4. Surface operations call the corresponding
// DCISURFACEINFO method; unsupported surfaces fail instead of enabling an
// unusable direct-access path.
#pragma once
namespace Win98Dci
{
using U = unsigned;
// GDI escape numbers. pushes 0x0c03 as the queried number and 8 as the
// escape; Windows 98 passes 0x0c03 itself.
enum : U
{
    QueryEscSupport = 8,
    DciCommand = 0x0c03,
};
// The two answers the module produces on its own, as 16-bit words.
enum : U
{
    // A null argument or a surface it cannot use.
    Generic = 0xffff,
    // Offscreen surfaces, overlays and enumeration are unsupported.
    Unsupported = 0xfffc,
};
// NE entry ordinals of DCIMAN.DLL, which are the catalogue ordinals. The three
// thunk targets that are not entry points keep 0x40 plus their index byte.
enum Ordinal : U
{
    OpenProviderOrdinal = 1,
    CloseProviderOrdinal = 2,
    SetSrcDestClipOrdinal = 8,
    WinWatchOpenOrdinal = 10,
    WinWatchCloseOrdinal = 11,
    WinWatchDidStatusChangeOrdinal = 12,
    WinWatchGetClipListOrdinal = 14,
    GetWindowRegionDataOrdinal = 20,
    GetDCRegionDataOrdinal = 21,
    DestroyOrdinal = 30,
    EndAccessOrdinal = 31,
    BeginAccessOrdinal = 32,
    DrawOrdinal = 33,
    SetClipListOrdinal = 34,
    SetDestinationOrdinal = 35,
    CreatePrimary32Ordinal = 40,
    EnumThunkOrdinal = 0x4c,      // index 12, the internal
    CreateOverlayThunkOrdinal = 0x4e, // index 14, the internal
    CreateOffscreenThunkOrdinal = 0x4f, // index 15, the internal
};
// What DCIOpenProvider needs from the system. All four are portable.
struct Environment
{
    virtual ~Environment() = default;
    // GetPrivateProfileInt(section, key, fallback, file).
    virtual U ProfileInt(const char *section, const char *key, U fallback, const char *file) = 0;
    // GetPrivateProfileString(section, key, "", out, size, file); true when the
    // answer is not empty.
    virtual bool ProfileString(const char *section, const char *key, char *out, U size,
                               const char *file) = 0;
    // CreateDC(driver, NULL, NULL, NULL) with SetErrorMode(SEM_NOOPENFILEERRORBOX)
    // around it, the way do it.
    virtual U CreateDriverDC(const char *driver) = 0;
    // Escape(hdc, QUERYESCSUPPORT, 2, &DCICOMMAND, NULL).
    virtual U QueryDciCommand(U hdc) = 0;
    virtual void DeleteDC(U hdc) = 0;
    // A line for the diagnosis, so a refused provider is visible.
    virtual void Note(const char *candidate, U hdc, U supported) {}
};
// DCIOpenProvider, no arguments, retf 0. Answers the HDC of the display
// driver that knows DCICOMMAND, or zero.
inline U OpenProvider(Environment &system)
{
    // [DrawDib] dva of WIN.INI, default 1. Zero switches the whole
    // direct video access off and Windows 98 answers zero.
    if (!system.ProfileInt("DrawDib", "dva", 1, "win.ini"))
        return 0;
    // [DrawDib] dcidva of WIN.INI, default 7. It is a bit mask over
    // the three candidates below.
    const U mask = system.ProfileInt("DrawDib", "dcidva", 7, "win.ini");
    if (!mask)
        return 0;
    char configured[128] = {};
    const char *candidates[3] = {};
    U count = 0;
    // bit 0 takes [Drivers] dci out of SYSTEM.INI, bit 1 the driver
    // called "display", bit 2 the one called "dcisvga". Windows 98 copies the two
    // constants with lstrcpy into the same buffer, so the order is fixed.
    if ((mask & 1) && system.ProfileString("Drivers", "dci", configured, sizeof configured,
                                           "system.ini"))
        candidates[count++] = configured;
    if (mask & 2)
        candidates[count++] = "display";
    if (mask & 4)
        candidates[count++] = "dcisvga";
    for (U i = 0; i < count; ++i)
    {
        const U hdc = system.CreateDriverDC(candidates[i]);
        if (!hdc)
            continue; // no DC, try the next candidate.
        const U supported = system.QueryDciCommand(hdc);
        system.Note(candidates[i], hdc, supported);
        if (supported)
            return hdc; //
        system.DeleteDC(hdc); //
    }
    return 0; //
}
} // namespace Win98Dci
