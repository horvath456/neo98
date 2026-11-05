// SPDX-License-Identifier: GPL-2.0-or-later
// The way of a scrollWheel event of the Mac to the units the guest gets,
// behind real events and without a visible window (tools/test_host_scroll_native.py builds and runs this).
// An SDL window that is never shown, the monitor of the host
// (HostSeamless::WatchScroll, tools/host_scroll_watch.mm) and synthetic
// events: CGEvents of the scroll wheel with phases, momentum, pixel or line
// units, made into NSEvents for that window and posted to the application.
// SDL takes them out of the queue and sends them on as it does with the
// window server's (the monitor runs there); a made event names no window,
// so it is handed to the view of the SDL window by this test, and from there
// it is SDL's way again. Each SDL_MOUSEWHEEL goes through HostWheelFromSdl,
// the function tools/neo98_host.cpp calls. What is checked is printed, a
// line a check, "ok" or "FAIL" in front.
#import <AppKit/AppKit.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <vector>
#include "host_wheel.h"

namespace HostSeamless
{
void WatchScroll();
}

namespace
{
int failures = 0, checks = 0;
void Check(bool okay, const char *name)
{
    ++checks;
    if (!okay) ++failures;
    printf("%s %s\n", okay ? "ok  " : "FAIL", name);
}

struct Sent
{
    bool precise = true;
    double y = 0, x = 0;        // points of a trackpad or notches of a wheel
    int phase = 0, momentum = 0; // kCGScrollPhase..., kCGMomentumScrollPhase...
    bool inverted = false;       // "natural" scrolling: the device's direction turned round
    bool small = false;          // points without a line delta: SDL queues nothing
};
struct Got
{
    HostWheelStep step;
    double delta = 0, sideways = 0;
    unsigned direction = 0;
};

NSWindow *native = nil;
SDL_Window *window = nullptr;

NSEvent *Make(const Sent &sent)
{
    CGEventRef event = CGEventCreateScrollWheelEvent2(nullptr, sent.precise ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine,
                                                      2, (int32_t)lround(sent.y), (int32_t)lround(sent.x), 0);
    if (sent.precise)
    {
        // Whole points; Quartz makes the line delta a tenth of them itself.
        CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 1);
        if (sent.small)
        {
            CGEventSetDoubleValueField(event, kCGScrollWheelEventFixedPtDeltaAxis1, 0);
            CGEventSetDoubleValueField(event, kCGScrollWheelEventFixedPtDeltaAxis2, 0);
        }
        CGEventSetIntegerValueField(event, kCGScrollWheelEventScrollPhase, sent.phase);
        CGEventSetIntegerValueField(event, kCGScrollWheelEventMomentumPhase, sent.momentum);
    }
    if (sent.inverted) CGEventSetIntegerValueField(event, (CGEventField)137, 1); // the field NSEvent reads the direction from
    const NSRect frame = native.frame;
    CGEventSetLocation(event, CGPointMake(NSMidX(frame), NSScreen.screens[0].frame.size.height - NSMidY(frame)));
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer, native.windowNumber);
    CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, native.windowNumber);
    NSEvent *made = [NSEvent eventWithCGEvent:event];
    CFRelease(event);
    return made;
}

