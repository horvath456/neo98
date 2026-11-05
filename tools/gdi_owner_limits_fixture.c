// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi_owner_limits_imports.h"
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)
__declspec(dllexport) volatile U phase, release;
__declspec(dllexport) U handles[2304];
void mainCRTStartup(void) {
    for (U i=0;i<2304;++i) {
        handles[i] = i%3==0 ? CreateSolidBrush(i) : i%3==1 ? CreateRectRgn(0,0,20,30) : CreateCompatibleDC(0);
        CHECK(handles[i]);
    }
    // Reuse holes after growing the owner table, including DC deletion.
    for (U i=0;i<2304;i+=7) {
        CHECK(i%3==2 ? DeleteDC(handles[i]) : DeleteObject(handles[i]));
        handles[i] = i%3==0 ? CreateSolidBrush(i) : i%3==1 ? CreateRectRgn(0,0,20,30) : CreateCompatibleDC(0);
        CHECK(handles[i]);
    }
    // Retained brushes in saved DC state must be released before objects.
    for (U i=2;i<2304;i+=3) {
        CHECK(SelectObject(handles[i],handles[i-2]));
        CHECK(SaveDC(handles[i]));
    }
    phase=1;
    while (!release) Sleep(0);
    for (U i=0;i<2304;++i) CHECK(GetObjectType(handles[i])==(i%3==0 ? 2 : i%3==1 ? 8 : 10));
    // Exit with every object live; process teardown must reclaim them.
    ExitProcess(0);
}
