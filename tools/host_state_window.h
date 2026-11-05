// SPDX-License-Identifier: GPL-2.0-or-later
// The window "States" of a game machine and the notes the host shows in the
// picture; tools/host_state_window.mm.
// The window lists the saved states of the game machine, the newest on top,
// with the tree drawn beside them the way a history of branches is drawn: a
// line down from each state to the one it came from, a new lane where the
// person went back and played on another way. On top stands "Now", the
// game as it is, which is where a new state comes from. Each row has its
// picture, its name and its time; automatic states and states that do not
// fit any more say so. The right side shows the chosen state large, with its
// name to edit, why it cannot be loaded when it cannot, and the buttons.
// Like the menu bar (tools/host_menu.h) the window only queues requests;
// tools/neo98_host.cpp takes them in its main loop and hands a new model
// back. The window knows nothing of files or machines.
// Without a screen (SDL's dummy driver, the tests) the window is built the
// same and never shown; Perform drives it and can draw it into a PNG:
// select=NAME a row by the name or the id of its state; "jetzt" for the top one
// load the button "Load" (refused when it is disabled)
// save=NAME the button "Save state" with this name (empty: none)
// rename=NAME the name field of the chosen state
// keep the button "Keep" of an automatic state
// delete the button "Delete …"; the question is answered yes
// without a screen, or by Answers
// close closes the window
// describe one line per row and one for the right side, to stdout
// png=PATH draws the window into PATH; "png-dark=PATH" in the dark appearance
// note=PATH|TITLE|DETAIL
// a note as the picture of the game shows it, into PATH;
// "note-bad" for one about something that did not work
#pragma once
#include <string>
#include <vector>

namespace HostStateWindow
{
struct Row
{
    std::string id, name, parent;
    std::string kind;    // "manual", "quick", "auto"
    std::string game;    // "dos", "windows"; "off": the files alone
    long long time = 0;  // seconds since 1970
    bool usable = false, damaged = false, current = false;
    std::string reason;  // why it cannot be loaded
    std::string revision; // the NEO98 that wrote it, for the person to see
    std::string picture; // Bild.png; empty when the state has none
};
struct Model
{
    std::string game; // the name of the game machine
    std::vector<Row> rows;
    // Whether a state can be saved now, and why not.
    bool can_save = false;
    std::string save_reason;
    // The picture of the game as it is: rows of 0x00RRGGBB.
    std::vector<unsigned> live;
    unsigned live_width = 0, live_height = 0;
    unsigned keep_automatic = 5;
};
struct Request
{
    enum Kind { Save, Load, Delete, Rename, Keep, Close } kind = Close;
    std::string id, name;
};
// Shows the window with these states, or brings it to the front. False
// where there is no AppKit.
bool Open(const Model &model);
// The states changed: lists them again. `select` chooses a row by its id.
void Update(const Model &model, const std::string &select = std::string());
bool IsOpen();
void Close();
bool Next(Request &request);
// Test switches (see above). False for a command that could not be done.
bool Perform(const std::string &command);
// What the questions that follow are answered with, "yes|no|...".
void Answers(const char *answers);

// A note for the picture of the game: a dark plate with a line of text and,
// below it, smaller text that may wrap. `bad` marks something that did not
// work. The plate is at most `max_width` pixels wide at `scale` pixels a
// point. Answers rows of B, G, R, A, the top one first, the colours not
// multiplied by alpha.
bool RenderNote(const std::string &title, const std::string &detail, bool bad, double scale, int max_width,
                std::vector<unsigned char> &pixels, int &width, int &height);
} // namespace HostStateWindow
