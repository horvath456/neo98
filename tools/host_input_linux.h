// SPDX-License-Identifier: GPL-2.0-or-later
// Timed input through the same application input methods as the SDL host.
#pragma once
#include "linux_session.h"
#include "../src/usb_keys.h"
#include "host_menu_linux.h"
#include <cstdlib>
#include <cstring>
namespace LinuxHost
{
inline std::function<bool(const std::string &,const std::string &)> native_input;
// hostshot=PATH: the window's next frame as the host draws it, video included.
inline std::function<void(const std::string &)> host_shot;
class Script
{
    struct Step { double time; std::string action, value; };
    std::vector<Step> steps;
    size_t at = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
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
            char *tail = nullptr; const double time = strtod(entry.substr(0, colon).c_str(), &tail);
            steps.push_back({time, entry.substr(colon + 1, equals - colon - 1), entry.substr(equals + 1)});
        }
    }
    void Pump(Session &session, SDL_Window *window = nullptr)
    {
        if (at == steps.size()) return;
        auto now = std::chrono::steady_clock::now();
        const auto &step = steps[at];
        if (std::chrono::duration<double>(now - start).count() < step.time) return;
        if (step.action == "wait" || step.action == "waitfile")
        {
            if (step.action == "wait") { if (!Window(step.value)) return; }
            else { struct stat info; if (stat(step.value.c_str(), &info)) return; }
            // Subsequent times count from when the window actually appeared.
            start = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(step.time));
        }
        else if (step.action.compare(0,7,"native-") == 0 && native_input) {
            if (!native_input(step.action,step.value)) return;
        }
        else if (step.action == "shot") session.Screenshot(step.value);
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
        else if (step.action == "type")
        {
            for (unsigned char character : step.value)
            {
                session.app->Key(0, character, false, false); session.app->KeyUp(0);
            }
        }
        else if (step.action == "pbdump")
        {
            std::string text = Clipboard::current ? Clipboard::current->ReadText() : "";
            FILE *file = fopen(step.value.c_str(), "wb");
            if (file) { fwrite(text.data(), 1, text.size(), file); fclose(file); }
        }
        else if (step.action == "menu")
        {
            auto split = step.value.find(',');
            HostMenu::Perform(step.value.substr(0, split).c_str(), split == std::string::npos ? nullptr : step.value.substr(split + 1).c_str());
        }
        else if (step.action == "wheel" && window)
        {
            double up = 0, right = 0;
            if (sscanf(step.value.c_str(), "%lf,%lf", &up, &right) >= 1)
            {
                SDL_Event event = {}; event.type = SDL_MOUSEWHEEL;
                event.wheel.windowID = SDL_GetWindowID(window);
                event.wheel.preciseY = up; event.wheel.preciseX = right;
                SDL_PushEvent(&event);
            }
        }
        else if (step.action == "key" || step.action == "ctrl")
        {
            const unsigned key = strtoul(step.value.c_str(), nullptr, 16);
            unsigned scan = 0; bool extended = false;
            const bool german = !(getenv("NEO98_KEYMAP") && !strcmp(getenv("NEO98_KEYMAP"),"US"));
            for (unsigned usage = 1; usage < 128; ++usage)
            {
                const auto mapped = UsbKeys::Map(usage,german,true);
                if (mapped.vk == key) { scan = mapped.scan; extended = mapped.extended; break; }
            }
            session.app->KeyboardModifiers(step.action == "ctrl" ? 1 : 0);
            session.app->RawKey(key,key == 0x0d ? '\r' : 0,true,scan,extended);
            session.app->RawKey(key,0,false,scan,extended); session.app->KeyboardModifiers(0);
        }
        else if (step.action == "click" || step.action == "dblclick" || step.action == "move")
        {
            int x = 0, y = 0;
            if (sscanf(step.value.c_str(), "%d,%d", &x, &y) == 2)
            {
                session.app->MouseButtons(x, y, 0);
                const unsigned clicks = step.action == "dblclick" ? 2 : step.action == "click" ? 1 : 0;
                for (unsigned i = 0; i < clicks; ++i)
                { session.app->MouseButtons(x, y, 1); session.app->MouseButtons(x, y, 0); }
            }
        }
        else fprintf(stderr, "Unknown Linux test input: %s\n", step.action.c_str());
        fprintf(stderr, "NEO98 Linux input %s=%s\n", step.action.c_str(), step.value.c_str());
        ++at;
        const char *once = getenv("NEO98_HOST_INPUT_ONCE");
        if (once && !strcmp(once, "1"))
        {
            std::string remaining;
            const double elapsed = std::chrono::duration<double>(now - start).count();
            for (size_t i = at; i < steps.size(); ++i)
                remaining += std::to_string(std::max(0.0, steps[i].time - elapsed)) + ":" + steps[i].action + "=" + steps[i].value + ";";
            setenv("NEO98_HOST_INPUT", remaining.c_str(), 1);
        }
    }
};
}
