// SPDX-License-Identifier: GPL-2.0-or-later
// DOSBox Pure as the DOS virtual machine of NEO98, over the libretro API.
// See host_dos_vm.h for the interface.
// Sessions. The core keeps its machine in globals, so every session runs on
// an image of its own: the core is a dynamic library (build_dosbox_pure.py),
// LoadDosCore copies it to a private path per session slot and opens the
// copy, which gives the slot its own globals in the same process. The
// libretro callbacks carry no context, so each slot hands the core callbacks
// of its own that lead to its Machine.
// Threads. The core starts its own thread for the DOS machine inside
// retro_load_game and synchronises it with retro_run through two semaphores:
// every retro_run hands the machine one frame of time and collects the
// picture and the sound of the frame before. retro_run therefore has to be
// called at the frame rate of the emulated video mode, 70 times a second for
// 320x200, and it must not be called from a guest fiber, because it waits for
// the machine. The runner thread of this file does exactly that and nothing
// else. Everything the guest touches is a copy behind a mutex.
#include "host_dos_vm.h"
#include "../src/sound_synth.h"
#include "../src/sound.h"
#include "dosbox_pure_neo98.h"
#include "../src/comm_host.h"
#include "../third_party/dosbox-pure/libretro-common/include/libretro.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __APPLE__
#include <copyfile.h>
#include <mach-o/dyld.h>
#endif
#include <filesystem>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifndef NEO98_DOSBOX_CORE
#define NEO98_DOSBOX_CORE "build/dosbox-pure-host/lib/libdosbox-pure.dylib"
#endif
#ifndef NEO98_DOS_CORE_ID
#define NEO98_DOS_CORE_ID "unknown"
#endif

namespace HostDosVm
{
namespace
{
Logger logger = nullptr;
// The pause of the whole machine, HostDosVm::Pause.
std::atomic<bool> machine_pause{false};
// A state waits for a machine that has not started its program yet
// (HostDosVm::StateLoad): the disks are those of the state already, so what
// the starting program writes to the drives the host keeps is swallowed.
std::atomic<bool> swallow_writes{false};
std::mutex awaited_lock;
std::vector<unsigned char> awaited_state;
bool state_awaited = false;

void Log(const char *text)
{
    if (logger) logger(text);
    else fprintf(stderr, "%s\n", text);
}

void Logf(const char *format, ...)
{
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    for (char *p = line; *p; ++p)
        if (*p == '\n' || *p == '\r') *p = ' ';
    Log(line);
}

// The number of this provider's part of a saved state (Machine::ServeState).
constexpr unsigned StateFormat = 3;
constexpr size_t BootKeyLimit = 65536; // at most 2 MiB per recorded input sequence

// The entry points of one image of the core.
struct Core
{
    void *handle = nullptr;
    void (*set_environment)(retro_environment_t) = nullptr;
    void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*set_input_poll)(retro_input_poll_t) = nullptr;
    void (*set_input_state)(retro_input_state_t) = nullptr;
    void (*init)() = nullptr;
    void (*deinit)() = nullptr;
    bool (*load_game)(const retro_game_info *) = nullptr;
    void (*unload_game)() = nullptr;
    void (*run)() = nullptr;
    size_t (*serialize_size)() = nullptr;
    bool (*serialize)(void *, size_t) = nullptr;
    bool (*unserialize)(const void *, size_t) = nullptr;
    size_t (*resume_size)() = nullptr;
    bool (*resume_save)(void *, size_t) = nullptr;
    int (*resume_load)(const void *, size_t, bool) = nullptr;
    bool (*resume_legacy_safe)() = nullptr;
    void (*get_system_av_info)(retro_system_av_info *) = nullptr;
    Neo98DbpState *(*state)() = nullptr;
};

// The seam of the sessions: an image of the core of its own for `slot`. The
// library is copied to a private file and opened from there, because dyld
// hands out the image it has already loaded for the same file; the copy is
// removed once it is open. NEO98_DOSBOX_CORE in the environment names
// another library. The image stays loaded for the life of the process and
// the slot uses it again for its next session.
bool LoadDosCore(unsigned slot, Core &core, char *error, unsigned capacity)
{
    if (core.handle) return true;
    const char *library = getenv("NEO98_DOSBOX_CORE");
    std::string packaged;
#ifdef __APPLE__
    if (!library || !*library) {
        char executable[4096] = {}; uint32_t size = sizeof executable;
        if (!_NSGetExecutablePath(executable, &size)) {
            // A game-machine restart executes its launcher symlink. Resolve it
            // before looking beside the host or inside the app bundle.
            std::error_code path_error;
            const auto resolved = std::filesystem::canonical(executable, path_error);
            const auto base = (path_error ? std::filesystem::path(executable) : resolved).parent_path();
            for (const char *relative : {"../Frameworks/libdosbox-pure.dylib",
                                         "../dosbox-pure-host/lib/libdosbox-pure.dylib"}) {
                const auto candidate = base / relative;
                if (std::filesystem::is_regular_file(candidate)) { packaged = candidate.string(); break; }
            }
        }
    }
#endif
#ifdef __linux__
    if (!library || !*library)
    {
        char executable[4096] = {};
        const ssize_t count = readlink("/proc/self/exe", executable, sizeof executable - 1);
        if (count > 0)
        {
            // The installed layout, then the build tree beside the host, as
            // on macOS: a host started in another directory finds its core.
            const auto base = std::filesystem::path(executable).parent_path();
            for (const char *relative : {"../lib/neo98/libdosbox-pure.so", "../dosbox-pure-host/lib/libdosbox-pure.so"})
            {
                const auto candidate = base / relative;
                if (std::filesystem::is_regular_file(candidate)) { packaged = candidate.string(); break; }
            }
        }
    }
#endif
    if (!library || !*library) library = packaged.empty() ? NEO98_DOSBOX_CORE : packaged.c_str();
    const char *temporary = getenv("TMPDIR");
    char copy[1024];
    snprintf(copy, sizeof copy, "%s/neo98-dos-core-%d-%u.dylib", temporary && *temporary ? temporary : "/tmp", (int)getpid(), slot);
    for (char *p = copy; *p; ++p)
        if (p[0] == '/' && p[1] == '/') memmove(p, p + 1, strlen(p));
    unlink(copy);
#ifdef __APPLE__
    const bool copied = !copyfile(library, copy, nullptr, COPYFILE_DATA);
#else
    std::error_code copy_error;
    const bool copied = std::filesystem::copy_file(library, copy, std::filesystem::copy_options::overwrite_existing, copy_error);
#endif
    if (!copied)
    {
#ifdef __APPLE__
        const std::string reason = strerror(errno);
#else
        const std::string reason = copy_error.message();
#endif
        snprintf(error, capacity, "the DOS machine %s could not be copied to %s: %s", library, copy, reason.c_str());
        return false;
    }
    core.handle = dlopen(copy, RTLD_NOW | RTLD_LOCAL);
    unlink(copy);
    if (!core.handle)
    {
        snprintf(error, capacity, "the DOS machine could not be loaded: %s", dlerror());
        return false;
    }
    bool okay = true;
    auto bind = [&](auto &field, const char *name) {
        field = reinterpret_cast<std::remove_reference_t<decltype(field)>>(dlsym(core.handle, name));
        if (!field) okay = false;
    };
    bind(core.set_environment, "retro_set_environment");
    bind(core.set_video_refresh, "retro_set_video_refresh");
    bind(core.set_audio_sample, "retro_set_audio_sample");
    bind(core.set_audio_sample_batch, "retro_set_audio_sample_batch");
    bind(core.set_input_poll, "retro_set_input_poll");
    bind(core.set_input_state, "retro_set_input_state");
    bind(core.init, "retro_init");
    bind(core.deinit, "retro_deinit");
    bind(core.load_game, "retro_load_game");
    bind(core.unload_game, "retro_unload_game");
    bind(core.run, "retro_run");
    bind(core.serialize_size, "retro_serialize_size");
    bind(core.serialize, "retro_serialize");
    bind(core.unserialize, "retro_unserialize");
    bind(core.resume_size, "DBP_Neo98ResumeSize");
    bind(core.resume_save, "DBP_Neo98ResumeSave");
    bind(core.resume_load, "DBP_Neo98ResumeLoad");
    bind(core.resume_legacy_safe, "DBP_Neo98ResumeLegacySafe");
    bind(core.get_system_av_info, "retro_get_system_av_info");
    bind(core.state, "_Z14DBP_Neo98Statev");
    if (!okay)
    {
        snprintf(error, capacity, "the DOS machine lacks an entry point");
        dlclose(core.handle);
        core = Core();
        return false;
    }
    Logf("DOSVM core image slot=%u", slot);
    return true;
}

// The core options this host answers. Everything not listed keeps the
// default of the core. The values are the ones core_options.h names; a wrong
// value falls back to the default there as well. These are fixed: they are
// about the frontend (menus, save states, on-screen keyboard, statistics),
// the window, or parts that are not built, and no PIF page offers them.
struct Option
{
    const char *key, *value;
};
const Option fixed_options[] = {
    // No start menu, and `exit` at the prompt ends the machine (DBP_WantAutoShutDown).
    {"dosbox_pure_menu_time", "0"},
    // The 3dfx Voodoo is drawn at the resolution of the program by the core;
    // the host that draws it instead scales it itself (src/dos_voodoo.cpp).
    {"dosbox_pure_voodoo_scale", "1"},
    // The picture stays in the pixels of the DOS mode; the window scales it.
    {"dosbox_pure_aspect_correction", "false"},
    {"dosbox_pure_overscan", "0"},
    // The DOS mouse driver takes the place of the Windows pointer (the
    // pointer device) and the steps for its motion counters, the way VMD
    // feeds the driver of a windowed VM.
    {"dosbox_pure_mouse_input", "direct"},
    {"dosbox_pure_on_screen_keyboard", "false"},
    {"dosbox_pure_perfstats", "none"},
    // States are taken and put in by the host; no rewind.
    {"dosbox_pure_savestate", "on"},
    {"dosbox_pure_auto_mapping", "false"},
    {"dosbox_pure_conf", "false"},
    {"dosbox_pure_strict_mode", "false"},
    {"dosbox_pure_audiorate", "44100"},
    {"dosbox_pure_force60fps", "false"},
    {"dosbox_pure_bootos_ramdisk", "false"},
};
// The defaults of the options the PIF's own pages set (src/x86_pif_options.cpp);
// a launch that names one of them wins.
const Option default_options[] = {
    // A 3dfx Voodoo with one texture unit sits in every machine, as in the
    // core; a program that does not look for it never notices.
    {"dosbox_pure_voodoo", "8mb"},
    {"dosbox_pure_machine", "svga"},
    {"dosbox_pure_cycles", "auto"},
    // The processor core as in DOSBox: auto takes the interpreter for real
    // mode and the dynamic recompiler for protected mode and for maximum
    // speed; dynamic always, normal never.
    {"dosbox_pure_cpu_core", "auto"},
    {"dosbox_pure_midi", "frontend"},
    // The Sound Blaster where Windows 98 puts it for a DOS box (BLASTER=A220
    // I5 D1); the core's own default is IRQ 7. A game set up on a Win98
    // machine names IRQ 5 in its own configuration and waits for an
    // interrupt that never comes on another line.
    {"dosbox_pure_sblaster_conf", "A220 I5 D1 H5"},
};

// A launch option by its name without the prefix of the core; null when the
// launch does not name it.
bool LaunchOption(const std::string &options, const char *key, std::string &value)
{
    const char *name = strncmp(key, "dosbox_pure_", 12) ? key : key + 12;
    const size_t length = strlen(name);
    size_t at = 0;
    while (at < options.size())
    {
        size_t end = options.find('\n', at);
        if (end == std::string::npos) end = options.size();
        if (end - at > length && !options.compare(at, length, name) && options[at + length] == '=')
        {
            value.assign(options, at + length + 1, end - at - length - 1);
            return true;
        }
        at = end + 1;
    }
    return false;
}

// The RETROK code of a PC scancode of set 1, the physical key. DOS wants the
// key, not the character; the machine's own layout (dosbox_pure_keyboard_layout,
// the layout of the Windows session) turns it into the character, so a
// German keyboard types German in DOS.
unsigned RetroKeyFromScancode(unsigned scan, bool extended)
{
    if (extended)
        switch (scan)
        {
        case 0x1c: return RETROK_KP_ENTER;
        case 0x1d: return RETROK_RCTRL;
        case 0x35: return RETROK_KP_DIVIDE;
        case 0x38: return RETROK_RALT;
        case 0x47: return RETROK_HOME;
        case 0x48: return RETROK_UP;
        case 0x49: return RETROK_PAGEUP;
        case 0x4b: return RETROK_LEFT;
        case 0x4d: return RETROK_RIGHT;
        case 0x4f: return RETROK_END;
        case 0x50: return RETROK_DOWN;
        case 0x51: return RETROK_PAGEDOWN;
        case 0x52: return RETROK_INSERT;
        case 0x53: return RETROK_DELETE;
        case 0x5b: return RETROK_LSUPER;
        case 0x5c: return RETROK_RSUPER;
        case 0x5d: return RETROK_MENU;
        default: break;
        }
    static const unsigned short table[0x59] = {
        RETROK_UNKNOWN, RETROK_ESCAPE, RETROK_1, RETROK_2, RETROK_3, RETROK_4, RETROK_5, RETROK_6, RETROK_7, RETROK_8, RETROK_9, RETROK_0,
        RETROK_MINUS, RETROK_EQUALS, RETROK_BACKSPACE, RETROK_TAB, RETROK_q, RETROK_w, RETROK_e, RETROK_r, RETROK_t, RETROK_y, RETROK_u,
        RETROK_i, RETROK_o, RETROK_p, RETROK_LEFTBRACKET, RETROK_RIGHTBRACKET, RETROK_RETURN, RETROK_LCTRL, RETROK_a, RETROK_s, RETROK_d,
        RETROK_f, RETROK_g, RETROK_h, RETROK_j, RETROK_k, RETROK_l, RETROK_SEMICOLON, RETROK_QUOTE, RETROK_BACKQUOTE, RETROK_LSHIFT,
        RETROK_BACKSLASH, RETROK_z, RETROK_x, RETROK_c, RETROK_v, RETROK_b, RETROK_n, RETROK_m, RETROK_COMMA, RETROK_PERIOD, RETROK_SLASH,
        RETROK_RSHIFT, RETROK_KP_MULTIPLY, RETROK_LALT, RETROK_SPACE, RETROK_CAPSLOCK, RETROK_F1, RETROK_F2, RETROK_F3, RETROK_F4, RETROK_F5,
        RETROK_F6, RETROK_F7, RETROK_F8, RETROK_F9, RETROK_F10, RETROK_NUMLOCK, RETROK_SCROLLOCK, RETROK_KP7, RETROK_KP8, RETROK_KP9,
        RETROK_KP_MINUS, RETROK_KP4, RETROK_KP5, RETROK_KP6, RETROK_KP_PLUS, RETROK_KP1, RETROK_KP2, RETROK_KP3, RETROK_KP0, RETROK_KP_PERIOD,
        RETROK_UNKNOWN, RETROK_UNKNOWN, RETROK_OEM_102, RETROK_F11, RETROK_F12};
    return scan < 0x59 ? table[scan] : RETROK_UNKNOWN;
}

// The RETROK code of a WM_KEYDOWN. DOS wants the key, not the character, so
// the virtual key names it and the scancode tells left from right and the
// keypad from the cursor block.
unsigned RetroKey(unsigned vk, unsigned scan, bool extended)
{
    if (vk >= 'A' && vk <= 'Z') return RETROK_a + (vk - 'A');
    if (vk >= '0' && vk <= '9') return RETROK_0 + (vk - '0');
    if (vk >= 0x70 && vk <= 0x7b) return RETROK_F1 + (vk - 0x70);
    if (vk >= 0x60 && vk <= 0x69) return RETROK_KP0 + (vk - 0x60);
    switch (vk)
    {
    case 0x08: return RETROK_BACKSPACE;
    case 0x09: return RETROK_TAB;
    case 0x0d: return extended ? RETROK_KP_ENTER : RETROK_RETURN;
    case 0x10: return scan == 0x36 ? RETROK_RSHIFT : RETROK_LSHIFT;
    case 0x11: return extended ? RETROK_RCTRL : RETROK_LCTRL;
    case 0x12: return extended ? RETROK_RALT : RETROK_LALT;
    case 0x13: return RETROK_PAUSE;
    case 0x14: return RETROK_CAPSLOCK;
    case 0x1b: return RETROK_ESCAPE;
    case 0x20: return RETROK_SPACE;
    // The cursor block without the extended bit is the keypad with Num Lock off.
    case 0x21: return extended ? RETROK_PAGEUP : RETROK_KP9;
    case 0x22: return extended ? RETROK_PAGEDOWN : RETROK_KP3;
    case 0x23: return extended ? RETROK_END : RETROK_KP1;
    case 0x24: return extended ? RETROK_HOME : RETROK_KP7;
    case 0x25: return extended ? RETROK_LEFT : RETROK_KP4;
    case 0x26: return extended ? RETROK_UP : RETROK_KP8;
    case 0x27: return extended ? RETROK_RIGHT : RETROK_KP6;
    case 0x28: return extended ? RETROK_DOWN : RETROK_KP2;
    case 0x0c: return RETROK_KP5;
    case 0x2c: return RETROK_PRINT;
    case 0x2d: return extended ? RETROK_INSERT : RETROK_KP0;
    case 0x2e: return extended ? RETROK_DELETE : RETROK_KP_PERIOD;
    case 0x6a: return RETROK_KP_MULTIPLY;
    case 0x6b: return RETROK_KP_PLUS;
    case 0x6d: return RETROK_KP_MINUS;
    case 0x6e: return RETROK_KP_PERIOD;
    case 0x6f: return RETROK_KP_DIVIDE;
    case 0x90: return RETROK_NUMLOCK;
    case 0x91: return RETROK_SCROLLOCK;
    case 0xa0: return RETROK_LSHIFT;
    case 0xa1: return RETROK_RSHIFT;
    case 0xa2: return RETROK_LCTRL;
    case 0xa3: return RETROK_RCTRL;
    case 0xa4: return RETROK_LALT;
    case 0xa5: return RETROK_RALT;
    case 0xba: return RETROK_SEMICOLON;
    case 0xbb: return RETROK_EQUALS;
    case 0xbc: return RETROK_COMMA;
    case 0xbd: return RETROK_MINUS;
    case 0xbe: return RETROK_PERIOD;
    case 0xbf: return RETROK_SLASH;
    case 0xc0: return RETROK_BACKQUOTE;
    case 0xdb: return RETROK_LEFTBRACKET;
    case 0xdc: return RETROK_BACKSLASH;
    case 0xdd: return RETROK_RIGHTBRACKET;
    case 0xde: return RETROK_QUOTE;
    case 0xe2: return RETROK_OEM_102;
    default: return RETROK_UNKNOWN;
    }
}

struct KeyEvent
{
    unsigned key;
    bool down;
};
struct BootKey
{
    unsigned long long frame;
    KeyEvent event;
    char program[16] = {};
};

// The callbacks a slot hands to its image of the core; they lead to the
// Machine of the slot (Slot<N> below).
struct SlotCallbacks
{
    retro_environment_t environment;
    retro_video_refresh_t video;
    retro_audio_sample_batch_t audio;
    retro_input_state_t input;
    bool (*midi_write)(uint8_t, uint32_t);
};
constexpr unsigned MaxSessions = 8;
extern const SlotCallbacks slot_callbacks[MaxSessions];

// One DOS session: a slot with its own image of the core.
class Machine
{
  public:
    unsigned slot = 0;
    bool in_use = false;
    Core core;
#include "host_dos_microphone.inc"

