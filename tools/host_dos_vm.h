// SPDX-License-Identifier: GPL-2.0-or-later
// The host side of the DOS virtual machine: DOSBox Pure behind the provider
// interface of src/dos_vm.h, driven over the libretro API.
// src/ knows nothing about libretro, exactly as with the screen, the sound and
// the crash report. This provider answers the environment of the core, takes
// its pictures and its sound, and hands it the keys and the mouse. The core
// runs its DOS machine on a thread of its own; retro_run is driven from a
// second thread that belongs to this provider, paced at the frame rate the
// core announces. Nothing here blocks a guest fiber: the guest asks for the
// newest picture and gets a copy, the sound waits in a ring until the guest
// drains it into the mixer.
// Up to eight machines at a time: the core keeps its whole state in globals,
// so each session runs on an image of the core of its own (LoadDosCore in
// host_dos_vm.cpp). A ninth Start is refused with a reason.
#pragma once
#include "../src/dos_vm.h"
#include "../src/volume.h"
#include <string>
#include <utility>
#include <vector>

namespace HostDosVm
{
using Logger = void (*)(const char *);
// A host directory behind a drive letter of the DOS side. The Win98 tree is
// mounted with a memory overlay: DOS may write to C:, the tree never changes.
struct Drive
{
    char letter = 0;
    const char *directory = nullptr;
    bool memory_overlay = false;
    // The host directory the Windows side writes this drive's changes to;
    // the DOS side writes there as well and reads it over `directory`.
    const char *overlay = nullptr;
    // The provider the Windows side has mounted for this drive, when the
    // drive is more than a host directory: the write layer of a game machine
    // (HostVolume::Layered::Foreign). The DOS machine then holds nothing of
    // the drive itself; every DOS call asks this provider, on the thread of
    // the DOS machine, and both sides see the same files, deletions and
    // 8.3 names. `directory` and `overlay` then only say where the
    // files lie on this Mac.
    Volume::Backend *volume = nullptr;
};
// Registers the provider with src/dos_vm.h. `logger` takes the lines of the
// core's log; null prints them to stderr. The drives are copied for future
// sessions; updating them leaves running machines' mounts unchanged.
void Install(Logger logger, const Drive *drives, unsigned count);
void Uninstall();
// Whether any machine runs right now.
bool Busy();
// The pause of the whole machine : every DOS machine stops at
// its next frame boundary and goes on from there. Pause(true) answers when
// the runner threads stand, or after half a second for one that waits for
// the guest inside its frame; such a thread does nothing until the guest
// runs again. Apart from the background pause of a single session.
void Pause(bool on);
// Frames of sound the machines have made and their processes have not given
// to the mixer yet, and how many machines run: what the line of a pause
// prints, together with X86::Application::DosVmSoundQueued.
unsigned AudioQueued(unsigned *machines = nullptr);

// --- Saved states -------------------------------------
// The DOS machine as the supplier of the state of a game machine whose game
// is a DOS program. The state is everything the core serializes (processor,
// memory, devices, the open files by name and position, the directories and
// the searches of the drives the host keeps) and what this provider holds
// beside it: the program that runs, the last picture, the pointer, the
// instruments and notes of the MIDI synthesizer.
// Saving and loading happen on the runner thread between two frames. A
// request is served at the next frame boundary; the machine then stands until
// StateRelease, so the caller can stop the rest of the machine (machine_pause)
// and copy the disks at exactly that moment.
// What a state of the DOS machine is laid out by: the number of this
// provider's part of Maschine.bin and the identity of the core it was built
// with (tools/build_dosbox_pure.py, state_identity), "dos-1-0123456789ab". A
// state made by another one is not put in. NEO98_DOS_CORE_ID in the
// environment stands in for the core's part in the tests.
std::string StateEngine();
// Whether the one DOS machine can be saved right now; the reason for the
// person when not. `machine` gets what the state will need of the machine
// that loads it (memory, graphics card).
bool StateReady(std::string &reason, std::vector<std::pair<std::string, std::string>> *machine = nullptr);
// Asks the machine for its state.
void StateSave();
// Hands the machine a state to put in. Before the machine starts: the next
// machine takes it when its program has started; until then it shows and
// plays nothing, takes no input, and what the starting program writes to the
// drives the host keeps is swallowed, because the disks are those of the
// state already. While a machine runs: it takes it at the next frame.
void StateLoad(std::vector<unsigned char> bytes);
// 0: not served yet. 1: done, the machine stands; a save hands its bytes
// out. -1: refused, with the reason.
int StatePoll(std::vector<unsigned char> *bytes, std::string &error);
// The machine goes on (after machine_pause has taken over, or at once).
void StateRelease();
// The last picture of the one DOS machine: rows of 0x00RRGGBB.
bool StatePicture(std::vector<unsigned> &pixels, unsigned &width, unsigned &height);
} // namespace HostDosVm
