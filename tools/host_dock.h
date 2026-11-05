// SPDX-License-Identifier: GPL-2.0-or-later
// A Dock icon for every program of the guest, tools/host_dock.mm and
// tools/host_dock_helper.mm.
// The host stays the one process with the guest and with every window. For
// each program of the guest that has a window with a button on the Win98
// taskbar it keeps a small application of its own: a bundle
// <directory>/<Name>.app with the program's name and icon, whose process
// carries nothing but the Dock icon. The two talk over a socket pair, one
// line per message:
// helper -> host "activate" the user chose the program (Dock, Cmd+Tab)
// "reopen" a click on its Dock icon while it was chosen
// "quit" Quit in its Dock menu, or Cmd+Q
// host -> helper "bye" the program ended
//
#pragma once
#include <string>
#include <vector>

namespace HostDock
{
struct Program
{
    unsigned process = 0;        // the guest's process identifier
    std::string name;            // "CALC.EXE"
    int width = 0, height = 0;   // the icon, zero without one
    std::vector<unsigned> argb;
};
struct Event
{
    enum Kind { Activate, Reopen, Quit } kind = Activate;
    unsigned process = 0;
};
// How the helpers start: Off creates nothing; Bundles writes the bundles and
// starts no process (a run without a screen); Silent starts them without a
// Dock icon (the tests of the channel); Dock is the real thing.
enum class Mode { Off, Bundles, Silent, Dock };
// The programs that run now. New ones get their bundle and helper, the
// helpers of those that are gone are told to end. `directory` holds the
// bundles, `helper` is the helper's executable, `fallback_icon` an.icns for
// a program whose window has no icon (may be empty). Returns the lines for
// the log, one per change.
std::vector<std::string> Sync(const std::vector<Program> &programs, const std::string &directory,
                              const std::string &helper, const std::string &fallback_icon, Mode mode);
// What the helpers reported since the last call.
bool Next(Event &event);
// Ends every helper (the host quits or leaves the seamless view).
void Shutdown();
// The name of a program as the Dock shows it: "CALC.EXE" -> "Calc".
std::string DisplayName(const std::string &program);
// Writes `argb` (rows of width, alpha 255 where it covers) as an.icns with
// the picture enlarged by whole factors, so the pixels of a 32x32 icon stay
// sharp. False when nothing could be written.
bool WriteIcon(const std::string &path, int width, int height, const std::vector<unsigned> &argb);
} // namespace HostDock
