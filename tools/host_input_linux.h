// SPDX-License-Identifier: GPL-2.0-or-later
// Timed input through the same application input methods as the SDL host.
#pragma once
#include "linux_session.h"
#include "../src/usb_keys.h"
#include "host_menu_linux.h"
#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
namespace LinuxHost
{
inline std::function<bool(const std::string &,const std::string &)> native_input;
// hostshot=PATH: the window's next frame as the host draws it, video included.
inline std::function<void(const std::string &)> host_shot;
// NEO98_HOST_INPUT="seconds:action=value;..." is the input script of the
// macOS host (tools/neo98_host.cpp) with the same steps and the same lines
// on stdout, so the host tests run on both. wait=CLASS|TITLE, waitchild=,
// wait=x,y,rrggbb and idle=MS hold the clock of the script until they are
// met, and give up after 240 s; the seconds of later steps count from then.
class Script
{
    struct Step { double time; std::string action, value; int x = 0, y = 0; };
    std::vector<Step> steps;
    size_t at = 0;
    unsigned held = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), busy_at = start;
    double paused = 0, waiting_since = -1;
    static bool Window(const std::string &name)
    {
        // CLASS|TITLE as the Mac host takes it; an empty class is any.
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
            char klass[256]; User::GetClassName(windows[i], klass, sizeof klass);
            if (name == klass && User::IsVisible(windows[i])) return true;
        }
        return false;
    }
    static std::string Title(unsigned h)
    {
        char text[256] = {}; User::GetWindowText(h, text, sizeof text); return text;
    }
    static bool Child(const std::string &name)
    {
        const auto bar = name.find('|');
        const std::string klass = name.substr(0, bar), title = bar == std::string::npos ? "" : name.substr(bar + 1);
        auto find = [&](auto &&self, unsigned parent, unsigned depth) -> bool {
            if (depth >= User::WindowCapacity) return false;
            for (unsigned child = User::GetTopWindow(parent); child; child = User::GetWindow(child, 2))
            {
                if (!User::IsVisible(child)) continue;
                char child_class[128] = {}; User::GetClassName(child, child_class, sizeof child_class);
                if ((klass.empty() || klass == child_class) && (title.empty() || title == Title(child))) return true;
                if (self(self, child, depth + 1)) return true;
            }
            return false;
        };
        return find(find, 0, 0);
    }
    static void Press(X86::Application &app, unsigned vk, unsigned character, unsigned modifiers, bool german)
    {
        // The modifiers the layout needs go down around the key, and USER
        // translates the key with that key state.
        unsigned scan = 0; bool extended = false;
        for (unsigned usage = 1; usage < 128; ++usage)
        {
            const auto mapped = UsbKeys::Map(usage, german, true);
            if (mapped.vk == vk) { scan = mapped.scan; extended = mapped.extended; break; }
        }
        if (modifiers & 1) app.RawKey(0xa0, 0, true, 0x2a, false);
        if (modifiers & 2) app.RawKey(0xa2, 0, true, 0x1d, false);
        if (modifiers & 4) app.RawKey(0xa4, 0, true, 0x38, false);
        app.RawKey(vk, character, true, scan, extended);
        app.RawKey(vk, 0, false, scan, extended);
        if (modifiers & 4) app.RawKey(0xa4, 0, false, 0x38, false);
        if (modifiers & 2) app.RawKey(0xa2, 0, false, 0x1d, false);
        if (modifiers & 1) app.RawKey(0xa0, 0, false, 0x2a, false);
    }
    static void Say(const char *format, ...) __attribute__((format(printf, 1, 2)))
    {
        va_list list; va_start(list, format); vprintf(format, list); va_end(list); fflush(stdout);
    }
  public:
    Script()
    {
        const char *environment = getenv("NEO98_HOST_INPUT"); if (!environment) return;
        std::string text = environment;
        for (size_t from = 0; from < text.size();)
        {
            auto end = text.find(';', from); if (end == std::string::npos) end = text.size();
            auto entry = text.substr(from, end - from); from = end + 1;
            auto colon = entry.find(':'), equals = entry.find('=');
            if (colon == std::string::npos || equals == std::string::npos || equals <= colon) continue;
            Step step{strtod(entry.substr(0, colon).c_str(), nullptr), entry.substr(colon + 1, equals - colon - 1), entry.substr(equals + 1)};
            char colour[16] = {};
            if (step.action == "wait" && sscanf(step.value.c_str(), "%d,%d,%15s", &step.x, &step.y, colour) == 3)
            { step.action = "pixel"; step.value = colour; }
            else if (step.action == "idle" || step.action == "tops" || step.action == "lists" || step.action == "trees" ||
                     step.action == "redraw" || step.action == "trace")
                step.x = atoi(step.value.c_str());
            else if (step.action == "keydown" || step.action == "keyup")
                step.x = int(strtoul(step.value.c_str(), nullptr, 16));
            else sscanf(step.value.c_str(), "%d,%d", &step.x, &step.y);
            steps.push_back(step);
        }
    }
    void Pump(Session &session, SDL_Window *window = nullptr)
    {
        auto &app = *session.app;
        const bool german = !(getenv("NEO98_KEYMAP") && !strcmp(getenv("NEO98_KEYMAP"),"US"));
        const auto now = std::chrono::steady_clock::now();
        if (app.SystemRunnable()) busy_at = now;
        const double real = std::chrono::duration<double>(now - start).count();
        while (at < steps.size() && real - paused >= steps[at].time)
        {
            const auto &step = steps[at];
            if (step.action == "wait" || step.action == "waitchild" || step.action == "waitfile" ||
                step.action == "idle" || step.action == "pixel")
            {
                bool met = false;
                if (waiting_since < 0) waiting_since = real;
                if (step.action == "wait") met = Window(step.value);
                else if (step.action == "waitchild") met = Window(step.value) || Child(step.value);
                else if (step.action == "waitfile") { struct stat info; met = !stat(step.value.c_str(), &info); }
                else if (step.action == "pixel")
                {
                    int width = 0, height = 0; User::PixelsSize(width, height);
                    const auto *pixels = User::Pixels();
                    const unsigned want = unsigned(strtoul(step.value.c_str(), nullptr, 16));
                    if (pixels && step.x >= 0 && step.x < width && step.y >= 0 && step.y < height)
                    {
                        const unsigned char *q = pixels + (size_t(step.y) * width + step.x) * 4;
                        met = abs(q[2] - int((want >> 16) & 255)) <= 48 && abs(q[1] - int((want >> 8) & 255)) <= 48 &&
                              abs(q[0] - int(want & 255)) <= 48;
                    }
                }
                else
                {
                    // Quiet since the wait began, not only before it: the
                    // input of the step before may not have run yet.
                    const double quiet = std::min(std::chrono::duration<double>(now - busy_at).count(), real - waiting_since);
                    met = quiet * 1000 >= step.x;
                }
                const bool expired = real - waiting_since > 240;
                if (!met && !expired) { paused = real - step.time; return; }
                Say("NEO98 HOST input %s=%s %s after %.1f s\n", step.action.c_str(),
                    step.action == "idle" ? std::to_string(step.x).c_str() : step.value.c_str(),
                    met ? "reached" : "timed out", real - waiting_since);
                fprintf(stderr, "NEO98 Linux input %s=%s\n", step.action.c_str(), step.value.c_str());
                waiting_since = -1;
                Next(real);
                continue;
            }
            const bool native = step.action.compare(0, 7, "native-") == 0 && native_input;
            if (native && !native_input(step.action, step.value)) return;
            Next(real);
            if (native) {}
            else if (step.action == "shot")
            {
                if (session.Screenshot(step.value)) Say("NEO98 HOST screenshot %s\n", step.value.c_str());
            }
            else if (step.action == "hostshot" && host_shot) host_shot(step.value);
            else if (step.action == "resize" && window)
            {
                int width = 0, height = 0;
                if (sscanf(step.value.c_str(), "%d,%d", &width, &height) == 2 && width > 0 && height > 0)
                    LinuxGtk::Resize(width, height);
            }
            else if (step.action == "pbtext" && Clipboard::current) Clipboard::current->SetText(step.value);
            else if (step.action == "pbtextfile" && Clipboard::current)
            { auto bytes = ReadFile(step.value.c_str()); Clipboard::current->SetText(std::string(bytes.begin(), bytes.end())); }
            else if (step.action == "pbfiles" && Clipboard::current)
            {
                std::vector<std::string> files; size_t from = 0;
                while (from < step.value.size()) { auto end = step.value.find('|', from); if (end == std::string::npos) end = step.value.size(); files.push_back(step.value.substr(from, end - from)); from = end + 1; }
                Clipboard::current->SetFiles(files);
            }
            else if (step.action == "pbimage" && Clipboard::current) Clipboard::current->SetPng(step.value.c_str());
            else if (step.action == "pbdescribe" && Clipboard::current) Clipboard::current->Describe(step.value.c_str());
            else if (step.action == "pbdump")
            {
                std::string text = Clipboard::current ? Clipboard::current->ReadText() : "";
                FILE *file = fopen(step.value.c_str(), "wb");
                if (file) { fwrite(text.data(), 1, text.size(), file); fclose(file); }
            }
            else if (step.action == "menu")
            {
                auto split = step.value.find('=') != std::string::npos ? step.value.find('=') : step.value.find(',');
                HostMenu::Perform(step.value.substr(0, split).c_str(), split == std::string::npos ? nullptr : step.value.substr(split + 1).c_str());
                // The request is served at the top of the next turn; a quit
                // must not see later steps taken.
                fprintf(stderr, "NEO98 Linux input %s=%s\n", step.action.c_str(), step.value.c_str());
                return;
            }
            else if (step.action == "wheel" && window)
            {
                double up = 0, right = 0;
                if (sscanf(step.value.c_str(), "%lf,%lf", &up, &right) >= 1)
                {
                    SDL_Event event = {}; event.type = SDL_MOUSEWHEEL;
                    event.wheel.windowID = SDL_GetWindowID(window);
                    event.wheel.preciseY = up; event.wheel.preciseX = right;
                    event.wheel.y = int(up); event.wheel.x = int(right);
                    SDL_PushEvent(&event);
                }
            }
            else if (step.action == "type" || step.action == "key")
            {
                if (step.action == "key")
                {
                    const unsigned key = strtoul(step.value.c_str(), nullptr, 16);
                    Press(app, key, key == 0x0d ? '\r' : 0, 0, german);
                }
                else for (size_t i = 0; i < step.value.size(); ++i)
                {
                    // The script is UTF-8; the guest page is Windows-1252,
                    // which agrees with Latin-1 above A0h and has the euro at 80h.
                    unsigned c = (unsigned char)step.value[i];
                    if (c >= 0xc0 && c < 0xe0 && i + 1 < step.value.size())
                        c = ((c & 0x1f) << 6) | ((unsigned char)step.value[++i] & 0x3f);
                    else if (c == 0xe2 && i + 2 < step.value.size() && (unsigned char)step.value[i + 1] == 0x82 &&
                             (unsigned char)step.value[i + 2] == 0xac)
                        c = 0x80, i += 2;
                    if (c > 255) continue;
                    // VkKeyScan of the installed layout.
                    const unsigned scanned = app.KeyScan(c);
                    if (scanned != 0xffff) Press(app, scanned & 255, c, (scanned >> 8) & 7, german);
                }
                Say("NEO98 HOST input %s %s\n", step.action.c_str(), step.value.c_str());
            }
            else if (step.action == "ctrl")
            {
                // ctrl=VK: the key with CONTROL held.
                const unsigned key = strtoul(step.value.c_str(), nullptr, 16);
                unsigned scan = 0; bool extended = false;
                for (unsigned usage = 1; usage < 128; ++usage)
                {
                    const auto mapped = UsbKeys::Map(usage, german, true);
                    if (mapped.vk == key) { scan = mapped.scan; extended = mapped.extended; break; }
                }
                app.KeyboardModifiers(1);
                app.RawKey(key, 0, true, scan, extended); app.RawKey(key, 0, false, scan, extended);
                app.KeyboardModifiers(0);
            }
            else if (step.action == "keydown" || step.action == "keyup")
            {
                // keydown=VK and keyup=VK hold a key over several steps, as a
                // hand holds ALT while it presses F.
                unsigned scan = 0; bool extended = false;
                for (unsigned usage = 1; usage < 128; ++usage)
                {
                    const auto mapped = UsbKeys::Map(usage, german, true);
                    if (mapped.vk == unsigned(step.x)) { scan = mapped.scan; extended = mapped.extended; break; }
                }
                switch (step.x)
                {
                case 0xa0: scan = 0x2a; break; case 0xa1: scan = 0x36; break;
                case 0xa2: case 0xa3: scan = 0x1d; break; case 0xa4: case 0xa5: scan = 0x38; break;
                default: break;
                }
                app.RawKey(unsigned(step.x), 0, step.action == "keydown", scan, extended || step.x == 0xa3 || step.x == 0xa5);
                Say("NEO98 HOST input %s %x\n", step.action.c_str(), unsigned(step.x));
            }
            else if (step.action == "tops")
            {
                // tops=1 lists the top-level windows front to back, tops=2
                // the child windows as well.
                unsigned nodes[192], count = 0;
                User::WindowSnapshot(User::GetDesktopWindow(), nodes, 192, count);
                Say("NEO98 HOST tops foreground=%x focus=%x\n", User::ForegroundActiveWindow(), User::GetFocus());
                for (unsigned i = 0; i < count; ++i)
                {
                    const unsigned h = nodes[i];
                    const bool child = (User::Parent(h) && User::Parent(h) != User::GetDesktopWindow()) || (User::Style(h) & 0x40000000u);
                    if (child && step.x != 2) continue;
                    char name[128] = {}, title[128] = {}; Gdi::RECT r{};
                    User::GetClassName(h, name, sizeof name); User::GetWindowText(h, title, sizeof title); User::GetWindowRect(h, &r);
                    Say(child ? "NEO98 HOST child id=%x visible=%d frame=%d,%d,%d,%d style=%08x process=%u props=%d class=%s title=%s\n"
                              : "NEO98 HOST top id=%x visible=%d frame=%d,%d,%d,%d style=%08x process=%u props=%d class=%s title=%s\n", h,
                        User::IsVisible(h) ? 1 : 0, r.left, r.top, r.right, r.bottom, User::Style(h), User::WindowProcess(h),
                        User::PropertyList(h) ? 1 : 0, name, title);
                }
            }
            else if (step.action == "trees")
                app.DumpTrees([](const char *line) { printf("NEO98 HOST %s\n", line); fflush(stdout); });
            else if (step.action == "lists")
            {
                Say("NEO98 HOST input lists\n");
                app.DumpLists([](const char *line) { printf("NEO98 HOST %s\n", line); });
                app.DumpListBoxes([](const char *line) { printf("NEO98 HOST %s\n", line); });
                fflush(stdout);
            }
            else if (step.action == "redraw")
                Say("NEO98 HOST redraw %d\n", User::RedrawWindow(User::GetDesktopWindow(), nullptr, 0, 0x485) ? 1 : 0);
            else if (step.action == "guestopen") X86::Application::RequestOpen(step.value.c_str());
            else if (step.action == "close")
            {
                // close=TITLE: the close button of the window of that title
                // (SC_CLOSE); close=* the active window.
                const unsigned found = step.value == "*" ? User::ForegroundActiveWindow() : User::FindWindow(nullptr, step.value.c_str());
                if (found) User::PostMessage(found, 0x112, 0xf060, 0);
                Say("NEO98 HOST input close %s hwnd=%x\n", step.value.c_str(), found);
            }
            else if (step.action == "button")
            {
                // A named button through BM_CLICK.
                auto find = [&](auto &&self, unsigned parent, unsigned depth) -> unsigned {
                    if (depth >= User::WindowCapacity) return 0;
                    for (unsigned h = User::GetTopWindow(parent); h; h = User::GetWindow(h, 2))
                    {
                        if (!User::IsVisible(h) || !User::IsEnabled(h)) continue;
                        std::string title = Title(h);
                        title.erase(std::remove(title.begin(), title.end(), '&'), title.end());
                        if (User::IsButton(h) && title == step.value) return h;
                        if (const unsigned child = self(self, h, depth + 1)) return child;
                    }
                    return 0;
                };
                const unsigned h = find(find, 0, 0);
                if (h) { User::NoteHostPress(h); User::PostMessage(h, 0xf5, 0, 0); }
                Say("NEO98 HOST input button=%s hwnd=%x\n", step.value.c_str(), h);
            }
            else if (step.action == "choose")
            {
                const unsigned h = User::FindWindow(nullptr, step.value.c_str());
                X86::Application::RequestForeground(h);
                Say("NEO98 HOST seamless choose %s hwnd=%x\n", step.value.c_str(), h);
            }
            else if (step.action == "trace")
            {
                for (unsigned i = 0; i < 16; ++i)
                    if (auto *process = static_cast<X86::Application *>(User::ProcessContextAt(i)))
                    { process->trace_calls = step.x != 0; process->trace_from = 0; }
                Say("NEO98 HOST input trace=%d\n", step.x);
            }
            else if (step.action == "down" || step.action == "up" || (step.action == "move" && held))
            {
                // down= and up= split a click over time, as a hand does; a
                // move= between them keeps the button held.
                if (step.action != "move") held = step.action == "down" ? 1 : 0;
                app.MouseButtons(step.x, step.y, held);
                Say("NEO98 HOST input %s %d,%d\n", step.action.c_str(), step.x, step.y);
            }
            else if (step.action == "click" || step.action == "dblclick" || step.action == "rclick" || step.action == "move")
            {
                const unsigned button = step.action == "rclick" ? 2 : 1;
                const unsigned presses = step.action == "dblclick" ? 2 : step.action == "move" ? 0 : 1;
                app.MouseButtons(step.x, step.y, 0);
                for (unsigned i = 0; i < presses; ++i)
                { app.MouseButtons(step.x, step.y, button); app.MouseButtons(step.x, step.y, 0); }
                Say("NEO98 HOST input %s %d,%d\n", step.action.c_str(), step.x, step.y);
            }
            else fprintf(stderr, "Unknown Linux test input: %s\n", step.action.c_str());
            fprintf(stderr, "NEO98 Linux input %s=%s\n", step.action.c_str(), step.value.c_str());
        }
    }
  private:
    void Next(double real)
    {
        ++at;
        const char *once = getenv("NEO98_HOST_INPUT_ONCE");
        if (once && !strcmp(once, "1"))
        {
            // The steps not yet taken stay in the environment for the execv
            // of a restart, with the seconds still to go.
            std::string remaining;
            const double elapsed = real - paused;
            for (size_t i = at; i < steps.size(); ++i)
            {
                const auto &step = steps[i];
                const bool pixel = step.action == "pixel";
                remaining += std::to_string(std::max(0.0, step.time - elapsed)) + ":" + (pixel ? "wait" : step.action) + "=" +
                             (pixel ? std::to_string(step.x) + "," + std::to_string(step.y) + "," + step.value : step.value) + ";";
            }
            setenv("NEO98_HOST_INPUT", remaining.c_str(), 1);
        }
    }
};
}
