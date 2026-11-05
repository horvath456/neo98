// SPDX-License-Identifier: GPL-2.0-or-later
// The guest side of USER's nonclient trackers: the nested modal pump for the
// move/size loop and the scrollbar loop, and the two messages that carry a
// rectangle into guest memory.
#include "x86_app.h"
namespace X86 {
// The same shape as RunMenuLoop: the loop owns its own wait, keeps timers
// running and gives the tracker first refusal on every retrieved message.
unsigned Application::RunFrameTrackLoop()
{
    auto &t = threads[current_thread];
    const unsigned thread = current_thread;
    bool kept = false;
    if (t.host_resume && t.host_resume_at < t.host_frame_count && t.host_frames[t.host_resume_at].kind == HostFrameTrack)
    {
        kept = true;
        if (++t.host_resume_at >= t.host_frame_count) t.host_resume = false;
        if (t.host_resume_level < t.host_resume_top || t.host_resume) ResumeInner();
    }
    else if (t.host_frame_count && t.host_frames[t.host_frame_count - 1].kind == HostSizeMove)
        kept = PushHostFrame(HostFrameTrack);
    while (User::FrameTrackActive() && state != Failed && state != Exited)
    {
        // The frame of the drag goes onto the screen; while another thread
        // has the primary surface locked, the loop waits where it stands
        // (src/x86_screen_lock.cpp).
        if (User::ScreenLockActive()) WaitScreenFree();
        if (state == Failed || state == Exited) break;
        User::FrameTrackTick();
        if (!User::FrameTrackActive()) break;
        User::Message message{};
        if (!ModalMessage(message))
        {
            if (!User::FrameTrackActive() || state == Failed || state == Exited) break;
            threads[thread].host_waiting = kept;
            Suspend(Idle);
            threads[thread].host_waiting = false;
            continue;
        }
        MessageHook(3, 0, 1, message);
        if (state == Failed || state == Exited) break;
        if (message.message == 0x12)
        {
            User::PostMessage(0, 0x12, message.wparam, 0);
            User::CancelFrameTrack();
            break;
        }
        if (!User::FrameTrackMessage(message))
        {
            User::TranslateMessage(&message);
            GiveTail(kept ? TailRecord : TailNone);
            Send(message.hwnd, message.message, message.wparam, message.lparam);
        }
    }
    if (User::FrameTrackActive()) User::CancelFrameTrack();
    if (kept) PopHostFrame();
    return 0;
}
// WM_SIZING and WM_MOVING pass a RECT the window may change; WM_GETMINMAXINFO
// passes a MINMAXINFO whose two tracking points are the only fields the loop
// reads back. Both live in guest memory for the duration of the callback.
unsigned Application::FrameTrackRect(unsigned hwnd, unsigned message, unsigned wparam, Gdi::RECT *rect)
{
    if (!rect || !User::IsWindow(hwnd)) return 0;
    if (message == 0x24)
    {
        // MINMAXINFO: reserved point, maximum size, maximum position, then the
        // two tracking sizes the loop clamps against.
        unsigned values[10] = {0, 0, 0, 0, 0, 0,
                               (unsigned)rect->left, (unsigned)rect->top,
                               (unsigned)rect->right, (unsigned)rect->bottom};
        const unsigned p = Allocate(sizeof values);
        if (!p) return 0;
        unsigned result = 0;
        if (Store(p, values, sizeof values))
        {
            SendFromNative(hwnd, 0x24, 0, p);
            if (User::IsWindow(hwnd) && Read(p, values, sizeof values))
            {
                rect->left = (int)values[6]; rect->top = (int)values[7];
                rect->right = (int)values[8]; rect->bottom = (int)values[9];
                result = 1;
            }
        }
        Free(p);
        return result;
    }
    unsigned values[4] = {(unsigned)rect->left, (unsigned)rect->top,
                          (unsigned)rect->right, (unsigned)rect->bottom};
    const unsigned p = Allocate(sizeof values);
    if (!p) return 0;
    unsigned result = 0;
    if (Store(p, values, sizeof values))
    {
        SendFromNative(hwnd, message, wparam, p);
        if (User::IsWindow(hwnd) && Read(p, values, sizeof values))
        {
            rect->left = (int)values[0]; rect->top = (int)values[1];
            rect->right = (int)values[2]; rect->bottom = (int)values[3];
            result = 1;
        }
    }
    Free(p);
    return result;
}
// The tracker commits through the ordinary SetWindowPos body, so the window
// sees WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_MOVE, WM_SIZE and
// WM_WINDOWPOSCHANGED in the original order.
void Application::FrameTrackCommit(unsigned hwnd, int x, int y, int width, int height, unsigned flags)
{
    if (!User::IsWindow(hwnd)) return;
    unsigned position[1][7] = {{hwnd, 0, (unsigned)x, (unsigned)y, (unsigned)width, (unsigned)height, flags}};
    PositionWindows(1, position);
}
} // namespace X86
