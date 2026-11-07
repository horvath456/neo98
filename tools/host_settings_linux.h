// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host_game_machine.h"
#include "host_game_machine_command_linux.h"
#include "host_menu.h"
#include "host_settings.h"
#include "linux_session.h"

namespace LinuxHost
{
class Preferences
{
    bool enabled = false;
    HostSettings::Settings saved;
    std::vector<std::string> arguments;
    std::vector<char *> pointers;
    std::string game;

  public:
    bool Enabled() const
    {
        return enabled;
    }
    const std::string &Game() const
    {
        return game;
    }
    const std::string &SourceMachine() const
    {
        return saved.game_source;
    }
    const std::string &StartDirectory() const
    {
        return saved.game_directory;
    }
    const std::string &GameName() const
    {
        return saved.game_name;
    }
    bool AskOpen(const std::string &path) const
    {
        return game.empty() && saved.open_ask &&
            std::none_of(saved.open_normal.begin(), saved.open_normal.end(),
                [&](const auto &known) { return HostGameMachine::SameFile(known, path); });
    }
    bool AskEnabled() const { return saved.open_ask; }
    void ToggleAsk(const HostMenu::State &state) { saved.open_ask = !saved.open_ask; Save(state); }
    void RememberOpen(const std::string &path, bool never, const HostMenu::State &state)
    {
        if (!path.empty()) saved.open_normal.push_back(path);
        if (never) saved.open_ask = false;
        Save(state);
    }
    bool Load(int &argc, char **&argv, Options &options)
    {
        const auto drives = HostVolume::Scan(argc, argv);
        for (int i = 1; i + 1 < argc; ++i)
            if (!strcmp(argv[i], "--game-machine"))
            {
                game = HostGameMachine::Resolve(argv[i + 1], drives.drive_c ? std::string(drives.drive_c) : MachineFolder());
                if (game.empty())
                {
                    fprintf(stderr, "Game machine not found: %s\n", argv[i + 1]);
                    return false;
                }
            }
        const char *override = getenv("NEO98_SETTINGS_FILE");
        enabled = !game.empty() || !drives.ram || (override && *override);
        if (!enabled)
            return true;
        HostSettings::FileOverride() =
            !game.empty() ? HostGameMachine::SettingsFile(game)
            : override && *override
                ? override
                : drives.drive_c ? std::string(drives.drive_c) + ".linux-host.ini" : std::string();
        if (!HostSettings::Load(saved))
            return true;
        if (!game.empty() && !HostSettings::Ready(saved.game_source))
        {
            fprintf(stderr, "The Windows 98 installation for this game machine is missing.\n");
            return false;
        }
        options.scale = saved.scale;
        options.fullscreen = saved.fullscreen;
        options.offline = !saved.online;
        options.wifi = saved.wifi;
        options.microphone = saved.microphone;
        options.net_listen = saved.net_listen;
        options.seamless = saved.seamless;
        options.seamless_taskbar = saved.seamless_taskbar;
        options.seamless_windows = saved.seamless_windows;
        options.seamless_icons = saved.seamless_icons;
        options.seamless_dock = saved.seamless_dock;
        options.pause_unfocused = saved.pause_unfocused;
        options.wheel_focus = saved.wheel_focus;
        for (unsigned port = 0; port < 4; ++port)
        {
            const auto name = "NEO98_COM" + std::to_string(port + 1);
            if (!getenv(name.c_str()) && !saved.com[port].empty())
                setenv(name.c_str(), saved.com[port].c_str(), 1);
        }
        for (int i = 0; i < argc; ++i)
            arguments.emplace_back(argv[i]);
        // A restart names the game machine by its folder: the name was
        // looked up in the machine folder, and the restart's --drive-c is
        // the write layer of the game machine.
        for (size_t i = 1; i + 1 < arguments.size(); ++i)
            if (arguments[i] == "--game-machine") arguments[i + 1] = game;
        bool letters[26] = {};
        for (unsigned i = 0; i < drives.count; ++i)
            letters[drives.extra[i].letter - 'A'] = true;
        if (!game.empty())
        {
            setenv("NEO98_LOG_DIR", (game + "/logs").c_str(), 1);
            arguments.insert(arguments.end(),
                             {"--system-tree", HostSettings::DriveC(saved.game_source), "--drive-c",
                              HostGameMachine::Layer(game), "--exe", saved.game_program, "--printer-output",
                              HostSettings::MachinePrintDirectory(game), "--crash-report-dir", game + "/crash-reports"});
            if (!saved.game_arguments.empty())
                arguments.insert(arguments.end(),
                                 {"--command", "\"" + saved.game_program + "\" " + saved.game_arguments});
            for (char letter = 'D'; letter <= 'Z'; ++letter)
                if (!saved.drives[letter - 'A'].empty() && !letters[letter - 'A'])
                {
                    arguments.insert(
                        arguments.end(),
                        {"--drive", std::string(1, letter) + "=" + HostGameMachine::DriveLayer(game, letter),
                         "--drive-below", std::string(1, letter) + "=" + saved.drives[letter - 'A']});
                    letters[letter - 'A'] = true;
                }
            HostGameMachine::WriteLauncher(game, HostSettings::Executable());
        }
        for (char letter = 'D'; letter <= 'Z'; ++letter)
            if (!letters[letter - 'A'] && !saved.drives[letter - 'A'].empty())
            {
                arguments.emplace_back("--drive");
                arguments.emplace_back(std::string(1, letter) + "=" + saved.drives[letter - 'A']);
            }
        // A command-line CD always takes precedence over the saved medium.
        bool cd = false;
        for (int i = 1; i < argc; ++i)
            if (!strcmp(argv[i], "--cdrom"))
                cd = true;
        if (!cd && !saved.cdrom.empty() && !letters[4] && saved.drives[4].empty())
        {
            arguments.emplace_back("--cdrom");
            arguments.emplace_back("E=" + saved.cdrom);
        }
        for (auto &argument : arguments)
            pointers.push_back(argument.data());
        argc = int(pointers.size());
        pointers.push_back(nullptr);
        argv = pointers.data();
        return true;
    }
    void Save(const HostMenu::State &state)
    {
        if (!enabled)
            return;
        saved.scale = state.scale;
        saved.fullscreen = state.fullscreen;
        saved.online = state.online;
        saved.wifi = state.wifi;
        saved.microphone = state.microphone;
        saved.net_listen = state.net_listen;
        saved.seamless = state.seamless;
        saved.seamless_taskbar = state.seamless_taskbar;
        saved.seamless_windows = state.seamless_windows;
        saved.seamless_icons = state.seamless_icons;
        saved.seamless_dock = state.seamless_dock;
        saved.pause_unfocused = state.pause_unfocused;
        saved.wheel_focus = state.wheel_focus;
        saved.cdrom = state.cdrom;
        if (game.empty())
            saved.drives[3] = state.drive_d;
        for (unsigned port = 0; port < 4; ++port)
            saved.com[port] = state.com[port];
        if (!HostSettings::Save(saved))
            fprintf(stderr, "Could not save settings: %s\n", HostSettings::File().c_str());
    }
};
} // namespace LinuxHost
