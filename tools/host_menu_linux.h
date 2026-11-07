// SPDX-License-Identifier: GPL-2.0-or-later
// GTK machine menu. Requests retain the shared HostMenu contract and are
// served by the host between guest pumps.
#pragma once
#include "host_menu.h"
#include "host_gtk_linux.h"
#include <SDL.h>
#include <deque>
namespace HostMenu
{
inline std::deque<Request> linux_requests;
inline State linux_state;
inline bool linux_installed = false;
inline bool Install() { return linux_installed = LinuxGtk::Init(); }
inline bool Installed() { return linux_installed; }
inline void Update(const State &state) { linux_state = state; LinuxGtk::Update(state); }
inline void Pump() { LinuxGtk::Pump(); }
inline void Notice(const char *title, const char *message) { LinuxGtk::Notice(title, message); }
inline bool Next(Request &request)
{
    if (linux_requests.empty()) return false;
    request = linux_requests.front(); linux_requests.pop_front(); return true;
}
inline bool Perform(const char *name, const char *path)
{
    if (!name) return false;
    std::string action = name;
    // The identifiers of the Mac menu bar (tools/host_menu.mm), which the
    // scripted menu= steps of the tests use, name the same items here.
    static const std::pair<const char *, const char *> mac[] = {
        {"scale-1", "scale1"}, {"scale-2", "scale2"}, {"cdrom-insert", "cdrom"}, {"cdrom-eject", "eject"},
        {"edit-cut", "cut"}, {"edit-copy", "copy"}, {"edit-paste", "paste"}, {"network-online", "network"},
        {"open-program", "open"}, {"state-list", "states"}, {"settings", "preferences"},
        {"guest-window", "window"}};
    for (const auto &[from, to] : mac)
        if (action == from) { action = to; break; }
    Request request;
    if (action == "pause") request.action = Action::Pause;
    else if (action == "reboot") request.action = Action::Reboot;
    else if (action == "report") request.action = Action::Report;
    else if (action == "preferences") request.action = Action::Preferences;
    else if (action == "open-ask") { request.action = Action::Preferences; request.value = 1; }
    else if (action == "game-create") request.action = Action::GameCreate;
    else if (action == "open-normal") { request.action = Action::Open; request.value = 1; }
    else if (action == "game-list") request.action = Action::GameList;
    else if (action == "game-launch") request.action = Action::GameLaunch;
    else if (action == "state-save") request.action = Action::StateSave;
    else if (action == "state-load") request.action = Action::StateLoad;
    else if (action == "states") request.action = Action::StateList;
    // dos-settings-file: "Configure another DOS file", the file named
    // by the caller (a script) instead of the one running.
    else if (action == "dos-settings" || action == "dos-settings-file") request.action = Action::DosSettings;
    else if (action == "quit") request.action = Action::Quit;
    else if (action == "open") request.action = Action::Open;
    else if (action == "drive-d") request.action = Action::ChooseDriveD;
    else if (action == "cdrom") request.action = Action::InsertCdrom;
    else if (action == "eject") request.action = Action::EjectCdrom;
    else if (action == "fullscreen") request.action = Action::Fullscreen;
    else if (action == "scale1" || action == "scale2") { request.action = Action::Scale; request.value = action == "scale2" ? 2 : 1; }
    else if (action == "network-listen") { request.action = Action::NetworkListen; request.value = linux_state.net_listen.compare(0, 4, "127.") == 0; }
    else if (action == "network") { request.action = Action::Network; request.value = !linux_state.online; }
    else if (action == "network-wifi") { request.action = Action::Wifi; request.value = !linux_state.wifi; }
    else if (action == "microphone") { request.action = Action::Microphone; request.value = !linux_state.microphone; }
    else if (action == "ctrl-alt-delete") request.action = Action::CtrlAltDelete;
    else if (action == "seamless") request.action = Action::Seamless;
    else if (action == "seamless-taskbar") request.action = Action::SeamlessTaskbar;
    else if (action == "seamless-windows") request.action = Action::SeamlessWindows;
    else if (action == "seamless-icons") request.action = Action::SeamlessIcons;
    else if (action == "seamless-dock") request.action = Action::SeamlessDock;
    else if (action == "pause-unfocused") request.action = Action::PauseUnfocused;
    else if (action == "wheel-focus") request.action = Action::WheelFocus;
    else if (action == "mouse-release") request.action = Action::MouseRelease;
    else if (action == "start-menu") request.action = Action::StartMenu;
    else if (action == "minimize") request.action = Action::Minimize;
    else if (action == "cut" || action == "copy" || action == "paste")
    { request.action = Action::Edit; request.value = action == "cut" ? 'X' : action == "copy" ? 'C' : 'V'; }
    else if (action == "window" && path)
    { request.action = Action::GuestWindow; request.value = strtoul(path, nullptr, 0); }
    else if (action.size() == 4 && action.compare(0, 3, "com") == 0 && action[3] >= '1' && action[3] <= '4')
    { request.action = Action::ComPort; request.value = action[3] - '0'; }
    else return false;
    request.path = path ? path : ""; linux_requests.push_back(std::move(request)); return true;
}
inline std::string Describe()
{
    return "File/open\nFile/quit\nEdit/cut\nEdit/copy\nEdit/paste\n"
           "Machine/pause\nMachine/pause-unfocused\nMachine/reboot\nMachine/report\nMachine/ctrl-alt-delete\n"
           "Machine/state-save\nMachine/state-load\nMachine/states\n"
           "File/dos-settings\n"
           "Machine/mouse-release\nMachine/wheel-focus\nMachine/microphone\nView/seamless\nView/seamless-taskbar\n"
           "View/seamless-windows\nView/seamless-icons\nView/seamless-dock\nView/fullscreen\n"
           "View/scale1\nView/scale2\nDrives/drive-d\nDrives/cdrom\nDrives/eject\n"
           "Drives/com1\nDrives/com2\nDrives/com3\nDrives/com4\nNetwork/network\nNetwork/network-listen\nNetwork/network-wifi\n"
           "Window/minimize\nWindow/start-menu\nWindow/window\nHelp/about\n";
}
inline void ShowLinux()
{
    LinuxGtk::Show();
}
}