    // The serial ports this machine holds at the host, a bit per port. The
    // core gives them back when it shuts down; a machine that is torn down
    // without that gives them back here.
    unsigned serial_held = 0;
    void ReleaseAllSerial()
    {
        for (unsigned port = 0; port < CommHost::PortCapacity; ++port) ReleaseSerial(port);
    }
    void ReleaseSerial(unsigned port)
    {
        if (!(serial_held & (1u << port))) return;
        serial_held &= ~(1u << port);
        if (const CommHost::Provider *h = CommHost::Current()) h->Close(h->owner, port, CommHost::DosHolder(slot));
    }

    // The drives of every session, from Install.
    Neo98DbpDrive drives[Neo98DbpState::DriveCapacity];
    unsigned drive_count = 0;

    bool Start(const DosVm::Launch &launch, char *error, unsigned capacity)
    {
        auto *state = core.state();
        *state = Neo98DbpState();
        state->active = true;
        microphone_requests=0;microphone_seen=0;microphone_time=0;
        state->microphone_owner=this;
        state->microphone=MicrophoneRead;
        for (unsigned i = 0; i < drive_count; ++i) state->drives[i] = drives[i];
        state->drive_count = drive_count;
        state->xms = launch.xms;
        state->ems = launch.ems;
        snprintf(state->working_directory, sizeof state->working_directory, "%s", launch.directory);
        snprintf(state->command, sizeof state->command, "%s", launch.command);
        snprintf(state->prelude, sizeof state->prelude, "%s", launch.prelude);
        state->lines = launch.lines;
        state->paste_rate = launch.fast_paste ? 8 : 1;
        options = launch.options;
        // The 3dfx Voodoo drawn by the host: the core hands over what the
        // card draws and reads the linear frame buffer from these buffers.
        voodoo_host = launch.voodoo_host;
        if (voodoo_host)
        {
            const size_t pixels = (size_t)DosVoodooReadback::Pitch * DosVoodooReadback::Rows;
            for (auto &buffer : voodoo_pixels) buffer.assign(pixels, 0);
            for (unsigned i = 0; i < 3; ++i) state->voodoo_readback.color[i] = voodoo_pixels[i].data();
            state->voodoo_readback.depth = voodoo_pixels[3].data();
            state->voodoo_frame = VoodooSink;
            state->voodoo_owner = this;
        }
        // The serial ports: a port the host has an endpoint for is a 16550
        // with that line, held by this machine from its first register write
        // until it ends.
        if (CommHost::Current())
        {
            state->serial_owner = this;
            state->serial_attached = [](void *, unsigned port) { return CommHost::Attached(port); };
            state->serial_claim = [](void *o, unsigned port) {
                const CommHost::Provider *h = CommHost::Current();
                auto *self = static_cast<Machine *>(o);
                if (!h || !h->Open(h->owner, port, CommHost::DosHolder(self->slot))) return false;
                self->serial_held |= 1u << port;
                return true;
            };
            state->serial_release = [](void *o, unsigned port) {
                auto *self = static_cast<Machine *>(o);
                self->ReleaseSerial(port);
            };
            state->serial_read = [](void *, unsigned port, unsigned char *out, unsigned capacity) {
                const CommHost::Provider *h = CommHost::Current();
                return h ? h->Read(h->owner, port, out, capacity) : 0u;
            };
            state->serial_write = [](void *, unsigned port, const unsigned char *data, unsigned count) {
                if (const CommHost::Provider *h = CommHost::Current()) h->Write(h->owner, port, data, count);
            };
            state->serial_lines = [](void *, unsigned port) {
                const CommHost::Provider *h = CommHost::Current();
                return h ? h->Lines(h->owner, port) : 0u;
            };
            state->serial_control = [](void *, unsigned port, bool dtr, bool rts, bool line_break) {
                if (const CommHost::Provider *h = CommHost::Current()) h->Control(h->owner, port, dtr, rts, line_break);
            };
            state->serial_configure = [](void *, unsigned port, unsigned baud, unsigned data_bits, unsigned parity,
                                         unsigned stop_bits) {
                if (const CommHost::Provider *h = CommHost::Current())
                    h->Configure(h->owner, port, baud, data_bits, parity, stop_bits);
            };
        }
        ResolveMidi();
        snprintf(memory_option, sizeof memory_option, "%u", launch.memory_mb ? launch.memory_mb : 16);
        // Classic PIF XMS fields are 16-bit KB and cannot request 128 MB.
        // The machine extension can; use only sizes supported by the core.
        std::string requested_memory;
        if (LaunchOption(options, "dosbox_pure_memory_size", requested_memory))
        {
            static const char *sizes[] = {"4", "8", "16", "24", "32", "48", "64", "96", "128", "224", "256", "512", "1024"};
            for (const char *size : sizes)
                if (requested_memory == size) snprintf(memory_option, sizeof memory_option, "%s", size);
        }
        if (!launch.xms && !launch.ems) snprintf(memory_option, sizeof memory_option, "none");
        snprintf(layout_option, sizeof layout_option, "%s", launch.keyboard_layout[0] ? launch.keyboard_layout : "us");
        Logf("DOSVM start command=\"%s\" directory=\"%s\" memory=%s drives=%u fullscreen=%u close=%u layout=%s lines=%u idle=%u",
             state->command, state->working_directory, memory_option, state->drive_count,
             launch.fullscreen ? 1u : 0u, launch.close_on_exit ? 1u : 0u, layout_option, launch.lines,
             launch.idle_sensitivity);
        for (size_t at = 0; at < options.size();)
        {
            size_t end = options.find('\n', at);
            if (end == std::string::npos) end = options.size();
            if (end > at) Logf("DOSVM option %.*s", (int)(end - at), options.c_str() + at);
            at = end + 1;
        }
        if (state->prelude[0]) Logf("DOSVM prelude %s", state->prelude);
        // The machine of the session before: its end, its last picture,
        // its keys and its sound must not reach this one.
        {
            std::lock_guard<std::mutex> guard(lock);
            finished = false;
            exit_code = 0;
            crashed = false;
            crash_text.clear();
            newest = -1;
            width = height = serial = 0;
            ring_head = ring_count = 0;
            keys.clear();
            memset(held, 0, sizeof held);
            mouse_dx = mouse_dy = 0;
            pointer_x = pointer_y = 0;
            mouse_buttons = 0;
            program[0] = 0;
            keyboard = nullptr;
            windows.clear();
            windows_seen = 0;
            text_columns = text_rows = 0;
            paused = false;
            frames_run = 0;
            input_clock = 0;
            boot_keys.clear();
            boot_prefix = boot_cursor = 0;
            boot_overflow = boot_prefix_overflow = boot_decoded = boot_rebuild = false;
            boot_next_frame = 0;
            boot_program[0] = 0;
        }
        {
            std::lock_guard<std::mutex> guard(voodoo_lock);
            voodoo_queue.clear();
            voodoo_taken.reset();
            voodoo_last = DosVoodooFrame();
            voodoo_seen = false;
        }
        {
            std::lock_guard<std::mutex> guard(midi_lock);
            synth.Reset();
            midi_length = midi_expected = 0;
            midi_status = 0;
            midi_messages = 0;
            MidiForget();
        }
        {
            std::lock_guard<std::mutex> guard(state_lock);
            ++state_generation;
            state_job = 0;
            state_result = 0;
            state_bytes.clear();
            state_error.clear();
            state_hold = false;
            state_loading = false;
        }
        {
            // A state that waits for this machine (HostDosVm::StateLoad).
            std::lock_guard<std::mutex> guard(awaited_lock);
            if (state_awaited)
            {
                state_awaited = false;
                state_loading = true;
                StateAsk(true, std::move(awaited_state));
                awaited_state.clear();
            }
        }
        const auto &callbacks = slot_callbacks[slot];
        core.set_environment(callbacks.environment);
        core.set_video_refresh(callbacks.video);
        core.set_audio_sample(AudioSample);
        core.set_audio_sample_batch(callbacks.audio);
        core.set_input_poll(InputPoll);
        core.set_input_state(callbacks.input);
        core.init();
        retro_game_info info = {};
        info.path = "";
        if (!core.load_game(&info))
        {
            snprintf(error, capacity, "DOSBox Pure refused the program");
            core.deinit();
            return false;
        }
        retro_system_av_info av = {};
        core.get_system_av_info(&av);
        fps = av.timing.fps > 1 ? av.timing.fps : 60;
        Logf("DOSVM machine up fps=%.3f rate=%.0f size=%ux%u", av.timing.fps, av.timing.sample_rate,
             av.geometry.base_width, av.geometry.base_height);
        stop = false;
        thread = std::thread([this] { Loop(); });
        return true;
    }
    void Stop()
    {
        stop = true;
        if (thread.joinable()) thread.join();
        core.unload_game();
        core.deinit();
        core.state()->active = false;
        Logf("DOSVM stopped pictures=%u runs=%u", serial, frames_run);
    }
    bool NewFrame(unsigned seen, DosVm::Frame &out)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (newest < 0 || serial == seen) return false;
        presented = back[newest];
        out.pixels = presented.data();
        out.width = width;
        out.height = height;
        out.serial = serial;
        return true;
    }
    void Key(unsigned vk, unsigned scan, bool extended, bool down)
    {
        unsigned key = scan ? RetroKeyFromScancode(scan, extended) : RETROK_UNKNOWN;
        if (key == RETROK_UNKNOWN) key = RetroKey(vk, scan, extended);
        if (key == RETROK_UNKNOWN || state_loading) return;
        std::lock_guard<std::mutex> guard(lock);
        if (keys.size() < 256) keys.push_back({key, down});
    }
    void Mouse(int dx, int dy, unsigned x, unsigned y, unsigned buttons)
    {
        if (state_loading) return;
        std::lock_guard<std::mutex> guard(lock);
        mouse_dx += dx;
        mouse_dy += dy;
        // libretro's pointer runs from -0x7fff to 0x7fff over the screen.
        pointer_x = (int)((long long)x * 0xfffe / 65535) - 0x7fff;
        pointer_y = (int)((long long)y * 0xfffe / 65535) - 0x7fff;
        mouse_buttons = buttons;
    }
    unsigned Audio(short *out, unsigned frames)
    {
        std::lock_guard<std::mutex> guard(lock);
        unsigned n = 0;
        while (n < frames && ring_count)
        {
            out[n * 2] = ring[ring_head * 2];
            out[n * 2 + 1] = ring[ring_head * 2 + 1];
            ring_head = (ring_head + 1) % RingFrames;
            --ring_count;
            ++n;
        }
        return n;
    }
    void Program(char *name, unsigned capacity)
    {
        std::lock_guard<std::mutex> guard(lock);
        snprintf(name, capacity, "%s", program);
    }
    bool WindowsProgram(char *program, unsigned pc, char *arguments, unsigned ac, char *directory, unsigned dc, bool &wait)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (windows.empty()) return false;
        const auto &w = windows.front();
        snprintf(program, pc, "%s", w.program.c_str());
        snprintf(arguments, ac, "%s", w.arguments.c_str());
        snprintf(directory, dc, "%s", w.directory.c_str());
        wait = w.wait;
        windows.erase(windows.begin());
        return true;
    }
    void PasteText(const char *text)
    {
        auto *state = core.state();
        for (const char *p = text; *p; ++p)
        {
            if (state->paste_tail - state->paste_head >= sizeof state->paste_text) break;
            state->paste_text[state->paste_tail % sizeof state->paste_text] = *p;
            state->paste_tail++;
        }
    }
    bool ScreenText(unsigned &columns, unsigned &rows, unsigned short *cells, unsigned capacity)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!text_columns || text_columns * text_rows > capacity) return false;
        columns = text_columns;
        rows = text_rows;
        memcpy(cells, text_cells, text_columns * text_rows * 2);
        return true;
    }
    bool TextCursor(unsigned &column, unsigned &row, unsigned &start, unsigned &end, unsigned &height)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!text_columns || !cursor_height) return false;
        column = cursor[0];
        row = cursor[1];
        start = cursor[2];
        end = cursor[3];
        height = cursor_height;
        return true;
    }
    void Pause(bool on) { paused = on; }
    // The runner thread is not inside a frame of the machine.
    bool Standing() const { return !looping || parked; }
    bool Loading() const { return state_loading; }
    unsigned AudioQueued()
    {
        std::lock_guard<std::mutex> guard(lock);
        return ring_count;
    }
    bool VoodooFrame(DosVoodooFrame &out)
    {
        std::lock_guard<std::mutex> guard(voodoo_lock);
        if (voodoo_queue.empty()) return false;
        voodoo_taken = std::move(voodoo_queue.front());
        voodoo_queue.pop_front();
        out = voodoo_taken->frame;
        voodoo_room.notify_all();
        return true;
    }
    // The guest has drawn the frame it took and filled the buffers the frame
    // asked for: the DOS thread, which waits in its read, goes on.
    void VoodooDrawn()
    {
        std::lock_guard<std::mutex> guard(voodoo_lock);
        if (voodoo_taken) voodoo_drawn = voodoo_taken->serial;
        voodoo_room.notify_all();
    }
    DosVoodooReadback *VoodooReadback() { return voodoo_host ? &core.state()->voodoo_readback : nullptr; }
    void WindowsProgramDone(unsigned exit_code)
    {
        auto *state = core.state();
        state->windows_exit_code = exit_code;
        state->windows_done = true;
    }
    bool Finished(unsigned &code, bool &crash, char *message, unsigned capacity)
    {
        if (!finished) return false;
        code = exit_code;
        crash = crashed;
        snprintf(message, capacity, "%s", crash_text.c_str());
        return true;
    }

    // --- Saved states -------------------------------
    // What a state needs of the machine that loads it; the core refuses one
    // made with another memory size or graphics card.
    void StateMachine(std::vector<std::pair<std::string, std::string>> &out)
    {
        std::string card = "svga";
        LaunchOption(options, "dosbox_pure_machine", card);
        out.emplace_back("speicher", memory_option);
        out.emplace_back("grafik", card);
    }
    bool StateReady(std::string &reason)
    {
        char name[9];
        Program(name, sizeof name);
        if (finished) reason = "The program has exited.";
        else if (state_loading) reason = "A state is being loaded.";
        else if (!frames_run || !name[0] || !strcmp(name, "DOSBOX"))
            reason = "The game is not running: the machine is starting or at the command prompt.";
        else return true;
        return false;
    }
    void StateAsk(bool load, std::vector<unsigned char> bytes)
    {
        std::lock_guard<std::mutex> guard(state_lock);
        ++state_generation;
        state_bytes = std::move(bytes);
        state_error.clear();
        state_result = 0;
        state_job = load ? 2 : 1;
    }
    int StatePoll(std::vector<unsigned char> *bytes, std::string &error)
    {
        std::lock_guard<std::mutex> guard(state_lock);
        if (!state_result && finished && state_job)
        {
            // The machine ended before it came to the frame boundary.
            ++state_generation;
            state_job = 0;
            state_loading = false;
            state_result = -1;
            state_error = "The program has exited.";
        }
        if (state_result > 0 && bytes) *bytes = std::move(state_bytes);
        if (state_result) state_bytes.clear();
        error = state_error;
        return state_result;
    }
    void StateRelease()
    {
        std::lock_guard<std::mutex> guard(state_lock);
        ++state_generation;
        state_job = 0;
        state_result = 0;
        state_bytes.clear();
        state_error.clear();
        if (state_loading) swallow_writes = false;
        state_loading = false;
        state_hold = false;
    }
    bool StatePicture(std::vector<unsigned> &pixels, unsigned &w, unsigned &h)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (newest < 0 || !width || !height) return false;
        pixels = back[newest];
        for (auto &pixel : pixels) pixel &= 0xffffff;
        w = width;
        h = height;
        return true;
    }

  private:
    // The state of the machine as bytes: a head of this provider, then what
    // the core serializes.
    // "NEO98DOS", version, the program DOS runs, the pointer and its
    // buttons, the last picture, the channels and notes of the MIDI
    // synthesizer, the length of the core's part, the core's part.
    // Whoever changes what is written here, or its order or sizes, raises
    // StateFormat (below); tools/test_savestates_dos_host.py holds the
    // length of this part for each number.
    static constexpr unsigned StateVersion = StateFormat;
    struct Bytes
    {
        std::vector<unsigned char> &out;
        void Raw(const void *data, size_t size)
        {
            const auto *bytes = static_cast<const unsigned char *>(data);
            out.insert(out.end(), bytes, bytes + size);
        }
        void Word(unsigned long long value) { Raw(&value, 8); }
    };
    struct Reader
    {
        const unsigned char *at, *end;
        bool okay = true;
        bool Raw(void *data, size_t size)
        {
            if (!okay || size_t(end - at) < size) return okay = false;
            memcpy(data, at, size);
            at += size;
            return true;
        }
        unsigned long long Word()
        {
            unsigned long long value = 0;
            Raw(&value, 8);
            return value;
        }
    };
    // On the runner thread, between two frames.
    void ServeState()
    {
        int job;
        uint64_t generation;
        {
            std::lock_guard<std::mutex> guard(state_lock);
            job = state_job;
            generation = state_generation;
            if (job == 2 && load_generation != generation)
            {
                load_bytes = std::move(state_bytes);
                load_generation = generation;
            }
        }
        if (!job) return;
        std::vector<unsigned char> saved_bytes;
        auto *state = core.state();
        std::string error;
        bool done = false;
        if (job == 1)
        {
            std::vector<unsigned char> bytes;
            if (boot_prefix_overflow)
            {
                std::lock_guard<std::mutex> guard(state_lock);
                if (generation != state_generation) return;
                state_job = 0;
                state_result = -1;
                state_error = "The startup sequence exceeds 65536 key events and cannot be replayed.";
                return;
            }
            // The DOS thread comes to the end of its frame first and stays
            // there. retro_run starts the next frame before it returns, so
            // between two calls of it the DOS thread is in the middle of
            // one; the core's serialize stops it where it is and lets it go
            // on afterwards, to the end of that frame. What the program
            // writes to its disk in that rest (the swap file of a DOS
            // extender, all the time) is then newer than the memory in the
            // state, and the state crashes whenever it is loaded.
            // retro_get_system_av_info is the core's entry that waits for
            // the end of the frame.
            if (!finished && !state->finished)
            {
                retro_system_av_info settled = {};
                core.get_system_av_info(&settled);
            }
            state->state_busy = 0;
            const size_t size = finished || state->finished ? 0 : core.serialize_size();
            std::vector<unsigned char> machine(size);
            const bool written = size && core.serialize(machine.data(), size);
            const size_t resume_size = written ? core.resume_size() : 0;
            std::vector<unsigned char> resume(resume_size);
            const bool continued = written && resume_size && core.resume_save(resume.data(), resume.size());
            unsigned resume_frames = 0;
            if (resume.size() >= 8) memcpy(&resume_frames, resume.data() + 4, 4);
            if (!written && state->state_busy && !finished && !state->finished)
            {
                // The machine stands inside a call of its DOS that waits
                // for the program (a page fault of a DOS extender while it
                // reads its swap file): not here. The next frame asks again;
                // a paused machine is let go for it by the host.
                if (!(state_busy_count++ % 50)) Logf("DOSVM state save waits: the machine is inside a call of its own (%u)", state_busy_count);
                return;
            }
            state_busy_count = 0;
            if (!written)
                error = StateErrorText(state, false);
            else if (!continued)
                error = "Cannot capture the native DOS resume state at this point.";
            else if (boot_overflow && resume_frames > 1)
                error = "The input sequence exceeds 65536 key events. The waiting DOS function cannot be restored safely.";
            else
            {
                Bytes out{bytes};
                out.Raw("NEO98DOS", 8);
                out.Word(StateVersion);
                char name[16] = {};
                memcpy(name, state->running_program, sizeof state->running_program);
                out.Raw(name, sizeof name);
                {
                    std::lock_guard<std::mutex> guard(lock);
                    out.Word((unsigned long long)(long long)pointer_x);
                    out.Word((unsigned long long)(long long)pointer_y);
                    out.Word(mouse_buttons);
                    const bool picture = newest >= 0 && width && height;
                    out.Word(picture ? width : 0);
                    out.Word(picture ? height : 0);
                    if (picture) out.Raw(back[newest].data(), (size_t)width * height * 4);
                }
                {
                    std::lock_guard<std::mutex> guard(midi_lock);
                    out.Raw(midi_setup, sizeof midi_setup);
                    out.Raw(midi_notes, sizeof midi_notes);
                }
                // Only the inputs before the current program began belong
                // to its ordinary bootstrap; native implementation is separate.
                out.Word(input_clock);
                out.Word(boot_prefix);
                for (size_t i = 0; i < boot_prefix; ++i)
                {
                    out.Word(boot_keys[i].frame);
                    out.Word(boot_keys[i].event.key | (boot_keys[i].event.down ? 0x10000u : 0));
                    out.Raw(boot_keys[i].program, sizeof boot_keys[i].program);
                }
                out.Word(resume.size());
                out.Raw(resume.data(), resume.size());
                // A later native input call may sit behind a game's intro.
                // Keep its observed input route separately; it is used only
                // if the fresh machine has not reached that call chain yet.
                const size_t route = resume_frames > 1 ? boot_keys.size() : 0;
                out.Word(route);
                for (size_t i = 0; i < route; ++i)
                {
                    out.Word(boot_keys[i].frame);
                    out.Word(boot_keys[i].event.key | (boot_keys[i].event.down ? 0x10000u : 0));
                    out.Raw(boot_keys[i].program, sizeof boot_keys[i].program);
                }
                out.Word(machine.size());
                out.Raw(machine.data(), machine.size());
                Logf("DOSVM state saved program=%s bytes=%zu core=%zu frame=%u", name, bytes.size(), machine.size(), frames_run);
                saved_bytes = std::move(bytes);
                done = true;
            }
        }
        else
        {
            // The state goes into a machine that runs the program it was
            // made in; a machine that has just started is not there yet.
            const auto &bytes = load_bytes;
            Reader in{bytes.data(), bytes.data() + bytes.size()};
            char magic[8] = {}, name[16] = {};
            in.Raw(magic, 8);
            const unsigned long long version = in.Word();
            in.Raw(name, sizeof name);
            name[8] = 0;
            const long long x = (long long)in.Word(), y = (long long)in.Word();
            const unsigned buttons = (unsigned)in.Word();
            const unsigned long long w = in.Word(), h = in.Word();
            // The picture is copied when the state goes in, not while it waits.
            const unsigned char *picture_at = in.at;
            const bool has_picture = in.okay && w && h && w <= 4096 && h <= 4096;
            if (has_picture && (size_t)(in.end - in.at) >= (size_t)(w * h * 4)) in.at += (size_t)(w * h * 4);
            else if (w || h) in.okay = false;
            short setup[16][MidiSetupSize];
            unsigned char notes[16][128];
            in.Raw(setup, sizeof setup);
            in.Raw(notes, sizeof notes);
            unsigned long long clock = 0;
            std::vector<BootKey> startup;
            if (version >= 2)
            {
                clock = in.Word();
                const unsigned long long count = in.Word();
                if (count > BootKeyLimit || count > (unsigned long long)(in.end - in.at) / 32) in.okay = false;
                if (in.okay) startup.resize((size_t)count);
                unsigned long long prior = 0;
                for (auto &key : startup)
                {
                    key.frame = in.Word();
                    const unsigned long long event = in.Word();
                    key.event = {(unsigned)(event & 0xffff), (event & 0x10000) != 0};
                    in.Raw(key.program, sizeof key.program);
                    if (event > 0x1ffff || key.event.key >= RETROK_LAST || key.frame < prior || key.frame > clock ||
                        key.program[8] || !key.program[0]) in.okay = false;
                    prior = key.frame;
                }
            }
            const unsigned char *resume_at = in.at;
            unsigned long long resume_size = 0;
            if (version >= 3)
            {
                resume_size = in.Word();
                resume_at = in.at;
                if (resume_size < 8 || resume_size > 1048576 || resume_size > (unsigned long long)(in.end - in.at)) in.okay = false;
                else in.at += resume_size;
            }
            std::vector<BootKey> route;
            if (version >= 3)
            {
                const unsigned long long count = in.Word();
                if (count > BootKeyLimit || count > (unsigned long long)(in.end - in.at) / 32) in.okay = false;
                if (in.okay) route.resize((size_t)count);
                unsigned long long prior = 0;
                for (auto &key : route)
                {
                    key.frame = in.Word();
                    const auto event = in.Word();
                    key.event = {(unsigned)(event & 0xffff), (event & 0x10000) != 0};
                    in.Raw(key.program, sizeof key.program);
                    if (event > 0x1ffff || key.event.key >= RETROK_LAST || key.frame < prior || key.frame > clock ||
                        key.program[8] || !key.program[0]) in.okay = false;
                    prior = key.frame;
                }
                if (!route.empty())
                {
                    if (route.size() < startup.size()) in.okay = false;
                    for (size_t i = 0; i < startup.size() && i < route.size(); ++i)
                        if (startup[i].frame != route[i].frame || startup[i].event.key != route[i].event.key ||
                            startup[i].event.down != route[i].event.down || memcmp(startup[i].program, route[i].program, sizeof startup[i].program)) in.okay = false;
                }
            }
            const unsigned long long size = in.Word();
            if (!in.okay || memcmp(magic, "NEO98DOS", 8) || version < 1 || version > StateVersion || size != (unsigned long long)(in.end - in.at))
                error = "The machine state is damaged or does not belong to a DOS program.";
            else if (finished || state->finished)
                error = "The program exited before the state was restored.";
            else if (!boot_decoded)
            {
                // Replay uses only keys this launch already received. The
                // user's live input stays disabled until the state is in.
                boot_prefix = startup.size();
                boot_keys = route.empty() ? std::move(startup) : std::move(route);
                boot_cursor = 0;
                boot_decoded = true;
                boot_next_frame = frames_run + 2;
                Logf("DOSVM state bootstrap keys=%zu program=%s", boot_prefix, name);
                return;
            }
            else if (!frames_run || boot_cursor < boot_prefix || strcasecmp(name, state->running_program))
                return; // not yet
            else
            {
                // From here on the drives are the machine's again: the files
                // the state had open are opened anew through them.
                // Match the recreated native call chain before changing
                // either guest state or host locals. A line input may be
                // suspended below CON.Read and its BIOS keyboard interrupt.
                if (resume_size)
                {
                    retro_system_av_info settled = {};
                    core.get_system_av_info(&settled);
                    const int match = core.resume_load(resume_at, (size_t)resume_size, false);
                    if (!match)
                    {
                        if (!boot_rebuild && boot_cursor < boot_keys.size())
                        {
                            boot_rebuild = true;
                            Logf("DOSVM state native bootstrap route=%zu already=%zu", boot_keys.size(), boot_cursor);
                        }
                        return;
                    }
                    if (match < 0) error = "The native DOS resume data is damaged.";
                }
                else if (version < 3)
                {
                    // Old providers never recorded locals of a pending DOS
                    // read. Defaulting them would silently discard a partial
                    // line. Safe legacy starts have only the shell EXEC frame.
                    retro_system_av_info settled = {};
                    core.get_system_av_info(&settled);
                    const size_t live_size = core.resume_size();
                    if (!live_size) return;
                    std::vector<unsigned char> live(live_size);
                    if (!core.resume_save(live.data(), live.size())) return;
                    unsigned live_frames = 0;
                    if (live.size() >= 8) memcpy(&live_frames, live.data() + 4, 4);
                    if (live_frames > 1)
                        error = "This older DOS state lacks resume data for the waiting DOS function. Loading it here would lose data.";
                }
                swallow_writes = false;
                const bool okay = error.empty() && core.unserialize(in.at, (size_t)size);
                if (!okay && error.empty() && (state->state_error == 3 || state->state_error == 4) && state_loading)
                {
                    // DOS or the program is not up yet after all.
                    std::lock_guard<std::mutex> guard(state_lock);
                    if (generation == state_generation) swallow_writes = true;
                    return;
                }
                if (!okay && error.empty()) error = StateErrorText(state, true);
                if (okay && version < 3 && !core.resume_legacy_safe())
                    error = "This older DOS state lacks resume data for the waiting DOS function. Loading it here would lose data.";
                if (okay && resume_size && core.resume_load(resume_at, (size_t)resume_size, true) != 1)
                    error = "Could not restore the native DOS resume state.";
                if (okay && error.empty())
                {
                    if (state->state_warnings)
                        Logf("DOSVM state warnings=%u%s%s%s", state->state_warnings, state->state_warnings & 1 ? " drives" : "",
                             state->state_warnings & 2 ? " devices" : "", state->state_warnings & 4 ? " program" : "");
                    {
                        std::lock_guard<std::mutex> guard(lock);
                        pointer_x = (int)x;
                        pointer_y = (int)y;
                        mouse_buttons = buttons;
                        mouse_dx = mouse_dy = 0;
                        keys.clear();
                        memset(held, 0, sizeof held);
                        ring_head = ring_count = 0;
                        // What the machine that started said about its text
                        // screen is not true of this one; its first frame says it anew.
                        text_columns = text_rows = 0;
                        cursor_height = 0;
                        if (has_picture)
                        {
                            const int slot = newest < 0 ? 0 : 1 - newest;
                            back[slot].resize((size_t)(w * h));
                            memcpy(back[slot].data(), picture_at, (size_t)(w * h * 4));
                            width = (unsigned)w;
                            height = (unsigned)h;
                            newest = slot;
                            ++serial;
                        }
                    }
                    {
                        // The synthesizer as the program had set it: every
                        // channel's instrument and controllers, then the
                        // notes that sounded.
                        std::lock_guard<std::mutex> guard(midi_lock);
                        synth.Reset();
                        memcpy(midi_setup, setup, sizeof midi_setup);
                        memcpy(midi_notes, notes, sizeof midi_notes);
                        midi_length = midi_expected = 0;
                        midi_status = 0;
                        for (unsigned channel = 0; channel < 16; ++channel)
                        {
                            for (unsigned controller = 0; controller < 128; ++controller)
                                if (setup[channel][controller] >= 0)
                                    synth.ShortMessage(0xb0 | channel | controller << 8 | (unsigned)setup[channel][controller] << 16);
                            if (setup[channel][128] >= 0) synth.ShortMessage(0xc0 | channel | (unsigned)setup[channel][128] << 8);
                            if (setup[channel][129] >= 0)
                                synth.ShortMessage(0xe0 | channel | ((unsigned)setup[channel][129] & 0x7f) << 8 |
                                                   ((unsigned)setup[channel][129] >> 7 & 0x7f) << 16);
                            for (unsigned note = 0; note < 128; ++note)
                                if (notes[channel][note])
                                {
                                    synth.ShortMessage(0x90 | channel | note << 8 | (unsigned)notes[channel][note] << 16);
                                    ++midi_messages;
                                }
                        }
                    }
                    input_clock = clock;
                    memcpy(boot_program, name, sizeof boot_program);
                    {
                        // Bootstrap GPU work belongs to the old core. The
                        // restored core must report its first status even
                        // when the card is off and the default was off too.
                        std::lock_guard<std::mutex> guard(voodoo_lock);
                        voodoo_queue.clear();
                        // VoodooFrame lent the current packet to the GPU
                        // thread. Only its next VoodooFrame call may drop it.
                        voodoo_last = DosVoodooFrame();
                        voodoo_seen = false;
                        voodoo_drawn = voodoo_serial;
                    }
                    Logf("DOSVM state loaded program=%s core=%llu frame=%u", name, size, frames_run);
                    done = true;
                }
            }
        }
        // A host-only fault injection exercises a real five-second timeout
        // after the runner captured the request, before it publishes a hold.
        if (job == 1 && !state_test_delayed)
            if (const char *value = getenv("NEO98_DOS_STATE_TEST_DELAY_MS"))
            {
                const unsigned delay = (unsigned)strtoul(value, nullptr, 10);
                if (delay && delay <= 10000)
                {
                    state_test_delayed = true;
                    Logf("DOSVM state test delays publication %u ms", delay);
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                }
            }
        std::lock_guard<std::mutex> guard(state_lock);
        if (generation != state_generation)
        {
            // Release or a newer request owns this machine now. In
            // particular, never recreate the hold after a timeout.
            if (job == 2) load_bytes.clear();
            Logf("DOSVM state cancelled before publication");
            return;
        }
        state_job = 0;
        state_loading = false;
        swallow_writes = false;
        if (job == 1 && done) state_bytes = std::move(saved_bytes);
        if (job == 2)
        {
            load_bytes.clear();
            load_bytes.shrink_to_fit();
        }
        if (done)
        {
            // The machine stands here until the caller lets it go on.
            state_hold = true;
            state_result = 1;
        }
        else
        {
            Logf("DOSVM state %s refused: %s", job == 1 ? "save" : "load", error.c_str());
            state_error = error;
            state_result = -1;
            // A machine that refused a state may hold half of it: it stands
            // until the host starts it over.
            if (job == 2) state_hold = true;
        }
    }
    // What the archive of the core said, for the person.
    std::string StateErrorText(const Neo98DbpState *state, bool load)
    {
        char text[256];
        switch (state->state_error)
        {
        case 3:
        case 4:
            return load ? "This state was saved while the game was not running and cannot be restored."
                        : "The game is not running: the machine is starting or at the command prompt.";
        case 5:
            return "The state uses a different graphics card. Restore the previous program settings.";
        case 6:
            snprintf(text, sizeof text,
                     "The state uses %u MB of memory; the program is configured for %s MB. Restore the previous program settings.",
                     state->state_error_info < 225 ? state->state_error_info : (state->state_error_info - 223) * 128, memory_option);
            return text;
        case 7:
            return "The state uses a different amount of graphics memory than the current program settings.";
        case 2:
            return "This DOS machine does not support the state's format version.";
        default:
            return load ? "The machine state is damaged." : "The DOS machine could not write its state.";
        }
    }
    std::mutex state_lock;
    uint64_t state_generation = 0; // protected by state_lock, invalidated by Release
    uint64_t load_generation = 0;  // runner-owned load_bytes generation
    bool state_test_delayed = false;
    int state_job = 0;    // 1 save, 2 load
    int state_result = 0; // 1 done, -1 refused
    std::vector<unsigned char> state_bytes;
    // The state on its way in, the runner thread's own.
    std::vector<unsigned char> load_bytes;
    std::string state_error;
    // The machine stands after a state was taken or put in.
    std::atomic<bool> state_hold{false};
    unsigned state_busy_count = 0;
    // A state is on its way into this machine: no picture, no sound, no input.
    std::atomic<bool> state_loading{false};
    // What the program told the synthesizer, per channel: controllers 0 to
    // 127, the instrument, the pitch bend (-1: never set), and the velocity
    // of every note that sounds.
    static constexpr unsigned MidiSetupSize = 130;
    short midi_setup[16][MidiSetupSize];
    unsigned char midi_notes[16][128] = {};
    void MidiForget()
    {
        for (auto &channel : midi_setup)
            for (auto &value : channel) value = -1;
        memset(midi_notes, 0, sizeof midi_notes);
    }
    void MidiNoted(const unsigned char *message, unsigned length)
    {
        const unsigned kind = message[0] & 0xf0, channel = message[0] & 0x0f;
        if (kind == 0x80 && length > 1) midi_notes[channel][message[1] & 0x7f] = 0;
        else if (kind == 0x90 && length > 2) midi_notes[channel][message[1] & 0x7f] = message[2] & 0x7f;
        else if (kind == 0xb0 && length > 2)
        {
            const unsigned controller = message[1] & 0x7f;
            // All sound off, all notes off and the mode messages end the notes.
            if (controller == 120 || controller >= 123) memset(midi_notes[channel], 0, sizeof midi_notes[channel]);
            else midi_setup[channel][controller] = message[2] & 0x7f;
        }
        else if (kind == 0xc0 && length > 1) midi_setup[channel][128] = message[1] & 0x7f;
        else if (kind == 0xe0 && length > 2) midi_setup[channel][129] = short((message[1] & 0x7f) | (message[2] & 0x7f) << 7);
    }

    // A frame of the 3dfx Voodoo with everything its pointers name. The core
    // hands a frame over on the runner thread and takes its memory back when
    // the call returns; the guest draws it later, on its own thread.
    struct VoodooPacket
    {
        DosVoodooFrame frame;
        unsigned long long serial = 0;
        std::vector<DosVoodooCommand> commands;
        std::vector<DosVoodooVertex> vertices;
        std::vector<DosVoodooTexture> textures;
        std::vector<unsigned> texels;
        std::vector<unsigned short> restore[4];
    };
    static void VoodooSink(void *owner, const DosVoodooFrame &frame) { static_cast<Machine *>(owner)->OnVoodoo(frame); }
    void OnVoodoo(const DosVoodooFrame &frame)
    {
        std::unique_lock<std::mutex> guard(voodoo_lock);
        // A frame that says nothing new: the card is as it was and drew nothing.
        const bool same = frame.active == voodoo_last.active && frame.width == voodoo_last.width &&
                          frame.height == voodoo_last.height && frame.shown == voodoo_last.shown &&
                          !memcmp(frame.gamma_exponent, voodoo_last.gamma_exponent, sizeof frame.gamma_exponent) &&
                          !memcmp(frame.gamma_factor, voodoo_last.gamma_factor, sizeof frame.gamma_factor);
        if (voodoo_seen && same && !frame.command_count && !frame.texture_count && !frame.read_mask && !frame.restore_pitch) return;
        voodoo_seen = true;
        voodoo_last = frame;
        voodoo_last.commands = nullptr;
        voodoo_last.vertices = nullptr;
        voodoo_last.textures = nullptr;
        std::unique_ptr<VoodooPacket> packet(new VoodooPacket);
        packet->frame = frame;
        packet->commands.assign(frame.commands, frame.commands + frame.command_count);
        packet->vertices.assign(frame.vertices, frame.vertices + frame.vertex_count);
        packet->textures.assign(frame.textures, frame.textures + frame.texture_count);
        auto texels = [](const DosVoodooTexture &t) {
            size_t count = 0;
            for (unsigned l = 0, w = t.width, h = t.height; l < t.levels; ++l, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1)
                count += (size_t)w * h;
            return count;
        };
        size_t total = 0;
        for (const auto &t : packet->textures) total += texels(t);
        packet->texels.resize(total);
        size_t at = 0;
        for (auto &t : packet->textures)
        {
            const size_t count = texels(t);
            memcpy(packet->texels.data() + at, t.texels, count * 4);
            t.texels = packet->texels.data() + at;
            at += count;
        }
        // The buffers of a saved state, rows of the card's pitch.
        if (frame.restore_pitch)
            for (unsigned i = 0; i < 4; ++i)
            {
                const unsigned short *from = i < 3 ? frame.restore_color[i] : frame.restore_depth;
                if (!from) continue;
                packet->restore[i].assign(from, from + (size_t)frame.restore_pitch * frame.height);
                (i < 3 ? packet->frame.restore_color[i] : packet->frame.restore_depth) = packet->restore[i].data();
            }
        packet->frame.commands = packet->commands.data();
        packet->frame.vertices = packet->vertices.data();
        packet->frame.textures = packet->textures.data();
        const unsigned long long serial = packet->serial = ++voodoo_serial;
        voodoo_queue.push_back(std::move(packet));
        if (frame.read_mask)
        {
            // The program reads what it drew: its thread stands until the
            // guest has drawn this frame and every one before it, and has
            // filled the buffers. The end of the session ends the wait.
            while (voodoo_drawn < serial && !stop)
                voodoo_room.wait_for(guard, std::chrono::milliseconds(20));
            return;
        }
        // The card must not run ahead of the GPU: a third frame waits until
        // the guest has taken one. The end of the session ends the wait.
        while (voodoo_queue.size() > 2 && !stop)
            voodoo_room.wait_for(guard, std::chrono::milliseconds(20));
    }
    bool voodoo_host = false;
    std::mutex voodoo_lock;
    std::condition_variable voodoo_room;
    std::deque<std::unique_ptr<VoodooPacket>> voodoo_queue;
    std::unique_ptr<VoodooPacket> voodoo_taken;
    unsigned long long voodoo_serial = 0, voodoo_drawn = 0;
    DosVoodooFrame voodoo_last;
    bool voodoo_seen = false;
    std::vector<unsigned short> voodoo_pixels[4];
    static constexpr unsigned RingFrames = DosVm::AudioQueueFrames;
    std::thread thread;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    std::mutex lock;
    std::vector<unsigned> back[2], presented;
    int newest = -1;
    unsigned width = 0, height = 0, serial = 0;
    std::vector<short> ring = std::vector<short>(RingFrames * 2);
    unsigned ring_head = 0, ring_count = 0;
    std::vector<KeyEvent> keys;
    // Runner-thread bootstrap history. Input after a program starts is
    // retained only in case it starts another program later. The bound
    // applies to collection as well as to the state reader.
    std::vector<BootKey> boot_keys;
    size_t boot_prefix = 0, boot_cursor = 0;
    bool boot_overflow = false, boot_prefix_overflow = false, boot_decoded = false, boot_rebuild = false;
    unsigned boot_next_frame = 0;
    unsigned long long input_clock = 0;
    char boot_program[9] = {};
    // The keys the machine was told are down, by RETROK code; written and
    // read on the runner thread.
    bool held[RETROK_LAST] = {};
    int mouse_dx = 0, mouse_dy = 0;
    int pointer_x = 0, pointer_y = 0;
    unsigned mouse_buttons = 0;
    double fps = 60;
    unsigned exit_code = 0;
    bool crashed = false;
    std::string crash_text;
    char memory_option[16] = "16";
    char layout_option[8] = "us";
    std::string answer; // the value GET_VARIABLE hands out last
    char program[9] = {};
    retro_keyboard_event_t keyboard = nullptr;
    struct WindowsLaunch { std::string program, arguments, directory; bool wait; };
    std::vector<WindowsLaunch> windows;
    unsigned windows_seen = 0;
    unsigned text_columns = 0, text_rows = 0;
    unsigned short text_cells[80 * 50] = {};
    unsigned cursor[4] = {}, cursor_height = 0;
    std::atomic<bool> paused{false};
    // The runner thread stands between two frames.
    std::atomic<bool> parked{false}, looping{false};
    unsigned frames_run = 0;
    // The options of the launch, "name=value" lines; the MIDI option after
    // ResolveMidi, and the host directory of its soundfont or ROM set.
    std::string options, midi_value;
    char system_directory[1024] = {};
    // MIDI of the machine into the synthesizer of NEO98 (src/sound_synth.h),
    // the "frontend" MIDI of the core: the MPU-401 bytes come in from the DOS
    // thread, the notes go out mixed into the sound of the machine.
    std::mutex midi_lock;
    Sound::Synth synth;
    unsigned char midi_bytes[512] = {};
    unsigned midi_length = 0, midi_expected = 0, midi_status = 0, midi_messages = 0;
    std::vector<short> midi_mix;

    // A soundfont, MT-32 or SC-55 set the PIF names by its DOS path: the core
    // reads it from its system directory, so that becomes the host directory
    // of the file, and the option its name there. The overlay of a drive is
    // tried before the directory below it, the way the union drive reads.
    void ResolveMidi()
    {
        system_directory[0] = 0;
        midi_value.clear();
        std::string value;
        if (!LaunchOption(options, "midi", value) || value.size() < 3 || value[1] != ':') return;
        const char letter = (char)(value[0] & 0x5f);
        std::string rest = value.substr(2);
        for (char &c : rest)
            if (c == '\\') c = '/';
        while (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
        for (unsigned i = 0; i < drive_count; ++i)
        {
            if ((drives[i].letter & 0x5f) != letter) continue;
            for (const char *base : {drives[i].overlay_directory, drives[i].directory})
            {
                if (!base[0]) continue;
                std::string full = base;
                if (full.back() != '/') full += '/';
                full += rest;
                if (FILE *f = fopen(full.c_str(), "rb"))
                {
                    fclose(f);
                    const size_t slash = full.rfind('/');
                    snprintf(system_directory, sizeof system_directory, "%.*s", (int)slash, full.c_str());
                    midi_value = full.substr(slash + 1);
                    Logf("DOSVM midi %s", full.c_str());
                    return;
                }
            }
        }
        Logf("DOSVM midi %s not found, General MIDI instead", value.c_str());
        midi_value = "frontend";
    }
  public:
    // A byte of the MPU-401 UART. Channel messages go to the synthesizer
    // whole, running status included; a system exclusive block as one; real
    // time bytes are dropped.
    void MidiByte(unsigned char b)
    {
        std::lock_guard<std::mutex> guard(midi_lock);
        if (b >= 0xf8) return;
        if (b & 0x80)
        {
            if (midi_status == 0xf0 && b == 0xf7)
            {
                if (midi_length < sizeof midi_bytes) midi_bytes[midi_length++] = b;
                synth.LongMessage(midi_bytes, midi_length);
                // A reset of General MIDI or of a Roland GS device.
                if ((midi_length == 6 && midi_bytes[1] == 0x7e && midi_bytes[3] == 0x09) ||
                    (midi_length == 11 && midi_bytes[1] == 0x41 && midi_bytes[5] == 0x40 && midi_bytes[6] == 0 && midi_bytes[7] == 0x7f))
                    MidiForget();
                ++midi_messages;
                midi_status = 0;
                midi_length = 0;
                return;
            }
            midi_status = b;
            midi_length = 0;
            midi_bytes[midi_length++] = b;
            const unsigned kind = b & 0xf0;
            midi_expected = b == 0xf0 ? ~0u : (kind == 0xc0 || kind == 0xd0 || b == 0xf1 || b == 0xf3) ? 2 : b >= 0xf4 ? 1 : 3;
            if (midi_expected == 1) { midi_status = 0; midi_length = 0; }
            return;
        }
        if (!midi_status) return;
        if (midi_status == 0xf0)
        {
            if (midi_length < sizeof midi_bytes) midi_bytes[midi_length++] = b;
            return;
        }
        if (midi_length == 0) midi_bytes[midi_length++] = (unsigned char)midi_status; // running status
        midi_bytes[midi_length++] = b;
        if (midi_length < midi_expected) return;
        if (midi_status < 0xf0)
        {
            synth.ShortMessage(midi_bytes[0] | (unsigned)midi_bytes[1] << 8 | (midi_length > 2 ? (unsigned)midi_bytes[2] << 16 : 0));
            MidiNoted(midi_bytes, midi_length);
            ++midi_messages;
        }
        midi_length = 0;
    }

  private:
    // The runner thread: keys in, one frame of the machine, paced at the
    // frame rate the core announced last.
    void Loop()
    {
        auto next = std::chrono::steady_clock::now();
        struct Looping
        {
            std::atomic<bool> &flag;
            explicit Looping(std::atomic<bool> &f) : flag(f) { flag = true; }
            ~Looping() { flag = false; }
        } looping_now(looping);
        while (!stop)
        {
            // A state is taken or put in between two frames, also while the
            // machine is paused; afterwards the machine stands until the
            // caller has stopped the rest and copied the disks.
            ServeState();
            if (state_hold)
            {
                parked = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                next = std::chrono::steady_clock::now();
                continue;
            }
            // A load must bootstrap through the caller's pause. A save
            // whose serializer reported a transient DOS/paging call must
            // likewise reach its safe boundary. USER remains frozen; only
            // this existing save request allows the core to advance, and
            // Release's generation/job cancellation removes that allowance.
            const bool pause_requested = paused || machine_pause;
            bool busy_save = false;
            if (pause_requested && state_busy_count)
            {
                std::lock_guard<std::mutex> guard(state_lock);
                busy_save = state_job == 1;
            }
            if (pause_requested && !state_loading && !busy_save)
            {
                // Background off and the window not in front, or the whole
                // machine paused : the machine stands still,
                // the frame clock starts anew afterwards. The time of DOS
                // comes from the frames, so it stands with them.
                parked = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(machine_pause ? 5 : 20));
                next = std::chrono::steady_clock::now();
                continue;
            }
            parked = false;
            std::vector<KeyEvent> pending;
            {
                std::lock_guard<std::mutex> guard(lock);
                pending.swap(keys);
            }
            if (state_loading && boot_decoded && boot_cursor < boot_keys.size() &&
                (boot_cursor < boot_prefix || boot_rebuild) && frames_run >= boot_next_frame &&
                !strcasecmp(boot_keys[boot_cursor].program, core.state()->running_program[0] ? core.state()->running_program : "DOSBOX"))
            {
                const auto frame = boot_keys[boot_cursor].frame;
                do { pending.push_back(boot_keys[boot_cursor++].event); }
                while (boot_cursor < boot_keys.size() && boot_keys[boot_cursor].frame == frame &&
                       (boot_cursor < boot_prefix || boot_rebuild));
                const unsigned long long delay = boot_cursor < boot_keys.size() ? boot_keys[boot_cursor].frame - frame : 2;
                // Compress idle time, preserving presses/releases for at
                // least two frames. A user who waited minutes at a gate
                // does not have to wait those minutes again when loading.
                boot_next_frame = frames_run + (unsigned)(delay < 2 ? 2 : delay > 12 ? 12 : delay);
            }
            for (const auto &event : pending)
            {
                if (!state_loading)
                {
                    if (boot_keys.size() < BootKeyLimit)
                    {
                        BootKey key{input_clock, event};
                        snprintf(key.program, sizeof key.program, "%s", core.state()->running_program[0] ? core.state()->running_program : "DOSBOX");
                        boot_keys.push_back(key);
                    }
                    else boot_overflow = true;
                }
                // The key stays held for the machine until its release is
                // delivered: DOSBox Pure asks the frontend every frame
                // whether a key it saw go down is still down, and releases
                // it at once when the answer is no.
                if (event.key < RETROK_LAST) held[event.key] = event.down;
                if (keyboard) keyboard(event.down, event.key, 0, 0);
            }
            core.run();
            auto *state = core.state();
            ++input_clock;
            if (!state_loading && strcmp(boot_program, state->running_program))
            {
                snprintf(boot_program, sizeof boot_program, "%s", state->running_program);
                boot_prefix = boot_keys.size();
                boot_prefix_overflow = boot_overflow;
            }
            if (++frames_run == 1)
            {
                // The av info before the first frame of a second machine in
                // the same process names no rate; after the frame
                // the core knows its mode.
                retro_system_av_info av = {};
                core.get_system_av_info(&av);
                if (av.timing.fps > 1) fps = av.timing.fps;
                Logf("DOSVM first frame fps=%.3f size=%ux%u", av.timing.fps, av.geometry.base_width, av.geometry.base_height);
            }
            {
                std::lock_guard<std::mutex> guard(lock);
                memcpy(program, state->running_program, sizeof program);
                program[8] = 0;
                text_columns = state->text_columns;
                text_rows = state->text_rows;
                if (text_columns && text_columns * text_rows <= 80 * 50) memcpy(text_cells, state->text_cells, text_columns * text_rows * 2);
                cursor[0] = state->cursor_column;
                cursor[1] = state->cursor_row;
                cursor[2] = state->cursor_start;
                cursor[3] = state->cursor_end;
                cursor_height = state->cursor_height;
            }
            // The DOS thread stands at the frame boundary after retro_run,
            // so the request is complete when it is read here.
            if (state->windows_request != windows_seen)
            {
                windows_seen = state->windows_request;
                state->windows_seen = windows_seen;
                Logf("DOSVM windows program=\"%s\" arguments=\"%s\" directory=\"%s\"", state->windows_program,
                     state->windows_arguments, state->windows_directory);
                std::lock_guard<std::mutex> guard(lock);
                windows.push_back({state->windows_program, state->windows_arguments, state->windows_directory, state->windows_wait});
            }
            if (state->finished)
            {
                exit_code = state->exit_code;
                crashed = state->crashed;
                crash_text = state->crash_message;
                Logf("DOSVM finished code=%u program=%u crashed=%u %s", exit_code,
                     state->program_finished ? 1u : 0u, crashed ? 1u : 0u, crash_text.c_str());
                // What a program that failed left on its text screen says why:
                // the lines of the screen go to the log.
                if (exit_code && state->text_columns && state->text_columns * state->text_rows <= 80 * 50)
                    for (unsigned row = 0; row < state->text_rows; ++row)
                    {
                        char line[81];
                        unsigned length = 0;
                        for (unsigned column = 0; column < state->text_columns && column < 80; ++column)
                        {
                            const unsigned char c = (unsigned char)(state->text_cells[row * state->text_columns + column] & 0xff);
                            line[column] = c >= 32 && c < 127 ? (char)c : ' ';
                            if (line[column] != ' ') length = column + 1;
                        }
                        line[length] = 0;
                        if (length) Logf("DOSVM screen %02u: %s", row, line);
                    }
                finished = true;
                break;
            }
            next += std::chrono::microseconds((long long)(1000000.0 / fps));
            const auto now = std::chrono::steady_clock::now();
            // Behind by more than a frame: do not try to catch up, the
            // machine has its own idea of time.
            if (next < now - std::chrono::milliseconds(50)) next = now;
            std::this_thread::sleep_until(next);
        }
    }

    // --- libretro callbacks ------------------------------------------
  public:
    bool OnEnvironment(unsigned command, void *data)
    {
        auto *self = this;
        switch (command)
        {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return *static_cast<const retro_pixel_format *>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            auto *variable = static_cast<retro_variable *>(data);
            variable->value = nullptr;
            if (!variable->key) return false;
            // For a test that narrows a fault down: NEO98_DOS_TEST_OPTIONS
            // "key=value;key=value" answers before everything else.
            if (const char *test = getenv("NEO98_DOS_TEST_OPTIONS"))
            {
                static std::map<std::string, std::string> answers;
                if (answers.empty())
                {
                    std::string text = test;
                    for (size_t at = 0; at < text.size();)
                    {
                        const size_t end = text.find(';', at), cut = end == std::string::npos ? text.size() : end;
                        const std::string pair = text.substr(at, cut - at);
                        if (const size_t equal = pair.find('='); equal != std::string::npos)
                            answers[pair.substr(0, equal)] = pair.substr(equal + 1);
                        at = cut + 1;
                    }
                }
                if (auto found = answers.find(variable->key); found != answers.end())
                {
                    variable->value = found->second.c_str();
                    return true;
                }
            }
            // Fixed first, then what the PIF's fields decide, then the PIF's
            // own option pages, then the defaults of this host.
            for (const auto &option : fixed_options)
                if (!strcmp(option.key, variable->key))
                {
                    variable->value = option.value;
                    return true;
                }
            // The 3dfx Voodoo: recorded for the host's GPU ("4", the OpenGL
            // renderer of the core, whose record the host takes), or drawn
            // in software on several threads. Never an OpenGL context: the
            // frontend has none.
            if (!strcmp(variable->key, "dosbox_pure_voodoo_perf"))
            {
                variable->value = self && self->voodoo_host ? "4" : "1";
                return true;
            }
            if (self && !strcmp(variable->key, "dosbox_pure_memory_size"))
            {
                variable->value = self->memory_option;
                return true;
            }
            if (self && !strcmp(variable->key, "dosbox_pure_midi") && !self->midi_value.empty())
            {
                variable->value = self->midi_value.c_str();
                return true;
            }
            if (self && LaunchOption(self->options, variable->key, self->answer))
            {
                variable->value = self->answer.c_str();
                return true;
            }
            if (self && !strcmp(variable->key, "dosbox_pure_keyboard_layout"))
            {
                variable->value = self->layout_option;
                return true;
            }
            for (const auto &option : default_options)
                if (!strcmp(option.key, variable->key))
                {
                    variable->value = option.value;
                    return true;
                }
            return false;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool *>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback *>(data)->log = CoreLog;
            return true;
        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
            if (self) self->keyboard = static_cast<const retro_keyboard_callback *>(data)->callback;
            return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        {
            const auto *message = static_cast<const retro_message_ext *>(data);
            Logf("DOSVM message level=%u %s", (unsigned)message->level, message->msg ? message->msg : "");
            return true;
        }
        case RETRO_ENVIRONMENT_SET_MESSAGE:
        {
            const auto *message = static_cast<const retro_message *>(data);
            Logf("DOSVM message %s", message->msg ? message->msg : "");
            return true;
        }
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        {
            const auto *av = static_cast<const retro_system_av_info *>(data);
            if (self && command == RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO && av->timing.fps > 1)
                self->fps = av->timing.fps;
            return true;
        }
        case RETRO_ENVIRONMENT_SHUTDOWN:
            // The core asks the frontend to end; the patched core reports the
            // end through Neo98DbpState instead, so nothing happens here.
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            // Only while a soundfont or ROM set is chosen: the directory of
            // the file (ResolveMidi). Otherwise there is none, and the core
            // scans nothing.
            if (!self || !self->system_directory[0]) return false;
            *static_cast<const char **>(data) = self->system_directory;
            return true;
        case RETRO_ENVIRONMENT_GET_MIDI_INTERFACE:
        {
            auto *midi = static_cast<retro_midi_interface *>(data);
            midi->input_enabled = MidiInputEnabled;
            midi->output_enabled = MidiOutputEnabled;
            midi->read = MidiRead;
            midi->write = slot_callbacks[slot].midi_write;
            midi->flush = MidiFlush;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *static_cast<bool *>(data) = true;
            return true;
        default:
            // Core options, disk control, VFS, performance and hardware
            // rendering: not offered. The core takes its defaults.
            return false;
        }
    }
    static void CoreLog(retro_log_level level, const char *format, ...)
    {
        char line[1024];
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(line, sizeof line, format, arguments);
        va_end(arguments);
        for (char *p = line; *p; ++p)
            if (*p == '\n' || *p == '\r') *p = ' ';
        Logf("DOSVM core level=%u %s", (unsigned)level, line);
    }
    void OnVideo(const void *data, unsigned w, unsigned h, size_t pitch)
    {
        auto *self = this;
        if (!self || !data || !w || !h || data == RETRO_HW_FRAME_BUFFER_VALID) return;
        // The pictures of a machine that starts only to take a state are not shown.
        if (self->state_loading) return;
        std::lock_guard<std::mutex> guard(self->lock);
        const auto *rows = static_cast<const unsigned char *>(data);
        // The core hands over every frame, also when nothing moved; a
        // picture like the last one is no new picture, so a DOS box that
        // waits at its prompt costs the desktop nothing but its cursor.
        if (self->newest >= 0 && self->width == w && self->height == h)
        {
            const auto &last = self->back[self->newest];
            bool same = true;
            for (unsigned y = 0; y < h && same; ++y)
                same = !memcmp(last.data() + (size_t)y * w, rows + y * pitch, (size_t)w * 4);
            if (same) return;
        }
        const int slot = self->newest < 0 ? 0 : 1 - self->newest;
        auto &buffer = self->back[slot];
        buffer.resize((size_t)w * h);
        for (unsigned y = 0; y < h; ++y)
            memcpy(buffer.data() + (size_t)y * w, rows + y * pitch, (size_t)w * 4);
        self->width = w;
        self->height = h;
        self->newest = slot;
        ++self->serial;
    }
    static bool MidiInputEnabled() { return false; }
    static bool MidiOutputEnabled() { return true; }
    static bool MidiRead(uint8_t *) { return false; }
    static bool MidiFlush() { return true; }
    static void AudioSample(int16_t, int16_t) {}
    size_t OnAudioBatch(const int16_t *data, size_t frames)
    {
        auto *self = this;
        if (!self || self->state_loading) return frames;
        // The synthesizer plays along with the frames of the machine, so its
        // notes keep the timing the MPU-401 bytes had.
        auto &mix = self->midi_mix;
        if (mix.size() < frames * 2) mix.resize(frames * 2);
        bool midi = false;
        {
            std::lock_guard<std::mutex> guard(self->midi_lock);
            if (self->midi_messages || self->synth.Sounding())
            {
                self->synth.Render(mix.data(), (unsigned)frames);
                midi = true;
            }
        }
        std::lock_guard<std::mutex> guard(self->lock);
        for (size_t i = 0; i < frames; ++i)
        {
            // Keep the newest sound when the Windows thread cannot drain.
            // Keeping the old head instead leaves a permanent audio delay
            // once the producer and consumer return to the same rate.
            if (self->ring_count == RingFrames)
            {
                self->ring_head = (self->ring_head + 1) % RingFrames;
                --self->ring_count;
            }
            const unsigned tail = (self->ring_head + self->ring_count) % RingFrames;
            int left = data[i * 2], right = data[i * 2 + 1];
            if (midi)
            {
                left += mix[i * 2];
                right += mix[i * 2 + 1];
                left = left < -32768 ? -32768 : left > 32767 ? 32767 : left;
                right = right < -32768 ? -32768 : right > 32767 ? 32767 : right;
            }
            self->ring[tail * 2] = (short)left;
            self->ring[tail * 2 + 1] = (short)right;
            ++self->ring_count;
        }
        return frames;
    }
    static void InputPoll() {}
    int16_t OnInputState(unsigned port, unsigned device, unsigned, unsigned id)
    {
        auto *self = this;
        if (!self || port) return 0;
        // A key is down from its press to its release, as on the PC's
        // keyboard: make code, typematic, break code. The core's check for
        // "sticky" keys asks here every frame. Runner thread only.
        if (device == RETRO_DEVICE_KEYBOARD) return id < RETROK_LAST && held[id] ? 1 : 0;
        if (device == RETRO_DEVICE_POINTER)
        {
            // The place of the pointer: with "direct" mouse input the DOS
            // driver puts its cursor there.
            std::lock_guard<std::mutex> guard(self->lock);
            switch (id)
            {
            case RETRO_DEVICE_ID_POINTER_X: return (int16_t)self->pointer_x;
            case RETRO_DEVICE_ID_POINTER_Y: return (int16_t)self->pointer_y;
            default: return 0;
            }
        }
        if (device == RETRO_DEVICE_MOUSE)
        {
            std::lock_guard<std::mutex> guard(self->lock);
            switch (id)
            {
            case RETRO_DEVICE_ID_MOUSE_X:
            {
                const int dx = self->mouse_dx;
                self->mouse_dx = 0;
                return (int16_t)dx;
            }
            case RETRO_DEVICE_ID_MOUSE_Y:
            {
                const int dy = self->mouse_dy;
                self->mouse_dy = 0;
                return (int16_t)dy;
            }
            case RETRO_DEVICE_ID_MOUSE_LEFT: return (self->mouse_buttons & 1) ? 1 : 0;
            case RETRO_DEVICE_ID_MOUSE_RIGHT: return (self->mouse_buttons & 2) ? 1 : 0;
            case RETRO_DEVICE_ID_MOUSE_MIDDLE: return (self->mouse_buttons & 4) ? 1 : 0;
            default: return 0;
            }
        }
        return 0;
    }
};
Machine machines[MaxSessions];