// Posts the events and takes what SDL makes of them through the host's function.
std::vector<Got> Run(HostWheel &wheel, const std::vector<Sent> &events, double scale, bool held = false)
{
    std::vector<Got> got;
    for (const Sent &sent : events)
    {
        @autoreleasepool
        {
            // Through the application's queue: the monitor sees the event
            // as it sees one of the window server. A made event names no
            // window, so AppKit's own step from there to the view under the
            // pointer is done here; from the view on it is SDL's way again.
            NSEvent *event = Make(sent);
            [NSApp postEvent:event atStart:NO];
            SDL_PumpEvents();
            NSView *content = native.contentView;
            [([content hitTest:NSMakePoint(NSMidX(content.bounds), NSMidY(content.bounds))] ?: content) scrollWheel:event];
            SDL_PumpEvents();
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type != SDL_MOUSEWHEEL) continue;
                Got one;
                one.delta = ev.wheel.preciseY ? ev.wheel.preciseY : ev.wheel.y;
                one.sideways = ev.wheel.preciseX ? ev.wheel.preciseX : ev.wheel.x;
                one.direction = ev.wheel.direction;
                if (getenv("NEO98_SCROLL_TEST_DUMP"))
                    for (const auto &seen : HostScrollSeen())
                        printf("     record points=%g lines=%g precise=%d momentum=%d | SDL delta=%g\n", seen.points_y, seen.lines_y,
                               seen.precise, seen.momentum, one.delta);
                one.step = HostWheelFromSdl(wheel, one.delta, one.sideways, false, held, 1, scale);
                got.push_back(one);
            }
        }
    }
    return got;
}
int SumY(const std::vector<Got> &got)
{
    int sum = 0;
    for (const Got &one : got) sum += one.step.kind == HostWheelStep::Notches ? one.step.steps * 120 : one.step.out.y;
    return sum;
}
int SumX(const std::vector<Got> &got)
{
    int sum = 0;
    for (const Got &one : got) sum += one.step.out.x;
    return sum;
}
// A gesture of a trackpad: the fingers come down, move `steps` times, leave,
// and the Mac adds `glide` steps of momentum.
std::vector<Sent> Gesture(double first, double step, int steps, int glide, bool inverted = false)
{
    std::vector<Sent> events;
    Sent begin;
    begin.y = first, begin.phase = kCGScrollPhaseBegan, begin.inverted = inverted;
    events.push_back(begin);
    for (int i = 0; i < steps; ++i)
    {
        Sent changed;
        changed.y = step, changed.phase = kCGScrollPhaseChanged, changed.inverted = inverted;
        events.push_back(changed);
    }
    Sent end;
    end.phase = kCGScrollPhaseEnded, end.inverted = inverted;
    events.push_back(end);
    for (int i = 0; i < glide; ++i)
    {
        Sent momentum;
        momentum.y = std::trunc(step * (glide - i) / (glide + 1)), momentum.inverted = inverted;
        if (!momentum.y) momentum.y = step < 0 ? -1 : 1;
        momentum.momentum = i ? kCGMomentumScrollPhaseContinue : kCGMomentumScrollPhaseBegin;
        events.push_back(momentum);
    }
    if (glide)
    {
        Sent stop;
        stop.momentum = kCGMomentumScrollPhaseEnd, stop.inverted = inverted;
        events.push_back(stop);
    }
    return events;
}
double Points(const std::vector<Sent> &events)
{
    double sum = 0;
    for (const Sent &sent : events) sum += sent.y;
    return sum;
}
} // namespace

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        // No Dock icon, no menu bar, no activation: nothing of this shows.
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        // As the host sets it (tools/neo98_host.cpp): without it SDL3 has
        // AppKit keep every momentum event to itself; "--no-momentum" shows that.
        const bool momentum_wanted = !(argc > 1 && !strcmp(argv[1], "--no-momentum"));
        SDL_SetHint("SDL_MAC_SCROLL_MOMENTUM", momentum_wanted ? "1" : "0");
        if (SDL_Init(SDL_INIT_VIDEO))
        {
            printf("skip: SDL has no video here (%s)\n", SDL_GetError());
            return 77;
        }
        if (strcmp(SDL_GetCurrentVideoDriver(), "cocoa"))
        {
            printf("skip: the video driver is %s, not cocoa\n", SDL_GetCurrentVideoDriver());
            return 77;
        }
        window = SDL_CreateWindow("NEO98 scroll test", 100, 100, 800, 600, SDL_WINDOW_HIDDEN);
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (!window || !SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_COCOA)
        {
            printf("skip: no Cocoa window (%s)\n", SDL_GetError());
            return 77;
        }
        native = info.info.cocoa.window;
        Check(!native.visible, "the window of the test is not on the screen");
        HostSeamless::WatchScroll();
        HostScrollSeen().clear();
        if (!momentum_wanted)
        {
            // What the host did before: the events after the fingers left do
            // not come to the application's monitor at all.
            HostWheel wheel;
            const auto events = Gesture(-4, -10, 4, 6);
            unsigned seen = 0;
            for (const Sent &sent : events)
            {
                @autoreleasepool
                {
                    HostScrollSeen().clear();
                    [NSApp postEvent:Make(sent) atStart:NO];
                    SDL_PumpEvents();
                    if (sent.momentum) seen += !HostScrollSeen().empty();
                }
            }
            printf("without the hint the monitor saw %u of 6 momentum events\n", seen);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return seen ? 1 : 0;
        }

        // 1. A gesture with momentum, a pixel of the guest a point.
        {
            HostWheel wheel;
            const auto events = Gesture(-4, -10, 20, 10);
            const auto got = Run(wheel, events, 1);
            unsigned moving = 0, watched = 0, begins = 0, glides = 0;
            for (const Sent &sent : events) moving += sent.y != 0;
            for (const Got &one : got)
            {
                watched += one.step.watched && one.step.seen.precise;
                begins += (one.step.out.flags & HostScrollBegin) != 0;
                glides += (one.step.out.flags & HostScrollMomentum) != 0;
            }
            const double points = Points(events);
            printf("     gesture: %zu events, %u with a way, %zu from SDL, %.1f points, %d units\n", events.size(), moving,
                   got.size(), points, SumY(got));
            Check(got.size() == moving, "SDL hands on one event for every event with a way, none twice");
            Check(watched == got.size(), "the monitor's record goes with each of them: a trackpad, with its points");
            Check(SumY(got) == int(points * 3), "the units are three a point, nothing lost and nothing added");
            Check(begins == 1 && (got[0].step.out.flags & HostScrollBegin), "the first step and only the first begins the gesture");
            Check(glides == 10, "the ten steps after the fingers left are marked as momentum");
            Check(HostScrollSeen().empty(), "no record is left over");
            // The line of the session log for it.
            HostScrollLog log;
            unsigned long long now = 1000;
            for (const Got &one : got) log.Add(one.step, one.delta, one.sideways, false, now += 8);
            char line[320] = "";
            const bool said = log.Line(line, sizeof line, 0x421, "Edit");
            printf("     %s\n", line);
            char units[40];
            snprintf(units, sizeof units, "units=%d,0", int(points * 3));
            Check(said && strstr(line, "device=trackpad source=monitor phases=begin+momentum") && strstr(line, units) &&
                      strstr(line, "target=421 class=Edit"),
                  "the log line names device, phases, way, units and target");
            HostScrollLog off;
            off.enabled = false;
            off.Add(got[0].step, 0, 0, false, 1);
            Check(!off.Line(line, sizeof line, 0, ""), "switched off it says nothing");
        }
        // 2. The direction: the content's way, as macOS hands it out; "natural" or not is decided there.
        {
            HostWheel plain, natural;
            const auto down = Run(plain, Gesture(4, 10, 6, 0), 1);
            const auto same = Run(natural, Gesture(4, 10, 6, 0, true), 1);
            Check(SumY(down) > 0, "a positive way (the content goes down) is the wheel turned up");
            const bool flagged = !same.empty() && same[0].direction == SDL_MOUSEWHEEL_FLIPPED;
            if (!flagged) printf("     (this macOS does not let a made event carry the flag of natural scrolling)\n");
            Check(SumY(same) == SumY(down), flagged ? "with natural scrolling (SDL: FLIPPED) the same events give the same units, turned once by macOS and not again"
                                                    : "the same events give the same units");
        }
        // 3. A wheel with notches, also sideways (Shift makes macOS do that), beside the trackpad.
        {
            HostWheel wheel;
            auto events = Gesture(-4, -9, 3, 0); // 31 points: 93 units
            Sent notch;
            notch.precise = false, notch.y = -1;
            events.push_back(notch);
            notch.y = 3;
            events.push_back(notch);
            Sent side;
            side.precise = false, side.x = 1;
            events.push_back(side);
            const auto more = Gesture(-4, -9, 3, 0);
            events.insert(events.end(), more.begin(), more.end());
            const auto got = Run(wheel, events, 1);
            Check(got.size() == 11, "four steps, three notches and four steps arrive");
            if (got.size() == 11)
            {
                Check(got[4].step.watched && !got[4].step.seen.precise && got[4].step.out.y == -120 && !got[4].step.out.flags,
                      "a notch of a wheel is 120 units and no trackpad step, whatever the trackpad left over");
                Check(got[5].step.out.y == 360, "three notches at once are 360 units");
                Check(got[6].step.out.y == 0 && got[6].step.out.x == -120 && got[6].sideways == -1,
                      "a notch sideways is 120 units sideways, as SDL's own number has it");
                Check((got[7].step.out.flags & HostScrollBegin) && got[7].step.out.y + got[8].step.out.y + got[9].step.out.y + got[10].step.out.y == -93,
                      "the trackpad after it begins anew, with nothing of the gesture before");
            }
        }
        // 4. Without the monitor's record SDL's own number stands: ten points a notch.
        {
            HostWheel with, without;
            unsigned long long now = 5000;
            const auto events = Gesture(-4, -10, 12, 0);
            const auto got = Run(with, events, 1);
            int alone = 0;
            for (const Got &one : got)
            {
                HostScrollSeen().clear();
                alone += HostWheelFromSdl(without, one.delta, one.sideways, false, false, 1, 1, now += 8).out.y;
            }
            printf("     with the monitor %d units, with SDL alone %d\n", SumY(got), alone);
            Check(abs(alone - SumY(got)) <= 3, "SDL alone comes to the same way, also where a step is a whole number of notches");
            // A notch of a wheel later is a notch again.
            HostScrollSeen().clear();
            Check(HostWheelFromSdl(without, -1, 0, false, false, 1, 1, now + 1000).out.y == -120,
                  "and a whole number a second later is a notch of a wheel");
        }
        // 5. A step of points without a line delta between others: SDL 2 keeps it to itself, SDL 3 hands it on.
        {
            HostWheel wheel;
            auto events = Gesture(-4, -10, 3, 0);
            Sent small;
            small.y = -3, small.phase = kCGScrollPhaseChanged, small.small = true;
            events.insert(events.begin() + 2, small);
            const auto got = Run(wheel, events, 1);
            unsigned watched = 0;
            for (const Got &one : got) watched += one.step.watched;
            printf("     five steps, SDL handed on %zu\n", got.size());
            Check((got.size() == 4 || got.size() == 5) && watched == got.size(), "every event of SDL finds its own record, also after that step");
            Check(SumY(got) == int(Points(events) * 3), "and the way of that step is not lost");
            Check(HostScrollSeen().empty(), "no record is left over (2)");
        }
        // 6. A window shown at half its size: a point is two pixels of the guest.
        {
            HostWheel wheel;
            const auto events = Gesture(-4, -10, 6, 0);
            const auto got = Run(wheel, events, 2);
            Check(SumY(got) == int(Points(events) * 3 / 2), "at scale 2 a point is half the units: the text keeps to the fingers");
        }
        // 7. A program that holds the mouse gets whole notches of SDL's number.
        {
            HostWheel wheel;
            const auto got = Run(wheel, Gesture(-4, -10, 20, 0), 1, true);
            int notches = 0;
            for (const Got &one : got) notches += one.step.steps;
            Check(notches == -20 && SumY(got) == -2400, "held: 204 points are 20 whole notches for the game");
        }
        // 8. A Mac window of the seamless view makes the same of the same events.
        {
            HostWheel sdl, view;
            auto events = Gesture(-4, -10, 20, 10);
            Sent notch;
            notch.precise = false, notch.y = 2;
            events.push_back(notch);
            const auto got = Run(sdl, events, 1);
            int units = 0, flags_differ = 0;
            size_t at = 0;
            bool begin = false;
            for (const Sent &sent : events)
            {
                NSEvent *event = Make(sent);
                if (event.phase == NSEventPhaseBegan || event.phase == NSEventPhaseMayBegin) begin = true;
                HostScrollEvent seen;
                seen.precise = event.hasPreciseScrollingDeltas;
                seen.points_y = event.scrollingDeltaY, seen.points_x = event.scrollingDeltaX;
                seen.lines_y = event.deltaY, seen.lines_x = event.deltaX;
                seen.begin = begin;
                seen.momentum = event.momentumPhase != NSEventPhaseNone;
                const HostScroll out = HostWheelFromView(view, seen);
                if (seen.precise && (seen.points_y || seen.points_x)) begin = false;
                units += out.y;
                if (!seen.points_y && !seen.lines_y) continue;
                if (at < got.size() && got[at].step.out.flags != out.flags) ++flags_differ;
                ++at;
            }
            Check(units == SumY(got) && SumX(got) == 0, "the window of the seamless view and the SDL window deliver the same units");
            Check(at == got.size() && !flags_differ, "with the same marks of begin and momentum, step for step");
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    printf("host scroll native: %d of %d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
