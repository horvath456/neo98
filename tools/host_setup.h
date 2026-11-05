// SPDX-License-Identifier: GPL-2.0-or-later
// The setup assistant and the settings window of the Mac app,
// tools/host_setup.mm. docs/BUILDING.md.
// The assistant appears instead of the guest screen while no machine folder
// is set up: ISO (chosen or dropped), product key, target folder, then the
// status lines of tools/setup_win98.py while it installs, or "Use existing
// folder…". It runs setup with /usr/bin/python3 and hands the key
// over on its standard input.
#pragma once
#include "host_settings.h"
#include <string>
#include <vector>

namespace HostSetup
{
// Runs the assistant. True when settings.machine names a machine folder that
// is ready (the settings are saved); false when the person quit.
// NEO98_HOST_WIZARD="step;step;..." drives it without a person, as
// NEO98_HOST_MENU drives the menu: iso=PATH, key=TEXT, folder=PATH, check
// (Check again), open=PATH (a file the Finder hands over), install, cancel-on=TEXT (Cancel when a status line
// contains TEXT), existing=PATH (Use existing folder), quit. Each
// step prints an `NEO98 WIZARD...` line; the key is never printed.
// NEO98_SETUP_TOOL_DIRS replaces /opt/homebrew/bin:/usr/local/bin for the
// search of QEMU, mtools, cabextract and tesseract.
bool RunAssistant(HostSettings::Settings &settings);
// The files the Finder asked the app to open while the assistant showed (a
// double click on a program before a machine exists). The host starts them
// once the machine runs.
const std::vector<std::string> &AssistantOpens();

// What the settings window shows.
struct Info
{
    std::string settings_file, machine, drive_c, logs, cdrom;
    char cdrom_letter = 0;
    std::string drives[26];
    int scale = 1;
    bool fullscreen = false, online = true;
    std::string net_listen = "127.0.0.1";
};
// NEO98 > Settings …: shows the window (or brings it to the front).
void ShowPreferences(const Info &info);
// The machine folder the person chose in the window since the last call;
// `restart` when they asked to restart the machine right away.
bool TakeMachineChoice(std::string &machine, bool &restart);
// The same choice without a window, for the test switch settings=PATH.
// False when the folder cannot be a machine folder.
bool ChooseMachine(const std::string &folder);
} // namespace HostSetup