// The callbacks of slot N: the core calls them without a context, so each
// slot has its own set that knows its Machine.
template <unsigned N> struct Slot
{
    static bool Environment(unsigned command, void *data) { return machines[N].OnEnvironment(command, data); }
    static void Video(const void *data, unsigned w, unsigned h, size_t pitch) { machines[N].OnVideo(data, w, h, pitch); }
    static size_t Audio(const int16_t *data, size_t frames) { return machines[N].OnAudioBatch(data, frames); }
    static int16_t Input(unsigned port, unsigned device, unsigned index, unsigned id)
    {
        return machines[N].OnInputState(port, device, index, id);
    }
    static bool MidiWrite(uint8_t byte, uint32_t)
    {
        machines[N].MidiByte(byte);
        return true;
    }
    static constexpr SlotCallbacks callbacks = {Environment, Video, Audio, Input, MidiWrite};
};
const SlotCallbacks slot_callbacks[MaxSessions] = {Slot<0>::callbacks, Slot<1>::callbacks, Slot<2>::callbacks, Slot<3>::callbacks,
                                                   Slot<4>::callbacks, Slot<5>::callbacks, Slot<6>::callbacks, Slot<7>::callbacks};
std::mutex slots_lock;
Neo98DbpDrive configured_drives[Neo98DbpState::DriveCapacity];
unsigned configured_drive_count = 0;

