// SPDX-License-Identifier: GPL-2.0-or-later
// The macOS menu bar of the host window, tools/host_menu.mm. It replaces the
// strip the host used to draw below the guest screen: the window shows only
// the guest's desktop, and everything that belongs to the machine around it
// (restart, crash report, CD-ROM, drive D:, network cable, scale, full
// screen) lives where a Mac user looks for it.
//
// The menu only queues requests; tools/neo98_host.cpp takes them out of the
// queue in its main loop, the same place that reads the keyboard and SIGUSR2.
//
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace HostMenu
{
enum class Action
{
    Reboot,       // Machine > Restart
    Report,       // Machine > Write crash report
    InsertCdrom,  // Machine > Mount ISO or folder as CD-ROM...; path set
    EjectCdrom,   // Machine > Eject CD-ROM
    ChooseDriveD, // Machine > Choose drive D:...; path set
    ComPort,      // Machine > Serial ports > COMn...; value 1 to 4, path the endpoint (empty: none)
    Scale,        // View > Scale 1x/2x; value 1 or 2
    Fullscreen,   // View > Fullscreen
    Seamless, // View > Seamless
    SeamlessTaskbar, // View > Taskbar in seamless mode
    SeamlessWindows, // View > One Mac window per guest window
    SeamlessIcons,   // View > Show desktop icons
    WheelFocus,      // Machine > Mouse wheel scrolls the focused window
    SeamlessDock,    // View > One Dock icon per program
    StartMenu,       // Window > Start menu: opens the Start menu of the guest's shell
    GuestWindow,     // Window > a window of the guest; value is its HWND
    NetworkListen, // Network > Allow LAN connections
    Network,      // Network > Internet connected; value 1 plugs the cable in, 0 pulls it
    Wifi,         // Network > Wi-Fi in Windows; value 1 shows the Mac's wireless adapter in Windows
    Quit,         // Neo98 > Quit Neo98
    Open, // File > Open program...; path set (docs/BUILDING.md)
    DosSettings,  // File > DOS program settings; path set
    Preferences,  // NEO98 > Settings...
    MouseRelease, // Machine > Release mouse (Ctrl+Option)
    CtrlAltDelete, // Machine > Send Ctrl+Alt+Delete
    Minimize,     // Window > Minimize (Cmd+M)
    Pause, // Machine > Pause (Cmd+Shift+P): stops the whole machine and lets it go on
    PauseUnfocused, // Machine > Pause when Neo98 loses focus
    Edit,         // Edit > Cut, Copy, Paste; value is the key the guest gets with Ctrl ('X', 'C', 'V')
    // A game machine :
    StateSave,    // Machine > Save state (Cmd+Shift+S); path: a name, empty for a quick save
    StateLoad,    // Machine > Load latest state (Cmd+Shift+L); path: the id of a state, empty for the last
    StateList,    // Machine > States … (Cmd+Shift+Z): the window with all states
    GameCreate, GameList, GameLaunch,
    Microphone, // Machine > Microphone in Windows; value 1 gives Windows the host's microphone
};
struct Request
{
    Action action;
    int value = 0;
    std::string path;
};
// What the menu shows as checked, enabled or as information.
struct State
{
    int scale = 1;
    bool fullscreen = false;
    bool seamless = false, seamless_taskbar = false, seamless_windows = true, seamless_icons = false, seamless_dock = true;
    bool wheel_focus = false;
    // The windows of the guest that have no Mac window of their own in the
    // seamless view (minimized ones, or all of them in one layer): HWND and
    // title, listed in the menu "Window".
    std::vector<std::pair<unsigned, std::string>> guest_windows;
    std::string dos_program; // settings target of the active DOS window
    std::string cdrom;   // host path behind the CD-ROM drive, empty without one
    char cdrom_letter = 0;
    std::string drive_d; // host path behind D:, empty without one
    std::string com[4]; // the endpoint of COM1 to COM4, empty without one
    bool hang = false;
    // The guest holds the mouse (1), or the user let it go and a press in the
    // window takes it back (2); 0 otherwise. The menu bar names the keys.
    int mouse = 0;
    bool paused = false; // the machine stands
    bool pause_unfocused = false; // it pauses by itself while another application is in front
    // A game machine: the items of its states exist. `states_blocked` says
    // why none can be saved right now (the items are grey and show it);
    // `states_last` that there is one "Load latest state" would load.
    bool states = false, states_last = false;
    std::string states_blocked;
    std::string net_listen = "127.0.0.1";
    bool online = true;      // the network cable of the guest's card
    bool wifi = false;       // Windows sees the Mac's wireless adapter (tools/host_devices)
    bool microphone = true;  // Windows sees the host's microphone as its recording device (src/sound.h)
    std::string address;     // the card's address as the guest sees it, empty without one
};

// Builds the menu bar and makes it the main menu of the application. Under
// SDL's Cocoa driver NSApp exists by then; under another driver (the dummy
// driver of the tests) Install creates NSApp itself, without a Dock icon, and
// Pump drains its events. Returns false where there is no AppKit.
bool Install();
bool Installed();
void Update(const State &state);
// Hands queued Cocoa events to AppKit when SDL's own event loop does not.
void Pump();
// In full screen the menu bar and the Dock stay away until the pointer
// reaches the edge of the screen, as in any Mac app in full screen. SDL hides
// both for good whenever its window takes the keyboard, so the host says it
// again every turn of its loop.
void FullScreenBars(bool fullscreen);
// The next queued request, oldest first.
bool Next(Request &request);
// Queues a request the host makes itself (a disc image opened in the Finder).
void Post(const Request &request);
void Notice(const char *title, const char *message);
// 0: close, 1: settings, 2: retry.
int DosFailure(const char *path, const char *message);

// One host per drive C: (tools/host_single.h). Activate brings this
// application to the front; false where NSApp is not SDL's (the dummy driver
// of the tests), which leaves the screen alone. ActivateProcess does it from
// the second start for the host that already runs. LaunchDocuments answers the
// files the Finder opened this process with (`open -n program.exe`): they come
// as an Apple event, which only a launched NSApplication receives; it waits
// for them for a moment. `as_game` takes the files of the Finder's entry
// "Open as game machine" (tools/host_game_menu.h), which macOS sends the
// same way to an app it had to start for it.
bool Activate();
void ActivateProcess(long process);
std::vector<std::string> LaunchDocuments(std::vector<std::string> *as_game = nullptr);

// What macOS started this process for, read before the machine boots. The first Apple
// event of a launch says it: a start for its own sake (Dock, a double click on
// the app), one with files (they come in the same event), or one to perform
// a service, whose request follows. LaunchWatch is called before the
// application object finishes launching (before SDL_Init), LaunchWait after:
// it serves events until that first event came, at most `launch_limit`
// seconds, and for a start to perform a service until `arrived` says the
// request is there, at most `service_limit` seconds more. A start for its
// own sake goes on as soon as its event came. Where NSApp is not SDL's (the
// dummy driver of the tests) LaunchWait creates it as Install does.
struct Launch
{
    bool finished = false; // the first event came
    bool plain = true;     // a start for its own sake
    bool service = false;  // started to perform a service
    double seconds = 0;    // how long LaunchWait waited
    std::string event;     // the event as macOS sent it, for the log
};
void LaunchWatch();
Launch LaunchWait(double launch_limit, double service_limit, bool (*arrived)());

// Test switches. Perform triggers the item named `name` exactly as a click
// does (NSMenu performActionForItemAtIndex:); `path` stands in for the answer
// of the open panel. PostShortcut puts a key-down such as "cmd+shift+b" into
// the application's event queue, where AppKit matches it against the key
// equivalents of the menu like a real key press. Describe lists the menu,
// one line per item.
bool Perform(const char *name, const char *path);
bool PostShortcut(const char *keys);
std::string Describe();

// hdiutil: attaches a disk image read-only and hidden from the Finder and
// answers its mount point. Reuses an existing macOS mount without owning it;
// owned is true only for a mount created here, which the host may detach.
bool Attach(const char *image, std::string &mount_point, bool &owned, std::string &error);
bool Detach(const char *mount_point, std::string &error);
} // namespace HostMenu
