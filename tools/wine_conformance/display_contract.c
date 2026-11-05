/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

START_TEST(display_contract)
{
    const DWORD dimensions = DM_PELSWIDTH | DM_PELSHEIGHT;
    const struct { DWORD fields, width, height, depth; } cases[] = {
        {DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL, 640, 480, 0},
        {DM_BITSPERPEL, 0, 0, 0},
        {DM_PELSWIDTH, 0, 0, 0},
        {DM_PELSHEIGHT, 0, 0, 0},
        {DM_PELSWIDTH, 640, 0, 0},
        {DM_PELSHEIGHT, 0, 480, 0},
        {DM_PELSWIDTH | DM_PELSHEIGHT, 0, 480, 0},
        {DM_PELSWIDTH | DM_PELSHEIGHT, 640, 0, 0},
        {0, 0, 0, 0},
        {DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL, 640, 480, 16}
    };
    DEVMODEA original, base, request, actual;
    unsigned i;
    LONG result;
    ZeroMemory(&original, sizeof original); original.dmSize = sizeof original;
    if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &original)) {
        ok(0, "read original mode\n"); return;
    }
    ZeroMemory(&base, sizeof base); base.dmSize = sizeof base;
    base.dmFields = dimensions | DM_BITSPERPEL;
    base.dmPelsWidth = 800; base.dmPelsHeight = 600; base.dmBitsPerPel = 16;
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        LONG tested, applied = 0x12345678;
        result = ChangeDisplaySettingsExA(NULL, &base, NULL, 0, NULL);
        ok(result == DISP_CHANGE_SUCCESSFUL, "base mode case=%u result=%ld\n", i, result);
        if (result != DISP_CHANGE_SUCCESSFUL) break;
        ZeroMemory(&actual, sizeof actual); actual.dmSize = sizeof actual;
        ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &actual), "read base mode case=%u\n", i);
        ok(actual.dmPelsWidth == 800 && actual.dmPelsHeight == 600 && actual.dmBitsPerPel == 16,
           "base mode case=%u actual=%lu,%lu,%lu\n", i,
           actual.dmPelsWidth, actual.dmPelsHeight, actual.dmBitsPerPel);
        ZeroMemory(&request, sizeof request); request.dmSize = sizeof request;
        request.dmFields = cases[i].fields;
        request.dmPelsWidth = cases[i].width; request.dmPelsHeight = cases[i].height;
        request.dmBitsPerPel = cases[i].depth;
        tested = ChangeDisplaySettingsExA(NULL, &request, NULL, CDS_TEST, NULL);
        if (tested == DISP_CHANGE_SUCCESSFUL)
            applied = ChangeDisplaySettingsExA(NULL, &request, NULL, 0, NULL);
        ZeroMemory(&actual, sizeof actual); actual.dmSize = sizeof actual;
        ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &actual), "read mode case=%u\n", i);
        trace("DISPLAY_CONTRACT case=%u test=%ld apply=%ld mode=%lu,%lu,%lu\n",
              i, tested, applied, actual.dmPelsWidth, actual.dmPelsHeight, actual.dmBitsPerPel);
        fflush(stdout);
    }
    original.dmFields = dimensions | DM_BITSPERPEL;
    result = ChangeDisplaySettingsExA(NULL, &original, NULL, 0, NULL);
    ok(result == DISP_CHANGE_SUCCESSFUL, "restore original mode result=%ld\n", result);
    ZeroMemory(&actual, sizeof actual); actual.dmSize = sizeof actual;
    ok(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &actual), "read restored mode\n");
    ok(actual.dmPelsWidth == original.dmPelsWidth && actual.dmPelsHeight == original.dmPelsHeight &&
       actual.dmBitsPerPel == original.dmBitsPerPel, "restored mode=%lu,%lu,%lu\n",
       actual.dmPelsWidth, actual.dmPelsHeight, actual.dmBitsPerPel);
}
