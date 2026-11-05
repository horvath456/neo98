// SPDX-License-Identifier: GPL-2.0-or-later
// Protect screen pixels while a program holds a DirectDraw primary-surface
// lock. Other threads wait only for operations that access those pixels.
// Unlock wakes them; a new HAL lock waits for their drawing to finish. The
// holder itself never waits.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gdi.h"
#include "user.h"
#include "x86_app.h"
namespace X86
{
// NEO98_SCREEN_RULES=0 switches the list below off, so that no call waits at
// its gate. tools/screen_pixel_exports.py runs its catalogue that way and
// reads from the guard which calls touch the screen's pixels.
bool Application::ScreenRulesEnabled()
{
    static const bool enabled = [] {
        const char *value = getenv("NEO98_SCREEN_RULES");
        return !value || strcmp(value, "0") != 0;
    }();
    return enabled;
}
// Which argument of an export names the DC whose pixels it writes or reads.
// The GDI32 drawing calls are the ones that accumulate bounds in GDI.EXE
// (src/x86_gdi32_gates.cpp, GdiAccumulate), the
// region painters and the readers of the screen; the USER32 ones draw through
// a DC or move the pixels of a window.
unsigned char Application::ScreenPixelRule(ApiName name)
{
    switch (name.id)
    {
    // The DC in the first argument.
    API_CASE("Arc") API_CASE("ArcTo") API_CASE("AngleArc") API_CASE("Chord") API_CASE("Ellipse")
    API_CASE("Pie") API_CASE("Rectangle") API_CASE("RoundRect") API_CASE("Polygon") API_CASE("Polyline")
    API_CASE("PolylineTo") API_CASE("PolyPolygon") API_CASE("PolyPolyline") API_CASE("PolyBezier")
    API_CASE("PolyBezierTo") API_CASE("PolyDraw") API_CASE("LineTo") API_CASE("PatBlt")
    API_CASE("StretchDIBits") API_CASE("SetDIBitsToDevice") API_CASE("SetPixel") API_CASE("SetPixelV")
    API_CASE("GetPixel") API_CASE("TextOutA") API_CASE("TextOutW") API_CASE("ExtTextOutA")
    API_CASE("ExtTextOutW") API_CASE("PolyTextOutA") API_CASE("PolyTextOutW") API_CASE("FillRgn")
    API_CASE("FrameRgn") API_CASE("InvertRgn") API_CASE("PaintRgn") API_CASE("FloodFill")
    API_CASE("ExtFloodFill") API_CASE("FillPath") API_CASE("StrokePath") API_CASE("StrokeAndFillPath")
    API_CASE("UpdateColors")
    API_CASE("FillRect") API_CASE("FrameRect") API_CASE("InvertRect") API_CASE("DrawTextA")
    API_CASE("DrawTextW") API_CASE("DrawTextExA") API_CASE("DrawTextExW") API_CASE("DrawEdge")
    API_CASE("DrawFocusRect") API_CASE("DrawIcon") API_CASE("DrawIconEx") API_CASE("DrawFrameControl")
    API_CASE("DrawStateA") API_CASE("DrawStateW") API_CASE("GrayStringA") API_CASE("GrayStringW")
    API_CASE("TabbedTextOutA") API_CASE("TabbedTextOutW") API_CASE("PaintDesktop") API_CASE("ScrollDC")
        return 1;
    // A blit: the destination in the first argument, the source in the sixth.
    API_CASE("BitBlt") API_CASE("StretchBlt") API_CASE("MaskBlt")
        return 2;
    // PlgBlt(dest, points, source,...)
    API_CASE("PlgBlt")
        return 3;
    // DrawCaption(hwnd, hdc, rect, flags)
    API_CASE("DrawCaption")
        return 4;
    // The pixels of a visible window: moved or drawn by USER itself.
    // UpdateWindow paints a native control at once (Application::UpdateWindowTree).
    API_CASE("ScrollWindow") API_CASE("ScrollWindowEx") API_CASE("DrawMenuBar") API_CASE("FlashWindow")
    API_CASE("UpdateWindow")
        return 5;
    // DefWindowProc draws for the painting messages and starts the move and
    // size loop, whose frame goes onto the screen.
    API_CASE("DefWindowProcA") API_CASE("DefWindowProcW")
        return 6;
    // The animation of DrawAnimatedRects goes onto the screen as a whole.
    API_CASE("DrawAnimatedRects")
        return 7;
    default:
        return 0;
    }
}
// Whether this call touches the pixels of the screen.
bool Application::ScreenPixelsTouched(unsigned rule, const unsigned *a)
{
    const auto shown = [](unsigned hwnd) { return User::IsWindow(hwnd) && User::IsVisible(hwnd); };
    switch (rule)
    {
    case 1: return User::DrawsOnScreen(a[0]);
    case 2: return User::DrawsOnScreen(a[0]) || User::DrawsOnScreen(a[5]);
    case 3: return User::DrawsOnScreen(a[0]) || User::DrawsOnScreen(a[2]);
    case 4: return User::DrawsOnScreen(a[1]);
    case 5: return shown(a[0]);
    case 6:
        switch (a[1])
        {
        case 0x0014: case 0x0027: case 0x0317: case 0x0318: // WM_ERASEBKGND, WM_ICONERASEBKGND, WM_PRINT, WM_PRINTCLIENT
            return User::DrawsOnScreen(a[2]);
        case 0x000c: case 0x000f: case 0x0085: case 0x0086: // WM_SETTEXT, WM_PAINT, WM_NCPAINT, WM_NCACTIVATE
        case 0x00a1: case 0x0112:                           // WM_NCLBUTTONDOWN, WM_SYSCOMMAND
            return shown(a[0]);
        default:
            return false;
        }
    case 7: return true;
    default: return false;
    }
}
// The wait of a call that touches the screen while another thread holds its
// lock. The call does nothing yet: it leaves `waiting` set, the thread
// suspends with block code 17 until Unlock hands it the screen
// (User::ScreenMayDraw), and the gate enters the call again. That second
// entry takes the hand-off and goes on.
bool Application::ScreenWait(unsigned rule, const unsigned *a)
{
    auto &t = threads[current_thread];
    const unsigned thread = ThreadId();
    if (t.screen_wait)
    {
        t.screen_wait = false;
        const bool may = User::ScreenMayDraw(process_id, thread);
        User::DoneScreenWait(process_id, thread);
        if (may) return false;
    }
    if (!User::ScreenLockActive() || !User::ScreenLockedElsewhere(process_id, thread) || !ScreenPixelsTouched(rule, a))
        return false;
    User::WaitScreen(process_id, thread);
    t.screen_wait = true;
    t.block = 17;
    waiting = true;
    return true;
}
// The same wait for a native loop that draws on the screen itself, the move
// and size loop (RunFrameTrackLoop): it waits where it stands.
void Application::WaitScreenFree()
{
    auto &t = threads[current_thread];
    const unsigned thread = ThreadId();
    while (state != Failed && state != Exited && User::ScreenLockedElsewhere(process_id, thread))
    {
        User::WaitScreen(process_id, thread);
        t.block = 17;
        Suspend(Idle);
    }
    User::DoneScreenWait(process_id, thread);
    t.block = 0;
}
// The guard behind the list (Gdi::SetScreenGuard). GDI calls this before it
// touches the screen's pixels while the lock is held. A thread that may draw
// is the holder, a thread the Unlock handed the screen to, or any thread
// while the holder waits for messages; for every other thread the call
// should have waited at its gate. It does not wait here: the native engine
// is in the middle of its work, with handles resolved and DC state read, and
// may not give the processor away at this point. It reports the export instead.
void Application::ScreenTouched(bool write)
{
    const unsigned process = User::CurrentProcess(), thread = User::CurrentThread();
    if (!User::ScreenLockedElsewhere(process, thread)) return;
    if (auto *app = static_cast<Application *>(User::ProcessContextOf(process))) app->NoteScreenMiss(write);
}
void Application::NoteScreenMiss(bool write)
{
    // The call that runs: the innermost call of this thread that has not
    // returned yet, or the Win16 gate.
    const char *dll = nullptr, *name = nullptr;
    unsigned call = 0;
    if (win16_gate_depth)
    {
        dll = win16_gate_dll;
        name = win16_gate_name;
    }
    else
    {
        const unsigned thread = ThreadId();
        for (unsigned back = 0; back < 64; ++back)
        {
            const auto *c = api_ring.Recent(back);
            if (!c) break;
            if (c->thread == thread && !c->returned)
            {
                dll = c->dll;
                name = c->name;
                call = c->sequence + 1;
                break;
            }
        }
    }
    ++screen_miss_count;
    // One line per call, not per pixel access: a call draws many times.
    if (call ? call == screen_miss_call : name == screen_miss_name) return;
    screen_miss_call = call;
    screen_miss_name = name;
    if (log && ++screen_miss_lines <= 256)
    {
        char text[200];
        snprintf(text, sizeof text, "SCREEN LOCK MISS %s!%s thread=%u %s", dll ? dll : "native", name ? name : "-",
                 ThreadId(), write ? "write" : "read");
        log(text);
    }
}
} // namespace X86
