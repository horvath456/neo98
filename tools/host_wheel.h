// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <cstdio>
#include <deque>

// The wheel and the trackpad of the Mac for the guest.
// A wheel sends notches, the guest's WHEEL_DELTA (120 units) each. A
// trackpad sends points of the fingers' way, many small steps, and goes on
// sending after the fingers left (momentum). A Mac program scrolls a pixel a
// point. A notch of Win98 is three lines, about 40 pixels, so 40 pixels of
// the guest's picture are 120 units: the way on the trackpad and the way the
// text moves stay in proportion, at every scale of the window.
enum : unsigned { HostScrollFine = 1, HostScrollBegin = 2, HostScrollMomentum = 4 }; // User::ScrollFine...
struct HostScroll
{
    int y = 0, x = 0;   // units up and to the right
    unsigned flags = 0;
};
// One scrollWheel event of the Mac as a monitor saw it before SDL did
// (HostSeamless::WatchScroll): SDL hands on neither the kind of the device
// nor the phases of a gesture.
struct HostScrollEvent
{
    double points_y = 0, points_x = 0; // scrollingDelta, the content's way: down and right positive
    double lines_y = 0, lines_x = 0;   // deltaY and deltaX of a wheel
    bool precise = false, begin = false, momentum = false;
};
inline std::deque<HostScrollEvent> &HostScrollSeen()
{
    static std::deque<HostScrollEvent> seen;
    return seen;
}
struct HostWheel
{
    constexpr static double PixelsPerNotch = 40;
    double remainder = 0, rest_y = 0, rest_x = 0;
    unsigned target = 0;
    bool begin = false;
    // Until when a whole number of SDL is still a step of the trackpad
    // (HostWheelFromSdl without the monitor).
    unsigned long long fine_until = 0;
    void Reset() { remainder = rest_y = rest_x = 0; target = 0; begin = false; fine_until = 0; }
    // Whole notches out of fractions of one: for a program that holds the
    // mouse (a game), which gets the wheel as it always did.
    int Steps(double delta, unsigned focus)
    {
        if (!std::isfinite(delta) || !delta) return 0;
        if (focus != target || delta * remainder < 0) remainder = 0;
        target = focus;
        // Bound a malformed host event before converting to int.
        delta = delta > 2048 ? 2048 : delta < -2048 ? -2048 : delta;
        remainder += delta;
        const int steps = int(remainder);
        remainder -= steps;
        return steps;
    }
    // A notch of a wheel: `notches` up.
    HostScroll Notches(int notches)
    {
        rest_y = rest_x = 0;
        begin = false;
        HostScroll out;
        out.y = notches * 120;
        return out;
    }
    // A step of a trackpad: the content's way in points, `scale` points of
    // the Mac a pixel of the guest. The first step of a gesture carries
    // HostScrollBegin, also when it comes after steps too small to count.
    HostScroll Points(double up, double right, double scale, bool first, bool momentum)
    {
        HostScroll out;
        if (!std::isfinite(up) || !std::isfinite(right)) return out;
        if (first)
        {
            rest_y = rest_x = 0;
            begin = true;
        }
        if (!(scale > 0)) scale = 1;
        const double units = 120.0 / (PixelsPerNotch * scale);
        const auto add = [&](double &rest, double points) {
            if (points * rest < 0) rest = 0; // the other way: what was collected is forgotten
            rest += (points > 4096 ? 4096 : points < -4096 ? -4096 : points) * units;
            const int whole = int(rest);
            rest -= whole;
            return whole;
        };
        out.y = add(rest_y, up);
        out.x = add(rest_x, right);
        if (!out.y && !out.x) return out;
        out.flags = HostScrollFine | (begin ? HostScrollBegin : 0u) | (momentum ? HostScrollMomentum : 0u);
        begin = false;
        return out;
    }
};

