// SPDX-License-Identifier: GPL-2.0-or-later
// Timed test input for the hosts without a desktop window (tools/neo98_kms.cpp,
// and the like), read from NEO98_HOST_INPUT. Include it after
// src/x86_app.h, as those hosts do.
#pragma once
#include "../../src/usb_keys.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
namespace HostKms
{
// Timed test input (NEO98_HOST_INPUT), the steps the other hosts know:
// wait=CLASS, wait=CLASS|TITLE (an empty class is any), wait=X,Y,RRGGBB
// (until the pixel of the screen has about that colour; the time since the
// step before goes to stderr), click=X,Y,
// dblclick=X,Y, move=X,Y, down=X,Y and up=X,Y (the left button pressed and
// let go in steps of their own, a move between them drags with it held),
// drag=X1,Y1,X2,Y2 (guest coordinates), key=VK,
// ctrl=VK and alt=VK (hexadecimal), keydown=VK and keyup=VK (a key held over
// the steps between them), type=TEXT, shot=PATH (the screen as PNG), quit=1
// (also menu=quit), and idle=MS: until no thread of the machine has been
// runnable for MS milliseconds since the step began, as the other hosts
// wait before a picture. A wait gives up after 240 s and says so; the
// seconds of the later steps count from the end of a wait.
class Script
{
    struct Step { double time; std::string action, value; };
    std::vector<Step> steps;
    size_t at = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), stepped = start, busy_at = start,
                                          waiting_since{};
    bool waiting = false;
    // The buttons a down= holds until its up=.
    unsigned held = 0;
    // Within 48 of each channel, as the other hosts compare.
    static bool Pixel(int x, int y, unsigned want)
    {
        int width = 0, height = 0;
        User::PixelsSize(width, height);
        const unsigned char *pixels = User::Pixels();
        if (!pixels || x < 0 || y < 0 || x >= width || y >= height) return false;
        const unsigned char *q = pixels + (size_t(y) * width + x) * 4;
        return abs(q[2] - int((want >> 16) & 255)) <= 48 && abs(q[1] - int((want >> 8) & 255)) <= 48 &&
               abs(q[0] - int(want & 255)) <= 48;
    }
    static bool Window(const std::string &name)
    {
        // CLASS|TITLE as the other hosts take it: a window whose title a
        // program sets when it has come so far.
        if (const auto bar = name.find('|'); bar != std::string::npos)
        {
            const std::string klass = name.substr(0, bar), title = name.substr(bar + 1);
            const unsigned h = User::FindWindow(klass.empty() ? nullptr : klass.c_str(), title.empty() ? nullptr : title.c_str());
            return h && User::IsVisible(h);
        }
        unsigned windows[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count)) return false;
        for (unsigned i = 0; i < count; ++i)
        {
            char klass[256];
            User::GetClassName(windows[i], klass, sizeof klass);
            if (name == klass && User::IsVisible(windows[i])) return true;
        }
        return false;
    }

  public:
    Script()
    {
        const char *environment = getenv("NEO98_HOST_INPUT");
        if (!environment) return;
        std::string text = environment;
        for (size_t from = 0; from < text.size();)
        {
            auto end = text.find(';', from);
            if (end == std::string::npos) end = text.size();
            auto entry = text.substr(from, end - from);
            from = end + 1;
            auto colon = entry.find(':'), equals = entry.find('=');
            if (colon == std::string::npos || equals == std::string::npos || equals <= colon) continue;
            steps.push_back({strtod(entry.substr(0, colon).c_str(), nullptr), entry.substr(colon + 1, equals - colon - 1), entry.substr(equals + 1)});
        }
    }
    // Returns false when the script asks to quit.
    bool Pump(X86::Application &app, const std::function<bool(const std::string &)> &shot)
    {
        const auto now = std::chrono::steady_clock::now();
        if (app.SystemRunnable()) busy_at = now;
        if (at == steps.size()) return true;
        const auto &step = steps[at];
        if (std::chrono::duration<double>(now - start).count() < step.time) return true;
        if (step.action == "wait" || step.action == "idle")
        {
            if (!waiting) { waiting = true; waiting_since = now; }
            const double waited = std::chrono::duration<double>(now - waiting_since).count();
            int x = 0, y = 0;
            unsigned colour = 0;
            char rest = 0;
            const bool pixel = step.action == "wait" && sscanf(step.value.c_str(), "%d,%d,%x%c", &x, &y, &colour, &rest) == 3;
            bool met;
            if (step.action == "idle")
            {
                // Quiet since the step began, not only before it: the input
                // of the step before may not have run yet.
                const double quiet = std::min(std::chrono::duration<double>(now - busy_at).count(), waited);
                met = quiet * 1000 >= atof(step.value.c_str());
            }
            else met = pixel ? Pixel(x, y, colour) : Window(step.value);
            const bool expired = waited > 240;
            if (!met && !expired) return true;
            waiting = false;
            start = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(step.time));
            if (!met)
            {
                // Not the line of a step that was taken: a test that looks
                // for it sees that the wait came to nothing.
                fprintf(stderr, "NEO98 KMS input %s=%s timed out after %.1f s\n", step.action.c_str(),
                        step.value.c_str(), waited);
                stepped = now;
                ++at;
                return true;
            }
            if (pixel)
                fprintf(stderr, "NEO98 KMS input pixel %d,%d is %06x after %.3f s\n", x, y, colour,
                        std::chrono::duration<double>(now - stepped).count());
            if (step.action == "idle")
                fprintf(stderr, "NEO98 KMS input idle=%s reached after %.1f s\n", step.value.c_str(), waited);
        }
        else if (step.action == "shot") shot(step.value);
        else if (step.action == "quit" || (step.action == "menu" && step.value == "quit")) { ++at; return false; }
        else if (step.action == "type")
            for (unsigned char character : step.value) { app.Key(0, character, false, false); app.KeyUp(0); }
        else if (step.action == "key" || step.action == "ctrl" || step.action == "alt")
        {
            const unsigned key = strtoul(step.value.c_str(), nullptr, 16);
            unsigned scan = 0; bool extended = false;
            for (unsigned usage = 1; usage < 128; ++usage)
            {
                const auto mapped = UsbKeys::Map(usage, false, true);
                if (mapped.vk == key) { scan = mapped.scan; extended = mapped.extended; break; }
            }
            // Alt is a key of its own for Windows (WM_SYSKEYDOWN), not only a state.
            const bool alt = step.action == "alt";
            app.KeyboardModifiers(step.action == "ctrl" ? 1 : alt ? 4 : 0);
            if (alt) app.RawKey(0x12, 0, true, 0x38, false);
            app.RawKey(key, key == 0x0d && !alt ? '\r' : 0, true, scan, extended);
            app.RawKey(key, 0, false, scan, extended);
            if (alt) app.RawKey(0x12, 0, false, 0x38, false);
            app.KeyboardModifiers(0);
        }
        else if (step.action == "keydown" || step.action == "keyup")
        {
            // A key held over several steps, as a hand holds a key down: the
            // same key with the same scan code as neo98-host's.
            const unsigned key = strtoul(step.value.c_str(), nullptr, 16);
            unsigned scan = 0; bool extended = false;
            for (unsigned usage = 1; usage < 128; ++usage)
            {
                const auto mapped = UsbKeys::Map(usage, false, true);
                if (mapped.vk == key) { scan = mapped.scan; extended = mapped.extended; break; }
            }
            switch (key)
            {
            case 0xa0: scan = 0x2a; break; case 0xa1: scan = 0x36; break;
            case 0xa2: case 0xa3: scan = 0x1d; break; case 0xa4: case 0xa5: scan = 0x38; break;
            default: break;
            }
            app.RawKey(key, 0, step.action == "keydown", scan, extended || key == 0xa3 || key == 0xa5);
        }
        else if (step.action == "down" || step.action == "up")
        {
            int x = 0, y = 0;
            if (sscanf(step.value.c_str(), "%d,%d", &x, &y) == 2)
            {
                held = step.action == "down" ? 1 : 0;
                app.MouseButtons(x, y, held);
            }
        }
        else if (step.action == "move" && held)
        {
            int x = 0, y = 0;
            if (sscanf(step.value.c_str(), "%d,%d", &x, &y) == 2) app.MouseButtons(x, y, held);
        }
        else if (step.action == "click" || step.action == "dblclick" || step.action == "move")
        {
            int x = 0, y = 0;
            if (sscanf(step.value.c_str(), "%d,%d", &x, &y) == 2)
            {
                app.MouseButtons(x, y, 0);
                const unsigned clicks = step.action == "dblclick" ? 2 : step.action == "click" ? 1 : 0;
                for (unsigned i = 0; i < clicks; ++i) { app.MouseButtons(x, y, 1); app.MouseButtons(x, y, 0); }
            }
        }
        else if (step.action == "drag")
        {
            // drag=X1,Y1,X2,Y2: press, move in steps, release.
            int x1, y1, x2, y2;
            if (sscanf(step.value.c_str(), "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4)
            {
                app.MouseButtons(x1, y1, 0); app.MouseButtons(x1, y1, 1);
                for (int i = 1; i <= 8; ++i) app.MouseButtons(x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8, 1);
                app.MouseButtons(x2, y2, 0);
            }
        }
        else fprintf(stderr, "Unknown test input: %s\n", step.action.c_str());
        fprintf(stderr, "NEO98 KMS input %s=%s\n", step.action.c_str(), step.value.c_str());
        stepped = std::chrono::steady_clock::now();
        ++at;
        return true;
    }
};
}
