// SPDX-License-Identifier: GPL-2.0-or-later
// The command line of the game machines on Linux, as tools/neo98_host.cpp
// has it on macOS, with the same options and the same lines:
// --game-machine-create PROGRAM [--game-machine-name NAME] [--game-machine-arguments TEXT]
// --game-machine-list
// --game-machine-delete NAME
// --game-machine-states NAME the list of its states
// --game-machine-state-save NAME [--state-name TEXT] [--state-kind KIND] its files as they are
// --game-machine-state-load NAME --state ID its files from the state, an automatic state before
// --game-machine-state-delete NAME --state ID
// --game-machine-state-rename NAME --state ID --state-name TEXT
// --machine FOLDER names the machine in place of the one of the settings.
// No machine runs for any of these.
#pragma once
#include "host_game_machine.h"
#include "host_machine_state.h"
#include "host_savestate.h"
#include "host_settings.h"
#include "host_single.h"
#include "host_volume.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace LinuxHost
{
inline std::string RealPath(const std::string &path)
{
    char real[PATH_MAX];
    return !path.empty() && realpath(path.c_str(), real) ? std::string(real) : path;
}
// The machine folder whose game machines these are: the one settings.ini
// of the app names (machine = FOLDER, as on macOS) when setup finished it,
// else drive C: itself, NEO98_WIN98_ROOT or build/win98-c, which holds its
// game machines beside it (HostGameMachine::Directory).
inline std::string MachineFolder()
{
    static const std::string folder = [] {
        HostSettings::Settings settings;
        const std::string kept = HostSettings::FileOverride();
        HostSettings::FileOverride().clear();
        const bool read = HostSettings::Load(settings);
        HostSettings::FileOverride() = kept;
        if (read && HostSettings::Ready(settings.machine)) return RealPath(settings.machine);
        return RealPath(HostVolume::DefaultDriveC());
    }();
    return folder;
}
// The machine of settings.ini also gives drive C: of a start that names
// none, as the app on macOS starts the machine of its settings.
inline void UseSettingsMachine(int argc, char **argv)
{
    if (const char *root = getenv("NEO98_WIN98_ROOT"); root && *root) return;
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--drive-c") || !strcmp(argv[i], "--system-tree") || !strcmp(argv[i], "--ram-drive")) return;
    const std::string machine = MachineFolder();
    if (HostSettings::Ready(machine) && HostSettings::DriveC(machine) != machine)
        setenv("NEO98_WIN98_ROOT", HostSettings::DriveC(machine).c_str(), 1);
}
inline std::string StateRevision()
{
    const char *given = getenv("NEO98_REVISION");
    return given && *given ? given : "linux-x64";
}
// The exit code, or -1 when the command line asks for none of these.
inline int GameMachineCommand(int argc, char **argv)
{
    const char *create = nullptr, *name = "", *arguments = "", *remove = nullptr;
    bool list = false;
    std::string machine = MachineFolder();
    for (int i = 1; i < argc; ++i)
    {
        const bool value = i + 1 < argc;
        if (!strcmp(argv[i], "--game-machine-list")) list = true;
        else if (!strcmp(argv[i], "--game-machine-create") && value) create = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-name") && value) name = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-arguments") && value) arguments = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-delete") && value) remove = argv[++i];
        else if (!strcmp(argv[i], "--machine") && value) machine = argv[++i];
    }
    if (!create && !remove && !list) return -1;
    if (!HostSettings::Ready(machine))
    {
        printf("NEO98 GAME MACHINE: no Windows 98 installation in machine folder %s\n", machine.empty() ? "-" : machine.c_str());
        return 15;
    }
    machine = RealPath(machine);
    if (list)
    {
        for (const auto &entry : HostGameMachine::List(machine))
            printf("NEO98 GAME MACHINE name=%s running=%d program=%s folder=%s file=%s\n", entry.name.c_str(), entry.running ? 1 : 0,
                   entry.program.c_str(), entry.folder.c_str(), entry.file.empty() ? "-" : entry.file.c_str());
        return 0;
    }
    if (remove)
    {
        std::string error;
        const std::string folder = HostGameMachine::Resolve(remove, machine);
        if (folder.empty()) error = std::string("No game machine named \"") + remove + "\".";
        if (!error.empty() || !HostGameMachine::Delete(folder, error))
        {
            printf("NEO98 GAME MACHINE delete failed: %s\n", error.c_str());
            return 15;
        }
        printf("NEO98 GAME MACHINE deleted name=%s folder=%s\n", HostGameMachine::Leaf(folder).c_str(), folder.c_str());
        return 0;
    }
    if (HostGameMachine::MachineRunning(machine))
    {
        printf("NEO98 GAME MACHINE create failed: the machine is running. Use Machine > Game machines.\n");
        return 14;
    }
    HostSettings::Settings source;
    {
        const std::string kept = HostSettings::FileOverride();
        HostSettings::FileOverride().clear();
        HostSettings::Load(source);
        HostSettings::FileOverride() = kept;
    }
    HostGameMachine::Request request;
    request.machine = machine;
    request.name = name;
    request.arguments = arguments;
    request.source = source;
    std::string roots[26];
    roots[2] = RealPath(HostSettings::DriveC(machine));
    for (int i = 3; i < 26; ++i)
        if (!source.drives[i].empty()) roots[i] = RealPath(source.drives[i]);
    const std::string given = create;
    if (given.size() > 3 && given[1] == ':' && given[2] == '\\')
    {
        request.program = given;
        const int letter = (given[0] & ~0x20) - 'A';
        if (letter >= 2 && letter < 26 && !roots[letter].empty())
            request.program_file = HostGameMachine::HostFile(roots[letter], HostVolume::ToHost(given.substr(3)));
    }
    else
    {
        request.program_file = RealPath(given);
        for (int i = 2; i < 26 && request.program.empty(); ++i)
        {
            const std::string inside = roots[i] + "/";
            if (roots[i].empty() || request.program_file.compare(0, inside.size(), inside)) continue;
            std::string rest = request.program_file.substr(inside.size());
            for (auto &c : rest) if (c == '/') c = '\\';
            request.program = std::string(1, char('A' + i)) + ":\\" + HostVolume::ToGuest(rest);
        }
    }
    HostGameMachine::Result result;
    if (request.program.empty() || request.program_file.empty() || !HostSettings::Exists(request.program_file))
        result.error = "The program " + given + " is not on any of the machine's drives.";
    else
        HostGameMachine::Create(request, HostSettings::Executable(), result);
    if (!result.error.empty())
    {
        printf("NEO98 GAME MACHINE create failed: %s\n", result.error.c_str());
        return 15;
    }
    printf("NEO98 GAME MACHINE created name=%s program=%s folder=%s\n", result.name.c_str(), request.program.c_str(),
           result.folder.c_str());
    return 0;
}
inline void PrintState(const HostSaveState::Entry &state)
{
    printf("NEO98 GAME MACHINE STATE id=%s kind=%s game=%s parent=%s depth=%u current=%d usable=%d damaged=%d time=%lld layers=%s machine=%llu name=\"%s\" reason=\"%s\"\n",
           state.id.c_str(), state.kind.empty() ? "-" : state.kind.c_str(), state.game.empty() ? "-" : state.game.c_str(),
           state.parent.empty() ? "-" : state.parent.c_str(), state.depth, state.current ? 1 : 0, state.usable ? 1 : 0,
           state.damaged ? 1 : 0, state.time, state.layers.empty() ? "-" : state.layers.c_str(), state.machine_size,
           state.name.c_str(), state.reason.c_str());
}
// The states of a game machine that does not run; -1 when none was asked for.
inline int GameMachineStateCommand(int argc, char **argv)
{
    const char *list = nullptr, *save = nullptr, *load = nullptr, *remove = nullptr, *rename_of = nullptr;
    const char *id = "", *name = "", *kind = "manual";
    std::string machine = MachineFolder();
    for (int i = 1; i < argc; ++i)
    {
        const bool value = i + 1 < argc;
        if (!strcmp(argv[i], "--game-machine-states") && value) list = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-state-save") && value) save = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-state-load") && value) load = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-state-delete") && value) remove = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-state-rename") && value) rename_of = argv[++i];
        else if (!strcmp(argv[i], "--state") && value) id = argv[++i];
        else if (!strcmp(argv[i], "--state-name") && value) name = argv[++i];
        else if (!strcmp(argv[i], "--state-kind") && value) kind = argv[++i];
        else if (!strcmp(argv[i], "--machine") && value) machine = argv[++i];
    }
    const char *given = list ? list : save ? save : load ? load : remove ? remove : rename_of;
    if (!given) return -1;
    const std::string folder = HostGameMachine::Resolve(given, machine.empty() ? machine : RealPath(machine));
    HostSaveState::Context context;
    context.engines = HostMachineState::Engines();
    if (folder.empty() || !HostSaveState::ContextOf(folder, StateRevision(), context))
    {
        printf("NEO98 GAME MACHINE STATE failed: no game machine named \"%s\".\n", given);
        return 15;
    }
    const auto fail = [](const std::string &error) {
        printf("NEO98 GAME MACHINE STATE failed: %s\n", error.c_str());
        return 15;
    };
    if (list)
    {
        for (const auto &state : HostSaveState::List(context)) PrintState(state);
        return 0;
    }
    std::string error;
    if (rename_of)
    {
        if (!HostSaveState::Rename(folder, id, name, error)) return fail(error);
        printf("NEO98 GAME MACHINE STATE renamed id=%s name=\"%s\"\n", id, HostSaveState::CleanName(name).c_str());
        return 0;
    }
    long owner = 0;
    if (HostSingle::Acquire(HostGameMachine::Layer(folder), owner) == HostSingle::Result::Running)
    {
        printf("NEO98 GAME MACHINE STATE failed: the game machine is running. Use Machine > States.\n");
        return 14;
    }
    HostSaveState::Recover(folder);
    int code = 0;
    if (remove)
    {
        if (!HostSaveState::Delete(folder, id, error)) code = fail(error);
        else printf("NEO98 GAME MACHINE STATE deleted id=%s\n", id);
    }
    else if (save)
    {
        HostSaveState::Request request;
        request.name = name;
        request.kind = kind;
        request.game = "off";
        request.revision = context.revision;
        std::string made;
        if (!HostSaveState::Save(folder, request, made, error)) code = fail(error);
        else printf("NEO98 GAME MACHINE STATE saved id=%s\n", made.c_str());
    }
    else
    {
        HostSaveState::Entry wanted;
        for (const auto &state : HostSaveState::List(context))
            if (state.id == id) wanted = state;
        if (wanted.id.empty()) code = fail(std::string("The state ") + id + " does not exist.");
        else if (!wanted.usable) code = fail(wanted.reason);
        else
        {
            HostSaveState::Request before;
            before.kind = "auto";
            before.game = "off";
            before.revision = context.revision;
            std::string kept;
            if (!HostSaveState::Save(folder, before, kept, error)) code = fail("Could not save the current state before loading: " + error);
            else if (!HostSaveState::Restore(folder, id, error)) code = fail(error);
            else
            {
                HostSaveState::Prune(folder, id);
                printf("NEO98 GAME MACHINE STATE loaded id=%s before=%s\n", id, kept.c_str());
            }
        }
    }
    HostSingle::Release(HostGameMachine::Layer(folder));
    return code;
}
}