// What one SDL_MOUSEWHEEL event becomes (tools/neo98_host.cpp; the same
// function runs in tools/test_host_scroll_native.mm behind real events).
struct HostWheelStep
{
    enum Kind { Nothing, Notches, Scroll } kind = Nothing; // Application::MouseWheel(steps) or MouseScroll(out)
    int steps = 0;
    HostScroll out;
    bool watched = false; // the record of the monitor was used
    HostScrollEvent seen;
};
// `delta` and `sideways` are SDL's wheel.preciseY and preciseX (y and x of an
// older SDL), `script` an event of NEO98_HOST_INPUT, `held` a program that
// holds the mouse (a game), `scale` points of the Mac a pixel of the guest.
// `now` in milliseconds, for the case without the monitor.
inline HostWheelStep HostWheelFromSdl(HostWheel &wheel, double delta, double sideways, bool script, bool held,
                                      unsigned focus, double scale, unsigned long long now = 0)
{
    HostWheelStep step;
    // What the monitor saw of this event (HostSeamless::WatchScroll): the
    // kind of the device and the phases, which SDL drops. The record is the
    // one with SDL's own numbers: SDL 2 passes deltaY and -deltaX on, SDL 3
    // (behind sdl2-compat) a tenth of scrollingDelta, both a notch of a wheel
    // rounded away from zero. Records in front of it belong to events SDL
    // did not queue and go; when none fits, SDL's numbers stand and the
    // records wait for their events, at most a handful.
    if (!script)
    {
        auto &queue = HostScrollSeen();
        const auto whole = [](double lines) { return lines > 0 ? std::ceil(lines) : std::floor(lines); };
        const auto same = [](double a, double b) { return std::fabs(a - b) < 0.002 + 0.001 * std::fabs(b); };
        for (size_t i = 0; i < queue.size(); ++i)
        {
            const HostScrollEvent &seen = queue[i];
            const bool fits = (same(seen.lines_y, delta) && same(-seen.lines_x, sideways)) ||
                              (seen.precise && same(seen.points_y / 10, delta) && same(-seen.points_x / 10, sideways)) ||
                              (!seen.precise && same(whole(seen.lines_y), delta) && same(-whole(seen.lines_x), sideways));
            if (!fits) continue;
            step.seen = seen;
            step.watched = true;
            // The way of the trackpad events in front that SDL kept to itself
            // (a delta too small for its own number) is not lost.
            for (size_t k = 0; k < i; ++k)
                if (queue[k].precise && seen.precise && queue[k].momentum == seen.momentum)
                {
                    step.seen.points_y += queue[k].points_y;
                    step.seen.points_x += queue[k].points_x;
                    step.seen.begin = step.seen.begin || queue[k].begin;
                }
            queue.erase(queue.begin(), queue.begin() + long(i) + 1);
            break;
        }
        if (!step.watched && queue.size() > 8) queue.clear();
    }
    // A program that holds the mouse (a game) and the script's wheel= get
    // whole notches as ever.
    if (script || held)
    {
        step.steps = wheel.Steps(delta, focus);
        step.kind = step.steps ? HostWheelStep::Notches : HostWheelStep::Nothing;
        return step;
    }
    const auto whole = [](double lines) { return lines > 0 ? int(std::ceil(lines)) : int(std::floor(lines)); };
    if (step.watched && step.seen.precise)
        step.out = wheel.Points(step.seen.points_y, -step.seen.points_x, scale, step.seen.begin, step.seen.momentum);
    else if (step.watched)
    {
        // A wheel with notches; with Shift macOS makes its notch a sideways one.
        step.out = wheel.Notches(whole(step.seen.lines_y));
        step.out.x = -whole(step.seen.lines_x) * 120;
    }
    else if (delta != std::floor(delta) || sideways != std::floor(sideways) || (now && now < wheel.fine_until))
    {
        // Without the monitor a fraction is a trackpad's: SDL counts ten
        // points a notch. A step of exactly ten or twenty points in the
        // middle of such a gesture is a whole number and still no notch of a
        // wheel (it was taken for one and scrolled four times its way).
        step.out = wheel.Points(delta * 10, sideways * 10, scale, false, false);
        if (now) wheel.fine_until = now + 400;
    }
    else
    {
        step.out = wheel.Notches(int(delta));
        step.out.x = int(sideways) * 120;
    }
    step.kind = step.out.y || step.out.x ? HostWheelStep::Scroll : HostWheelStep::Nothing;
    return step;
}

