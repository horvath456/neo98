// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host_game_machine.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <map>
#include <set>

namespace LinuxDock
{
inline std::string machine, game;
inline std::map<std::string, std::string> written;
inline std::string Quote(const std::string &text)
{
    std::string argument, result = "\"";
    for (char c : text)
    {
        if (c == '\\' || c == '"' || c == '`' || c == '$') argument += '\\';
        argument += c;
    }
    for (char c : argument)
    {
        if (c == '\\') result += '\\';
        if (c == '%') result += '%';
        result += c;
    }
    return result + "\"";
}
inline std::string Identity(const std::string &program)
{
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char c : machine + "\n" + game + "\n" + program) { hash ^= unsigned(tolower(c)); hash *= 1099511628211ull; }
    char id[48]; snprintf(id, sizeof id, "neo98-program-%016llx", hash);
    return id;
}
inline std::string Ensure(const std::string &program, const char *name, int width, int height, const unsigned *argb)
{
    const auto id = Identity(program);
    // Guest paths use the filesystem provider's single-byte encoding;
    // desktop files and their command arguments must be UTF-8.
    const auto desktop_program = HostVolume::ToHost(program);
    const std::string data = getenv("XDG_DATA_HOME") ? getenv("XDG_DATA_HOME") : std::string(getenv("HOME")) + "/.local/share";
    const auto directory = data + "/applications", icon = data + "/icons/neo98/" + id + ".png";
    const auto arguments = game.empty() ? " --drive-c " + Quote(machine) : " --game-machine " + Quote(game);
    // GLib validates argv[0] before expanding %% in field codes. Keep that
    // executable free of field codes even when the bundle path contains %.
    const auto command = "/usr/bin/env -- " + Quote(HostSettings::Executable()) + arguments;
    std::string title = HostVolume::ToHost(HostGameMachine::Leaf(name));
    if (const auto dot = title.rfind('.'); dot != std::string::npos && dot) title.resize(dot);
    if (std::none_of(title.begin(), title.end(), [](unsigned char c) { return c >= 'a' && c <= 'z'; }))
        for (size_t i = 1; i < title.size(); ++i) if (title[i] >= 'A' && title[i] <= 'Z') title[i] += 'a' - 'A';
    for (char &c : title) if (c == '\n' || c == '\r') c = ' ';
    const bool has_icon = width > 0 && height > 0 && width <= 128 && height <= 128;
    std::string text = "[Desktop Entry]\nType=Application\nName=" + title + "\nNoDisplay=true\nTerminal=false\nStartupWMClass=" + id +
        "\nIcon=" + (has_icon ? icon : "applications-other") + "\nExec=" + command + " --activate-program " + Quote(desktop_program) +
        "\nActions=Activate;Quit;\n\n[Desktop Action Activate]\nName=Activate\nExec=" + command + " --activate-program " + Quote(desktop_program) +
        "\n\n[Desktop Action Quit]\nName=Quit\nExec=" + command + " --quit-program " + Quote(desktop_program) + "\n";
    // Repeated window updates must not rewrite files or wake the desktop indexer.
    std::string signature = text;
    if (width > 0 && height > 0 && width <= 128 && height <= 128)
        signature.append(reinterpret_cast<const char *>(argb), size_t(width) * height * 4);
    if (written[id] == signature) return id;
    HostSettings::MakeDirectories(directory);
    HostSettings::MakeDirectories(data + "/icons/neo98");
    if (width > 0 && height > 0 && width <= 128 && height <= 128)
    {
        GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
        auto *bytes = gdk_pixbuf_get_pixels(image); const int stride = gdk_pixbuf_get_rowstride(image);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        {
            const unsigned pixel = argb[y * width + x]; auto *p = bytes + y * stride + x * 4;
            p[0] = pixel >> 16; p[1] = pixel >> 8; p[2] = pixel; p[3] = pixel >> 24;
        }
        const auto temporary = icon + "." + std::to_string(getpid());
        const bool saved = gdk_pixbuf_save(image, temporary.c_str(), "png", nullptr, nullptr);
        g_object_unref(image);
        if (!saved || rename(temporary.c_str(), icon.c_str())) { unlink(temporary.c_str()); return id; }
    }
    const auto entry = directory + "/" + id + ".desktop", temporary = entry + "." + std::to_string(getpid());
    if (HostGameMachine::WriteText(temporary, text, 0644) && !rename(temporary.c_str(), entry.c_str()))
        written[id] = std::move(signature);
    else unlink(temporary.c_str());
    return id;
}
}