// A drive the Windows side keeps (HostDosVm::Drive::volume): the calls of the
// DOS drive in the core (tools/dosbox_pure_files.inc) land here, on the DOS
// thread, and go to the provider. DOS knows a file by its 8.3 name, which is
// the alias of the volume or, for a name that is one already, the name.
struct KeptDrive
{
    Volume::Backend *volume = nullptr;
    char letter = 0;
    Neo98DbpFiles files;
};
KeptDrive kept_drives[26];
std::string KeptPath(void *owner, const char *path)
{
    return std::string(1, static_cast<KeptDrive *>(owner)->letter) + ":\\" + (path ? path : "");
}
Volume::Backend *KeptVolume(void *owner) { return static_cast<KeptDrive *>(owner)->volume; }
bool KeptEntry(const Volume::Entry &entry, Neo98DbpEntry &out)
{
    out = Neo98DbpEntry();
    const char *name = entry.alias[0] ? entry.alias : entry.name;
    if (strlen(name) > 12) return false;
    for (unsigned i = 0; name[i]; ++i) out.name[i] = char(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
    out.attributes = entry.attributes & 0x3f;
    out.size = entry.size;
    out.date = entry.times[2].date;
    out.time = entry.times[2].time;
    return true;
}
bool KeptFind(void *owner, const char *path, Neo98DbpEntry &out)
{
    Volume::Entry entry;
    return KeptVolume(owner)->Find(KeptPath(owner, path).c_str(), entry) && KeptEntry(entry, out);
}
bool KeptChild(void *owner, const char *directory, unsigned index, Neo98DbpEntry &out)
{
    Volume::Entry entry;
    if (!KeptVolume(owner)->Child(KeptPath(owner, directory).c_str(), index, entry)) return false;
    // A name DOS cannot spell is passed over: the caller skips an empty one.
    if (!KeptEntry(entry, out)) out = Neo98DbpEntry();
    return true;
}
unsigned KeptRead(void *owner, const char *path, unsigned offset, void *buffer, unsigned size, unsigned &done)
{
    return KeptVolume(owner)->Read(KeptPath(owner, path).c_str(), offset, buffer, size, done);
}
unsigned KeptWrite(void *owner, const char *path, unsigned offset, const void *buffer, unsigned size, unsigned &done)
{
    // A machine that starts only to take a state: the disks are the state's
    // already, and what the program writes on its way up never happened.
    if (swallow_writes)
    {
        done = size;
        return 0;
    }
    return KeptVolume(owner)->Write(KeptPath(owner, path).c_str(), offset, buffer, size, done);
}
unsigned KeptResize(void *owner, const char *path, unsigned size)
{
    if (swallow_writes) return 0;
    return KeptVolume(owner)->Resize(KeptPath(owner, path).c_str(), size);
}
unsigned KeptCreate(void *owner, const char *path, unsigned attributes, bool directory)
{
    if (swallow_writes) return 0;
    // The clock of the DOS machine is the clock of this Mac.
    Volume::Entry entry;
    const time_t now = time(nullptr);
    struct tm parts = {};
    localtime_r(&now, &parts);
    const unsigned date = unsigned(((parts.tm_year < 80 ? 0 : parts.tm_year - 80) << 9) | ((parts.tm_mon + 1) << 5) | parts.tm_mday);
    const unsigned clock = unsigned((parts.tm_hour << 11) | (parts.tm_min << 5) | (parts.tm_sec / 2));
    entry.times[0] = Files::TimeStamp{date, clock, 0};
    entry.times[1] = Files::TimeStamp{date, 0, 0};
    entry.times[2] = Files::TimeStamp{date, clock, 0};
    return KeptVolume(owner)->Create(KeptPath(owner, path).c_str(), attributes, directory, entry);
}
unsigned KeptRemove(void *owner, const char *path, bool directory)
{
    if (swallow_writes) return 0;
    return KeptVolume(owner)->Remove(KeptPath(owner, path).c_str(), directory, false);
}
unsigned KeptRename(void *owner, const char *from, const char *to)
{
    if (swallow_writes) return 0;
    return KeptVolume(owner)->Rename(KeptPath(owner, from).c_str(), KeptPath(owner, to).c_str());
}
unsigned KeptSetTime(void *owner, const char *path, unsigned date, unsigned clock)
{
    if (swallow_writes) return 0;
    Volume::Entry entry;
    const std::string full = KeptPath(owner, path);
    if (!KeptVolume(owner)->Find(full.c_str(), entry)) return 2;
    entry.times[2] = Files::TimeStamp{date, clock, 0};
    if (!entry.times[1].date) entry.times[1] = Files::TimeStamp{date, 0, 0};
    return KeptVolume(owner)->SetTimes(full.c_str(), entry.times);
}
void KeptRetain(void *owner, const char *path) { KeptVolume(owner)->Retain(KeptPath(owner, path).c_str()); }
void KeptRelease(void *owner, const char *path) { KeptVolume(owner)->Release(KeptPath(owner, path).c_str()); }
void KeptSpace(void *owner, unsigned long long &total, unsigned long long &available)
{
    KeptVolume(owner)->Space(total, available);
}

DosVm::Session *ProviderStart(void *, const DosVm::Launch &launch, char *error, unsigned capacity)
{
    Machine *free_machine = nullptr;
    {
        std::lock_guard<std::mutex> guard(slots_lock);
        for (auto &m : machines)
            if (!m.in_use)
            {
                free_machine = &m;
                m.slot = unsigned(&m - machines);
                m.drive_count = configured_drive_count;
                for (unsigned i = 0; i < m.drive_count; ++i) m.drives[i] = configured_drives[i];
                m.in_use = true;
                break;
            }
    }
    if (!free_machine)
    {
        snprintf(error, capacity, "%u MS-DOS sessions run already", MaxSessions);
        return nullptr;
    }
    if (!LoadDosCore(free_machine->slot, free_machine->core, error, capacity) || !free_machine->Start(launch, error, capacity))
    {
        free_machine->ReleaseAllSerial();
        free_machine->in_use = false;
        return nullptr;
    }
    return reinterpret_cast<DosVm::Session *>(free_machine);
}
void ProviderProgram(void *, DosVm::Session *session, char *name, unsigned capacity)
{
    reinterpret_cast<Machine *>(session)->Program(name, capacity);
}
void ProviderStop(void *, DosVm::Session *session)
{
    auto *m = reinterpret_cast<Machine *>(session);
    if (!m || !m->in_use) return;
    m->Stop();
    m->CloseMicrophone();
    std::lock_guard<std::mutex> guard(slots_lock);
    m->ReleaseAllSerial();
    m->in_use = false;
}
bool ProviderNewFrame(void *, DosVm::Session *session, unsigned seen, DosVm::Frame &out)
{
    return reinterpret_cast<Machine *>(session)->NewFrame(seen, out);
}
void ProviderKey(void *, DosVm::Session *session, unsigned vk, unsigned scan, bool extended, bool down)
{
    reinterpret_cast<Machine *>(session)->Key(vk, scan, extended, down);
}
void ProviderMouse(void *, DosVm::Session *session, int dx, int dy, unsigned x, unsigned y, unsigned buttons)
{
    reinterpret_cast<Machine *>(session)->Mouse(dx, dy, x, y, buttons);
}
unsigned ProviderAudio(void *, DosVm::Session *session, short *out, unsigned frames)
{
    return reinterpret_cast<Machine *>(session)->Audio(out, frames);
}
bool ProviderFinished(void *, DosVm::Session *session, unsigned &code, bool &crashed, char *message,
                      unsigned capacity)
{
    return reinterpret_cast<Machine *>(session)->Finished(code, crashed, message, capacity);
}
bool ProviderWindowsProgram(void *, DosVm::Session *session, char *program, unsigned pc, char *arguments, unsigned ac,
                            char *directory, unsigned dc, bool &wait)
{
    return reinterpret_cast<Machine *>(session)->WindowsProgram(program, pc, arguments, ac, directory, dc, wait);
}
void ProviderWindowsProgramDone(void *, DosVm::Session *session, unsigned exit_code)
{
    reinterpret_cast<Machine *>(session)->WindowsProgramDone(exit_code);
}
void ProviderPasteText(void *, DosVm::Session *session, const char *text) { reinterpret_cast<Machine *>(session)->PasteText(text); }
bool ProviderScreenText(void *, DosVm::Session *session, unsigned &columns, unsigned &rows, unsigned short *cells, unsigned capacity)
{
    return reinterpret_cast<Machine *>(session)->ScreenText(columns, rows, cells, capacity);
}
void ProviderPause(void *, DosVm::Session *session, bool on) { reinterpret_cast<Machine *>(session)->Pause(on); }
bool ProviderVoodooFrame(void *, DosVm::Session *session, DosVoodooFrame &out)
{
    return reinterpret_cast<Machine *>(session)->VoodooFrame(out);
}
DosVoodooReadback *ProviderVoodooReadback(void *, DosVm::Session *session)
{
    return reinterpret_cast<Machine *>(session)->VoodooReadback();
}
void ProviderVoodooDrawn(void *, DosVm::Session *session)
{
    reinterpret_cast<Machine *>(session)->VoodooDrawn();
}
bool ProviderTextCursor(void *, DosVm::Session *session, unsigned &column, unsigned &row, unsigned &start, unsigned &end,
                        unsigned &height)
{
    return reinterpret_cast<Machine *>(session)->TextCursor(column, row, start, end, height);
}
} // namespace

