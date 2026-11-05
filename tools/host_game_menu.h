// SPDX-License-Identifier: GPL-2.0-or-later
// The game machines in the menu bar of the normal machine: Machine >
// Game machines, tools/host_game_menu.mm. The files
// and folders behind it are tools/host_game_machine.h.
// Like tools/host_menu.h the menu only queues requests; tools/neo98_host.cpp
// takes them in its main loop. The items have identifiers, so the test
// switches of the menu bar (NEO98_HOST_MENU, the menu= step) reach them:
// game-machine-create=PROGRAM[|NAME] a game machine for a program, by guest
// path or by its file on this Mac
// game-machine-create-front[=NAME] for the program in front
// game-machine-start=NAME, game-machine-reveal=NAME, game-machine-delete=NAME
// game-machine-desktop=NAME|FOLDER an alias of the launcher in FOLDER (a test
// never writes to the desktop of the person)
// game-machine-ask the question for programs opened in the Finder, on and off
// Two more switches have no item of the menu:
// game-machine-open=FILE the entry of the Finder "Open as game machine"
// (the service of Info.plist, tools/mac_app.py)
// game-machine-answers=A|B|... what the next dialogs are answered with, one
// each: the question of the Finder takes normal,
// game or cancel (with +never for its checkbox),
// the name a text (empty: the suggestion) or
// cancel, the choice among several game machines
// a name or cancel
#pragma once
#include <string>
#include <vector>

namespace HostGameMenu
{
struct Request
{
    enum Kind { Create, CreateFront, Start, Reveal, Delete, Desktop, Open, Ask } kind = Create;
    std::string program; // Create: the file the person chose, or a guest path. Open: a file of this Mac.
                         // Desktop: the folder for the alias, empty for the desktop
    std::string name;    // the game machine; for Create the wanted name, empty to ask
};
// Adds the submenu "Game machines" to the menu "Machine" of
// HostMenu::Install. `machine` is the machine folder whose game machines it
// lists, `drive_c` the folder the open panel starts in.
bool Install(const std::string &machine, const std::string &drive_c);
// Lists the game machines again (after one was made or deleted; the menu also
// does it whenever it opens). `front` is the name of the program in front,
// empty without one.
void Refresh(const std::string &front);
bool Next(Request &request);
// Queues a request the host makes itself (game-machine-open of a test).
void Post(const Request &request);
// The service of the Finder "Open as game machine" answers from here on
// (Install does it too). A start calls it before the machine boots, so a
// request macOS started the app for is there in time; one that comes later
// waits in the queue for the main loop. Only in the app bundle and with a
// screen; false otherwise.
bool InstallService();
// Whether a request waits.
bool Waiting();
// A message for the person where no menu bar is installed yet (a start that
// opens a game machine and boots nothing). Without a screen only the line in
// the log.
void Alert(const std::string &title, const std::string &message);
// What a test gives in place of the panels; nullptr for none.
void Answer(const char *answer);
// What a test answers the next dialogs with, "A|B|..." (see above).
void Answers(const char *answers);
// The check mark of "Ask when opening from Finder".
void SetAsk(bool on);
// A program was opened from the Finder and nothing is known about it yet:
// normally, or as a game machine? `never` is the checkbox
// "Do not show this dialog again". Without a screen and without an answer of a test the
// question is not asked.
enum class OpenAs { NotAsked, Normal, Game, Cancel };
OpenAs AskOpen(const std::string &program, bool &never);
// Several game machines run this program: which one? False when the person
// cancels, and without a screen and without an answer of a test.
bool Choose(const std::string &program, const std::vector<std::string> &names, std::string &chosen);
// An alias of `original` in `directory` (empty: the desktop of the person),
// as the Finder makes one: it follows the original when that moves. `made` is
// the alias file.
bool MakeAlias(const std::string &original, const std::string &directory, std::string &made, std::string &error);
// What an alias file leads to; empty when it leads nowhere.
std::string AliasTarget(const std::string &alias);
// The name macOS knows this process by (Dock, Cmd+Tab) and its bundle.
std::string ApplicationName(std::string *bundle = nullptr);
// Asks for the name of a new game machine. False when the person cancels.
// Without a screen the suggestion is taken.
bool AskName(const std::string &program, const std::string &suggested, std::string &name);
// Asks before a game machine is deleted; `states` is how many saved states
// go with it, and the question says so (DeleteQuestion is its text).
// Without a screen the answer is yes.
bool ConfirmDelete(const std::string &name, unsigned states = 0);
std::string DeleteQuestion(unsigned states);
// Shows the folder in the Finder (nothing without a screen).
void Reveal(const std::string &path);
// The picture of an.icns as the icon of this application in the Dock: a
// game machine shows the icon of its game.
void SetApplicationIcon(const std::string &icns);
} // namespace HostGameMenu