// A scrollWheel event at a Mac window of the seamless view
// (tools/host_seamless.mm): the view shows the guest a pixel a point.
// `first`: a phase of beginning came since the last step.
inline HostScroll HostWheelFromView(HostWheel &wheel, const HostScrollEvent &event)
{
    if (event.precise)
    {
        if (!event.points_y && !event.points_x) return HostScroll();
        return wheel.Points(event.points_y, -event.points_x, 1, event.begin, event.momentum);
    }
    const auto whole = [](double lines) { return lines > 0 ? int(std::ceil(lines)) : int(std::floor(lines)); };
    HostScroll out = wheel.Notches(whole(event.lines_y));
    out.x = -whole(event.lines_x) * 120;
    return out;
}

// One line in the session log a gesture, so that "it feels wrong" can be
// read afterwards : the device, where the host knew
// it from, the phases, the events, the way in points and what reached the
// guest. A gesture ends with the next one's begin, a change of the device or
// 0.4 s without an event (User::RouteWheel takes the same pause).
struct HostScrollLog
{
    bool enabled = true, open = false, precise = false, watched = false, held = false, begun = false, momentum = false;
    bool window = false; // a Mac window of the seamless view took the events
    unsigned events = 0, delivered = 0;
    double points_y = 0, points_x = 0;
    int units_y = 0, units_x = 0;
    unsigned long long last = 0;
    // `delta`, `sideways`: what SDL said, for an event the monitor did not
    // True when the gesture before ended with this event: the caller prints it first.
    bool Starts(const HostWheelStep &step, unsigned long long now) const
    {
        const bool fine = step.watched ? step.seen.precise : (step.out.flags & HostScrollFine) != 0;
        return open && (window || (step.watched && step.seen.begin && !step.seen.momentum) || fine != precise || now - last > 400);
    }
    void Add(const HostWheelStep &step, double delta, double sideways, bool program_holds, unsigned long long now)
    {
        if (!open)
        {
            *this = HostScrollLog{enabled};
            open = true;
            precise = step.watched ? step.seen.precise : (step.out.flags & HostScrollFine) != 0;
            watched = step.watched;
            held = program_holds;
        }
        ++events;
        last = now;
        begun = begun || (step.watched && step.seen.begin);
        momentum = momentum || (step.watched && step.seen.momentum);
        if (step.watched && step.seen.precise) points_y += step.seen.points_y, points_x += step.seen.points_x;
        else if (step.watched) points_y += step.seen.lines_y, points_x += step.seen.lines_x;
        else points_y += delta, points_x += sideways;
        if (step.kind == HostWheelStep::Notches) units_y += step.steps * 120, ++delivered;
        if (step.kind == HostWheelStep::Scroll) units_y += step.out.y, units_x += step.out.x, ++delivered;
    }
    // The units a window of the seamless view made of its events.
    void AddUnits(const HostScroll &out, unsigned long long now)
    {
        const bool fine = (out.flags & HostScrollFine) != 0;
        if (!open)
        {
            *this = HostScrollLog{enabled};
            open = window = true;
            precise = fine;
        }
        ++events, ++delivered;
        last = now;
        begun = begun || (out.flags & HostScrollBegin);
        momentum = momentum || (out.flags & HostScrollMomentum);
        units_y += out.y, units_x += out.x;
    }
    bool StartsUnits(const HostScroll &out, unsigned long long now) const
    {
        return open && (!window || (out.flags & HostScrollBegin) || ((out.flags & HostScrollFine) != 0) != precise || now - last > 400);
    }
    bool Due(unsigned long long now) const { return open && now - last > 400; }
    // The line, without its end; false when there is nothing to say.
    bool Line(char *text, size_t capacity, unsigned target, const char *target_class)
    {
        if (!open) return false;
        open = false;
        if (!enabled) return false;
        snprintf(text, capacity,
                 "NEO98 HOST scroll gesture device=%s source=%s phases=%s%s events=%u %s=%.1f,%.1f units=%d,%d "
                 "sent=%u held=%d target=%x class=%s",
                 precise ? "trackpad" : "wheel", window ? "window" : watched ? "monitor" : "sdl", begun ? "begin" : "none",
                 momentum ? "+momentum" : "", events, precise ? "points" : "notches", points_y, points_x, units_y, units_x,
                 delivered, held ? 1 : 0, target, target_class && *target_class ? target_class : "-");
        return true;
    }
};