void Install(Logger log, const Drive *drives, unsigned count)
{
    logger = log;
    {
        std::lock_guard<std::mutex> guard(slots_lock);
        configured_drive_count = 0;
        for (unsigned i = 0; i < count && configured_drive_count < Neo98DbpState::DriveCapacity; ++i)
        {
            if ((!drives[i].directory && !drives[i].volume) || !drives[i].letter) continue;
            auto &d = configured_drives[configured_drive_count++];
            d = Neo98DbpDrive();
            d.letter = drives[i].letter;
            if (const unsigned index = unsigned((d.letter & 0x5f) - 'A'); drives[i].volume && index < 26)
            {
                // A machine that runs keeps the pointer to this record; the
                // provider behind a letter stays the same for the process.
                auto &kept = kept_drives[index];
                kept.volume = drives[i].volume;
                kept.letter = char('A' + index);
                kept.files.owner = &kept;
                kept.files.find = KeptFind;
                kept.files.child = KeptChild;
                kept.files.read = KeptRead;
                kept.files.write = KeptWrite;
                kept.files.resize = KeptResize;
                kept.files.create = KeptCreate;
                kept.files.remove = KeptRemove;
                kept.files.rename = KeptRename;
                kept.files.set_time = KeptSetTime;
                kept.files.retain = KeptRetain;
                kept.files.release = KeptRelease;
                kept.files.space = KeptSpace;
                snprintf(kept.files.label, sizeof kept.files.label, "%s", drives[i].volume->Label());
                d.files = &kept.files;
            }
            snprintf(d.directory, sizeof d.directory, "%s", drives[i].directory ? drives[i].directory : "");
            d.memory_overlay = drives[i].memory_overlay;
            if (drives[i].overlay) snprintf(d.overlay_directory, sizeof d.overlay_directory, "%s", drives[i].overlay);
        }
    }
    DosVm::Provider provider;
    provider.owner = machines;
    provider.Start = ProviderStart;
    provider.Stop = ProviderStop;
    provider.Microphone = [](void *, DosVm::Session *session, unsigned long long now) {
        reinterpret_cast<Machine *>(session)->ServiceMicrophone(now);
    };
    provider.NewFrame = ProviderNewFrame;
    provider.Key = ProviderKey;
    provider.Mouse = ProviderMouse;
    provider.Audio = ProviderAudio;
    provider.Program = ProviderProgram;
    provider.Finished = ProviderFinished;
    provider.WindowsProgram = ProviderWindowsProgram;
    provider.WindowsProgramDone = ProviderWindowsProgramDone;
    provider.PasteText = ProviderPasteText;
    provider.ScreenText = ProviderScreenText;
    provider.Pause = ProviderPause;
    provider.TextCursor = ProviderTextCursor;
    provider.VoodooFrame = ProviderVoodooFrame;
    provider.VoodooReadback = ProviderVoodooReadback;
    provider.VoodooDrawn = ProviderVoodooDrawn;
    DosVm::SetProvider(&provider);
}
void Uninstall()
{
    for (auto &m : machines)
        if (m.in_use)
        {
            m.Stop();
            m.ReleaseAllSerial();
            m.in_use = false;
        }
    DosVm::SetProvider(nullptr);
}
bool Busy()
{
    for (const auto &m : machines)
        if (m.in_use) return true;
    return false;
}
unsigned AudioQueued(unsigned *count)
{
    unsigned frames = 0, running = 0;
    std::lock_guard<std::mutex> guard(slots_lock);
    for (auto &m : machines)
        if (m.in_use)
        {
            frames += m.AudioQueued();
            ++running;
        }
    if (count) *count = running;
    return frames;
}
namespace
{
// The one machine a state is about; null when none or several run.
Machine *StateMachine(std::string *reason)
{
    Machine *found = nullptr;
    unsigned count = 0;
    std::lock_guard<std::mutex> guard(slots_lock);
    for (auto &m : machines)
        if (m.in_use)
        {
            found = &m;
            ++count;
        }
    if (reason && !count) *reason = "No DOS program is running.";
    if (reason && count > 1) *reason = "Multiple DOS programs are running. A state can capture only one.";
    return count == 1 ? found : nullptr;
}
} // namespace
std::string StateEngine()
{
    const char *core = getenv("NEO98_DOS_CORE_ID");
    return "dos-" + std::to_string(StateFormat) + "-" + (core && *core ? core : NEO98_DOS_CORE_ID);
}
bool StateReady(std::string &reason, std::vector<std::pair<std::string, std::string>> *machine)
{
    Machine *m = StateMachine(&reason);
    if (!m || !m->StateReady(reason)) return false;
    if (machine) m->StateMachine(*machine);
    return true;
}
void StateSave()
{
    if (Machine *m = StateMachine(nullptr)) m->StateAsk(false, {});
}
void StateLoad(std::vector<unsigned char> bytes)
{
    if (Machine *m = StateMachine(nullptr))
    {
        m->StateAsk(true, std::move(bytes));
        return;
    }
    std::lock_guard<std::mutex> guard(awaited_lock);
    awaited_state = std::move(bytes);
    state_awaited = true;
    swallow_writes = true;
}
int StatePoll(std::vector<unsigned char> *bytes, std::string &error)
{
    {
        std::lock_guard<std::mutex> guard(awaited_lock);
        if (state_awaited) return 0;
    }
    Machine *m = StateMachine(&error);
    return m ? m->StatePoll(bytes, error) : -1;
}
void StateRelease()
{
    {
        // A state nobody took: the drives are the machine's again.
        std::lock_guard<std::mutex> guard(awaited_lock);
        if (state_awaited)
        {
            state_awaited = false;
            awaited_state.clear();
            swallow_writes = false;
        }
    }
    std::lock_guard<std::mutex> guard(slots_lock);
    bool active = false;
    for (auto &m : machines)
        if (m.in_use) { active = true; m.StateRelease(); }
    // A startup failure can consume awaited_state before ProviderStart
    // returns a session. There is then no live machine whose Stop clears it.
    if (!active) swallow_writes = false;
}
bool StatePicture(std::vector<unsigned> &pixels, unsigned &width, unsigned &height)
{
    Machine *m = StateMachine(nullptr);
    return m && m->StatePicture(pixels, width, height);
}
void Pause(bool on)
{
    machine_pause = on;
    if (!on) return;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    for (;;)
    {
        bool running = false;
        {
            std::lock_guard<std::mutex> guard(slots_lock);
            for (const auto &m : machines)
                if (!m.Standing()) running = true;
        }
        if (!running || std::chrono::steady_clock::now() >= give_up) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
} // namespace HostDosVm
