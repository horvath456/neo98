// SPDX-License-Identifier: GPL-2.0-or-later
// The state of the machine in a saved state of a game machine: who supplies
// it.
// tools/host_savestate.h keeps the states on disk and copies the layers; it
// knows nothing of what runs in the machine. What runs there is a DOS program
// (the DOS machine, tools/host_dos_vm.cpp) or a Windows program (the runtime
// of src/, tools/host_windows_state.h). Each kind has a supplier that turns the machine
// into bytes (Maschine.bin) and bytes back into a machine. The host
// (tools/neo98_host.cpp) picks the supplier by what the first process of the
// game machine is and drives it from its main loop.
// The contract:
// Engine What the bytes of a state are laid out by: a short text that
// changes exactly when a state written before can no longer be
// read (a format number of the supplier's own, the identity of
// the code whose structures it writes). It stands in zustand.ini
// as `engine`, and a state of another engine is refused. The
// revision of NEO98 is not part of it: a state must survive a
// new build that did not touch its supplier.
// Ready Whether a state can be taken right now. When not, the reason
// as a sentence for the person (English, no technical name of a
// component); the menu items are grey and show it. `machine`
// gets what a state needs of the machine that loads it, as pairs
// of a name and a value (memory size, kind of card): the host
// writes them into zustand.ini and refuses a state whose values
// differ from what the running machine says.
// Save Asks for the state. Returns at once.
// Load Hands over a state to put in. It is called in a host that has
// just started for this: the layers and the registry journal are
// those of the state already, the drives are mounted, and the
// program of the game machine has not started yet. The supplier
// may let the program start and replace it (the DOS machine does)
// or build the machine from the bytes alone. Until it is done it
// must not let the starting program change the disks, show a
// picture or make a sound.
// Poll Called every turn of the host's main loop after Save or Load.
// 0: not yet. 1: done, and the supplier's part of the machine
// stands still at the point of the state; after a Save `bytes`
// holds the state. -1: refused, `error` says why, for the person.
// The host stops the rest of the machine (machine_pause), writes
// everything out and copies the disks while the supplier stands.
// Release The supplier's part goes on (it then obeys the pause of the
// machine like everything else).
// Picture The last picture of the game, rows of 0x00RRGGBB, for Bild.png.
// May be null or answer false: the host then takes the picture
// of the guest's screen.
// A state is taken at a boundary where the supplier's machine is whole: the
// DOS machine between two frames. All calls come from the host's main thread.
#pragma once
#include "host_dos_vm.h"
#include "host_windows_state.h"
#include "host_dos_ui_state.h"
#include <string>
#include <utility>
#include <vector>

namespace HostMachineState
{
struct Supplier
{
    // The `game` of zustand.ini: "dos", "windows".
    const char *kind;
    std::string (*Engine)();
    bool (*Ready)(std::string &reason, std::vector<std::pair<std::string, std::string>> *machine);
    void (*Save)();
    void (*Load)(std::vector<unsigned char> bytes);
    int (*Poll)(std::vector<unsigned char> *bytes, std::string &error);
    void (*Release)();
    bool (*Picture)(std::vector<unsigned> &pixels, unsigned &width, unsigned &height);
};

// A DOS program: the DOS machine.
inline bool DosReady(std::string &reason, std::vector<std::pair<std::string, std::string>> *machine)
{
    if (X86::Application::DosSettingsStateOpen()) {
        reason = "Close DOS settings before saving. This dialog cannot yet be saved with the game.";
        return false;
    }
    return HostDosVm::StateReady(reason, machine);
}
inline const Supplier *Dos()
{
    static const Supplier supplier = {"dos",
                                      HostDosVm::StateEngine,
                                      DosReady,
                                      HostDosVm::StateSave,
                                      HostDosVm::StateLoad,
                                      HostDosVm::StatePoll,
                                      HostDosVm::StateRelease,
                                      HostDosVm::StatePicture};
    return &supplier;
}

// A Windows program: the runtime of src/, written field by field
// (tools/host_windows_state.h). It builds the
// machine from the bytes alone, before the program of the game machine
// would start (HostWindowsState::LoadNow in tools/neo98_host.cpp).
inline const Supplier *Windows()
{
    static const Supplier supplier = {"windows",
                                      HostWindowsState::Engine,
                                      HostWindowsState::Ready,
                                      HostWindowsState::Save,
                                      HostWindowsState::Load,
                                      HostWindowsState::Poll,
                                      HostWindowsState::Release,
                                      nullptr};
    return &supplier;
}
inline const Supplier *DosUi()
{
    static const Supplier supplier = {"dos-ui", HostDosUiState::Engine, HostDosUiState::Ready,
        HostDosUiState::Save, HostDosUiState::Load, HostDosUiState::Poll, HostDosUiState::Release,
        HostDosUiState::Picture};
    return &supplier;
}
inline const Supplier *ByKind(const std::string &kind)
{ return kind == "dos" ? Dos() : kind == "windows" ? Windows() : kind == "dos-ui" ? DosUi() : nullptr; }
// The engines of this host, by kind: what HostSaveState::Check compares the
// `engine` of a state with.
inline std::vector<std::pair<std::string, std::string>> Engines()
{
    return {{Dos()->kind, Dos()->Engine()}, {Windows()->kind, Windows()->Engine()}, {DosUi()->kind, DosUi()->Engine()}};
}
} // namespace HostMachineState
