// SPDX-License-Identifier: GPL-2.0-or-later
// The last step of USER's caption icon search. The port already had
// the window property of WM_SETICON and the class icon; this
// is the question the original asks the window itself when both are empty.
#include "user.h"
#include "x86_app.h"
#include <stdio.h>
namespace X86 {
namespace {
constexpr unsigned QueryDragIcon16 = 0x0037;  // WM_QUERYDRAGICON,
constexpr unsigned QueryDragTimeout = 100;    // the limit Windows 98 passes to
} // namespace

// USER. The window gets WM_QUERYDRAGICON with a limit of 100 ms and
// answers an icon handle or zero. skips the question for a window whose
// module claims Windows 4.0: the message is a Windows 3.x contract, and a
// Win32 program sets its icon with the class or with WM_SETICON instead. In
// NEO98 the Win32 side of that distinction is the process: a process that runs
// no 16-bit task is never asked.
unsigned Application::QueryDragIcon(unsigned hwnd)
{
    if (!Win16Process() || !User::IsWindow(hwnd)) return 0;
    const unsigned answer = Allocate(4);
    if (!answer) return 0;
    unsigned zero = 0;
    Store(answer, &zero, sizeof zero);
    const unsigned send[] = {hwnd, QueryDragIcon16, 0, 0, 0, QueryDragTimeout, answer};
    unsigned icon = 0;
    if (Api("SendMessageTimeoutA", send) && Read(answer, &icon, sizeof icon))
    {
        // A 16-bit window answers with its own icon word; the caption draws
        // native handles, so the word goes through the image alias table.
        if (const unsigned native = Win16ImageNative(icon & 0xffff)) icon = native;
        else if (!FindIcon(icon)) icon = 0;
    }
    else icon = 0;
    if (log && trace_calls) { char t[120]; unsigned raw=0; Read(answer,&raw,4); snprintf(t,sizeof t,"WM_QUERYDRAGICON hwnd=%u answer=%08x icon=%08x",hwnd,raw,icon); log(t); }
    Free(answer);
    return icon;
}
} // namespace X86
