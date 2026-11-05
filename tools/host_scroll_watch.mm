// SPDX-License-Identifier: GPL-2.0-or-later
// The monitor of the scrollWheel events of the Mac. SDL hands on neither the kind of the device nor the phases of
// a gesture; this monitor sees every event before SDL does and keeps what it
// needs for the SDL event that follows (HostScrollSeen, tools/host_wheel.h).
// A file of its own, so that tools/test_host_scroll_native.mm runs the same
// monitor behind synthetic events.
#import <AppKit/AppKit.h>
#include "host_wheel.h"

namespace HostSeamless
{
void WatchScroll(); // tools/host_seamless.h
void WatchScroll()
{
    static id monitor = nil;
    if (monitor || !NSApp) return;
    monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *event) {
        static bool begin = false;
        if (event.phase == NSEventPhaseBegan || event.phase == NSEventPhaseMayBegin) begin = true;
        // SDL queues an event only for a movement; so does this.
        if (event.deltaX || event.deltaY || event.scrollingDeltaX || event.scrollingDeltaY)
        {
            HostScrollEvent seen;
            seen.precise = event.hasPreciseScrollingDeltas;
            seen.points_y = event.scrollingDeltaY;
            seen.points_x = event.scrollingDeltaX;
            seen.lines_y = event.deltaY;
            seen.lines_x = event.deltaX;
            seen.begin = begin;
            seen.momentum = event.momentumPhase != NSEventPhaseNone;
            begin = false;
            auto &queue = HostScrollSeen();
            if (queue.size() < 64) queue.push_back(seen);
        }
        return event;
    }];
}
} // namespace HostSeamless
