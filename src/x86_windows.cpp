// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/user_menus.h"
#endif
#include "x86_app.h"
#include "win98_broadcast.h"
#include <string.h>
namespace X86
{
unsigned Application::WindowApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("WindowFromPoint") API_CASE("ChildWindowFromPoint") API_CASE("ChildWindowFromPointEx") API_CASE("RealChildWindowFromPoint"){
        unsigned op=API_IS(name,"WindowFromPoint")?0:API_IS(name,"ChildWindowFromPoint")?1:API_IS(name,"ChildWindowFromPointEx")?2:3;
        return User::PointWindow(op,op?a[0]:0,int(a[op?1:0]),int(a[op?2:1]),op==2?a[3]&65535:0,this,[](void*p,unsigned h,unsigned m,unsigned w,unsigned l){return static_cast<Application*>(p)->Send(h,m,w,l);},ThreadId());
    }
    // USER32 window regions. The guest HRGN is the native GDI handle; the
    // original WORD narrowing of / is not applied to native
    // generation handles.
    API_CASE("SetWindowRgn")return User::SetWindowRegion(a[0],a[1],a[2]!=0,this,
        [](void *context,unsigned hwnd,unsigned region,unsigned flags){
            unsigned position[1][7]={{hwnd,0,0,0,0,0,flags}};
            return static_cast<Application*>(context)->PositionWindows(1,position,hwnd,region);
        });
    API_CASE("GetWindowRgn")return User::GetWindowRegion(a[0],a[1]);
    API_CASE("GetWindowContextHelpId")return User::WindowContextHelp(a[0]);
    API_CASE("SetWindowContextHelpId")return User::SetWindowContextHelp(a[0],a[1]);
    API_CASE("SendMessageTimeoutA")
    {
        const bool gate = TakeFresh();
        // HWND_BROADCAST: the 32-bit -1 narrows to USER's 0FFFFh, and the
        // walk of SendMessage goes to every top-level window. SHELL32 asks
        // for its DDE server this way.
        if (a[0] == 0xffffffffu || a[0] == 0xffffu)
        {
            if (a[4] & ~0xbu) { last_error = 120; return 0; }
            BroadcastMessage(Win98Broadcast::Send, a[1], a[2], a[3]);
            if (state == Failed || state == Exited) return 0;
            const unsigned zero = 0;
            if (a[6] && !Store(a[6], &zero, 4)) return 0;
            return 1;
        }
        if (!User::IsWindow(a[0]))
        {
            last_error = 1400;
            return 0;
        }
        if (a[4] & ~0xbu)
        {
            last_error = 120;
            return 0;
        }
        // Same-thread delivery is synchronous. Cross-thread and cross-process
        // waits have a deadline and cancel records that have not started
        // delivery.
        bool completed=true;
        const unsigned owner=User::WindowProcess(a[0]);
        const bool cross_process=owner&&owner!=process_id;
        const bool cross_thread=owner==process_id&&User::WindowThread(a[0])!=ThreadId();
        const bool recorded = gate && PushHostFrame(HostSendTimeout, a[6], cross_process || cross_thread);
        GiveTail(recorded ? TailRecord : TailNone);
        const unsigned result = cross_process
            ? SendCrossProcess(a[0],a[1],a[2],a[3],a[5],a[4],&completed)
            : cross_thread
            ? SendCrossThread(a[0],a[1],a[2],a[3],a[5],a[4],&completed)
            : Send(a[0], a[1], a[2], a[3]);
        if (recorded) PopHostFrame();
        return FinishSendTimeout(a[6], result, completed);
    }
    API_CASE("GetSystemMenu")
    {
        const unsigned hwnd = a[0], style = User::Style(hwnd);
        if (!User::IsWindow(hwnd) || hwnd == User::GetDesktopWindow())
            return 0;
        if (a[1])
        {
            User::SetSystemMenuRoot(hwnd, 0);
            return 0;
        }
        if (!(style & 0x80000))
            return 0;
        unsigned root = User::SystemMenuRoot(hwnd);
        if (!root)
        {
            unsigned position = 4;
            const bool child = style & 0x40000000;
            root = ParseMenu(child ? OriginalSystemMenu2 : OriginalSystemMenu1,
                             child ? sizeof OriginalSystemMenu2 : sizeof OriginalSystemMenu1, position, 0);
            if (!root || !User::SetSystemMenuRoot(hwnd, root))
                return 0;
            const unsigned menu = User::GetSubMenu(root, 0);
            User::SetMenuFlags(menu, 0xf120, style & 0x21000000 ? 0 : 3, false, 3);
            User::SetMenuFlags(menu, 0xf000, style & 0x40000 ? 0 : 3, false, 3);
            User::SetMenuFlags(menu, 0xf020, style & 0x20000 ? 0 : 3, false, 3);
            User::SetMenuFlags(menu, 0xf030, style & 0x10000 ? 0 : 3, false, 3);
        }
        return User::GetSubMenu(root, 0);
    }
    API_CASE("GetDesktopWindow")
        return User::GetDesktopWindow();
    API_CASE("GetWindow")
        return User::GetWindow(a[0], a[1]);
    API_CASE("GetTopWindow")
        return User::GetTopWindow(a[0]);
    API_CASE("GetWindowThreadProcessId")
    {
        if (!User::IsWindow(a[0]))
        {
            last_error = 1400;
            return 0;
        }
        // The desktop window belongs to a thread of the system, and Win98
        // answers that thread and its process, never zero. DSOUND keeps the
        // thread of the window given to SetCooperativeLevel and takes zero for
        // "no level set": SDL 1.2 without a window of its own hands it
        // GetDesktopWindow, and every Play then failed with
        // DSERR_PRIOLEVELNEEDED, so the KIDS50 games had no sound and the
        // spoken help of Labyrinth never ran. The two ids name no
        // thread and no process of a program.
        if (a[0] == User::GetDesktopWindow())
        {
            if (a[1])
                Write32(a[1], DesktopOwnerProcess);
            return DesktopOwnerThread;
        }
        const auto thread = User::WindowThread(a[0]);
        if (a[1])
            Write32(a[1], User::WindowProcess(a[0]));
        return thread;
    }
    default:
        break;
    }
    unsigned parent = 0, thread = 0, procedure = a[0], parameter = a[1];
    if (API_IS(name,"EnumChildWindows"))
    {
        parent = a[0];
        procedure = a[1];
        parameter = a[2];
        // USER32 rejects a zero parent, unlike EnumWindows.
        if (!parent || !User::IsWindow(parent))
            return 0;
    }
    else if (API_IS(name,"EnumThreadWindows"))
    {
        thread = a[0];
        procedure = a[1];
        parameter = a[2];
        // USER rejects IDs that KERNEL cannot map to a live queue.
        bool live = false;
        for (const auto &candidate : threads)
            if (candidate.fiber && !candidate.done && candidate.id == thread)
                live = true;
        if (!thread || !live)
            return 0;
    }
    if (!procedure)
    {
        last_error = 87;
        return 0;
    }
    unsigned windows[User::WindowCapacity], count = 0;
    if (!User::WindowSnapshot(parent, windows, User::WindowCapacity, count))
    {
        last_error = 8;
        return 0;
    }
    // USER iterates a snapshot. Deleted entries do not reach callbacks;
    // new windows are absent, and surviving windows retain their initial order.
    unsigned result = count ? 1 : 0;
    for (unsigned i = 0; i < count && result && state != Failed && state != Exited; ++i)
    {
        const unsigned hwnd = windows[i];
        if (!User::IsWindow(hwnd) || (thread && User::WindowThread(hwnd) != thread))
            continue;
        const unsigned args[] = {hwnd, parameter};
        result = Call(procedure, args, 2) & 65535;
    }
    // The USER32 enumeration thunk sign-extends the Win16 callback result.
    return result & 0x8000 ? result | 0xffff0000u : result;
}
} // namespace X86

#include "x86_classes.inc"

#include "x86_window_parent.inc"
