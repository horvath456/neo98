#include "host_windows_state_data.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// Interactive host runner: the same native USER/GDI/kernel code and Unicorn
// x86 execution as the Pi kernel and the probe runner, presented in an SDL2
// window on the development machine. Mouse and keyboard events enter through
// the same Application::MouseButtons / RawKey packets the USB path in
// src/app_ui.cpp produces, so the guest cannot tell this apart from the Pi.
// This is a display and input
// adapter around the existing runtime.
#define private public
#include "../src/x86_app.h"
#include "../src/arm64_machine.h"
#undef private
#include "../src/usb_keys.h"
#include "../sdk/lib/neo98_touch_abi.h"
#include "crash_report_files.h"
#include "guest_screen_scale.h"
#include "host_audio.h"
#include "host_menu.h"
#include "host_pasteboard.h"
#include "../src/host_clipboard.h"
#include "host_gpu.h"
#include "../src/gpu_backend.h"
#include "host_net.h"
#include "host_comm.h"
#include "host_dos_vm.h"
#include "host_pacing.h"
#include "host_hang.h"
#include "host_mouse_buttons.h"
#include "host_wheel.h"
#include "host_settings.h"
#include "host_setup.h"
#include "host_single.h"
#include "host_game_machine.h"
#include "host_savestate.h"
#include "host_machine_state.h"
#include "host_state_window.h"
#include "host_game_menu.h"
#include "host_exe_icon.h"
#include "host_seamless.h"
#include "host_dock.h"
#include "host_state.h"
#include "host_pause_mark.h"
#include "host_devices/service.h"
#ifdef NEO98_WITH_HOST_MEDIA
#include "host_media/service.h"
#include "host_media/seed.h"
#include "host_media/capture.h"
#endif
#include <sys/resource.h>
#include "../src/render_profile.h"
#include <unordered_map>
#include <functional>
#include "host_volume.h"
#include "registry_file.h"
#include "host_registry.h"
#include "printer_port_host.h"
#include "system_tree_host.h"
#include "../src/win98_wininit.h"
#include "../src/profile_files.h"
#include <SDL.h>
#include <algorithm>
#include <csignal>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || (unsigned long long)n > 0xffffffffull) { fclose(f); return {}; }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    fclose(f);
    return b;
}
static const char *leaf(const char *s)
{
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static bool quiet = false;
static void logger(const char *s)
{
    if (quiet && !strncmp(s, "APP ", 4)) return;
    puts(s);
}
// tools/import_start_menu.py writes "guest path<TAB>host path" lines; a
// directory is marked with "-" as its source. Same format the probe reads.
static bool seed_manifest(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;
    char line[1024];
    while (fgets(line, sizeof line, file)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        char *source = tab + 1;
        for (char *p = source; *p; ++p) if (*p == '\n' || *p == '\r') { *p = 0; break; }
        if (!line[0] || !source[0]) continue;
        if (!strcmp(source, "-")) {
            const unsigned error = Files::System().MakeDirectory(line);
            if (error && error != 183) { fclose(file); return false; }
            continue;
        }
        auto data = load(source);
        if (data.empty() || Files::System().Seed(line, data.data(), data.size())) { fclose(file); return false; }
    }
    fclose(file);
    return true;
}
// The picture the window shows and its size. USER's host frame has the size
// of the desktop mode (User::PixelsSize); the text view of a console is
// 800x600, and a display in power saving is black in the frame's size.
static const unsigned char *frame_pixels(X86::Application *app, int &width, int &height, bool *text = nullptr)
{
    User::PixelsSize(width, height);
    const auto *console = app ? reinterpret_cast<const unsigned char *>(app->ConsolePixels()) : nullptr;
    if (text) *text = console && !User::DisplayPower();
    if (console) {
        if (!User::DisplayPower()) { width = 800; height = 600; }
        return console;
    }
    return User::Pixels();
}
static bool screenshot(const char *path,X86::Application *app)
{
    int width = 800, height = 600;
    const auto *pixels = frame_pixels(app, width, height);
    if (!pixels) return false;
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (unsigned i = 0; i < unsigned(width * height); ++i) {
        unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        fwrite(rgb, 1, 3, file);
    }
    return fclose(file) == 0;
}
// SDL scancodes use the USB HID usage numbers for every key the Pi's boot
// keyboard reports, so the kernel's own usage table applies unchanged.
// With the German layout the right Option key is AltGr. A PC keyboard sends
// CONTROL with it for such a layout, so the key state holds CONTROL and ALT
// and KEYBOARD.DRV translates with the AltGr state (Q gives @, E the euro).
static unsigned modifier_mask(bool altgr)
{
    const SDL_Keymod m = SDL_GetModState();
    unsigned mask = 0;
    if (m & KMOD_LCTRL) mask |= 1;
    if (m & KMOD_LSHIFT) mask |= 2;
    if (m & KMOD_LALT) mask |= 4;
    if (m & KMOD_LGUI) mask |= 8;
    if (m & KMOD_RCTRL) mask |= 16;
    if (m & KMOD_RSHIFT) mask |= 32;
    if (m & KMOD_RALT) mask |= 64;
    if (m & KMOD_RGUI) mask |= 128;
    if (altgr && (m & KMOD_RALT)) mask |= 1;
    return mask;
}
static unsigned character_for(const SDL_Keysym &k)
{
    if (k.sym == SDLK_RETURN || k.sym == SDLK_KP_ENTER) return 13;
    if (k.sym == SDLK_BACKSPACE) return 8;
    if (k.sym == SDLK_TAB) return 9;
    if (k.sym == SDLK_ESCAPE) return 0;
    if (k.sym < 32 || k.sym > 126) return 0;
    if (k.mod & KMOD_CTRL) return 0;
    unsigned c = (unsigned)k.sym;
    const bool shift = (k.mod & KMOD_SHIFT) != 0, caps = (k.mod & KMOD_CAPS) != 0;
    if (c >= 'a' && c <= 'z') { if (shift != caps) c -= 32; return c; }
    if (shift) {
        static const char *from = "1234567890-=[];'\\,./`", *to_us = "!@#$%^&*()_+{}:\"|<>?~",
                          *to_de = "!\"§$%&/()=?`{}:'|;:_~";
        const char *to = getenv("NEO98_KEYMAP") && !strcmp(getenv("NEO98_KEYMAP"), "US") ? to_us : to_de;
        if (const char *p = strchr(from, (int)c)) return (unsigned char)to[p - from];
    }
    return c;
}

// SIGUSR1 (or F11) prints every thread of every process: block code, wait
// handle, EIP, the API gate it rests in. This is how a hang gets diagnosed
// without a debugger: `kill -USR1 <pid>` and read the log.
static volatile sig_atomic_t dump_requested = 0;
static void on_dump_signal(int) { dump_requested = 1; }
// SIGUSR2 writes a crash report without rebooting, for scripts that cannot
// press Cmd+Shift+B. Same report, same CRASHREPORT line.
static volatile sig_atomic_t report_requested = 0;
static void on_report_signal(int) { report_requested = 1; }
// SIGHUP ends the host as Cmd+Q does. SDL turns SIGINT and SIGTERM into
// SDL_QUIT itself. The orderly end matters: it takes the machine of the ARM64
// processes down before the process goes.
static volatile sig_atomic_t quit_requested = 0;
static void on_quit_signal(int) { quit_requested = 1; }
// ExitWindowsEx: USER agreed the end of session with every top-level window
// and now needs the machine to go away. Leaving it is the host's job, and the
// host already has exactly one way to do that, the restart path of the reboot
// control. The hook only records the request: it runs inside the guest call,
// where nothing may be torn down, so the main loop acts on it afterwards.
static bool session_end_requested = false;
static unsigned session_end_flags = 0, session_end_code = 0;
// EWX_SHUTDOWN (1) or EWX_POWEROFF (8) without EWX_REBOOT (2). A game
// machine also ends when its game logs off (EWX_LOGOFF, 0): there is no
// session to come back to, and bringing the machine up again would start
// the game the person has just left. Only an
// explicit restart starts it again.
static bool session_logoff_ends = false;
static bool session_powers_off(unsigned flags) { return !(flags & 2) && ((flags & 9) || session_logoff_ends); }
static void on_session_end(void *, X86::Application &, unsigned flags, unsigned code)
{
    session_end_flags = flags;
    session_end_code = code;
    session_end_requested = true;
}
static void host_describe_handle(X86::Application &app, unsigned thread, unsigned handle)
{
    static const char *const kinds[] = {"free", "event", "thread", "mutex", "semaphore", "mapping",
                                        "process", "process-thread", "change-notification", "?"};
    auto *entry = app.FindKernelHandle(handle);
    if (!entry) { printf("NEO98 HOST WAIT thread=%u handle=%08x kind=invalid\n", thread, handle); return; }
    const char *state = "?";
    if (entry->kind == 1) state = app.events[entry->object].signaled ? "signaled" : "clear";
    else if (entry->kind == 8) state = app.change_notifications[entry->object].signaled ? "signaled" : "clear";
    else if (entry->kind == 2) state = app.threads[entry->object].done ? "done" : "running";
    printf("NEO98 HOST WAIT thread=%u handle=%08x kind=%s object=%u state=%s\n", thread, handle,
           kinds[entry->kind < 9 ? entry->kind : 9], entry->object, state);
}
static void host_dump_process(X86::Application &app, const char *label)
{
    printf("NEO98 HOST STALL %s pid=%u state=%u calls=%u current=%u depth=%u\n", label, app.ProcessId(),
           (unsigned)app.Status(), app.ApiCalls(), app.current_thread, app.depth);
    for (unsigned i = 0; i < 16; ++i)
    {
        const auto &t = app.threads[i];
        if (!t.fiber) continue;
        const unsigned eip = app.ThreadEip(i);
        printf("NEO98 HOST THREAD index=%u id=%u block=%u wait=%08x done=%u suspended=%u waiting=%u pending=%08x depth=%u eip=%08x runnable=%u\n",
               i, t.id, t.block, t.wait_handle, t.done ? 1u : 0u, t.suspended, t.waiting ? 1u : 0u, t.pending, t.depth,
               eip, app.Runnable(i) ? 1u : 0u);
        if (eip >= X86::Application::GateBase && eip < X86::Application::GateBase + app.import_count * 16 && !((eip - X86::Application::GateBase) & 15))
            printf("NEO98 HOST GATE index=%u api=%s!%s\n", i, app.imports[(eip - X86::Application::GateBase) / 16].dll,
                   app.imports[(eip - X86::Application::GateBase) / 16].name);
        if (t.wait_handle) host_describe_handle(app, i, t.wait_handle);
        for (unsigned n = 0; n < t.multiwait.count && n < 64; ++n) host_describe_handle(app, i, t.multiwait.objects[n]);
    }
}
static void host_dump_all(X86::Application *a)
{
    host_dump_process(*a, "root");
    for (unsigned slot = 0; slot < X86::Application::ChildCapacity; ++slot)
    {
        bool done = false;
        if (auto *child = a->ChildApplication(slot, done); child && !done) host_dump_process(*child, "child");
    }
    printf("NEO98 HOST STALL windows=%u\n", User::LiveWindows());
    fflush(stdout);
}

// --- Crash reports ----------------------------------------------------------
// Every stop and every manual request lands in its own directory. The host
// prints the path on stderr as one "CRASHREPORT <pfad>" line, so an agent or a
// script finds it without parsing the rest of the log.
static const char *crash_report_dir = "build/crash-reports";
// switched on with --pacing or NEO98_PACING=1; off it costs nothing.
static HostPacing::Observer pacing;
static X86::Application *host_app = nullptr;
static int host_argc = 0;
static char **host_argv = nullptr;
static std::chrono::steady_clock::time_point host_start;
static unsigned hang_ms_now = 0;
static bool hang_now = false;
static const unsigned char *host_pixels(int &width, int &height)
{
    return frame_pixels(host_app, width, height);
}
static bool write_report(X86::Application &app, const char *reason, char *path, unsigned capacity)
{
    // The report is large; one heap allocation keeps it off a 512 KiB stack.
    auto *report = new CrashReport::Report();
    CrashFiles::Describe(*report, reason, "neo98-host", host_argc, host_argv);
    report->uptime_ms = (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - host_start).count();
    report->hang = hang_now ? 1 : 0;
    report->hang_ms = hang_ms_now;
    if (hang_now)
        snprintf(report->hang_text, sizeof report->hang_text,
                 "The guest has made no API calls for %u ms although input is pending "
                 "or a thread is runnable.", hang_ms_now);
    app.CollectCrashReport(*report);
    int width = 800, height = 600;
    const unsigned char *pixels = host_pixels(width, height);
    const bool okay = CrashFiles::Write(*report, crash_report_dir, pixels, width, height, path,
                                        capacity);
    delete report;
    return okay;
}
static void on_crash(void *, X86::Application &app, const char *reason)
{
    char path[1024] = {};
    const auto before = std::chrono::steady_clock::now();
    write_report(app, reason, path, sizeof path);
    pacing.HostWork("crash-report", HostPacing::MillisecondsBetween(before, std::chrono::steady_clock::now()));
}

// --- Window layout ---------------------------------------------------------
// The window shows the guest's canvas and nothing else; restart, report,
// media, network, scale and full screen live in the macOS menu bar
// (tools/host_menu.mm). The canvas has the size of the desktop mode
// (User::PixelsSize: 640x480, 800x600, 1024x768), and the window follows it:
// one point per canvas pixel at scale 1, so on a Retina screen the backing
// store shows each guest pixel as 2x2 device pixels. A canvas that does not
// fit the Mac screen gets the largest window that does, with its aspect
// ratio kept (GuestScreenScale::WindowForCanvas). In a window the canvas
// fills it; in full screen it is centred at the largest size that keeps its
// aspect ratio. Every mouse position goes through this rectangle, so the
// guest always sees canvas pixels whatever the window size.
//
static int canvas_w = 800, canvas_h = 600;
// --desktop: a desktop size of its own; zero for the one the registry names.
static int desktop_w = 0, desktop_h = 0;
// Full screen out of the seamless view shows one window of the guest: the
// canvas is then that window's client area, at this place of the guest's
// screen.
static bool canvas_crop = false;
static int canvas_crop_x = 0, canvas_crop_y = 0;
struct Layout
{
    SDL_Rect guest{0, 0, 800, 600}; // window coordinates
    int output_w = 800, output_h = 600, window_w = 800, window_h = 600;
    int canvas_w = 800, canvas_h = 600;
};
static Layout LayoutFor(SDL_Window *window, SDL_Renderer *renderer)
{
    Layout layout;
    layout.canvas_w = canvas_w;
    layout.canvas_h = canvas_h;
    SDL_GetWindowSize(window, &layout.window_w, &layout.window_h);
    if (SDL_GetRendererOutputSize(renderer, &layout.output_w, &layout.output_h) != 0) {
        layout.output_w = layout.window_w;
        layout.output_h = layout.window_h;
    }
    int w = layout.window_w, h = layout.window_h;
    if (w < 1 || h < 1) { w = canvas_w; h = canvas_h; }
    if ((long long)w * canvas_h > (long long)h * canvas_w) w = (int)((long long)h * canvas_w / canvas_h);
    else h = (int)((long long)w * canvas_h / canvas_w);
    layout.guest = {(layout.window_w - w) / 2, (layout.window_h - h) / 2, w, h};
    return layout;
}
// Window point to canvas pixel; false outside the guest area.
static bool WindowToCanvas(const Layout &layout, int x, int y, int &cx, int &cy)
{
    const auto &g = layout.guest;
    const bool inside = x >= g.x && y >= g.y && x < g.x + g.w && y < g.y + g.h;
    cx = g.w ? (int)((long long)(x - g.x) * layout.canvas_w / g.w) : 0;
    cy = g.h ? (int)((long long)(y - g.y) * layout.canvas_h / g.h) : 0;
    cx = cx < 0 ? 0 : cx > layout.canvas_w - 1 ? layout.canvas_w - 1 : cx;
    cy = cy < 0 ? 0 : cy > layout.canvas_h - 1 ? layout.canvas_h - 1 : cy;
    return inside;
}
static void CanvasToWindow(const Layout &layout, int cx, int cy, int &x, int &y)
{
    const auto &g = layout.guest;
    x = g.x + (int)(((long long)cx * g.w + g.w / 2) / layout.canvas_w);
    y = g.y + (int)(((long long)cy * g.h + g.h / 2) / layout.canvas_h);
}
// The window size for the current canvas at `scale`, fitted to the usable
// area of the display the window is on.
static SDL_Rect UsableArea(SDL_Window *window)
{
    SDL_Rect usable{0, 0, 0, 0};
    const int display = window ? SDL_GetWindowDisplayIndex(window) : 0;
    if (SDL_GetDisplayUsableBounds(display < 0 ? 0 : display, &usable) != 0) usable = {0, 0, 0, 0};
    // The dummy driver of the tests reports a 1024x768 display; a test that
    // wants another screen says so (NEO98_HOST_SCREEN=WxH, in points).
    if (const char *screen = getenv("NEO98_HOST_SCREEN"); screen && *screen)
        sscanf(screen, "%dx%d", &usable.w, &usable.h);
    // The Dock grows and shrinks by a few points as programs come and go
    // (an icon more makes all of them smaller). The seamless desktop has the
    // size of this area, and following every such step changed the guest's
    // screen size, with the whole desktop showing for a moment. A
    // small change keeps the area; a Dock that moves or hides is a large one.
    static SDL_Rect held{0, 0, 0, 0};
    const int slack = 48;
    if (held.w && abs(usable.x - held.x) <= slack && abs(usable.y - held.y) <= slack &&
        abs(usable.w - held.w) <= slack && abs(usable.h - held.h) <= slack)
        return held;
    held = usable;
    return usable;
}
static void WindowSizeFor(SDL_Window *window, int scale, int &width, int &height)
{
    const SDL_Rect usable = UsableArea(window);
    GuestScreenScale::WindowForCanvas(canvas_w, canvas_h, scale, usable.w, usable.h, width, height);
}

// --- Other media while the machine runs ---------------------------------------
// A CD-ROM or a D: chosen in the menu reaches the running guest the way a disc
// reaches Win98: the host mounts the new provider for the letter and the
// runtime announces the change (X86::Application::ChangeMedium). The
// command line keeps the media as well, so a reboot starts with them. These
// helpers edit it.
static std::vector<std::string> host_arguments;
// Where the options begin in it: after EXE and FONT, or right after the
// program name when the host runs as the app (tools/host_settings.h).
static size_t host_first_option = 3;
static void RemoveOption(const char *name, const char *drive_letter = nullptr)
{
    for (size_t i = host_first_option; i + 1 < host_arguments.size();) {
        const auto &value = host_arguments[i + 1];
        const bool match = host_arguments[i] == name &&
                           (!drive_letter || (value.size() > 1 && value[1] == '=' &&
                                              (value[0] | 0x20) == (*drive_letter | 0x20)));
        if (match) host_arguments.erase(host_arguments.begin() + i, host_arguments.begin() + i + 2);
        else ++i;
    }
}
static void SetOption(const char *name, const std::string &value)
{
    RemoveOption(name);
    host_arguments.push_back(name);
    host_arguments.push_back(value);
}
static void RemoveFlag(const char *name)
{
    for (size_t i = host_first_option; i < host_arguments.size();)
        if (host_arguments[i] == name) host_arguments.erase(host_arguments.begin() + i);
        else ++i;
}
// The providers the menu mounted while the machine runs, one per letter. The
// ones of the command line are HostVolume::Mount's own and stay.
static HostVolume::Drive *live_drives[26];
static HostCdImage::Image *live_images[26];
// The folder on this Mac behind each drive letter, links resolved; empty for
// a letter without a drive and for a disc image. A file opened from the
// Finder finds its guest path here (docs/BUILDING.md).
static std::string host_roots[26];
// Explicit drive sources also include images, which have no host folder.
static std::string host_sources[26];
static void ClaimHostDrives()
{
    std::string paths[52];
    for (unsigned i = 0; i < 26; ++i) { paths[i] = host_roots[i]; paths[i + 26] = host_sources[i]; }
    HostDeviceService::Claim(paths, 52);
}
static std::string RealPath(const std::string &path)
{
    char real[PATH_MAX];
    return realpath(path.c_str(), real) ? std::string(real) : path;
}
// The guest path of a file below one of the mounted folders; the longest
// folder wins when one drive lies inside another.
static bool GuestPathOf(const std::string &real, std::string &guest)
{
    int best = -1;
    for (int i = 2; i < 26; ++i) {
        const auto &root = host_roots[i];
        if (root.empty() || real.compare(0, root.size(), root)) continue;
        if (real.size() > root.size() && root != "/" && real[root.size()] != '/') continue;
        if (best < 0 || root.size() > host_roots[best].size()) best = i;
    }
    if (best < 0) return false;
    std::string rest = real.substr(host_roots[best].size());
    while (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
    for (auto &c : rest) if (c == '/') c = '\\';
    guest = std::string(1, char('A' + best)) + ":\\" + HostVolume::ToGuest(rest);
    return true;
}
// The file on this Mac behind a guest path, for the clipboard: every
// component may be a long name or an 8.3 alias, so the volume names it. False
// for a path that is no file of this Mac (a disc image, a drive without a
// folder).
static bool HostPathOf(const std::string &guest, std::string &host)
{
    if (guest.size() < 3 || guest[1] != ':' || guest[2] != '\\') return false;
    const char letter = char(guest[0] & ~0x20);
    if (letter < 'A' || letter > 'Z' || host_roots[letter - 'A'].empty()) return false;
    // Below the write layer of a game machine a file may lie in the drive of
    // the normal machine : the layer first, then that.
    const auto walk = [&](Volume::Backend *backend, const std::string &root) {
        host = root;
        std::string prefix = guest.substr(0, 2);
        for (size_t at = 3; at < guest.size();) {
            size_t end = guest.find('\\', at);
            if (end == std::string::npos) end = guest.size();
            const std::string part = guest.substr(at, end - at);
            at = end + 1;
            if (part.empty()) continue;
            prefix += "\\" + part;
            Volume::Entry entry;
            const bool found = backend && backend->Find(prefix.c_str(), entry) && entry.name[0];
            host += "/" + HostVolume::ToHost(found ? std::string(entry.name) : part);
        }
        struct stat info = {};
        return lstat(host.c_str(), &info) == 0;
    };
    // The provider of a layer knows on which side the file lies.
    if (auto *layer = HostVolume::Layer(letter); layer && Volume::Mounted(letter) == layer) {
        struct stat info = {};
        host = layer->HostFile(guest.c_str());
        return !host.empty() && lstat(host.c_str(), &info) == 0;
    }
    if (walk(Volume::Mounted(letter), host_roots[letter - 'A'])) return true;
    return letter == 'C' && SystemTree::Mounted() && HostVolume::TreeDrive() &&
           walk(HostVolume::TreeDrive(), HostVolume::TreeRoot());
}
// The pasteboard of the Mac as the provider of the guest's clipboard
// (src/host_clipboard.h). The guest asks from a clipboard call on the host's
// own thread, when a program reads what the Mac offered.
static std::function<bool(const std::string &, std::string &)> clipboard_guest_path;
static bool ClipboardText(void *, uint16_t **units, unsigned *count)
{
    std::u16string text;
    if (!HostPasteboard::ReadText(text)) return false;
    *units = static_cast<uint16_t *>(malloc((text.size() + 1) * sizeof(uint16_t)));
    if (!*units) return false;
    memcpy(*units, text.data(), text.size() * sizeof(uint16_t));
    *count = unsigned(text.size());
    return true;
}
static bool ClipboardFiles(void *, char **list, unsigned *bytes)
{
    std::vector<std::string> paths;
    if (!HostPasteboard::ReadFiles(paths) || !clipboard_guest_path) return false;
    std::string out;
    for (const auto &path : paths) {
        std::string guest;
        if (!clipboard_guest_path(path, guest)) continue;
        out += guest;
        out += '\0';
    }
    if (out.empty()) return false;
    out += '\0';
    *list = static_cast<char *>(malloc(out.size()));
    if (!*list) return false;
    memcpy(*list, out.data(), out.size());
    *bytes = unsigned(out.size());
    return true;
}
static bool ClipboardImage(void *, unsigned char **dib, unsigned *bytes)
{
    std::vector<unsigned char> image;
    if (!HostPasteboard::ReadImage(image)) return false;
    *dib = static_cast<unsigned char *>(malloc(image.size()));
    if (!*dib) return false;
    memcpy(*dib, image.data(), image.size());
    *bytes = unsigned(image.size());
    return true;
}
// Puts `path` behind `letter`: a folder, or a CUE sheet or ISO image for a
// CD-ROM; an empty path leaves an empty CD-ROM drive or, for a fixed drive, no
// drive at all. Then the guest learns of it. False when the path cannot be
// opened; the drive keeps what it had.
// The folder of the game machine this host runs, empty for the normal
// machine: a hard disk it is given while it runs goes below a write layer
// there, like the ones it starts with.
static std::string swap_layers_folder;
static bool SwapMedium(X86::Application &app, char letter, const std::string &path, bool cdrom, bool announce = true)
{
    const unsigned index = unsigned(letter - 'A');
    if (index >= 26 || letter == 'C') return false;
    HostVolume::Drive *drive = nullptr;
    HostCdImage::Image *image = nullptr;
    const bool is_image = cdrom && path.size() > 4 &&
                          (!strcasecmp(path.c_str() + path.size() - 4, ".cue") ||
                           !strcasecmp(path.c_str() + path.size() - 4, ".iso"));
    if (is_image) {
        image = new HostCdImage::Image;
        std::string error;
        if (!image->Open(path.c_str(), error)) {
            printf("NEO98 HOST cdrom %s: %s\n", path.c_str(), error.c_str());
            delete image;
            return false;
        }
        Volume::Mount(letter, image, Volume::Cdrom, false);
        CdAudio::Insert(letter, image);
    } else if (!path.empty()) {
        const bool layered = !cdrom && !swap_layers_folder.empty();
        drive = new HostVolume::Drive;
        if (!drive->Open(path.c_str(), !cdrom && !layered, HostVolume::LeafLabel(path.c_str()).c_str())) {
            printf("NEO98 HOST drive %c: cannot open %s\n", letter, path.c_str());
            delete drive;
            return false;
        }
        if (layered) {
            // The layer and the drive below it stay for the life of the
            // host: a DOS machine may still read through them.
            auto *layer = new HostVolume::Layered;
            if (!layer->Open(HostGameMachine::DriveLayer(swap_layers_folder, letter).c_str(), drive)) {
                printf("NEO98 HOST drive %c: cannot open the layer above %s\n", letter, path.c_str());
                return false;
            }
            HostVolume::Layer(letter) = layer;
            Volume::Mount(letter, layer, Volume::Fixed, true);
            CdAudio::Insert(letter, nullptr);
            app.ChangeMedium(letter, announce);
            delete live_drives[index];
            delete live_images[index];
            live_drives[index] = nullptr;
            live_images[index] = nullptr;
            host_roots[index] = RealPath(path);
            host_sources[index] = RealPath(path);
            ClaimHostDrives();
            printf("NEO98 HOST medium %c: layer above %s\n", letter, path.c_str());
            fflush(stdout);
            return true;
        }
        Volume::Mount(letter, drive, cdrom ? Volume::Cdrom : Volume::Fixed, !cdrom);
        CdAudio::Insert(letter, nullptr);
    } else {
        Volume::Mount(letter, nullptr, cdrom ? Volume::Cdrom : Volume::Fixed, false);
        CdAudio::Insert(letter, nullptr);
    }
    app.ChangeMedium(letter, announce);
    // The store no longer knows the old provider.
    delete live_drives[index];
    delete live_images[index];
    live_drives[index] = drive;
    live_images[index] = image;
    host_roots[index] = drive ? RealPath(path) : std::string();
    host_sources[index] = path.empty() ? std::string() : RealPath(path);
    ClaimHostDrives();
    printf("NEO98 HOST medium %c: %s %s\n", letter, cdrom ? "cdrom" : "fixed", path.empty() ? "(none)" : path.c_str());
    fflush(stdout);
    return true;
}
// The first letter from D: on that is no drive, for a CD-ROM that has none.
static char FreeLetter()
{
    for (char letter = 'D'; letter <= 'Z'; ++letter)
        if (!Volume::Exists(letter)) return letter;
    return 0;
}
// An image mounted through hdiutil by this host, kept across the execv of a
// restart so that ejecting or quitting detaches it again.
static const char *AttachedEnv = "NEO98_HOST_ATTACHED";
static void DetachAttached(const char *why)
{
    const char *mounted = getenv(AttachedEnv);
    if (!mounted || !*mounted) return;
    std::string error;
    if (HostMenu::Detach(mounted, error)) printf("NEO98 HOST hdiutil detach %s (%s)\n", mounted, why);
    else printf("NEO98 HOST hdiutil detach %s failed: %s\n", mounted, error.c_str());
    fflush(stdout);
    unsetenv(AttachedEnv);
}

// NEO98_HOST_TRACE_EIP, see its use in main.
struct HostEipWatch { unsigned address; std::vector<std::pair<int, int>> fields; };
static void HostEipHook(X86::Cpu &cpu, unsigned address, unsigned, void *user)
{
    const auto *watch = static_cast<const HostEipWatch *>(user);
    static const int ids[8] = {X86::Reg::Eax, X86::Reg::Ecx, X86::Reg::Edx, X86::Reg::Ebx,
                               X86::Reg::Esp, X86::Reg::Ebp, X86::Reg::Esi, X86::Reg::Edi};
    static const char *names[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    unsigned r[8] = {};
    for (int i = 0; i < 8; ++i) cpu.Get(ids[i], r[i]);
    unsigned word = 0, ret = 0;
    cpu.Read(r[4], &ret, 4);
    printf("NEO98 HOST eip %08x eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x [esp]=%08x",
           address, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], ret);
    for (const auto &f : watch->fields)
    {
        const unsigned at = r[f.first] + (unsigned)f.second;
        printf(" [%s%c%x]=%08x", names[f.first], f.second < 0 ? '-' : '+',
               f.second < 0 ? 0u - unsigned(f.second) : unsigned(f.second),
               cpu.Read(at, &word, 4) == X86::CpuError::Ok ? word : 0xbadbad00u);
    }
    printf("\n");
    fflush(stdout);
}
static void HostTraceEip(X86::Application &app, const char *spec)
{
    static std::vector<HostEipWatch> watches;
    static const char *names[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    std::string text = spec;
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find(';', at);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(at, end - at);
        at = end + 1;
        HostEipWatch w{(unsigned)strtoul(item.c_str(), nullptr, 16), {}};
        const size_t equals = item.find('=');
        if (equals != std::string::npos)
        {
            std::string rest = item.substr(equals + 1) + ",";
            size_t from = 0, comma;
            while ((comma = rest.find(',', from)) != std::string::npos)
            {
                const std::string field = rest.substr(from, comma - from);
                from = comma + 1;
                if (field.size() < 4) continue;
                for (int n = 0; n < 8; ++n)
                    if (!field.compare(0, 3, names[n]))
                    {
                        const int offset = (int)strtol(field.c_str() + 4, nullptr, 16);
                        w.fields.push_back({n, field[3] == '-' ? -offset : offset});
                    }
            }
        }
        watches.push_back(w);
    }
    for (auto &w : watches)
    {
        const unsigned error = app.cpu->AddTrace(X86::TraceKind::Instruction, w.address, w.address, HostEipHook, &w);
        printf("NEO98 HOST trace-eip %08x %s\n", w.address, error == X86::CpuError::Ok ? "set" : "refused by the engine");
    }
    fflush(stdout);
}

// NEO98_HOST_MENU="seconds:item[=argument];..." triggers menu items without a
// person, for tools/test_host_menu_guest.py: `item` is an identifier of
// tools/host_menu.mm (reboot, report, cdrom-insert=PATH, cdrom-eject,
// drive-d=PATH, scale-1, scale-2, fullscreen, network-online, quit), `shortcut=cmd+shift+b`
// posts a key press to the menu bar, `describe` prints the menu. Each step
// waits its seconds after the previous one. The steps not yet taken stay in
// the environment, so a list continues after the execv of a restart.
static void MenuCommand(const std::string &command);
static void MenuScript()
{
    static auto previous = std::chrono::steady_clock::now();
    const char *script = getenv("NEO98_HOST_MENU");
    if (!script || !*script) return;
    const std::string text = script;
    const auto end = text.find(';');
    const std::string step = text.substr(0, end), rest = end == std::string::npos ? "" : text.substr(end + 1);
    const auto colon = step.find(':');
    const long seconds = strtol(step.c_str(), nullptr, 10);
    if (std::chrono::steady_clock::now() - previous < std::chrono::seconds(seconds)) return;
    previous = std::chrono::steady_clock::now();
    setenv("NEO98_HOST_MENU", rest.c_str(), 1);
    MenuCommand(colon == std::string::npos ? step : step.substr(colon + 1));
}

// One item of the menu bar by its identifier, from NEO98_HOST_MENU or from a
// menu= step of NEO98_HOST_INPUT.
static void MenuCommand(const std::string &command)
{
    const auto equals = command.find('=');
    const std::string name = command.substr(0, equals);
    const std::string argument = equals == std::string::npos ? "" : command.substr(equals + 1);
    bool okay = false;
    if (name == "shortcut") okay = HostMenu::PostShortcut(argument.c_str());
    else if (name == "describe") {
        const auto tree = HostMenu::Describe();
        okay = !tree.empty();
        size_t at = 0;
        while (at < tree.size()) {
            const auto line_end = tree.find('\n', at);
            printf("NEO98 HOST menu-item %s\n", tree.substr(at, line_end - at).c_str());
            at = line_end == std::string::npos ? tree.size() : line_end + 1;
        }
    } else if (name == "game-machine-answers") {
        // What the dialogs that follow are answered with (tools/host_game_menu.h).
        HostGameMenu::Answers(argument.c_str());
        okay = true;
    } else if (name == "state-window") {
        // The window "States" (tools/host_state_window.h): select=NAME,
        // load, save=NAME, rename=NAME, keep, delete, close, describe, png=PATH.
        okay = HostStateWindow::Perform(argument);
    } else if (name == "state-answers") {
        HostStateWindow::Answers(argument.c_str());
        okay = true;
    } else if (name == "game-machine-open") {
        // The entry of the Finder "Open as game machine".
        HostGameMenu::Request request;
        request.kind = HostGameMenu::Request::Open;
        request.program = argument;
        if ((okay = !argument.empty())) HostGameMenu::Post(request);
    } else {
        // The items of Machine > Game machines take their answer themselves.
        const bool game = !name.compare(0, 13, "game-machine-");
        if (game) HostGameMenu::Answer(equals == std::string::npos ? nullptr : argument.c_str());
        okay = HostMenu::Perform(name.c_str(), equals == std::string::npos || game ? nullptr : argument.c_str());
        if (game) HostGameMenu::Answer(nullptr);
    }
    printf("NEO98 HOST menu-script %s %s\n", command.c_str(), okay ? "ok" : "refused");
    fflush(stdout);
}

// NEO98_POINTER_TRACE=1 prints every change of what the host does with the
// Mac pointer: shown or hidden, SDL's relative mode, the window grab, mouse
// and keyboard focus, full screen, and each warp with its distance to the
// window centre and to the previous centre warp. a pointer that is
// held by the window after a program ends shows up here without a debugger.
static bool pointer_trace = false;
// The Win98 pointer is drawn by USER into the guest frame. Over the guest area
// the macOS pointer is therefore hidden; over the bars beside the canvas in
// full screen it comes back, because nothing draws a pointer there.
static void host_pointer(bool over_guest)
{
    static int shown = -1;
    const int want = over_guest ? SDL_DISABLE : SDL_ENABLE;
    if (shown == want) return;
    shown = want;
    SDL_ShowCursor(want);
    if (pointer_trace) { printf("NEO98 HOST POINTER show=%d\n", want == SDL_ENABLE ? 1 : 0); fflush(stdout); }
}

// The conversion between this window's canvas (the size of the desktop mode)
// and whatever screen the guest currently has (tools/guest_screen_scale.h;
// the pure math has its own SDL-independent unit test,
// tools/test_guest_screen_scale_native.cpp). A DirectDraw or GDI mode switch
// to a size other than the desktop's is the only case where the two differ;
// on the desktop both functions below are the identity.
static void CanvasToGuest(int &x, int &y)
{
    int width = 800, height = 600, depth = 32;
    User::ScreenMode(width, height, depth);
    if (canvas_crop) { x += canvas_crop_x; y += canvas_crop_y; return; }
    GuestScreenScale::CanvasToGuest(x, y, width, height, canvas_w, canvas_h);
}
static void GuestToCanvas(int &x, int &y)
{
    int width = 800, height = 600, depth = 32;
    User::ScreenMode(width, height, depth);
    if (canvas_crop) { x -= canvas_crop_x; y -= canvas_crop_y; return; }
    GuestScreenScale::GuestToCanvas(x, y, width, height, canvas_w, canvas_h);
}
// NEO98_CURSOR_TRACE=1 prints the warp target the guest asked for beside the
// canvas position it becomes, and every mouse-motion sample beside the guest
// position it turns into, so a pointer-alignment check can compare
// the warp with the SDL event that follows it (x, y, xrel, yrel) without a
// debugger. Off by default: it would otherwise print on every pixel of mouse
// movement.
static bool cursor_trace = false;
// The pointer state the host last saw; a line goes out when it changes.
struct PointerState {
    int shown = -1, relative = -1, grab = -1, mouse_focus = -1, input_focus = -1, fullscreen = -1;
    bool operator==(const PointerState &o) const
    {
        return shown == o.shown && relative == o.relative && grab == o.grab && mouse_focus == o.mouse_focus &&
               input_focus == o.input_focus && fullscreen == o.fullscreen;
    }
};
// The host never wants SDL's relative mouse mode: the Win98 pointer is a
// position, and the mickeys DINPUT.VXD reports are computed from it
// (src/x86_dinput_vxd.cpp). SDL3 switches relative mode on by itself when a
// hidden cursor is warped to the window centre twice within 30 ms
// (SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE, on by default), which is what a
// game recentring its pointer every frame does through SetCursorPos. It
// ends only with SDL_ShowCursor, which the host calls when the pointer leaves
// the guest area, and a pointer in relative mode never leaves: the Mac
// pointer stayed caught in the window after the program ended and the menu
// bar could not be reached. The hint is off before SDL_Init; this
// check releases the mode should anything else turn it on.
static void watch_pointer(SDL_Window *window)
{
    static PointerState last;
    if (SDL_GetRelativeMouseMode()) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        printf("NEO98 HOST pointer relative mode released\n");
        fflush(stdout);
    }
    if (!pointer_trace) return;
    const Uint32 flags = SDL_GetWindowFlags(window);
    PointerState now;
    now.shown = SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
    now.relative = SDL_GetRelativeMouseMode() ? 1 : 0;
    now.grab = SDL_GetWindowGrab(window) ? 1 : 0;
    now.mouse_focus = SDL_GetMouseFocus() == window;
    now.input_focus = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    now.fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if (now == last) return;
    last = now;
    printf("NEO98 HOST POINTER state shown=%d relative=%d grab=%d mouse-focus=%d input-focus=%d fullscreen=%d\n",
           now.shown, now.relative, now.grab, now.mouse_focus, now.input_focus, now.fullscreen);
    fflush(stdout);
}
// NEO98_HOST_INPUT_ONCE=1: a restart goes on with the steps of
// NEO98_HOST_INPUT not yet taken instead of playing the script again.
static std::string host_input_remaining;
static bool host_input_once = false;
// Where the jobs of LPT1 go; --printer-output DIR.
static const char *printer_output = "build/print-output";

// The app: started without EXE and FONT the host takes everything
// tools/host_run.py passes from its settings and its own bundle
// (tools/host_settings.h, docs/BUILDING.md). The result is the command line
// the rest of main reads; options given by hand come last and win.
static bool app_mode = false;
static HostSettings::Settings settings;
// --- The game machine (tools/host_game_machine.h) ---
// Not empty: this host runs the game machine of that folder. Its settings
// are the settings.ini in the folder, its program is the game and not the
// shell, and it ends when the last process of the game has ended.
static std::string game_machine_folder;
// --game-machine-open FILE: the program this start is to run as a game
// machine, the entry of the Finder from the command line.
static std::string game_open_at_start;
// The icon of a game machine from the resources of its program file
// (tools/host_exe_icon.h): Symbol.icns in its folder and the icon of its
// launcher bundle, so the Finder and the Dock show the game from the first
// start. False for a program without one (16-bit, DOS); its game machine
// takes the icon of its first window (serve_game_icon).
static bool GameMachineIcon(const std::string &folder, const std::string &file, int &width, int &height)
{
    std::vector<unsigned> argb;
    if (file.empty() || !HostExeIcon::Load(file, width, height, argb)) return false;
    const bool okay = HostDock::WriteIcon(folder + "/Symbol.icns", width, height, argb) &&
                      HostDock::WriteIcon(HostGameMachine::LauncherIcon(folder), width, height, argb);
    // The Finder reads the icon of a bundle again when the bundle changed.
    utimes(HostGameMachine::Launcher(folder).c_str(), nullptr);
    return okay;
}
// SDL's video subsystem, once. main starts it before the window; a start
// that has to know what macOS launched it for, or has to ask the person
// something before any machine boots, starts it earlier (OpenWithoutMachine).
static bool StartVideo()
{
    static bool started = false;
    if (started) return true;
    // SDL3 (under sdl2-compat) turns two warps of a hidden cursor to the
    // window centre within 30 ms into relative mouse mode, and a program
    // that recentres the pointer every frame does exactly that. The Mac
    // pointer then stays caught in the window after the program ends; the warps are the host's own, so they stay warps.
    SDL_SetHint("SDL_MOUSE_EMULATE_WARP_WITH_RELATIVE", "0");
    // USER supplies its own primary-finger fallback and suppresses it for
    // registered games. SDL's promoted mouse events would double-deliver it.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    // A window in full screen stays below the menu bar, which comes when the
    // pointer reaches the top of the screen (HostMenu::FullScreenBars).
    SDL_SetHint("SDL_ALLOW_TOPMOST", "0");
    // SDL3 (under sdl2-compat) tells AppKit that the application wants no
    // momentum of the trackpad (AppleMomentumScrollSupported = NO) unless
    // this hint says otherwise; then the Mac sends nothing after the fingers
    // left, neither to the SDL window nor to the windows of the seamless
    // view, and a swipe stops dead.
    SDL_SetHint("SDL_MAC_SCROLL_MOMENTUM", "1");
    started = SDL_Init(SDL_INIT_VIDEO) == 0;
    return started;
}

// --- A start that opens a game machine and boots nothing ---------------------
// Asked from the Mac for a program as a game machine while no host runs the
// machine, this start is that game machine: it finds or makes it and then
// becomes its host (HostGameMachine::Become). The normal machine does not
// come up. The ways of asking: --game-machine-open FILE, the entry of the
// Finder "Open as game machine" (a service macOS starts the app for), a
// double click on a program that has a game machine, and the answer "Als
// game machine" to the question about a program opened for the first
// time.
// Files of the launch this start does not take itself; the machine opens
// them once it has booted, as it opens NEO98_HOST_OPEN_AFTER_SETUP.
static std::vector<std::string> launch_opens;
// The lines of this part of the start. A start from the Finder has no
// standard output yet; the lines go into the log once it is redirected, or
// into launch.log when no machine boots.
static std::string launch_lines;
static void LaunchSay(const char *format, ...)
{
    char line[2048];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    launch_lines += std::string("NEO98 HOST ") + line + "\n";
    printf("NEO98 HOST %s\n", line);
    fflush(stdout);
}
// The start ends here without a machine: its lines for a start from the Finder.
static int LaunchEnds(int code)
{
    struct stat info;
    if (!fstat(1, &info) && S_ISCHR(info.st_mode) && !isatty(1) && HostSettings::MakeDirectories(HostSettings::LogDirectory()))
        HostGameMachine::WriteText(HostSettings::LogDirectory() + "/launch.log", launch_lines, 0644);
    return code;
}
// -1: the normal machine boots (with launch_opens). Else the exit code of
// this start; where it became the game machine it does not return.
static int OpenWithoutMachine(int argc, char **argv)
{
    std::string machine = settings.machine;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--machine") && i + 1 < argc) machine = argv[++i];
        // A drive C: of the command line is a run of development.
        else if (!strcmp(argv[i], "--drive-c")) return -1;
    }
    // Without a machine the assistant comes first; a machine that runs is
    // handed the request (HandToRunningHost), as it always was.
    if (!HostSettings::Ready(machine)) return -1;
    machine = RealPath(machine);
    if (HostGameMachine::MachineRunning(machine)) return -1;
    // Programs opened from the Finder and programs asked for as game machines.
    std::vector<std::string> finder, games;
    bool from_service = false;
    if (!game_open_at_start.empty()) games.push_back(game_open_at_start);
    // Test switch: files as the Finder names them in the event of a launch.
    if (const char *given = getenv("NEO98_LAUNCH_OPEN")) {
        const std::string text = given;
        unsetenv("NEO98_LAUNCH_OPEN");
        for (size_t from = 0; from < text.size();) {
            const size_t end = std::min(text.find('\n', from), text.size());
            if (end > from) finder.push_back(text.substr(from, end - from));
            from = end + 1;
        }
    }
    const char *video = getenv("SDL_VIDEODRIVER");
    const bool screen = !(video && !strcmp(video, "dummy"));
    // What macOS started the app for stands in the first Apple event of the
    // launch. Only a start by macOS itself (the Finder, the Dock, `open`, a
    // service: the parent is launchd) of the app in its bundle waits for it;
    // a start from a terminal has its request on the command line.
    // Once per launch: an image this process or another host started
    // (the restart of the machine, the start after the assistant,
    // HostGameMachine::Spawn) is no launch by macOS and gets no such event.
    // NEO98_LAUNCH_PROBE=1 and =0 force it for a test.
    bool probe = screen && getppid() == 1 && !HostSettings::BundleContents().empty() && !getenv("NEO98_LAUNCH_READ");
    setenv("NEO98_LAUNCH_READ", "1", 1);
    if (const char *forced = getenv("NEO98_LAUNCH_PROBE")) {
        probe = *forced == '1';
        unsetenv("NEO98_LAUNCH_PROBE");
    }
    if (probe) {
        HostMenu::LaunchWatch();
        // The application object as SDL makes it, only earlier than main
        // would; the service answers from now on.
        if (screen && StartVideo()) {
            SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
            HostGameMenu::InstallService();
        }
        const auto launch = HostMenu::LaunchWait(2.0, 5.0, []() { return HostGameMenu::Waiting(); });
        // The files of the launch are in SDL's queue (application:openURLs:
        // of SDL's delegate), the request of the service in HostGameMenu's.
        // What is not taken here stays there for the main loop.
        SDL_Event events[16];
        if (screen) SDL_PumpEvents();
        for (int count; screen && (count = SDL_PeepEvents(events, 16, SDL_GETEVENT, SDL_DROPFILE, SDL_DROPFILE)) > 0;)
            for (int i = 0; i < count; ++i)
                if (events[i].drop.file) {
                    finder.push_back(events[i].drop.file);
                    SDL_free(events[i].drop.file);
                }
        HostGameMenu::Request request;
        std::vector<HostGameMenu::Request> other;
        while (HostGameMenu::Next(request)) {
            if (request.kind == HostGameMenu::Request::Open) { games.push_back(request.program); from_service = true; }
            else other.push_back(request);
        }
        for (const auto &kept : other) HostGameMenu::Post(kept);
        LaunchSay("launch: %s after %.0f ms, event %s, %zu file(s), %zu as game machine",
                  !launch.finished ? "no launch event" : launch.service ? "for a service" : launch.plain ? "plain" : "with a request",
                  launch.seconds * 1000, launch.event.c_str(), finder.size(), games.size());
    }
    // The normal machine boots and takes over what this start does not.
    const auto boot = [&]() {
        for (const auto &file : finder) launch_opens.push_back(file);
        if (from_service)
            for (const auto &file : games) {
                HostGameMenu::Request request;
                request.kind = HostGameMenu::Request::Open;
                request.program = file;
                HostGameMenu::Post(request);
            }
        return -1;
    };
    if (finder.empty() && games.empty()) return -1;
    // One request is one game machine, and this process can be one only.
    // Several at once go the way they always went.
    if (finder.size() + games.size() != 1) {
        LaunchSay("open without machine: %zu requests at once, the machine boots", finder.size() + games.size());
        return boot();
    }
    // What a test answers the dialogs with (tools/host_game_menu.h).
    if (const char *answers = getenv("NEO98_GAME_MACHINE_ANSWERS")) {
        HostGameMenu::Answers(answers);
        unsetenv("NEO98_GAME_MACHINE_ANSWERS");
    }
    const bool as_game = !games.empty();
    const std::string file = RealPath(as_game ? games[0] : finder[0]);
    struct stat found = {};
    const bool program = !stat(file.c_str(), &found) && S_ISREG(found.st_mode) && HostGameMachine::ProgramName(file);
    // A document, a shortcut, a disc image: the machine opens it.
    if (!program && !as_game) return boot();
    // From here on the person may be asked; the panels need the application.
    if (screen && StartVideo()) HostMenu::Activate();
    if (!program) {
        LaunchSay("game machine: %s is no program file", file.c_str());
        HostGameMenu::Alert("Game machine not created", "A game machine requires the game's executable file (.exe or .com).");
        return LaunchEnds(0);
    }
    // The guest path, by the drives of the settings: C: is the machine's.
    std::string roots[26], guest;
    roots[2] = RealPath(HostSettings::DriveC(machine));
    for (int i = 3; i < 26; ++i)
        if (!settings.drives[i].empty() && HostSettings::IsDirectory(settings.drives[i])) roots[i] = RealPath(settings.drives[i]);
    for (int i = 2, best = -1; i < 26; ++i) {
        const std::string inside = roots[i] + "/";
        if (roots[i].empty() || file.compare(0, inside.size(), inside) || (best >= 0 && roots[i].size() <= roots[best].size())) continue;
        best = i;
        std::string rest = file.substr(inside.size());
        for (auto &c : rest) if (c == '/') c = '\\';
        guest = std::string(1, char('A' + i)) + ":\\" + HostVolume::ToGuest(rest);
    }
    if (as_game) LaunchSay("open as game machine %s", file.c_str());
    std::string folder;
    if (const auto machines = HostGameMachine::FindFor(machine, file, guest); !machines.empty()) {
        // The game machine the program has, or the one the person chooses.
        std::string chosen = machines[0].name;
        if (machines.size() > 1) {
            std::vector<std::string> names;
            for (const auto &entry : machines) names.push_back(entry.name);
            if (!HostGameMenu::Choose(HostGameMachine::Leaf(file), names, chosen)) {
                LaunchSay("game machine: %zu for %s, none chosen", machines.size(), file.c_str());
                return LaunchEnds(0);
            }
            LaunchSay("game machine: %zu for %s, chosen %s", machines.size(), file.c_str(), chosen.c_str());
        }
        for (const auto &entry : machines)
            if (entry.name == chosen) folder = entry.folder;
    } else {
        if (!as_game) {
            // A double click on a program nothing is known about: the
            // question of serve_mac_opens, asked before anything boots.
            bool known = false;
            for (const auto &answered : settings.open_normal) known = known || HostGameMachine::SameFile(answered, file);
            const bool windows = !strncasecmp(guest.c_str(), "C:\\WINDOWS\\", 11);
            if (!settings.open_ask || known || windows || HostGameMachine::InstallerName(file)) return boot();
            bool never = false;
            const auto answer = HostGameMenu::AskOpen(HostGameMachine::Leaf(file), never);
            using HostGameMenu::OpenAs;
            if (answer == OpenAs::NotAsked) return boot();
            LaunchSay("open %s asked: %s%s", file.c_str(),
                      answer == OpenAs::Game ? "game machine" : answer == OpenAs::Normal ? "normal" : "cancelled",
                      never && answer != OpenAs::Cancel ? ", never ask again" : "");
            if (answer == OpenAs::Cancel) return LaunchEnds(0);
            if (never) settings.open_ask = false;
            if (answer == OpenAs::Normal) settings.open_normal.push_back(file);
            if ((never || answer == OpenAs::Normal) && !HostSettings::Save(settings))
                LaunchSay("settings: cannot write %s", HostSettings::File().c_str());
            if (answer == OpenAs::Normal) return boot();
        }
        // A new game machine, from the machine as it lies on the disk: no
        // host has registry changes in memory, the hives and the journal are
        // current.
        HostGameMachine::Request make;
        make.machine = machine;
        make.program = guest;
        make.program_file = file;
        make.source = settings;
        if (make.program.empty()) {
            // A program outside the drives: its folder becomes the next free
            // drive of the game machine, as a running machine mounts it for
            // a double click. The CD-ROM of the settings takes the first
            // free letter.
            bool disc = !settings.cdrom.empty();
            for (int i = 3; i < 26 && make.program.empty(); ++i) {
                if (!roots[i].empty() || !settings.drives[i].empty()) continue;
                if (disc) { disc = false; continue; }
                make.source.drives[i] = HostSettings::Parent(file);
                make.program = std::string(1, char('A' + i)) + ":\\" + HostVolume::ToGuest(HostGameMachine::Leaf(file));
            }
        }
        if (make.program.empty()) {
            LaunchSay("game machine: no drive for %s", file.c_str());
            HostGameMenu::Alert("Game machine not created", "No drive letter is available for the program's folder.");
            return LaunchEnds(0);
        }
        const std::string suggested = HostGameMachine::FreeName(HostGameMachine::Directory(machine),
                                                                HostGameMachine::SuggestedName(make.program, make.program_file));
        if (!HostGameMenu::AskName(make.program, suggested, make.name)) {
            LaunchSay("game machine for %s: cancelled", make.program.c_str());
            return LaunchEnds(0);
        }
        // The machine is held while its registry is copied, and no longer.
        // A normal machine started while the person was asked runs by now:
        // its host makes the game machine, it has the registry in memory.
        const std::string drive_c = HostSettings::DriveC(machine);
        long owner = 0;
        if (HostSingle::Acquire(drive_c, owner) == HostSingle::Result::Running) {
            const bool handed = HostSingle::Hand(drive_c, file, "game");
            LaunchSay("machine %s runs in process %ld by now; %s game %s", HostSingle::Base(drive_c).c_str(), owner,
                      handed ? "handed over:" : "cannot hand over:", file.c_str());
            return LaunchEnds(handed ? 0 : 14);
        }
        const auto before = std::chrono::steady_clock::now();
        HostGameMachine::Result result;
        const bool made = HostGameMachine::Create(make, HostSettings::Executable(), result);
        HostSingle::Release(drive_c);
        const long ms = long(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count());
        if (!made) {
            LaunchSay("game machine create failed: %s", result.error.c_str());
            HostGameMenu::Alert("Game machine not created", result.error);
            return LaunchEnds(15);
        }
        LaunchSay("game machine created name=%s program=%s ms=%ld folder=%s (machine not running, held for the copy only)",
                  result.name.c_str(), make.program.c_str(), ms, result.folder.c_str());
        if (int width = 0, height = 0; GameMachineIcon(result.folder, make.program_file, width, height))
            LaunchSay("game machine icon %dx%d from the file %s", width, height, make.program_file.c_str());
        folder = result.folder;
    }
    // A launcher written by a host that lay elsewhere starts this one.
    if (!HostGameMachine::LauncherCurrent(folder, HostSettings::Executable()))
        HostGameMachine::WriteLauncher(folder, HostSettings::Executable());
    // Through the link of its launcher bundle, as every start of a game
    // machine: macOS shows it under the name and with the icon of the game.
    const auto line = HostGameMachine::StartLine(folder, HostSettings::Executable());
    LaunchSay("open without machine: this start becomes the game machine name=%s (%s)", HostGameMachine::Leaf(folder).c_str(),
              line[0].c_str());
    LaunchEnds(0);
    SDL_Quit();
    HostGameMachine::Become(line, {});
    LaunchSay("game machine start name=%s failed: %s", HostGameMachine::Leaf(folder).c_str(), strerror(errno));
    HostGameMenu::Alert("Game machine not started", "The host " + line[0] + " could not be started.");
    return LaunchEnds(11);
}
static std::vector<std::string> AppCommandLine(int argc, char **argv, std::string &machine)
{
    std::vector<std::string> given(argv + 1, argv + argc), rest;
    bool letters[26] = {}, cdrom = false;
    // The Finder and `open` may add arguments of their own (-psn_..., -NSxyz
    // YES); only the options of this host pass, anything else is left out.
    static const char *const with_value[] = {"--exe", "--command", "--scale", "--drive", "--drive-readonly", "--drive-below", "--cdrom",
                                             "--drive-c", "--registry", "--crash-report-dir", "--printer-output",
                                             "--native-module", "--seed-manifest", "--desktop", "--directory", "--net-listen"};
    static const char *const flags[] = {"--fullscreen", "--seamless", "--seamless-no-taskbar", "--seamless-taskbar", "--seamless-layer", "--seamless-icons", "--seamless-no-dock", "--offline", "--wifi", "--no-microphone", "--pause-unfocused", "--quiet", "--pacing", "--ram-drive",
                                        "--no-registry-file"};
    for (size_t i = 0; i < given.size(); ++i) {
        // The machine of a game machine is its own folder; --machine beside
        // --game-machine NAME only said whose game machine it is.
        if (given[i] == "--machine" && i + 1 < given.size()) { if (game_machine_folder.empty()) machine = given[i + 1]; ++i; continue; }
        if ((given[i] == "--game-machine" || given[i] == "--game-machine-open" || given[i] == "--load-state") && i + 1 < given.size()) { ++i; continue; }
        bool value = false, flag = false;
        for (const char *name : with_value) if (given[i] == name) value = true;
        for (const char *name : flags) if (given[i] == name) flag = true;
        if (!flag && !(value && i + 1 < given.size())) {
            printf("NEO98 HOST app ignores argument %s\n", given[i].c_str());
            continue;
        }
        if ((given[i] == "--drive" || given[i] == "--drive-readonly") && i + 1 < given.size() && given[i + 1].size() > 1)
            if (const char letter = char(given[i + 1][0] & ~0x20); letter >= 'A' && letter <= 'Z') letters[letter - 'A'] = true;
        if (given[i] == "--cdrom") cdrom = true;
        // A hard disk a game machine is given is one of the normal machine:
        // it goes below a layer of the game machine like the others.
        if (!game_machine_folder.empty() && given[i] == "--drive" && value && given[i + 1].size() > 2 && given[i + 1][1] == '=') {
            const char letter = char(given[i + 1][0] & ~0x20);
            rest.insert(rest.end(), {"--drive", std::string(1, letter) + "=" + HostGameMachine::DriveLayer(game_machine_folder, letter),
                                     "--drive-below", given[i + 1]});
            ++i;
            continue;
        }
        rest.push_back(given[i]);
        if (value) rest.push_back(given[++i]);
    }
    // The drive C: of a game machine is the one of the normal machine, read
    // where it lies, below a write layer of its own (the tree and the volume
    // above it). The normal machine has no layer:
    // tree and drive are one folder, and HostVolume::Mount drops the tree.
    const bool game = !game_machine_folder.empty();
    const std::string tree = HostSettings::DriveC(game ? settings.game_source : machine);
    const std::string drive_c = game ? HostGameMachine::Layer(game_machine_folder) : tree;
    std::vector<std::string> line{argv[0], "-", "C:\\WINDOWS\\FONTS\\VGAFIX.FON",
                                  "--exe", "C:\\WINDOWS\\EXPLORER.EXE", "--quiet",
                                  "--system-tree", tree, "--drive-c", drive_c,
                                  "--scale", std::to_string(settings.scale),
                                  "--crash-report-dir", HostSettings::LogDirectory() + "/crash-reports",
                                  "--printer-output", game ? HostSettings::MachinePrintDirectory(game_machine_folder) : HostSettings::PrintDirectory(machine)};
    if (settings.fullscreen) line.push_back("--fullscreen");
    // A desktop size of its own and the seamless view exclude each other:
    // the seamless desktop has the size of the Mac's screen.
    if (!settings.desktop.empty()) { line.push_back("--desktop"); line.push_back(settings.desktop); }
    else if (settings.seamless) line.push_back("--seamless");
    if (settings.seamless_taskbar) line.push_back("--seamless-taskbar");
    if (!settings.seamless_windows) line.push_back("--seamless-layer");
    if (settings.seamless_icons) line.push_back("--seamless-icons");
    if (settings.wheel_focus) line.push_back("--wheel-focus");
    if (!settings.wheel_log) line.push_back("--no-scroll-log");
    if (!settings.seamless_dock) line.push_back("--seamless-no-dock");
    if (!settings.online) line.push_back("--offline");
    if (settings.wifi) line.push_back("--wifi");
    if (!settings.microphone) line.push_back("--no-microphone");
    line.insert(line.end(), {"--net-listen", settings.net_listen});
    if (settings.pause_unfocused) line.push_back("--pause-unfocused");
    // A game machine starts its game in place of the shell.
    if (!settings.game_program.empty()) {
        line.push_back("--exe");
        line.push_back(settings.game_program);
        if (!settings.game_arguments.empty()) {
            line.push_back("--command");
            line.push_back("\"" + settings.game_program + "\" " + settings.game_arguments);
        }
        if (!settings.game_directory.empty()) { line.push_back("--directory"); line.push_back(settings.game_directory); }
    }
    // A folder or an image that is gone leaves its drive out; the setting stays.
    for (char letter = 'D'; letter <= 'Z'; ++letter) {
        const auto &folder = settings.drives[letter - 'A'];
        if (folder.empty() || letters[letter - 'A']) continue;
        if (!HostSettings::IsDirectory(folder)) { printf("NEO98 HOST settings drive %c: %s missing\n", letter, folder.c_str()); continue; }
        // A game machine reads the hard disks of the normal machine and
        // writes into a layer of its own for each.
        if (game) {
            line.insert(line.end(), {"--drive", std::string(1, letter) + "=" + HostGameMachine::DriveLayer(game_machine_folder, letter),
                                     "--drive-below", std::string(1, letter) + "=" + folder});
            continue;
        }
        line.push_back(settings.drives_readonly ? "--drive-readonly" : "--drive");
        line.push_back(std::string(1, letter) + "=" + folder);
    }
    if (!settings.cdrom.empty() && !cdrom) {
        const auto &disc = settings.cdrom;
        const bool image = disc.size() > 4 && (!strcasecmp(disc.c_str() + disc.size() - 4, ".cue") ||
                                               !strcasecmp(disc.c_str() + disc.size() - 4, ".iso"));
        if (HostSettings::Exists(disc) && (image || HostSettings::IsDirectory(disc))) { line.push_back("--cdrom"); line.push_back(disc); }
        else printf("NEO98 HOST settings cdrom %s missing\n", disc.c_str());
    }
    line.insert(line.end(), rest.begin(), rest.end());
    return line;
}

// Another host runs the machine on this drive C: (tools/host_single.h): Win98
// does not start twice from one disk. The window of that host comes to the
// front, a program this start was asked for goes to it, and this start ends.
// Explorer without a command line is the start of the machine itself and
// hands nothing over.
static int HandToRunningHost(const std::string &folder, long owner, int argc, char **argv, int first_option)
{
    // `games`: programs that are to run as game machines (--game-machine-open,
    // the entry of the Finder); the owner makes and starts them.
    std::vector<std::string> paths, games;
    const char *exe = first_option > 1 && argc > 1 && strcmp(argv[1], "-") ? argv[1] : nullptr;
    bool command = false;
    for (int i = first_option; i < argc; ++i) {
        if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--command") && i + 1 < argc) { command = true; ++i; }
        else if (!strcmp(argv[i], "--game-machine-open") && i + 1 < argc) games.push_back(RealPath(argv[++i]));
    }
    if (exe && (command || strcasecmp(exe, "C:\\WINDOWS\\EXPLORER.EXE"))) {
        // A guest path goes as it is, a file of this Mac by its full path.
        const bool guest = exe[0] && exe[1] == ':' && exe[2] == '\\';
        char real[PATH_MAX];
        paths.push_back(!guest && realpath(exe, real) ? real : exe);
    }
    const char *video = getenv("SDL_VIDEODRIVER");
    const bool screen = !(video && !strcmp(video, "dummy"));
    // Started from the Finder (`open -n program.exe`), the file comes as an
    // Apple event and not in the command line.
    if (screen && first_option == 1 && !HostSettings::BundleContents().empty())
        for (const auto &document : HostMenu::LaunchDocuments(&games)) paths.push_back(document);
    // A machine that started while this start read its launch.
    for (const auto &document : launch_opens) paths.push_back(document);
    bool handed = true;
    if (paths.empty() && games.empty()) handed = HostSingle::Hand(folder, std::string());
    for (const auto &path : paths) handed = HostSingle::Hand(folder, path) && handed;
    for (const auto &path : games) handed = HostSingle::Hand(folder, path, "game") && handed;
    printf("NEO98 HOST machine %s runs in process %ld; %s", HostSingle::Base(folder).c_str(), owner,
           handed ? "handed over:" : "cannot hand over:");
    if (paths.empty() && games.empty()) printf(" front");
    for (const auto &path : paths) printf(" open %s", path.c_str());
    for (const auto &path : games) printf(" game %s", path.c_str());
    printf("\n");
    fflush(stdout);
    if (screen && owner > 0) HostMenu::ActivateProcess(owner);
    return handed ? 0 : 14;
}

// The command line of the game machines, for the
// machine of the settings or of --machine FOLDER:
// --game-machine NAME|FOLDER starts one (main does that)
// --game-machine-create PROGRAM [--game-machine-name NAME] [--game-machine-arguments TEXT]
// --game-machine-list
// --game-machine-delete NAME
// --game-machine-open FILE runs a program file of this Mac as a game machine: the
// one it has, else a new one (main and the main loop do that)
// PROGRAM is a guest path or a file of this Mac on one of the drives. The
// exit code, or -1 when the command line asks for none of these.
static int GameMachineCommand(int argc, char **argv)
{
    const char *create = nullptr, *name = "", *arguments = "", *remove = nullptr;
    bool list = false;
    std::string machine = settings.machine;
    for (int i = 1; i < argc; ++i) {
        const bool value = i + 1 < argc;
        if (!strcmp(argv[i], "--game-machine-list")) list = true;
        else if (!strcmp(argv[i], "--game-machine-create") && value) create = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-name") && value) name = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-arguments") && value) arguments = argv[++i];
        else if (!strcmp(argv[i], "--game-machine-delete") && value) remove = argv[++i];
        else if (!strcmp(argv[i], "--machine") && value) machine = argv[++i];
    }
    if (!create && !remove && !list) return -1;
    if (!HostSettings::Ready(machine)) {
        printf("NEO98 GAME MACHINE: no Windows 98 installation in machine folder %s\n", machine.empty() ? "-" : machine.c_str());
        return 15;
    }
    machine = RealPath(machine);
    if (list) {
        for (const auto &entry : HostGameMachine::List(machine))
            printf("NEO98 GAME MACHINE name=%s running=%d program=%s folder=%s file=%s\n", entry.name.c_str(), entry.running ? 1 : 0,
                   entry.program.c_str(), entry.folder.c_str(), entry.file.empty() ? "-" : entry.file.c_str());
        return 0;
    }
    if (remove) {
        std::string error;
        const std::string folder = HostGameMachine::Resolve(remove, machine);
        if (folder.empty()) error = std::string("No game machine named \"") + remove + "\".";
        if (!error.empty() || !HostGameMachine::Delete(folder, error)) {
            printf("NEO98 GAME MACHINE delete failed: %s\n", error.c_str());
            return 15;
        }
        printf("NEO98 GAME MACHINE deleted name=%s folder=%s\n", HostGameMachine::Leaf(folder).c_str(), folder.c_str());
        return 0;
    }
    // A machine that runs has registry changes in memory; its own host writes
    // them out before it copies the journal (Machine > Game machines).
    if (HostGameMachine::MachineRunning(machine)) {
        printf("NEO98 GAME MACHINE create failed: the machine is running. Use Machine > Game machines.\n");
        return 14;
    }
    HostGameMachine::Request request;
    request.machine = machine;
    request.name = name;
    request.arguments = arguments;
    request.source = settings;
    // The roots of the drives as the settings name them; C: is the machine's.
    std::string roots[26];
    roots[2] = RealPath(HostSettings::DriveC(machine));
    for (int i = 3; i < 26; ++i)
        if (!settings.drives[i].empty()) roots[i] = RealPath(settings.drives[i]);
    const std::string given = create;
    if (given.size() > 3 && given[1] == ':' && given[2] == '\\') {
        request.program = given;
        const int letter = (given[0] & ~0x20) - 'A';
        if (letter >= 2 && letter < 26 && !roots[letter].empty())
            request.program_file = HostGameMachine::HostFile(roots[letter], HostVolume::ToHost(given.substr(3)));
    } else {
        request.program_file = RealPath(given);
        for (int i = 2; i < 26 && request.program.empty(); ++i) {
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
    if (!result.error.empty()) {
        printf("NEO98 GAME MACHINE create failed: %s\n", result.error.c_str());
        return 15;
    }
    printf("NEO98 GAME MACHINE created name=%s program=%s folder=%s\n", result.name.c_str(), request.program.c_str(),
           result.folder.c_str());
    if (int width = 0, height = 0; GameMachineIcon(result.folder, request.program_file, width, height))
        printf("NEO98 GAME MACHINE icon %dx%d of %s\n", width, height, request.program_file.c_str());
    return 0;
}

// --- Saved states (tools/host_savestate.h) -------------
// The NEO98 that writes a state: told in zustand.ini and in the window, never
// a reason to refuse one (that is the engine of its supplier,
// tools/host_machine_state.h). NEO98_REVISION in the environment stands in
// for it in the tests.
static std::string StateRevision()
{
    if (const char *given = getenv("NEO98_REVISION"); given && *given) return given;
#ifdef NEO98_REVISION
    return NEO98_REVISION;
#else
    return "unknown";
#endif
}
static void PrintState(const HostSaveState::Entry &state)
{
    printf("NEO98 GAME MACHINE STATE id=%s kind=%s game=%s parent=%s depth=%u current=%d usable=%d damaged=%d time=%lld layers=%s machine=%llu name=\"%s\" reason=\"%s\"\n",
           state.id.c_str(), state.kind.empty() ? "-" : state.kind.c_str(), state.game.empty() ? "-" : state.game.c_str(),
           state.parent.empty() ? "-" : state.parent.c_str(), state.depth, state.current ? 1 : 0, state.usable ? 1 : 0,
           state.damaged ? 1 : 0, state.time, state.layers.empty() ? "-" : state.layers.c_str(), state.machine_size,
           state.name.c_str(), state.reason.c_str());
}
// A state this start is to begin with (--load-state ID), read before the
// drives are mounted: the files of the state are put in the place of the
// layers and the journal here, and what the state holds of the machine waits
// in `bytes` for the machine to come up.
struct StateStart
{
    std::string id, name, game;
    std::vector<unsigned char> bytes;         // Maschine.bin
    std::vector<HostSaveState::BaseFile> files;
    std::vector<std::pair<std::string, std::string>> view;
    std::string error;                        // why the state was not taken, for the person
    bool waiting = false;                     // the supplier has the bytes and has not put them in yet
    bool settling = false;                    // they are in; the window paints the picture of the state
    std::chrono::steady_clock::time_point settle_until;
    bool loaded = false;                      // the machine stands on it and waits for the person
};
static StateStart state_start;
// What the start of a game machine does with its states while it holds the
// lock and before it opens a layer.
static void StartStates(int argc, char **argv)
{
    const std::string &folder = game_machine_folder;
    HostSaveState::Recover(folder);
    const char *id = nullptr;
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--load-state")) id = argv[++i];
    if (!id) return;
    // The host that asked for this load has saved its state before.
    const char *kept = getenv("NEO98_STATE_KEPT");
    const std::string before = kept ? kept : "";
    unsetenv("NEO98_STATE_KEPT");
    HostSaveState::Context context;
    HostSaveState::ContextOf(folder, StateRevision(), context);
    context.engines = HostMachineState::Engines();
    HostSaveState::Entry wanted;
    for (const auto &state : HostSaveState::List(context))
        if (state.id == id) wanted = state;
    auto &start = state_start;
    start.id = id;
    if (wanted.id.empty()) start.error = std::string("The state ") + id + " does not exist.";
    else if (!wanted.usable) start.error = wanted.reason;
    start.name = wanted.name;
    if (start.error.empty() && wanted.game != "off") {
        std::string bytes;
        unsigned long long size = 0;
        if (!HostMachineState::ByKind(wanted.game))
            start.error = "This Neo98 build does not recognize the state's program type (" + wanted.game + ").";
        else if (HostSaveState::FileSum(HostSaveState::MachineFile(wanted.folder), size) != wanted.machine_sum || size != wanted.machine_size ||
                 !HostSaveState::ReadFile(HostSaveState::MachineFile(wanted.folder), bytes))
            start.error = "The state is damaged: its machine data (Maschine.bin) has changed since it was saved.";
        else start.bytes.assign(bytes.begin(), bytes.end());
    }
    std::string error;
    if (start.error.empty() && before.empty()) {
        // A start from a state while the game machine did not run: what it
        // had written is kept as a state of its files.
        HostSaveState::Request request;
        request.kind = "auto";
        request.game = "off";
        request.revision = context.revision;
        std::string made;
        if (!HostSaveState::Save(folder, request, made, error)) start.error = "Could not save the current state before loading: " + error;
    }
    if (start.error.empty() && !HostSaveState::Restore(folder, id, error)) start.error = error;
    if (!start.error.empty()) {
        printf("NEO98 HOST state load id=%s refused: %s\n", id, start.error.c_str());
        start.bytes.clear();
        return;
    }
    HostSaveState::Prune(folder, id);
    start.game = wanted.game;
    start.files = wanted.files;
    start.view = wanted.view;
    printf("NEO98 HOST state load id=%s game=%s files restored layers=%s machine=%zu bytes before=%s\n", id, wanted.game.c_str(),
           wanted.layers.empty() ? "-" : wanted.layers.c_str(), start.bytes.size(), before.empty() ? "-" : before.c_str());
}

// The command line for the states of a game machine that does not run; a
// negative answer means none was asked for.
// --game-machine-states NAME the list
// --game-machine-state-save NAME [--state-name TEXT] its files as they are (no machine runs)
// --game-machine-state-load NAME --state ID its files from the state, an automatic state before
// --game-machine-state-delete NAME --state ID
// --game-machine-state-rename NAME --state ID --state-name TEXT
static int GameMachineStateCommand(int argc, char **argv)
{
    const char *list = nullptr, *save = nullptr, *load = nullptr, *remove = nullptr, *rename_of = nullptr;
    const char *id = "", *name = "", *kind = "manual";
    std::string machine = settings.machine;
    for (int i = 1; i < argc; ++i) {
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
    if (folder.empty() || !HostSaveState::ContextOf(folder, StateRevision(), context)) {
        printf("NEO98 GAME MACHINE STATE failed: no game machine named \"%s\".\n", given);
        return 15;
    }
    const auto fail = [](const std::string &error) {
        printf("NEO98 GAME MACHINE STATE failed: %s\n", error.c_str());
        return 15;
    };
    if (list) {
        for (const auto &state : HostSaveState::List(context)) PrintState(state);
        return 0;
    }
    std::string error;
    if (rename_of) {
        if (!HostSaveState::Rename(folder, id, name, error)) return fail(error);
        printf("NEO98 GAME MACHINE STATE renamed id=%s name=\"%s\"\n", id, HostSaveState::CleanName(name).c_str());
        return 0;
    }
    // What follows changes what a running game machine holds or reads: its
    // own host does that (Machine > states).
    long owner = 0;
    if (HostSingle::Acquire(HostGameMachine::Layer(folder), owner) == HostSingle::Result::Running) {
        printf("NEO98 GAME MACHINE STATE failed: the game machine is running. Use Machine > States.\n");
        return 14;
    }
    HostSaveState::Recover(folder);
    int code = 0;
    if (remove) {
        if (!HostSaveState::Delete(folder, id, error)) code = fail(error);
        else printf("NEO98 GAME MACHINE STATE deleted id=%s\n", id);
    } else if (save) {
        HostSaveState::Request request;
        request.name = name;
        request.kind = kind;
        request.game = "off";
        request.revision = context.revision;
        std::string made;
        if (!HostSaveState::Save(folder, request, made, error)) code = fail(error);
        else printf("NEO98 GAME MACHINE STATE saved id=%s\n", made.c_str());
    } else {
        HostSaveState::Entry wanted;
        for (const auto &state : HostSaveState::List(context))
            if (state.id == id) wanted = state;
        if (wanted.id.empty()) code = fail(std::string("The state ") + id + " does not exist.");
        else if (!wanted.usable) code = fail(wanted.reason);
        else {
            // What the game machine wrote since goes; it is kept as a state.
            HostSaveState::Request before;
            before.kind = "auto";
            before.game = "off";
            before.revision = context.revision;
            std::string kept;
            if (!HostSaveState::Save(folder, before, kept, error)) code = fail("Could not save the current state before loading: " + error);
            else if (!HostSaveState::Restore(folder, id, error)) code = fail(error);
            else {
                HostSaveState::Prune(folder, id);
                printf("NEO98 GAME MACHINE STATE loaded id=%s before=%s\n", id, kept.c_str());
            }
        }
    }
    HostSingle::Release(HostGameMachine::Layer(folder));
    return code;
}

int main(int real_argc, char **real_argv)
{
    int argc = real_argc;
    char **argv = real_argv;
    static std::vector<std::string> app_line;
    static std::vector<char *> app_argv;
    static std::string restart_program;
    // The DOS machine of a bundle lies beside the program.
    if (const auto core = HostSettings::BundledDosCore(); !core.empty() && !getenv("NEO98_DOSBOX_CORE"))
        setenv("NEO98_DOSBOX_CORE", core.c_str(), 1);
    if (HostSettings::AppMode(real_argc, real_argv)) {
        if (real_argc > 1 && (!strcmp(real_argv[1], "--help") || !strcmp(real_argv[1], "-h"))) {
            printf("Neo98: without arguments, starts the machine from %s.\n"
                   "Options: --machine FOLDER --exe GUESTPATH --command LINE --scale N --desktop WxH --fullscreen --offline --net-listen ADDRESS\n"
                   "          --drive X=FOLDER --cdrom PATH --registry FILE\n"
                   "Game machines: --game-machine NAME|FOLDER, --game-machine-create PROGRAM [--game-machine-name NAME]\n"
                   "          [--game-machine-arguments TEXT], --game-machine-list, --game-machine-delete NAME,\n"
                   "          --game-machine-open FILE (open as game machine, as in Finder)\n"
                   "Development: neo98-host EXE FONT [options]\n",
                   HostSettings::File().c_str());
            return 0;
        }
        app_mode = true;
        host_first_option = 1;
        HostSettings::Load(settings);
        if (const int code = GameMachineCommand(real_argc, real_argv); code >= 0) return code;
        if (const int code = GameMachineStateCommand(real_argc, real_argv); code >= 0) return code;
        // --game-machine NAME|FOLDER: this host is the game machine. From
        // here on its settings are the settings.ini in its folder, and its
        // log and crash reports have a folder of their own.
        {
            const char *game = nullptr;
            std::string of = settings.machine;
            for (int i = 1; i + 1 < real_argc; ++i) {
                if (!strcmp(real_argv[i], "--game-machine")) game = real_argv[++i];
                else if (!strcmp(real_argv[i], "--machine")) of = real_argv[++i];
                // Once: the restart of the machine has the same command line.
                else if (!strcmp(real_argv[i], "--game-machine-open") && !getenv("NEO98_GAME_OPEN_DONE")) game_open_at_start = RealPath(real_argv[++i]);
            }
            if (game) {
                const std::string folder = HostGameMachine::Resolve(game, of);
                if (folder.empty()) {
                    printf("NEO98 HOST game machine %s: not found\n", game);
                    return 15;
                }
                game_machine_folder = folder;
                session_logoff_ends = true;
                HostSettings::FileOverride() = HostGameMachine::SettingsFile(folder);
                HostSettings::Load(settings);
                settings.machine = folder;
                // Its drive C: is the one of the normal machine below its layer.
                if (!HostSettings::Ready(settings.game_source)) {
                    printf("NEO98 HOST game machine %s: main machine %s is missing\n", game, settings.game_source.c_str());
                    return 15;
                }
                // Once: the restart of the machine (execv) comes by here again.
                if (const char *marked = getenv("NEO98_GAME_MACHINE"); !marked || folder != marked) {
                    if (const char *base = getenv("NEO98_LOG_DIR"); base && *base) setenv("NEO98_GAME_MACHINE_LOG_BASE", base, 1);
                    setenv("NEO98_LOG_DIR", (HostSettings::LogDirectory() + "/Spielmaschinen/" + HostGameMachine::Leaf(folder)).c_str(), 1);
                    setenv("NEO98_GAME_MACHINE", folder.c_str(), 1);
                }
            }
        }
        // Asked for a game machine while no machine runs, this start becomes
        // it and the normal machine stays down.
        if (game_machine_folder.empty())
            if (const int code = OpenWithoutMachine(real_argc, real_argv); code >= 0) return code;
        // The machine that already runs keeps its log: look before the log
        // of this start replaces it.
        {
            std::string wanted = settings.machine;
            const char *other = nullptr;
            for (int i = 1; i < real_argc; ++i) {
                if (!strcmp(real_argv[i], "--machine") && i + 1 < real_argc) { if (game_machine_folder.empty()) wanted = real_argv[i + 1]; ++i; }
                else if (!strcmp(real_argv[i], "--drive-c") && i + 1 < real_argc) other = real_argv[++i];
            }
            long owner = 0;
            // The lock of a game machine lies beside its layer; the normal
            // machine below it is only read and may run at the same time.
            const std::string locked = game_machine_folder.empty() ? HostSettings::DriveC(wanted)
                                                                    : HostGameMachine::Layer(game_machine_folder);
            if (!other && (HostSettings::Ready(wanted) || !game_machine_folder.empty()) &&
                HostSingle::Acquire(locked, owner) == HostSingle::Result::Running)
                return HandToRunningHost(locked, owner, real_argc, real_argv, 1);
        }
        const bool logged = HostSettings::RedirectLog();
        // The states of a game machine : this host holds
        // its lock and has opened none of its layers yet.
        if (!game_machine_folder.empty()) StartStates(real_argc, real_argv);
        std::string machine = settings.machine;
        app_line = AppCommandLine(real_argc, real_argv, machine);
        printf("NEO98 HOST app settings=%s machine=%s log=%s\n", HostSettings::File().c_str(),
               machine.empty() ? "-" : machine.c_str(), logged ? HostSettings::LogDirectory().c_str() : "stdout");
        // What the start found out before the log was there.
        if (logged) fputs(launch_lines.c_str(), stdout);
        if (!game_machine_folder.empty())
            printf("NEO98 HOST game machine name=%s program=%s folder=%s\n", HostGameMachine::Leaf(game_machine_folder).c_str(),
                   settings.game_program.c_str(), game_machine_folder.c_str());
        fflush(stdout);
        restart_program = HostSettings::Executable();
        // A game machine stays the program of its launcher bundle over a
        // restart (HostGameMachine::LauncherProgram).
        if (!game_machine_folder.empty() && HostGameMachine::LauncherCurrent(game_machine_folder, restart_program))
            restart_program = HostGameMachine::LauncherProgram(game_machine_folder);
        if (game_machine_folder.empty() && !HostSettings::Ready(machine)) {
            // No machine yet: the assistant sets one up or takes an existing
            // folder, then the app starts over with it (docs/BUILDING.md). A
            // --machine of the command line has done its part as the
            // assistant's suggestion.
            settings.machine = machine;
            if (!HostSetup::RunAssistant(settings)) return 0;
            fflush(stdout);
            // The assistant took the events of this launch.
            setenv("NEO98_LAUNCH_READ", "1", 1);
            unsetenv("NEO98_HOST_WIZARD");
            // What was double clicked meanwhile starts with the machine.
            if (std::string kept; !HostSetup::AssistantOpens().empty()) {
                for (const auto &path : HostSetup::AssistantOpens()) kept += path + "\n";
                setenv("NEO98_HOST_OPEN_AFTER_SETUP", kept.c_str(), 1);
            }
            std::vector<char *> again;
            for (int i = 0; i < real_argc; ++i) {
                if (i && !strcmp(real_argv[i], "--machine")) { ++i; continue; }
                again.push_back(real_argv[i]);
            }
            again.push_back(nullptr);
            Arm64::ShutdownMachines();
            execv(restart_program.c_str(), again.data());
            fprintf(stderr, "cannot re-exec %s\n", restart_program.c_str());
            return 11;
        }
        HostSettings::MakeDirectories(HostSettings::LogDirectory());
        // From the Finder the working directory is /; F12 screenshots and
        // anything else with a relative name go beside the log.
        char here[PATH_MAX];
        if (getcwd(here, sizeof here) && !strcmp(here, "/")) chdir(HostSettings::LogDirectory().c_str());
        for (auto &word : app_line) app_argv.push_back(word.data());
        app_argv.push_back(nullptr);
        argc = int(app_line.size());
        argv = app_argv.data();
    }
    if (argc < 3) {
        fprintf(stderr, "usage: neo98-host EXE FONT [DLL...] [--native-module NAME]... [--seed-manifest FILE]... "
                        "[--command LINE] [--scale N] [--desktop WxH] [--quiet] [--crash-report-dir DIR] "
                        "[--drive-c DIR] [--drive X=DIR]... [--drive-readonly X=DIR]... [--cdrom DIR] [--ram-drive] [--pacing] "
                        "[--printer-output DIR] [--fullscreen] [--offline] [--net-listen ADDRESS] [--seamless] [--seamless-taskbar] [--seamless-layer] [--seamless-icons] [--seamless-no-dock]\n");
        return 2;
    }
    const char *command = nullptr;
    // --directory GUESTFOLDER: the folder the program starts in, where that is
    // not its own (the "Start in" of a shortcut).
    const char *start_directory = nullptr;
    if (const char *on = getenv("NEO98_PACING"); on && *on && strcmp(on, "0")) pacing.enabled = true;
    if (const char *on = getenv("NEO98_CURSOR_TRACE"); on && *on && strcmp(on, "0")) cursor_trace = true;
    if (const char *on = getenv("NEO98_POINTER_TRACE"); on && *on && strcmp(on, "0")) pointer_trace = true;
    std::vector<const char *> native_modules, manifests;
    int scale = 1;
    bool fullscreen = false, offline = false, wifi = false, microphone = true;
    std::string net_listen = "127.0.0.1";
    // The seamless view : wanted by the menu or the command
    // line, with or without the taskbar.
    // `seamless_windows`: a Mac window per guest window instead of one layer.
    // `seamless_icons`: the icons on the shell's desktop show as well.
    // The taskbar of the shell stays out of the view unless asked for; the
    // Start menu opens from the menu bar of the Mac (Window > Start menu).
    // `seamless_dock`: a Dock icon for every program of the guest.
    bool seamless = false, seamless_taskbar = false, seamless_windows = true, seamless_icons = false, seamless_dock = true;
    // `wheel_focus`: the wheel goes to the window with the focus (Win98)
    // instead of the window under the pointer.
    bool wheel_focus = false;
    // A line a scroll gesture in the session log (HostScrollLog).
    bool scroll_log_wanted = true;
    // The machine pauses by itself while another application is in front
    // (settings.ini pause.unfocused).
    bool pause_unfocused = false;
    host_arguments.assign(real_argv, real_argv + real_argc);
    // A state this start began with is not loaded again by a restart.
    RemoveOption("--load-state");
    // Mount the Win98 tree before anything touches the RAM volume; see
    // tools/system_tree_host.h.
    auto tree_options = SystemTreeHost::Scan(argc, argv);
    if (tree_options.tree && !SystemTreeHost::Mount(tree_options.root))
        return 11;
    // C: is a directory on this Mac and keeps what the guest writes: by
    // default the Win98 folder setup created, which then needs no tree layer
    // below it. --ram-drive brings back the old volatile volume.
    // README.md.
    const auto drive_options = HostVolume::Scan(argc, argv);
    if (!drive_options.ram && !drive_options.drive_c && !SystemTreeHost::Ready(HostVolume::DefaultDriveC())) {
        SystemTreeHost::ExplainSetup(HostVolume::DefaultDriveC());
        return 11;
    }
    const char *drive_root = drive_options.ram ? nullptr
                                               : (drive_options.drive_c ? drive_options.drive_c
                                                                        : HostVolume::DefaultDriveC());
    // One host per drive C: (tools/host_single.h), before anything
    // of this start writes into the folder.
    if (drive_root) {
        long owner = 0;
        const auto single = HostSingle::Acquire(drive_root, owner);
        if (single == HostSingle::Result::Running)
            return HandToRunningHost(drive_root, owner, argc, argv, 3);
        if (single == HostSingle::Result::NoLock)
            printf("NEO98 HOST no lock beside %s: %s\n", drive_root, strerror(errno));
    }
    if (!HostVolume::Mount(drive_options)) {
        fprintf(stderr, "cannot mount the host drives\n");
        return 13;
    }
    {
        Files::Session files(Files::System());
        std::string error;
        if (!HostWindowsData::Load([&](const char *path, HostWindowsData::Bytes &bytes) {
                return HostRegistry::ReadGuestFile(files, path, bytes);
            }, error)) {
            fprintf(stderr, "Neo98: %s\n", error.c_str());
            return 11;
        }
    }
    HostWindowsData::RegisterStateData();
    if (drive_root) {
        printf("NEO98 HOST drive C: %s\n", drive_root);
        host_roots['C' - 'A'] = RealPath(drive_root);
    }
    // What the menu shows and what a restart with other media changes.
    HostMenu::State menu_state;
    for (unsigned i = 0; i < drive_options.count; ++i) {
        const auto &extra = drive_options.extra[i];
        if (!extra.letter || extra.letter == 'C') continue;
        printf("NEO98 HOST drive %c: %s %s\n", extra.letter,
               extra.cdrom ? "cdrom" : extra.readonly ? "readonly" : extra.below ? "layer" : "fixed", extra.directory);
        // Below a layer lies the folder a file of this Mac is found in.
        if (extra.below) { printf("NEO98 HOST drive %c: below %s\n", extra.letter, extra.below); host_roots[extra.letter - 'A'] = RealPath(extra.below); }
        else if (HostSettings::IsDirectory(extra.directory)) host_roots[extra.letter - 'A'] = RealPath(extra.directory);
        host_sources[extra.letter - 'A'] = RealPath(extra.directory);
        if (extra.cdrom) { menu_state.cdrom = extra.directory; menu_state.cdrom_letter = extra.letter; }
        else if (extra.letter == 'D') menu_state.drive_d = extra.directory;
    }
    if (const char *mounted = getenv(AttachedEnv); mounted && *mounted && menu_state.cdrom != mounted)
        DetachAttached("not the CD-ROM any more");
    fflush(stdout);
    ClaimHostDrives();
    HostDeviceService::Service hardware;hardware.Attach();
    if (RegistryFile::Attach(argc, argv, drive_root))
        printf("NEO98 HOST registry %s\n",
               static_cast<RegistryFile::File *>(Registry::Attached())->Path());
    // Win98 reads its registry from the hives of the disk when it starts; the
    // journal above goes on top (tools/host_registry.h).
    HostRegistry::Start();
    if (HostRegistry::FinishImportedSetup(Registry::System(), drive_root))
        printf("NEO98 HOST imported Windows setup completed\n");
    // The machine starts here, so here is where Win98's WININIT.EXE runs:
    // after IO.SYS, before the first Windows process. It carries out the
    // [Rename] lines setup programs left in C:\WINDOWS\WININIT.INI (DXSETUP
    // replacing the DirectX 6.1 files). src/win98_wininit.h.
    Win98Wininit::Run(Files::System(), logger);
    if (HostRegistry::SyncDirectXVersion())
        printf("NEO98 HOST DirectX version repaired from installed 9.0c runtime files\n");
    // SYSTEM.INI still names the display adapter of the machine Win98 was
    // installed on; the file says what the port is from the first start on
    // (README.md, "Outside Device Manager").
    if (ProfileFiles::System().SyncHardware())
        printf("NEO98 HOST SYSTEM.INI names the display adapter of the port\n");
    if (ProfileFiles::System().SyncOemInfo())
        printf("NEO98 HOST OEMINFO.INI names NEO98 and its GitHub repository\n");
    if (ProfileFiles::System().SyncOemLogo())
        printf("NEO98 HOST OEMLOGO.BMP shows the NEO98 icon\n");
    // The DOS machine sees the same drives as the Windows side: the Win98
    // tree as C: with the writes in the same host directory the Windows side
    // writes its C: to, every mounted host directory under its own letter.
    // A drive the menu changes later reaches DOS machines that start after it.
    static std::string dos_directories[26];
    for (unsigned i = 0; i < drive_options.count; ++i)
        if (drive_options.extra[i].letter && drive_options.extra[i].letter != 'C' && !drive_options.extra[i].cdrom)
            dos_directories[drive_options.extra[i].letter - 'A'] = drive_options.extra[i].directory;
    auto refresh_dos_drives = [&]() {
        static std::vector<HostDosVm::Drive> dos_drives;
        dos_drives.clear();
        // A drive with a write layer is the provider's on both sides: the
        // DOS machine asks it as the store does.
        const auto layered = [&](char letter) {
            auto *layer = HostVolume::Layer(letter);
            if (!layer || Volume::Mounted(letter) != layer) return false;
            dos_drives.push_back({letter, layer->BelowRoot().c_str(), false, layer->Root().c_str(), layer->Foreign()});
            return true;
        };
        if (layered('C')) {
        } else if (SystemTree::Mounted())
            dos_drives.push_back({'C', SystemTreeHost::MountedRoot(), true, drive_root});
        else if (drive_root)
            dos_drives.push_back({'C', drive_root, false, nullptr});
        for (unsigned i = 3; i < 26; ++i)
            if (layered(char('A' + i))) continue;
            else if (!dos_directories[i].empty() && Volume::Exists(char('A' + i)) && Volume::Type(char('A' + i)) != Volume::Cdrom)
                dos_drives.push_back({char('A' + i), dos_directories[i].c_str(), false});
        HostDosVm::Install(logger, dos_drives.data(), (unsigned)dos_drives.size());
    };
    refresh_dos_drives();
    // A game machine ends with its game, a DOS game included.
    DosVm::EndWithProgram(!game_machine_folder.empty());
    swap_layers_folder = game_machine_folder;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--system-tree")) { if (i + 1 < argc && argv[i + 1][0] != '-') ++i; continue; }
        if (!strcmp(argv[i], "--exe")) { if (++i == argc) return 2; continue; }
        if (!strcmp(argv[i], "--ram-drive")) continue;
        if (!strcmp(argv[i], "--drive-c") || !strcmp(argv[i], "--drive") || !strcmp(argv[i], "--drive-readonly") ||
            !strcmp(argv[i], "--drive-below") || !strcmp(argv[i], "--cdrom") ||
            !strcmp(argv[i], "--registry")) {
            if (++i == argc) return 2;
            continue;
        }
        if (!strcmp(argv[i], "--no-registry-file")) continue;
        if (!strcmp(argv[i], "--native-module")) { if (++i == argc) return 2; native_modules.push_back(argv[i]); continue; }
        if (!strcmp(argv[i], "--seed-manifest")) { if (++i == argc) return 2; manifests.push_back(argv[i]); continue; }
        if (!strcmp(argv[i], "--command")) { if (++i == argc) return 2; command = argv[i]; continue; }
        if (!strcmp(argv[i], "--directory")) { if (++i == argc) return 2; start_directory = argv[i]; continue; }
        if (!strcmp(argv[i], "--scale")) { if (++i == argc) return 2; scale = atoi(argv[i]); if (scale < 1) scale = 1; continue; }
        if (!strcmp(argv[i], "--quiet")) { quiet = true; continue; }
        if (!strcmp(argv[i], "--desktop")) {
            // The desktop comes up in this size instead of the one the
            // registry names; the display driver offers it, upright
            // and lying, beside its fixed modes.
            if (++i == argc || sscanf(argv[i], "%dx%d", &desktop_w, &desktop_h) != 2 || desktop_w < 640 || desktop_h < 480 ||
                desktop_w > 4096 || desktop_h > 4096) {
                fprintf(stderr, "--desktop WIDTHxHEIGHT, at least 640x480\n");
                return 2;
            }
            continue;
        }
        if (!strcmp(argv[i], "--fullscreen")) { fullscreen = true; continue; }
        if (!strcmp(argv[i], "--seamless")) { seamless = true; continue; }
        if (!strcmp(argv[i], "--seamless-no-taskbar")) { seamless_taskbar = false; continue; }
        if (!strcmp(argv[i], "--seamless-taskbar")) { seamless_taskbar = true; continue; }
        if (!strcmp(argv[i], "--seamless-layer")) { seamless_windows = false; continue; }
        if (!strcmp(argv[i], "--seamless-icons")) { seamless_icons = true; continue; }
        if (!strcmp(argv[i], "--wheel-focus")) { wheel_focus = true; continue; }
        if (!strcmp(argv[i], "--no-scroll-log")) { scroll_log_wanted = false; continue; }
        if (!strcmp(argv[i], "--seamless-no-dock")) { seamless_dock = false; continue; }
        if (!strcmp(argv[i], "--net-listen")) {
            NetHost::U address;
            if (++i == argc || !HostNet::PosixSockets::ParseListen(argv[i], address)) return 2;
            net_listen = argv[i]; continue;
        }
        if (!strcmp(argv[i], "--offline")) { offline = true; continue; }
        if (!strcmp(argv[i], "--wifi")) { wifi = true; continue; }
        // Machine > Microphone in Windows off.
        if (!strcmp(argv[i], "--no-microphone")) { microphone = false; continue; }
        if (!strcmp(argv[i], "--pause-unfocused")) { pause_unfocused = true; continue; }
        if (!strcmp(argv[i], "--pacing")) { pacing.enabled = true; continue; }
        if (HostState::Option(argc, argv, i)) continue;
        if (!strcmp(argv[i], "--crash-report-dir")) { if (++i == argc) return 2; crash_report_dir = argv[i]; continue; }
        if (!strcmp(argv[i], "--printer-output")) { if (++i == argc) return 2; printer_output = argv[i]; continue; }
        auto bytes = load(argv[i]);
        const char *name = leaf(argv[i]);
        const bool program = strlen(name) > 4 && !strcasecmp(name + strlen(name) - 4, ".EXE");
        char guest[260];
        snprintf(guest, sizeof guest, program ? "C:\\WINDOWS\\%s" : "C:\\WINDOWS\\SYSTEM\\%s", name);
        if (bytes.empty() || Files::System().Seed(guest, bytes.data(), bytes.size())) {
            fprintf(stderr, "cannot seed %s\n", argv[i]);
            return 3;
        }
    }
    for (auto *path : manifests)
        if (!seed_manifest(path)) { fprintf(stderr, "cannot seed manifest %s\n", path); return 9; }
    // The far end of LPT1: every print job becomes a file there, and a PDF
    // when the Mac has a PostScript converter.
    static PrinterPortHost::Directory printer(printer_output);
    PrinterPort::Install(&printer);
    printf("NEO98 HOST printer LPT1 -> %s\n", printer_output);
    // The system font: a file with the FNT bytes, or the font file of the
    // drive by its guest path (the app, tools/host_registry.h).
    auto font = HostSystemFont::GuestPath(argv[2]) ? decltype(load(argv[2]))() : load(argv[2]);
    if (HostSystemFont::GuestPath(argv[2])) {
        const bool found = HostSystemFont::Load(argv[2], font);
        printf("NEO98 HOST system font %s %s\n", argv[2], found ? "read from the drive" : "not found");
    }
    // The start of the machine is the start of its shell, and Windows takes
    // the shell from SYSTEM.INI: [boot] shell=. Explorer named without a
    // command line is that start (the app does it); a machine whose
    // SYSTEM.INI names another shell gets that one, as under Win98.
    static std::string machine_shell;
    // Windows starts here, not a program: MSGSRV32.EXE plays the startup
    // sound before it starts the shell (X86::Application::PlayStartupSound).
    // A program started on its own (--exe, a game machine with its game in
    // place of the shell) is the start of that program and has none.
    const bool windows_start =
        tree_options.exe && !command && !strcasecmp(tree_options.exe, "C:\\WINDOWS\\EXPLORER.EXE");
    if (windows_start) {
        std::vector<unsigned char> ini;
        if (SystemTreeHost::ReadGuest("C:\\WINDOWS\\SYSTEM.INI", ini)) {
            const std::string text(ini.begin(), ini.end());
            bool boot = false;
            for (size_t from = 0; from < text.size();) {
                size_t end = text.find('\n', from);
                if (end == std::string::npos) end = text.size();
                std::string line = text.substr(from, end - from);
                from = end + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (!line.empty() && line[0] == '[') { boot = !strcasecmp(line.c_str(), "[boot]"); continue; }
                if (!boot || strncasecmp(line.c_str(), "shell", 5)) continue;
                size_t at = 5;
                while (at < line.size() && line[at] == ' ') ++at;
                if (at >= line.size() || line[at] != '=') continue;
                std::string value = line.substr(at + 1);
                while (!value.empty() && value[0] == ' ') value.erase(0, 1);
                // The program; what follows it on the line is not passed on.
                value = value.substr(0, value.find(' '));
                if (value.empty()) break;
                const size_t slash = value.rfind('\\');
                const std::string name = slash == std::string::npos ? value : value.substr(slash + 1);
                if (!strcasecmp(name.c_str(), "EXPLORER.EXE")) break;
                machine_shell = value.size() > 2 && value[1] == ':' ? value : "C:\\WINDOWS\\" + value;
                break;
            }
        }
        if (!machine_shell.empty()) {
            printf("NEO98 HOST shell %s (SYSTEM.INI [boot] shell=)\n", machine_shell.c_str());
            tree_options.exe = machine_shell.c_str();
        }
    }
#ifdef NEO98_WITH_HOST_MEDIA
    // Media Center 98 on C: before the program starts, which may be it.
    if (game_machine_folder.empty() && !HostMediaSeed::Seed())
        fprintf(stderr, "NEO98 HOST media: Media Center 98 is missing beside the host program\n");
#endif
    std::vector<unsigned char> exe;
    // --exe starts any program without a profile, by guest path out of the
    // mounted tree or by host path.
    char program[260] = {};
    const char *source = tree_options.exe ? tree_options.exe : argv[1];
    if (tree_options.exe && SystemTreeHost::GuestPath(tree_options.exe)) {
        if (!SystemTreeHost::ReadGuest(tree_options.exe, exe)) {
            fprintf(stderr, "cannot read %s from the Win98 tree\n", tree_options.exe);
            return 12;
        }
        snprintf(program, sizeof program, "%s", tree_options.exe);
    } else {
        exe = load(source);
        snprintf(program, sizeof program, "%s", leaf(source));
        // The program also lives in C:\WINDOWS under its own name: the shell
        // resolves "explorer.exe,0" icon locations through SearchPath.
        if (!exe.empty()) {
            char guest[260];
            snprintf(guest, sizeof guest, "C:\\WINDOWS\\%s", leaf(source));
            Files::System().Seed(guest, exe.data(), exe.size());
        }
    }
    // A machine out of a saved state needs no program to start.
    if ((exe.empty() && HostState::load_path.empty()) || font.empty()) return 3;
    // A host without a window is a test or a probe: it plays no sound, so
    // test runs stay quiet on the user's Mac. NEO98_AUDIO_TAP still records.
    if (const char *video = getenv("SDL_VIDEODRIVER"); video && !strcmp(video, "dummy") && !getenv("SDL_AUDIODRIVER"))
        setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!StartVideo()) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 10; }
    hardware.Install();
    hardware.SetWifi(wifi);
#ifdef NEO98_WITH_HOST_MEDIA
    // Kodi's catalogue for Media Center 98, only in a
    // build with NEO98_WITH_HOST_MEDIA=1.
    HostMediaService::Service media; media.Install();
    // The video inside Media Center 98's window: Kodi's window through
    // ScreenCaptureKit, not in the seamless view. NEO98_MEDIA_WINDOW=0 keeps
    // the old way (Kodi takes the screen).
    const bool media_window = !getenv("NEO98_MEDIA_WINDOW") || strcmp(getenv("NEO98_MEDIA_WINDOW"), "0");
    media.embeds = [&] { return media_window && !seamless && HostMediaCapture::Allowed(); };
#endif
    hardware.media_changed=[&](char letter,const std::string &path) {
        dos_directories[letter-'A']=path;refresh_dos_drives();
    };
    User::SetTouchscreenProbe([]() {
        for (int i = 0; i < SDL_GetNumTouchDevices(); ++i)
            if (SDL_GetTouchDeviceType(SDL_GetTouchDevice(i)) == SDL_TOUCH_DEVICE_DIRECT) return true;
        return false;
    });
    if (pointer_trace) {
        const char *hint = SDL_GetHint("SDL_MOUSE_EMULATE_WARP_WITH_RELATIVE");
        printf("NEO98 HOST POINTER hint emulate-warp-with-relative=%s\n", hint ? hint : "(default)");
        fflush(stdout);
    }
    // The speaker of the guest. The device itself opens at the first
    // waveOutOpen or PlaySound.
    static HostAudio::Device audio;
    audio.Install();
    // Off, Windows has no recording device and the microphone is never asked.
    Sound::Microphone::SetEnabled(microphone);
    // COM1 to COM4 with an endpoint in NEO98_COM1..4 reach the Mac; without one nothing changes.
    // The app takes the endpoints from settings.ini (com.1 to com.4); a
    // variable of the environment wins, as an option wins elsewhere.
    static HostComm::Ports serial_ports;
    if (app_mode)
        for (unsigned port = 0; port < 4; ++port)
            if (!settings.com[port].empty() && serial_ports.Endpoint(port).empty())
                serial_ports.Attach(port, settings.com[port].c_str());
    for (unsigned port = 0; port < 4; ++port) {
        menu_state.com[port] = serial_ports.Endpoint(port);
        if (!menu_state.com[port].empty()) printf("NEO98 HOST COM%u -> %s\n", port + 1, menu_state.com[port].c_str());
    }
    serial_ports.Install();
    // The rasterizer behind the Direct3D HAL of the display driver.
    auto *gpu_backend = HostGpu::Install();
    X86::Application::SetGpuBackend(gpu_backend);
    // The sockets of the host under the native WSOCK2.VXD.
    static HostNet::PosixSockets net;
    if (!net.ConfigureListen(net_listen)) {
        fprintf(stderr, "Invalid listen address for the guest network.\n"); return 2;
    }
    NetHost::Install(&net);
    // --offline starts with the cable pulled; the menu Network plugs it in
    // and pulls it while the machine runs.
    if (offline) net.SetOnline(false);
    // The card's address as the guest sees it: the host interface of the
    // default route, else the first one that is not the loopback (WSOCK2's
    // Adapters in src/x86_wsock2.cpp).
    auto card_address = [&]() {
        NetHost::Interface list[16];
        const unsigned count = net.Interfaces(list, 16);
        const NetHost::Interface *pick = nullptr;
        for (unsigned i = 0; i < count && !pick; ++i)
            if (!list[i].loopback && list[i].address && list[i].gateway) pick = &list[i];
        for (unsigned i = 0; i < count && !pick; ++i)
            if (!list[i].loopback && list[i].address) pick = &list[i];
        if (!pick) return std::string();
        char text[32];
        snprintf(text, sizeof text, "%u.%u.%u.%u", pick->address >> 24, (pick->address >> 16) & 255,
                 (pick->address >> 8) & 255, pick->address & 255);
        return std::string(text);
    };
    // The window appears once the guest has chosen its desktop mode (the
    // mode of the registry, set by the first process), in that size.
    if (seamless) fullscreen = false;
    HostWheel host_wheel;
    // The line of a scroll gesture in the session log; wheel.log = 0 in settings.ini, --no-scroll-log or
    // NEO98_SCROLL_LOG=0 switch it off.
    HostScrollLog scroll_log;
    scroll_log.enabled = scroll_log_wanted;
    if (const char *wanted = getenv("NEO98_SCROLL_LOG"); wanted && *wanted) scroll_log.enabled = *wanted != '0';
    auto scroll_log_line = [&]() {
        char text[320], name[64] = "";
        const unsigned target = User::WheelTarget();
        if (target) User::GetClassName(target, name, sizeof name);
        if (scroll_log.Line(text, sizeof text, target, name)) {
            puts(text);
            fflush(stdout);
        }
    };
    // The window of a game machine carries the name of its game.
    static const std::string window_name = game_machine_folder.empty() ? std::string("Neo98 - Windows 98 on ARM64")
                                                                       : HostGameMachine::Leaf(game_machine_folder);
    SDL_Window *window = SDL_CreateWindow(window_name.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          800 * scale, 600 * scale,
                                          SDL_WINDOW_HIDDEN | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    // Without a display (SDL_VIDEODRIVER=dummy, used by the tests that
    // drive the real host loop) only the software renderer exists.
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 800, 600);
    int texture_w = 800, texture_h = 600;
    if (!window || !renderer || !texture) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 10; }
    // While Kodi plays a video the window leaves the screen and Kodi comes
    // to the front; afterwards the window is back on top.
#ifdef NEO98_WITH_HOST_MEDIA
    media.show_screen = [&](unsigned to) {
        if (seamless) return;
        if (to == HM_SCREEN_KODI) { SDL_HideWindow(window); HostMediaKodi::Activate(); }
        // NEO98 takes the front back from Kodi's full screen space.
        else { SDL_ShowWindow(window); SDL_RaiseWindow(window); HostMenu::Activate(); }
    };
    // Media Center 98's full screen: the Mac window too, unless it was
    // already; back in the window afterwards. Kodi stays behind.
    bool media_full_before = false;
    // NEO98_MEDIA_HOST_FULLSCREEN=0 (tests) leaves the Mac window as it is.
    media.video_fullscreen = [&](bool on) {
        if (seamless || (getenv("NEO98_MEDIA_HOST_FULLSCREEN") && !strcmp(getenv("NEO98_MEDIA_HOST_FULLSCREEN"), "0"))) return;
        if (on) {
            media_full_before = fullscreen;
            if (!fullscreen) SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            HostMenu::Activate();
        } else if (!media_full_before) {
            SDL_SetWindowFullscreen(window, 0);
            int ww = 0, wh = 0;
            WindowSizeFor(window, scale, ww, wh);
            SDL_SetWindowSize(window, ww, wh);
        }
    };
#endif
    // The picture goes to the window as it is, alpha included: the seamless
    // view is transparent where the alpha says so.
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    // The seamless view starts with the desktop in the size of the Mac's
    // usable area: the display driver takes it in place of the registry's
    // size when the first process starts it.
    bool seamless_display = false;
    if (desktop_w) {
        seamless = false;
        X86::Application::SetHostDisplay(desktop_w, desktop_h, false);
        int hw = 0, hh = 0;
        X86::Application::HostDisplay(hw, hh);
        printf("NEO98 HOST desktop %dx%d %s\n", desktop_w, desktop_h, hw ? "offered" : "refused");
    }
    if (seamless) {
        const SDL_Rect usable = UsableArea(window);
        X86::Application::SetHostDisplay(usable.w, usable.h, false);
        int hw = 0, hh = 0;
        X86::Application::HostDisplay(hw, hh);
        seamless_display = hw != 0;
        if (!seamless_display) seamless = false;
    }
    // The picture of the Glide driver at its own resolution (User::Overlay), made when one appears and again when its size changes.
    SDL_Texture *overlay = nullptr;
    int overlay_width = 0, overlay_height = 0;
    unsigned overlay_serial = 0;
    SDL_StartTextInput();
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    // The menu bar replaces the old Restart and Report buttons that
    // used to sit below the guest screen.
    // NEO98_WHEEL_FOCUS=1 is --wheel-focus for a test.
    if (const char *wanted = getenv("NEO98_WHEEL_FOCUS"); wanted && *wanted == '1') wheel_focus = true;
    User::SetWheelToPointer(!wheel_focus);
    if (HostMenu::Install()) {
        HostSeamless::WatchScroll();
        menu_state.scale = scale;
        menu_state.fullscreen = fullscreen;
        menu_state.seamless = seamless;
        menu_state.seamless_taskbar = seamless_taskbar;
        menu_state.seamless_windows = seamless_windows;
        menu_state.seamless_icons = seamless_icons;
        menu_state.wheel_focus = wheel_focus;
        menu_state.seamless_dock = seamless_dock;
        menu_state.pause_unfocused = pause_unfocused;
        menu_state.online = net.Online();
        menu_state.wifi = wifi;
        menu_state.microphone = microphone;
        menu_state.net_listen = net.ListenPolicy();
        menu_state.address = card_address();
        HostMenu::Update(menu_state);
    }
    // Machine > Game machines, in the normal machine only: a game machine
    // makes no game machines. The machine folder is the one of the settings,
    // or the Win98 folder the command line names as drive C:. (A game
    // machine has the items of its saved states instead, serve_states.)
    const std::string games_of = !game_machine_folder.empty() ? std::string()
                                 : app_mode ? RealPath(settings.machine)
                                 : drive_root && HostSettings::Ready(drive_root) ? RealPath(drive_root) : std::string();
    if (!games_of.empty() && HostGameMenu::Install(games_of, HostSettings::DriveC(games_of))) {
        // A launcher written by a host that lay elsewhere (the app was moved)
        // starts this one from now on.
        for (const auto &entry : HostGameMachine::List(games_of))
            if (!HostGameMachine::LauncherCurrent(entry.folder, HostSettings::Executable()))
                HostGameMachine::WriteLauncher(entry.folder, HostSettings::Executable());
    }
    // The Dock shows a game machine with the icon of its game, once the game
    // has shown a window in an earlier run (serve_game_icon).
    if (!game_machine_folder.empty() && HostSettings::Exists(game_machine_folder + "/Symbol.icns"))
        HostGameMenu::SetApplicationIcon(game_machine_folder + "/Symbol.icns");
    // The name macOS has for this process: the one of the launcher bundle
    // when the host was started through its link, however that came about.
    if (!game_machine_folder.empty()) {
        std::string bundle;
        const std::string name = HostGameMenu::ApplicationName(&bundle);
        printf("NEO98 HOST game machine application name=%s bundle=%s\n", name.c_str(), bundle.c_str());
        // A launcher of an older form, or of a host that lay elsewhere,
        // starts this one from the next time on.
        if (!HostGameMachine::LauncherCurrent(game_machine_folder, HostSettings::Executable()))
            HostGameMachine::WriteLauncher(game_machine_folder, HostSettings::Executable());
    }
    if (!games_of.empty()) HostGameMenu::SetAsk(settings.open_ask);
    // USER draws the Win98 cursor as the top layer of its own composition.
    User::ShowPointer(true);
    const bool german = !(getenv("NEO98_KEYMAP") && !strcmp(getenv("NEO98_KEYMAP"), "US"));
    // The guest clock starts at the Mac's local time, as Win98 starts from the
    // battery clock; see StartGuestClock.
    {
        const time_t now = time(nullptr);
        struct tm parts = {};
        localtime_r(&now, &parts);
        const unsigned short fields[8] = {
            (unsigned short)(parts.tm_year + 1900), (unsigned short)(parts.tm_mon + 1),
            (unsigned short)parts.tm_wday, (unsigned short)parts.tm_mday, (unsigned short)parts.tm_hour,
            (unsigned short)parts.tm_min, (unsigned short)(parts.tm_sec > 59 ? 59 : parts.tm_sec), 0};
        X86::Application::StartGuestClock(fields);
    }
    X86::Application *a = nullptr;
    std::vector<std::pair<std::string, std::string>> dos_failures;
    X86::Application::SetDosFailureHook(&dos_failures, [](void *context, const char *path, const char *error) {
        auto &failures = *static_cast<std::vector<std::pair<std::string, std::string>> *>(context);
        failures.emplace_back(path ? path : "", error ? error : "Unknown DOS error");
    });
    {
    a = new X86::Application(logger);
    a->ConfigureKeyboardLayout(german?"DE":"US");
    // NEO98_TRACE_CALLS_FROM=N logs every API call of the first program from its call N on.
    if (const char *from = getenv("NEO98_TRACE_CALLS_FROM")) { a->trace_calls = true; a->trace_from = (unsigned)strtoul(from, nullptr, 10); }
    for (auto *name : native_modules)
        if (!a->AddNativeModule(name)) { fprintf(stderr, "cannot override %s\n", name); return 6; }
    // Explorer starts a program in its own folder; a program started by its
    // guest path does the same, so it finds the files beside it (Kroko-Mania
    // opens its script by its bare name).
    if (tree_options.exe && SystemTreeHost::GuestPath(tree_options.exe)) {
        char folder[260];
        snprintf(folder, sizeof folder, "%s", tree_options.exe);
        if (char *slash = strrchr(folder, '\\')) {
            slash[slash == folder + 2 ? 1 : 0] = 0;
            if (a->files.SetDirectory(folder))
                fprintf(stderr, "cannot enter %s\n", folder);
        }
    }
    if (start_directory && a->files.SetDirectory(start_directory))
        fprintf(stderr, "cannot enter %s\n", start_directory);
    // Without --command the program starts as Explorer starts it: the open
    // verb of exefile is "%1" %*, so a path with spaces arrives quoted. The
    // Flash projector read the unquoted rest of its own path as the URL of
    // its movie.
    std::string quoted;
    if (!command && strchr(program, ' ')) { quoted = std::string("\"") + program + "\""; command = quoted.c_str(); }
    // The process keeps the bytes of its program and of the system font for
    // its whole life. They are blocks of their own and not the host's
    // vectors, so a saved state carries them like any block of the port.
    auto *exe_bytes = static_cast<unsigned char *>(malloc(exe.size() ? exe.size() : 1));
    auto *font_bytes = static_cast<unsigned char *>(malloc(font.size() ? font.size() : 1));
    memcpy(exe_bytes, exe.data(), exe.size());
    memcpy(font_bytes, font.data(), font.size());
    // A state this start begins with (StartStates): its files are in place,
    // the drives are mounted. The layers hold again what the state had read
    // of the normal drives, and the supplier gets what it is to put into the
    // machine before the program starts.
    if (!state_start.id.empty() && state_start.error.empty()) {
        for (char letter = 'C'; letter <= 'Z'; ++letter) {
            auto *layer = HostVolume::Layer(letter);
            if (!layer) continue;
            std::vector<HostVolume::Layered::BelowFile> read;
            for (const auto &file : state_start.files)
                if (file.drive == letter) {
                    HostVolume::Layered::BelowFile known;
                    known.path = file.path;
                    known.size = file.size;
                    known.seconds = file.seconds;
                    known.nanoseconds = file.nanoseconds;
                    // The supplier's machine had read it.
                    (state_start.game == "dos" || state_start.game == "dos-ui" ? known.foreign : known.store) = true;
                    read.push_back(known);
                }
            layer->NoticeBelowFiles(read);
        }
        if (const auto *supplier = HostMachineState::ByKind(state_start.game); supplier && !state_start.bytes.empty()) {
            supplier->Load(std::move(state_start.bytes));
            state_start.bytes.clear();
            state_start.waiting = true;
        }
    }
    // The machine of a state in the place of a started program: of a state
    // of the game machine with a Windows game (tools/host_windows_state.h),
    // or of --state-load FILE (tools/host_state.h). Everything the host
    // installed above stays.
    HostWindowsState::first = a;
    unsigned short state_clock[8];
    HostState::WallClock(state_clock);
    bool state_failed = false;
    std::string state_error;
    const bool state_machine = state_start.game == "dos-ui"
        ? HostDosUiState::LoadNow(*a, state_clock, state_failed, state_error)
        : HostWindowsState::LoadNow(*a, state_clock, state_failed, state_error);
    if (state_failed) {
        // The port is half one machine and half the other: the host starts
        // over on the files of the state, which are in place, and says so.
        printf("NEO98 HOST state load id=%s failed: %s\n", state_start.id.c_str(), state_error.c_str());
        fflush(stdout);
        setenv("NEO98_STATE_NOT_LOADED", state_error.c_str(), 1);
        for (size_t i = host_first_option; i + 1 < host_arguments.size();)
            if (host_arguments[i] == "--load-state") host_arguments.erase(host_arguments.begin() + i, host_arguments.begin() + i + 2);
            else ++i;
        std::vector<char *> arguments;
        for (auto &argument : host_arguments) arguments.push_back(argument.data());
        arguments.push_back(nullptr);
        HostSingle::KeepAcrossExec();
        Arm64::ShutdownMachines();
        execv(restart_program.empty() ? real_argv[0] : restart_program.c_str(), arguments.data());
        fprintf(stderr, "restart failed after a state that did not load\n");
        _exit(23);
    }
    if (state_machine || HostState::LoadMachine(*a)) {
        HostState::loaded = true;
        free(exe_bytes);
        free(font_bytes);
    } else if (!a->Start(exe_bytes, exe.size(), font_bytes, font.size(), program, command)) {
        fprintf(stderr, "start failed: %s\n", a->Error());
        return 4;
    } else if (windows_start) {
        // MSGSRV32 : after the logon the sound of the scheme's
        // "SystemStart" begins, then the shell of SYSTEM.INI is started. A
        // machine out of a saved state did not start, it goes on.
        a->PlayStartupSound();
    }
    }
    // NEO98_HOST_TRACE_EIP="ADDR[=REG+OFF,...];..." prints the registers each
    // time the first process executes ADDR (hex), and the dword at each
    // REG+OFF (hex offset, also REG-OFF) after them: a hand check can read the
    // fields of an object at the place a decision is made. Needs an engine
    // with instruction traces, NEO98_CPU=unicorn.
    if (const char *spec = getenv("NEO98_HOST_TRACE_EIP")) HostTraceEip(*a, spec);
    // The window follows the size of the desktop mode (User::PixelsSize):
    // at the start, after DESK's "Screen area" and after
    // ChangeDisplaySettings with CDS_UPDATEREGISTRY. A program mode of another
    // size keeps the window and goes stretched onto the canvas. Full screen
    // keeps the screen and centres the new canvas.
    // The seamless view: `overlay_on` while the window lies over the Mac's
    // usable area and shows the guest's windows only.
    bool overlay_on = false, recentre = false;
    auto follow_canvas = [&](int width, int height) {
        if (width == canvas_w && height == canvas_h) return;
        canvas_w = width;
        canvas_h = height;
        if (!fullscreen && !overlay_on) {
            // A program mode as large as the screen (DirectDraw in exclusive
            // mode before it sets a mode of its own, and after it restored
            // the desktop's) has no room for a title bar: a window with one
            // reached beyond the screen. It covers the usable area
            // without a frame, opaque, as a game in full screen does.
            const SDL_Rect usable = UsableArea(window);
            const bool whole = seamless && usable.w > 0 && width >= usable.w && height >= usable.h;
            SDL_SetWindowBordered(window, whole ? SDL_FALSE : SDL_TRUE);
            // The window of a program mode can be sized by hand; the picture
            // keeps its proportions in it (LayoutFor).
            SDL_SetWindowResizable(window, seamless && !whole ? SDL_TRUE : SDL_FALSE);
            if (whole) {
                SDL_SetWindowPosition(window, usable.x, usable.y);
                SDL_SetWindowSize(window, usable.w, usable.h);
                recentre = true;
            } else {
                int ww = 0, wh = 0;
                WindowSizeFor(window, scale, ww, wh);
                if (seamless && usable.w > 0) {
                    // A program mode in the seamless view: a Mac window in
                    // the middle of the screen at a size one can play in,
                    // twice the mode where that fits and else as large as
                    // the usable area allows below its title bar.
                    const int room_w = usable.w, room_h = usable.h - 28;
                    double factor = 2.0;
                    if (width * factor > room_w) factor = double(room_w) / width;
                    if (height * factor > room_h) factor = double(room_h) / height;
                    if (factor < 1.0) factor = 1.0;
                    ww = int(width * factor);
                    wh = int(height * factor);
                    recentre = true;
                }
                SDL_SetWindowSize(window, ww, wh);
                // Back from the overlay the window stands in the middle again.
                if (recentre) SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                recentre = false;
                // A host that started over to load a state: the window
                // stands where it stood, in the size it had for this picture.
                if (const char *place = getenv("NEO98_HOST_WINDOW_AT")) {
                    int x = 0, y = 0, w = 0, h = 0, cw = 0, ch = 0;
                    if (sscanf(place, "%d,%d,%d,%d,%d,%d", &x, &y, &w, &h, &cw, &ch) == 6 && cw == width && ch == height) {
                        SDL_SetWindowSize(window, w, h);
                        SDL_SetWindowPosition(window, x, y);
                        unsetenv("NEO98_HOST_WINDOW_AT");
                    }
                }
            }
            {
                int ww = 0, wh = 0;
                SDL_GetWindowSize(window, &ww, &wh);
                if (whole) printf("NEO98 HOST window covers the screen (program mode of its size)\n");
                else printf("NEO98 HOST window framed %dx%d for %dx%d resizable=%d\n", ww, wh, width, height,
                            seamless ? 1 : 0);
            }
        }
        const auto layout = LayoutFor(window, renderer);
        printf("NEO98 HOST canvas %dx%d window=%dx%d guest=%d,%d,%dx%d\n", canvas_w, canvas_h, layout.window_w,
               layout.window_h, layout.guest.x, layout.guest.y, layout.guest.w, layout.guest.h);
        fflush(stdout);
    };
    // --- The seamless view -------------------------------------------------
    // Wanted (`seamless`), it asks the guest for a desktop in the size of the
    // Mac's usable area and, once the desktop has it, lays the window over
    // that area without a frame. A program mode (DirectDraw in full screen, a
    // mode of another size, the text view of a console, the Glide picture)
    // takes the overlay away until it ends: the window is an ordinary one in
    // the size of that mode.
    HostSeamless::Mask mask;
    std::vector<HostSeamless::Item> items;
    HostSeamless::Cursor shown_cursor;
    bool cursor_known = false;
    bool mouse_released_now = false; // mouse_hold.released, for seamless_cursor
    unsigned hover = 0;
    std::vector<unsigned> seamless_picture;
    bool script_pointer = false, pointer_opaque = true, pointer_known = false;
    int script_pointer_x = 0, script_pointer_y = 0;
    unsigned seamless_buttons = 0;
    constexpr Uint32 ScriptMouse = 0x98;
    auto seamless_arguments = [&]() {
        RemoveFlag("--seamless");
        RemoveFlag("--seamless-no-taskbar");
        RemoveFlag("--seamless-taskbar");
        RemoveFlag("--seamless-layer");
        RemoveFlag("--seamless-icons");
        RemoveFlag("--wheel-focus");
        if (wheel_focus) host_arguments.push_back("--wheel-focus");
        RemoveFlag("--seamless-no-dock");
        if (seamless_icons) host_arguments.push_back("--seamless-icons");
        if (!seamless_dock) host_arguments.push_back("--seamless-no-dock");
        menu_state.seamless_dock = seamless_dock;
        if (seamless) host_arguments.push_back("--seamless");
        if (seamless_taskbar) host_arguments.push_back("--seamless-taskbar");
        if (!seamless_windows) host_arguments.push_back("--seamless-layer");
        menu_state.seamless = seamless;
        menu_state.seamless_taskbar = seamless_taskbar;
        menu_state.seamless_windows = seamless_windows;
        menu_state.seamless_icons = seamless_icons;
        menu_state.wheel_focus = wheel_focus;
        menu_state.fullscreen = fullscreen;
        HostMenu::Update(menu_state);
    };
    // A thread that waits for messages takes the new desktop size the next
    // time it asks (X86::Application::SetHostDisplay); a WM_NULL lets it ask.
    auto nudge = [&]() {
        unsigned handles[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, handles, User::WindowCapacity, count)) return;
        for (unsigned i = 0; i < count; ++i)
            if (User::IsWindow(handles[i])) User::PostMessage(handles[i], 0, 0, 0);
    };
    // Full screen chosen in the seamless view: the window in the guest's
    // foreground alone, over the whole screen, until it is gone or the user
    // leaves full screen; then the seamless view is back by itself. Nothing
    // of it is a setting.
    unsigned zoomed = 0;
    auto set_overlay = [&](bool on, const SDL_Rect &usable, const char *why) {
        overlay_on = on;
        pointer_known = false;
        if (on) {
            if (fullscreen) {
                SDL_SetWindowFullscreen(window, 0);
                fullscreen = false;
                RemoveFlag("--fullscreen");
            }
            SDL_SetWindowBordered(window, SDL_FALSE);
            SDL_SetWindowPosition(window, usable.x, usable.y);
            SDL_SetWindowSize(window, usable.w, usable.h);
            canvas_w = usable.w;
            canvas_h = usable.h;
            HostSeamless::Transparent(window, true);
            // USER draws no pointer into the frame; the Mac's pointer shows
            // the guest's cursor (seamless_cursor).
            User::ShowPointer(false);
            host_pointer(false);
            shown_cursor = HostSeamless::Cursor();
            cursor_known = false;
            // A click on a guest window while another Mac application is in
            // front is a click, not only an activation.
            SDL_SetHint("SDL_MOUSE_FOCUS_CLICKTHROUGH", "1");
        } else {
            HostSeamless::CloseItems();
            HostSeamless::Transparent(window, false);
            HostSeamless::ClickThrough(window, false);
            SDL_SetHint("SDL_MOUSE_FOCUS_CLICKTHROUGH", "0");
            SDL_SetWindowBordered(window, SDL_TRUE);
            // The next picture gives the window its size and place.
            canvas_w = 0;
            recentre = true;
            User::ShowPointer(true);
        }
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window, &ww, &wh);
        printf("NEO98 HOST seamless overlay=%d window=%dx%d screen=%d,%d,%dx%d taskbar=%d (%s)\n", on ? 1 : 0, ww, wh,
               usable.x, usable.y, usable.w, usable.h, seamless_taskbar ? 1 : 0, why);
        fflush(stdout);
    };
    auto seamless_off = [&](const char *why) {
        if (!seamless) return;
        seamless = false;
        seamless_display = false;
        int hw = 0, hh = 0;
        X86::Application::HostDisplay(hw, hh);
        if (hw) {
            // Back to the size the registry names.
            X86::Application::SetHostDisplay(0, 0, true);
            nudge();
        }
        if (overlay_on) set_overlay(false, UsableArea(window), why);
        canvas_w = 0; // a window that covered the screen for a program mode gets its frame back
        printf("NEO98 HOST seamless off (%s)\n", why);
        fflush(stdout);
        seamless_arguments();
    };
    // What Collect shows: the taskbar, the icons of the shell's desktop (they
    // need the frame's pixels), and the time for a window that has not
    // painted yet.
    auto seamless_view = [&](const unsigned char *pixels) {
        HostSeamless::View view;
        view.taskbar = seamless_taskbar;
        view.icons = seamless_icons;
        view.pixels = pixels;
        view.now = SDL_GetTicks();
        if (view.icons) {
            int width = 0, height = 0, depth = 0; User::ScreenMode(width, height, depth);
            view.icon_mask = X86::Application::HostDesktopIconMask(view.now, width, height);
        }
        static const bool trace = getenv("NEO98_SEAMLESS_TRACE") != nullptr;
        view.trace = trace;
        // NEO98_SEAMLESS_GRACE=milliseconds, for the tests.
        static const char *grace = getenv("NEO98_SEAMLESS_GRACE");
        if (grace && *grace) view.grace = unsigned(strtoul(grace, nullptr, 10));
        return view;
    };
    // The guest windows without a Mac window of their own, for the menu
    // "Window": the minimized ones, which the shell parks outside the
    // screen, and in one layer all that would have a button on the taskbar.
    auto list_guest_windows = [&]() {
        std::vector<std::pair<unsigned, std::string>> list;
        unsigned handles[User::WindowCapacity], count = 0;
        if (overlay_on && User::WindowSnapshot(0, handles, User::WindowCapacity, count)) {
            const unsigned shell = User::RegisteredShellWindow();
            for (unsigned i = 0; i < count; ++i) {
                const unsigned h = handles[i];
                if (h == shell || !User::IsVisible(h)) continue;
                const unsigned extended = User::ExtendedStyle(h);
                if (!((extended & 0x40000u) || (!User::Owner(h) && !(extended & 0x80u)))) continue;
                // With a Mac window per guest window the Mac lists those
                // itself; left are the minimized ones and the ones another
                // window covers wholly (the setup wizard of Plus! 98 behind
                // its autorun window).
                if (seamless_windows && !(User::Style(h) & 0x20000000u) &&
                    std::find(mask.covered.begin(), mask.covered.end(), h) == mask.covered.end())
                    continue;
                char text[256] = {};
                User::GetWindowText(h, text, sizeof text);
                if (!*text) continue;
                std::string title;
                for (const unsigned char *q = reinterpret_cast<const unsigned char *>(text); *q; ++q) {
                    if (*q < 0x80) title += char(*q);
                    else { title += char(0xc0 | (*q >> 6)); title += char(0x80 | (*q & 0x3f)); }
                }
                list.emplace_back(h, title);
            }
        }
        if (list != menu_state.guest_windows) {
            menu_state.guest_windows = list;
            HostMenu::Update(menu_state);
        }
    };
    auto serve_seamless = [&](bool text_view, const unsigned char *pixels) {
        if (!seamless) { list_guest_windows(); return; }
        const SDL_Rect usable = UsableArea(window);
        int hw = 0, hh = 0;
        X86::Application::HostDisplay(hw, hh);
        if (!X86::Application::HostDisplayPending()) {
            // The guest stored a screen size of its own (DESK's "Screen
            // area"): that is its choice, and the host's screen gives way.
            if (seamless_display && !hw) { seamless_off("the guest chose a screen size"); return; }
            if (hw != usable.w || hh != usable.h) {
                X86::Application::SetHostDisplay(usable.w, usable.h, true);
                X86::Application::HostDisplay(hw, hh);
                if (!hw) { seamless_off("no desktop of this size"); return; }
                seamless_display = true;
                nudge();
                printf("NEO98 HOST seamless desktop %dx%d asked\n", usable.w, usable.h);
                fflush(stdout);
            }
        }
        int ow = 0, oh = 0, dw = 0, dh = 0;
        unsigned serial = 0;
        const bool program = text_view || User::ScreenModeActive() || User::ExclusiveMode() ||
                             User::Overlay(ow, oh, serial) != nullptr;
        User::PixelsSize(dw, dh);
        const bool want = !program && !zoomed && dw == usable.w && dh == usable.h;
        if (want != overlay_on)
            set_overlay(want, usable, want ? "desktop" : zoomed ? "full screen of a program" : program ? "program mode"
                                                                                               : "desktop of another size");
        if (overlay_on) HostSeamless::Collect(mask, seamless_view(pixels));
        list_guest_windows();
    };
    auto zoom_end = [&](const char *why) {
        if (!zoomed) return;
        zoomed = 0;
        canvas_crop = false;
        SDL_SetWindowFullscreen(window, 0);
        canvas_w = 0; // the next picture places the window (or the overlay does)
        printf("NEO98 HOST full screen of a program ends (%s)\n", why);
        fflush(stdout);
    };
    // The client area of the zoomed window is the canvas; gone or minimized,
    // the zoom ends.
    auto serve_zoom = [&]() {
        if (!zoomed) return;
        Gdi::RECT client{};
        if (!User::IsWindow(zoomed) || !User::IsVisible(zoomed) || (User::Style(zoomed) & 0x20000000u) ||
            !User::ClientScreenRect(zoomed, &client) || client.right - client.left < 8 || client.bottom - client.top < 8) {
            zoom_end("the program's window is gone");
            return;
        }
        int sw = 0, sh = 0, depth = 0;
        User::ScreenMode(sw, sh, depth);
        if (client.left < 0) client.left = 0;
        if (client.top < 0) client.top = 0;
        if (client.right > sw) client.right = sw;
        if (client.bottom > sh) client.bottom = sh;
        // Of a DOS window only the machine's picture, not the toolbar.
        if (const int strip = X86::Application::DosWindowStrip(zoomed); strip > 0 && client.bottom - client.top > strip + 8)
            client.top += strip;
        if (client.right - client.left < 8 || client.bottom - client.top < 8) { zoom_end("the window left the screen"); return; }
        canvas_crop = true;
        canvas_crop_x = client.left;
        canvas_crop_y = client.top;
        if (canvas_w != client.right - client.left || canvas_h != client.bottom - client.top) {
            canvas_w = client.right - client.left;
            canvas_h = client.bottom - client.top;
            printf("NEO98 HOST full screen of a program shows %d,%d,%dx%d of hwnd=%x\n", canvas_crop_x, canvas_crop_y,
                   canvas_w, canvas_h, zoomed);
            fflush(stdout);
        }
    };
    // A point of the overlay belongs to the guest when a window of the guest
    // is visible there, or while the guest holds the mouse: a button that
    // went down on a window, or a capture (an open menu) while the
    // application is the active one.
    // With a Mac window per guest window only the first holds: a press beside
    // an open menu lands on the Mac, and the guest loses the activation as
    // under Win98 by a press on a window of another program
    // (host_activation).
    auto seamless_takes = [&](int x, int y) {
        if (seamless_buttons) return true;
        if (!seamless_windows && User::CaptureOperation(0) && (script_pointer || HostSeamless::Active())) return true;
        return mask.Opaque(x, y);
    };
    // The guest holds the mouse: a program that puts the pointer
    // back after every movement (SetCursorPos, the mouse emulation of DINPUT)
    // takes the Mac pointer with it, and the hand never reaches the menu bar
    // or another window. Ctrl+Option lets the mouse go, as in Parallels and
    // VMware: the host stops moving the Mac pointer for the guest, shows it,
    // and keeps the mouse from the guest until a press in the window takes it
    // back. Losing the activation lets go as well.
    struct MouseHold {
        std::chrono::steady_clock::time_point warps[4]{};
        unsigned next = 0;
        bool released = false;
        unsigned released_for = 0; // the foreground window at that moment
        bool released_clip = false; // a pointer clip held it
        int shown = 0;             // what the title and the menu say: 0 free, 1 held, 2 let go
    } mouse_hold;
    // Held: a pointer clip smaller than the screen, or four SetCursorPos
    // within a second, the last of them not three seconds ago (a program that
    // recentres only moves the pointer while the hand moves).
    auto mouse_recentres = [&]() {
        if (mouse_hold.next < 4) return false;
        const auto oldest = mouse_hold.warps[mouse_hold.next & 3], newest = mouse_hold.warps[(mouse_hold.next + 3) & 3];
        return newest - oldest < std::chrono::seconds(1) &&
               std::chrono::steady_clock::now() - newest < std::chrono::seconds(3);
    };
    auto mouse_held = [&]() {
        if (X86::Application::DinputMouseExclusive()) return true;
        if (User::PointerClipActive()) {
            Gdi::RECT clip{};
            int sw = 0, sh = 0, depth = 0;
            User::ScreenMode(sw, sh, depth);
            if (User::PointerClip(&clip) && (clip.right - clip.left < sw || clip.bottom - clip.top < sh)) return true;
        }
        return mouse_recentres();
    };
    bool title_stale = false;
    std::string shown_program_title;
    auto mouse_release = [&](const char *why) {
        if (mouse_hold.released || !mouse_held()) return false;
        mouse_hold.released = true;
        mouse_hold.released_for = User::GetForegroundWindow();
        mouse_hold.released_clip = User::PointerClipActive();
        host_pointer(false);
        printf("NEO98 HOST mouse released (%s)\n", why);
        fflush(stdout);
        return true;
    };
    // A press in the window: the guest has the mouse again.
    auto mouse_capture = [&](const char *why) {
        if (!mouse_hold.released) return;
        mouse_hold.released = false;
        printf("NEO98 HOST mouse captured (%s)\n", why);
        fflush(stdout);
    };
    auto serve_mouse_hold = [&]() {
        // The program that held the mouse is gone: nothing is left to take back.
        if (mouse_hold.released && mouse_hold.released_for && !User::IsWindow(mouse_hold.released_for) && !mouse_held())
            mouse_capture("the window that held it is gone");
        if (mouse_hold.released && mouse_hold.released_clip && !mouse_held()) mouse_capture("the pointer clip is gone");
        mouse_released_now = mouse_hold.released;
        const int now = mouse_hold.released ? 2 : mouse_held() ? 1 : 0;
        if (now == mouse_hold.shown) return;
        mouse_hold.shown = now;
        menu_state.mouse = now;
        HostMenu::Update(menu_state);
        title_stale = true;
        printf("NEO98 HOST mouse hold=%d released=%d\n", now ? 1 : 0, now == 2 ? 1 : 0);
        fflush(stdout);
    };
    // The Mac took the activation from NEO98 (another application came to
    // the front, a press went to a Mac window) or gave it back. For the guest
    // a window of another program took the foreground: an open menu ends, the
    // active window is deactivated, a dropped list of a combo box closes on
    // WM_KILLFOCUS (X86::Application::HostActivation).
    bool guest_active = true;
    // The pause of the whole machine. machine_pause is the
    // one switch of the host: the menu, the key, the script step and the
    // loss of the focus all go through it. pause_auto: this pause began when
    // NEO98 lost the focus and ends when it has it again.
    // pause_took_mouse: the pause let go a mouse the guest held, and gives it
    // back when it ends.
    bool machine_paused = false, pause_auto = false, pause_took_mouse = false;
    auto pause_started = std::chrono::steady_clock::now();
    std::function<void(bool, const char *)> machine_pause;
    auto focus_pause = [&](bool focused) {
        if (!focused) {
            if (!pause_unfocused || machine_paused) return;
            machine_pause(true, "NEO98 lost the focus");
            pause_auto = true;
        } else if (pause_auto && machine_paused) machine_pause(false, "NEO98 has the focus again");
    };
    auto host_activation = [&](bool active, const char *why) {
        if (active == guest_active) return;
        guest_active = active;
        if (active) focus_pause(true);
        if (!active) { User::TouchCancelAll(true); mouse_release("another application is active"); }
        X86::Application::HostActivation(active);
        nudge();
        printf("NEO98 HOST seamless activation=%d (%s)\n", active ? 1 : 0, why);
        fflush(stdout);
        if (!active) focus_pause(false);
    };
    // The cursor of the Mac follows the cursor the guest has set.
    auto seamless_cursor = [&]() {
        if (!overlay_on) return;
        // A SetCursor of the guest shows the drawn pointer again.
        User::ShowPointer(false);
        static unsigned picture[128 * 128];
        HostSeamless::Cursor now;
        now.shown = X86::Application::HostPointer(now.identifier, now.handle, now.width, now.height, now.hot_x, now.hot_y,
                                                  picture, 128 * 128);
        if (now.shown && now.width > 0) now.argb.assign(picture, picture + size_t(now.width) * size_t(now.height));
        // A mouse the user let go (Ctrl+Option) is the Mac's arrow.
        if (mouse_released_now) { now = HostSeamless::Cursor(); now.shown = true; now.identifier = 32512; }
        if (cursor_known && now == shown_cursor) return;
        cursor_known = true;
        shown_cursor = now;
        HostSeamless::SetCursor(now);
        const char *name = HostSeamless::CursorName(now);
        if (!strcmp(name, "custom"))
            printf("NEO98 HOST seamless cursor=custom %dx%d hot=%d,%d\n", now.width, now.height, now.hot_x, now.hot_y);
        else printf("NEO98 HOST seamless cursor=%s\n", name);
        fflush(stdout);
    };
    auto seamless_activation = [&]() {
        // Outside the overlay the window is an ordinary one and the guest
        // keeps its foreground, as before.
        if (!overlay_on) { host_activation(true, "no overlay"); return; }
        int x = 0, y = 0;
        if (script_pointer || !HostSeamless::Pointer(window, x, y)) return; // no window server, or a script decides
        const bool active = HostSeamless::Active();
        // A press on a guest window that brings NEO98 back activates that
        // window itself.
        if (active && seamless_buttons) { guest_active = true; return; }
        host_activation(active, active ? "NEO98 is the active application" : "another application is active");
    };
    // Where the Mac pointer is decides, between two events, whether the next
    // click is the guest's or goes through the window to the Mac, and whose
    // pointer shows.
    auto seamless_pointer = [&]() {
        if (!overlay_on) return;
        // With a Mac window per guest window the window of SDL shows nothing
        // and takes no click; the item windows have the mouse. A script still
        // gets the answer of the mask.
        if (seamless_windows && !script_pointer) {
            HostSeamless::ClickThrough(window, true);
            return;
        }
        int x = script_pointer_x, y = script_pointer_y;
        if (!script_pointer && !HostSeamless::Pointer(window, x, y)) return;
        const bool opaque = seamless_takes(x, y);
        if (pointer_known && opaque == pointer_opaque) return;
        pointer_known = true;
        pointer_opaque = opaque;
        HostSeamless::ClickThrough(window, !opaque);
        // The pointer is the Mac's, with the cursor of the guest over a
        // window of the guest (seamless_cursor).
        User::ShowPointer(false);
        host_pointer(false);
        HostSeamless::ApplyCursor(opaque);
        if (script_pointer || pointer_trace) {
            printf("NEO98 HOST seamless pointer=%d,%d opaque=%d through=%d\n", x, y, opaque ? 1 : 0, opaque ? 0 : 1);
            fflush(stdout);
        }
    };
    // What the Mac's mouse did in the Mac window of a guest window
    // (HostSeamless::NextMouse), in screen pixels of the guest.
    std::function<void(const HostSeamless::MouseEvent &)> item_mouse;
    {
        int width = 800, height = 600;
        User::PixelsSize(width, height);
        canvas_w = 0;
        User::SetHostFrameFollowsMode(seamless);
        X86::Application::SetHostKeepsDosWindows(seamless);
        serve_seamless(false, User::Pixels());
        if (!overlay_on) follow_canvas(width, height);
        SDL_ShowWindow(window);
    }
    signal(SIGUSR1, on_dump_signal);
    signal(SIGUSR2, on_report_signal);
    signal(SIGHUP, on_quit_signal);
    host_app = a;
    host_argc = argc;
    host_argv = argv;
    host_start = std::chrono::steady_clock::now();
    X86::Application::SetCrashHook(nullptr, on_crash);
    // A person sits in front of this runner: an unhandled exception shows
    // the error box of Win98 and ends only its process.
    X86::Application::SetFaultBox(true);
    X86::Application::SetSessionEndHook(nullptr, on_session_end);
    bool running = true, announced = false, reboot = false;
    // A game machine ends when the last process of its game has ended: a
    // launcher that starts the game and leaves, a setup helper, a video
    // player are all the game. Processes Win98 keeps out of the task list
    // (DDHELP) do not count, as in LiveChildren.
    auto game_goes_on = [&]() { return !game_machine_folder.empty() && X86::Application::LiveChildren() != 0; };
    const char *reboot_reason = "reboot";
    unsigned shots = 0, held_vk[256] = {}, held_scan[256] = {};
    int last_mouse_x = -1, last_mouse_y = -1;
    unsigned last_mouse_buttons = 0;
    // A guest that calls SetCursorPos moves the pointer of the window with it,
    // the way Win98 moves the real pointer. Without this the next SDL motion
    // event carries the distance between the place the guest put the cursor
    // and the place the user's hand is, and a program that centres the cursor
    // after every movement, as the mouse emulation of DINPUT does, reads that
    // distance as its step.
    // last_mouse_x/y (and the warp's own idea of the pointer) are always
    // canvas pixels, the same space as every SDL event; x, y here arrive in
    // the guest's own current screen (its SetCursorPos argument) and are
    // converted with GuestToCanvas before anything compares or warps with
    // them.
    struct Warp { SDL_Window *window; SDL_Renderer *renderer; int *x; int *y; MouseHold *hold; } warp{window, renderer, &last_mouse_x, &last_mouse_y, &mouse_hold};
    X86::Application::tolerate_missing_exports = true;
    X86::Application::SetHostCursorProvider(&warp, [](void *context, int x, int y) {
        auto &w = *static_cast<Warp *>(context);
        int nx = x, ny = y;
        GuestToCanvas(nx, ny);
        if (cursor_trace) { printf("NEO98 HOST CURSOR warp guest=%d,%d canvas=%d,%d\n", x, y, nx, ny); fflush(stdout); }
        w.hold->warps[w.hold->next++ & 3] = std::chrono::steady_clock::now();
        // Let go, the Mac pointer is the hand's alone.
        if (w.hold->released) return;
        if (*w.x == nx && *w.y == ny) return;
        *w.x = nx; *w.y = ny;
        int wx = 0, wy = 0;
        CanvasToWindow(LayoutFor(w.window, w.renderer), nx, ny, wx, wy);
        if (pointer_trace) {
            // SDL3's own test for a warp it may turn into relative mode: the
            // window centre (floor..ceil of half the size) twice within 30 ms
            // while the cursor is hidden.
            static auto last_centre = std::chrono::steady_clock::time_point();
            int ww = 0, wh = 0;
            SDL_GetWindowSize(w.window, &ww, &wh);
            const bool centre = wx >= ww / 2 && wx <= (ww + 1) / 2 && wy >= wh / 2 && wy <= (wh + 1) / 2;
            const auto now = std::chrono::steady_clock::now();
            const double since = last_centre.time_since_epoch().count() == 0 ? -1.0 :
                std::chrono::duration<double, std::milli>(now - last_centre).count();
            if (centre) last_centre = now;
            printf("NEO98 HOST POINTER warp window=%d,%d size=%dx%d centre=%d hidden=%d since-centre-ms=%.1f\n", wx, wy, ww,
                   wh, centre ? 1 : 0, SDL_ShowCursor(SDL_QUERY) == SDL_DISABLE ? 1 : 0, centre ? since : -1.0);
            fflush(stdout);
        }
        SDL_WarpMouseInWindow(w.window, wx, wy);
    });
    bool held_extended[256] = {};
    auto last_present = std::chrono::steady_clock::now();
    // Hang observation: the guest is considered stuck when it has dispatched no
    // API call for HangSeconds while input waits for it or a thread is still
    // runnable. It is shown and reported; nothing reboots by itself.
    const auto HangSeconds = std::chrono::seconds(3);
    unsigned last_calls = HostPacing::TotalCalls(*a);
    auto last_call_time = host_start, last_input_time = host_start, waiting_since = host_start;
    bool was_waiting = false;
    auto deliver_touch = [&](unsigned long long device, unsigned long long finger, unsigned phase,
                             int x, int y, int pressure, unsigned timestamp) {
        CanvasToGuest(x, y);
        const auto mouse = User::TouchInput(device, finger, phase, x, y, timestamp, pressure);
        if (mouse.activate) X86::Application::RequestForeground(mouse.activate);
        if (mouse.emit) a->MouseButtons(mouse.x, mouse.y, mouse.buttons);
        last_input_time = std::chrono::steady_clock::now();
        pacing.Input();
    };
    auto request_report = [&](const char *reason) {
        char path[1024] = {};
        write_report(*a, reason, path, sizeof path);
        printf("NEO98 HOST report %s\n", path);
        fflush(stdout);
    };
    // Every reboot writes its report; the media change without one.
    const bool report_on_reboot = true;
    auto view_changed = [&]() {
        menu_state.scale = scale;
        menu_state.fullscreen = fullscreen;
        menu_state.seamless = seamless;
        HostMenu::Update(menu_state);
        const auto layout = LayoutFor(window, renderer);
        printf("NEO98 HOST view scale=%d fullscreen=%d window=%dx%d guest=%d,%d,%dx%d\n", scale, fullscreen ? 1 : 0,
               layout.window_w, layout.window_h, layout.guest.x, layout.guest.y, layout.guest.w, layout.guest.h);
        fflush(stdout);
    };
    // The app keeps what the menu changed in its settings, so the next start
    // from the Finder has the same view, cable and media. The disc is kept
    // by the path the person chose, not by the mount point of its image.
    std::string cdrom_source = app_mode && !settings.cdrom.empty() && !menu_state.cdrom.empty() ? settings.cdrom
                                                                                               : menu_state.cdrom;
    // A file opened from outside: a double click in the Finder, a file
    // dropped onto the window, File > Open program. A disc image goes
    // into the CD-ROM drive. Anything else is started by its guest path the
    // way Explorer starts it (src/x86_host_open.cpp); a file outside the
    // drives gets its folder mounted under the next free letter first, as
    // "Choose drive D:" mounts one. The start waits for the shell.
    std::vector<std::string> pending_opens;
    // Program files opened from the Mac, by their path here. `finder_opens`
    // came by a double click in the Finder and may belong into a game
    // machine, or the person is asked once; `game_opens` came by the Finder's
    // entry "Open as game machine". serve_game_machines takes both.
    std::vector<std::string> finder_opens, game_opens;
    if (!game_open_at_start.empty()) {
        game_opens.push_back(game_open_at_start);
        setenv("NEO98_GAME_OPEN_DONE", "1", 1);
    }
    // D: mounted for an opened file shows in the menu and is not a setting.
    bool drive_d_session = false;
    // Cmd+X, Cmd+C and Cmd+V reach the guest as Ctrl+X, Ctrl+C and Ctrl+V.
    // The Command key is the guest's Windows key and went down there when
    // the person pressed it; from the chord until Command is released the
    // guest sees the Windows key up. Ctrl goes down before the Windows key
    // goes up, so the shell does not take the release for a press of the
    // Windows key alone (the Start menu).
    bool command_chord = false;
    auto guest_modifiers = [&]() {
        unsigned mask = modifier_mask(german);
        if (!(mask & (8u | 128u))) command_chord = false;
        if (command_chord) mask &= ~(8u | 128u);
        return mask;
    };
    auto guest_chord = [&](unsigned key) {
        const unsigned scan = key == 'X' ? 0x2d : key == 'C' ? 0x2e : 0x2f;
        command_chord = true;
        const unsigned mask = guest_modifiers();
        a->KeyboardModifiers(mask | 1);
        a->RawKey(key, 0, true, scan, false);
        a->RawKey(key, 0, false, scan, false);
        a->KeyboardModifiers(mask);
        last_input_time = std::chrono::steady_clock::now();
        printf("NEO98 HOST edit key ctrl+%c\n", char(key | 0x20));
        fflush(stdout);
    };
    // The guest path of a file of this Mac. A file outside the drives gets
    // `directory` mounted under the next free letter for the session.
    auto guest_path = [&](const std::string &path, const std::string &directory, std::string &guest) {
        if (GuestPathOf(path, guest)) return true;
        const char letter = FreeLetter();
        if (!letter || !SwapMedium(*a, letter, directory, false, false) || !GuestPathOf(path, guest)) return false;
        dos_directories[letter - 'A'] = directory;
        refresh_dos_drives();
        if (letter == 'D') {
            menu_state.drive_d = directory;
            drive_d_session = true;
            HostMenu::Update(menu_state);
        }
        return true;
    };
    // `finder`: the file came from the Mac side (Finder, a second start, a
    // drop onto the window), not from File > Open program.
    auto open_path = [&](const std::string &given, bool properties = false, bool finder = false) {
        // A game machine runs its game and nothing else. What is opened here
        // (a file dropped onto its window, File > Open program) goes to
        // the normal machine: handed to its host when it runs, else started
        // with it.
        if (!game_machine_folder.empty() && !properties) {
            const std::string normal = settings.game_source, path = RealPath(given);
            const bool runs = HostGameMachine::MachineRunning(normal);
            bool okay = false;
            if (runs) okay = HostSingle::Hand(HostSettings::DriveC(normal), path);
            else okay = HostSettings::Ready(normal) &&
                        HostGameMachine::Spawn({HostSettings::Executable(), "--machine", normal}, {"NEO98_HOST_OPEN_AFTER_SETUP=" + path});
            printf("NEO98 HOST game machine: open %s %s the normal machine %s%s\n", path.c_str(), runs ? "handed to" : "starts",
                   normal.c_str(), okay ? "" : " (failed)");
            fflush(stdout);
            return;
        }
        // A program file from the Finder: its game machine, the question, or
        // on to the normal way below (docs/BUILDING.md, "Double-click").
        if (finder && !properties && !games_of.empty() && HostGameMachine::ProgramName(given)) {
            const std::string file = RealPath(given);
            if (struct stat found = {}; !stat(file.c_str(), &found) && S_ISREG(found.st_mode)) {
                finder_opens.push_back(file);
                return;
            }
        }
        // Somebody opens a program: the machine has to run for it.
        if (machine_paused) machine_pause(false, "a file was opened");
        if (properties && given.size() > 2 && given[1] == ':' && given[2] == '\\') {
            const unsigned owner = User::GetForegroundWindow();
            const char *active = X86::Application::DosSettingsPath(owner);
            a->RequestDosSettings(given.c_str(), active && given == active ? owner : 0);
            return;
        }
        const std::string path = RealPath(given);
        struct stat info = {};
        if (stat(path.c_str(), &info) != 0) {
            printf("NEO98 HOST open %s: not found\n", given.c_str());
            fflush(stdout);
            return;
        }
        const bool folder = S_ISDIR(info.st_mode);
        const char *dot = strrchr(path.c_str(), '.');
        if (!properties && !folder && dot && (!strcasecmp(dot, ".iso") || !strcasecmp(dot, ".cue"))) {
            HostMenu::Request request;
            request.action = HostMenu::Action::InsertCdrom;
            request.path = path;
            HostMenu::Post(request);
            printf("NEO98 HOST open %s -> CD-ROM\n", path.c_str());
            fflush(stdout);
            return;
        }
        std::string guest;
        if (const std::string directory = folder ? path : HostSettings::Parent(path); !guest_path(path, directory, guest)) {
            printf("NEO98 HOST open %s: no drive for %s\n", path.c_str(), directory.c_str());
            fflush(stdout);
            return;
        }
        printf("NEO98 HOST open %s -> %s\n", path.c_str(), guest.c_str());
        fflush(stdout);
        if (properties) a->RequestDosSettings(guest.c_str());
        else pending_opens.push_back(guest);
    };
    auto serve_opens = [&]() {
        if (pending_opens.empty() || !User::LiveWindows()) return;
        if (User::ExclusiveMode() || User::ScreenModeActive() || zoomed) {
            pending_opens.clear();
            HostMenu::Notice("Another fullscreen app is not supported",
                             "Neo98 does not currently support multiple fullscreen games. "
                             "Exit fullscreen in the running game, then open the other app again.");
            return;
        }
        const unsigned tray = User::FindWindow("Shell_TrayWnd", nullptr);
        if (!(tray && User::IsVisible(tray)) &&
            std::chrono::steady_clock::now() - host_start < std::chrono::seconds(20))
            return;
        for (const auto &guest : pending_opens) X86::Application::RequestOpen(guest.c_str());
        pending_opens.clear();
    };
    // The files macOS launched the app with (OpenWithoutMachine took them
    // out of SDL's queue).
    for (const auto &file : launch_opens) open_path(file, false, true);
    launch_opens.clear();
    if (const char *kept = getenv("NEO98_HOST_OPEN_AFTER_SETUP")) {
        const std::string text = kept;
        unsetenv("NEO98_HOST_OPEN_AFTER_SETUP");
        for (size_t from = 0; from < text.size();) {
            const size_t end = std::min(text.find('\n', from), text.size());
            if (end > from) open_path(text.substr(from, end - from), false, true);
            from = end + 1;
        }
    }
    auto remember = [&]() {
        if (!app_mode) return;
        settings.scale = scale;
        settings.fullscreen = fullscreen;
        settings.seamless = seamless;
        settings.seamless_taskbar = seamless_taskbar;
        settings.seamless_windows = seamless_windows;
        settings.seamless_icons = seamless_icons;
        settings.wheel_focus = wheel_focus;
        settings.seamless_dock = seamless_dock;
        settings.online = menu_state.online;
        settings.wifi = menu_state.wifi;
        settings.microphone = menu_state.microphone;
        settings.net_listen = menu_state.net_listen;
        settings.pause_unfocused = pause_unfocused;
        settings.cdrom = cdrom_source;
        if (!drive_d_session) settings.drives['D' - 'A'] = menu_state.drive_d;
        if (!HostSettings::Save(settings)) printf("NEO98 HOST settings: cannot write %s\n", HostSettings::File().c_str());
        else printf("NEO98 HOST settings saved\n");
        fflush(stdout);
    };
    // The requests of the menu bar, from a click, a key equivalent or the
    // NEO98_HOST_MENU test switch. Also served while a failed guest leaves
    // its last frame on screen.
    // What a second start on this drive C: handed over (tools/host_single.h):
    // the window comes to the front, a named program starts in this session.
    // The host also activates itself once when it starts, and so after the
    // execv of a restart: started as the child of a terminal, or re-executed,
    // its window came up behind the others and without the keyboard.
    const char *front_requested = "start";
    // --- The clipboard (docs/BUILDING.md, "Clipboard") ---
    // NEO98_HOST_PASTEBOARD=general|unique|NAME|off. Without it the Mac's own
    // pasteboard is used where there is a screen and none under another
    // driver: a test never reads or writes what the person copied.
    {
        const char *wanted = getenv("NEO98_HOST_PASTEBOARD");
        const char *driver = SDL_GetCurrentVideoDriver();
        if (!wanted) wanted = driver && !strcmp(driver, "cocoa") ? "general" : "off";
        if (strcmp(wanted, "off") && HostPasteboard::Open(wanted)) {
            static const X86::HostClipboard::Provider provider = {nullptr, ClipboardText, ClipboardFiles, ClipboardImage};
            X86::HostClipboard::SetProvider(&provider);
            printf("NEO98 HOST clipboard pasteboard=%s\n", HostPasteboard::Name().c_str());
            fflush(stdout);
        }
    }
    // A file the Mac copied, as the guest reaches it: below a mounted folder,
    // or with its folder mounted like the folder of a file opened from the
    // Finder. Nothing is copied before the guest pastes.
    clipboard_guest_path = [&](const std::string &given, std::string &guest) {
        const std::string path = RealPath(given);
        const bool okay = guest_path(path, HostSettings::Parent(path), guest);
        printf("NEO98 HOST clipboard file %s -> %s\n", path.c_str(), okay ? HostVolume::ToHost(guest).c_str() : "(no drive)");
        fflush(stdout);
        return okay;
    };
    // Both directions, once per turn of the loop. What a program of the guest
    // put on its clipboard goes to the pasteboard; a change of the pasteboard
    // that is not this host's own is offered to the guest, which fetches the
    // data when a program reads it.
    long pasteboard_seen = -1;
    unsigned clipboard_seen = X86::HostClipboard::Guest().generation;
    auto serve_clipboard = [&]() {
        if (!HostPasteboard::Active()) return;
        const auto &guest = X86::HostClipboard::Guest();
        if (guest.generation != clipboard_seen) {
            clipboard_seen = guest.generation;
            std::u16string text;
            for (unsigned i = 0; i < guest.text_units; ++i)
                if (!(guest.text[i] == '\r' && i + 1 < guest.text_units && guest.text[i + 1] == '\n'))
                    text += char16_t(guest.text[i]);
            std::vector<std::string> files;
            for (unsigned at = 0; at < guest.files_bytes;) {
                const std::string name = guest.files + at;
                at += unsigned(name.size()) + 1;
                std::string host;
                const bool okay = HostPathOf(name, host);
                printf("NEO98 HOST clipboard guest file %s -> %s\n", HostVolume::ToHost(name).c_str(), okay ? host.c_str() : "(not on this Mac)");
                if (okay) files.push_back(host);
            }
            const std::vector<unsigned char> image(guest.image, guest.image + guest.image_bytes);
            if (!text.empty() || !files.empty() || !image.empty()) {
                pasteboard_seen = HostPasteboard::Write(text.empty() ? nullptr : &text, files.empty() ? nullptr : &files,
                                                        image.empty() ? nullptr : &image);
                printf("NEO98 HOST clipboard to mac text=%zu files=%zu effect=%u image=%zu change=%ld\n", text.size(),
                       files.size(), guest.effect, image.size(), pasteboard_seen);
            } else printf("NEO98 HOST clipboard to mac: nothing the Mac can take\n");
            fflush(stdout);
        }
        static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(200)) return;
        last = now;
        const long count = HostPasteboard::ChangeCount();
        if (count == pasteboard_seen) return;
        pasteboard_seen = count;
        const unsigned kinds = HostPasteboard::Kinds();
        X86::HostClipboard::Offer(kinds);
        printf("NEO98 HOST clipboard from mac change=%ld text=%d files=%d image=%d\n", count, (kinds & HostPasteboard::Text) != 0,
               (kinds & HostPasteboard::Files) != 0, (kinds & HostPasteboard::Image) != 0);
        fflush(stdout);
    };
    // The switch. Stopping: the mouse goes back to the Mac, the clock, the
    // mixer and the input of the guest stop (X86::Application::PauseMachine),
    // the DOS machines park their threads; the main loop then leaves Pump
    // alone, which is all the Windows threads and the ARM64 processors there
    // are. Going on: the same backwards, and the keys and buttons that were
    // let go meanwhile are released for the guest, so none stays stuck. A
    // key that went down during the pause is not pressed afterwards.
    HostWindowsState::pause = [&](bool on, const char *why) { machine_pause(on, why); };
    HostWindowsState::paused = [&]() { return machine_paused; };
    machine_pause = [&](bool on, const char *why) {
        if (on == machine_paused) return;
        pause_auto = false;
        // Read while the clock stands: after going on the next timer
        // interrupt may already have moved the cell.
        const unsigned tick_standing = on ? 0 : X86::Application::TickCount();
        if (on) {
            pause_took_mouse = mouse_release("pause");
            pause_started = std::chrono::steady_clock::now();
            host_pointer(false);
            X86::Application::PauseMachine(true);
            HostDosVm::Pause(true);
            machine_paused = true;
        } else {
            HostDosVm::Pause(false);
            X86::Application::PauseMachine(false);
            machine_paused = false;
            // Whoever lets the machine go on after a load has gone on.
            state_start.loaded = false;
            int count = 0;
            const Uint8 *down = SDL_GetKeyboardState(&count);
            unsigned released = 0;
            for (unsigned usage = 0; usage < 128; ++usage) {
                if (!held_vk[usage] || (int(usage) < count && down[usage])) continue;
                a->RawKey(held_vk[usage], 0, false, held_scan[usage], held_extended[usage]);
                held_vk[usage] = 0;
                ++released;
            }
            a->KeyboardModifiers(guest_modifiers());
            const Uint32 state = SDL_GetGlobalMouseState(nullptr, nullptr);
            const unsigned buttons = (state & SDL_BUTTON_LMASK ? 1 : 0) | (state & SDL_BUTTON_RMASK ? 2 : 0) |
                                     (state & SDL_BUTTON_MMASK ? 4 : 0);
            if (last_mouse_buttons & ~buttons) {
                last_mouse_buttons &= buttons;
                seamless_buttons &= buttons;
                int mx = last_mouse_x < 0 ? 0 : last_mouse_x, my = last_mouse_y < 0 ? 0 : last_mouse_y;
                CanvasToGuest(mx, my);
                a->MouseButtons(mx, my, last_mouse_buttons);
                ++released;
            }
            // The mouse the pause let go is the guest's again, unless a press
            // took it back meanwhile or something else let it go. The Mac
            // pointer returns to where the guest's pointer stands, so the
            // next movement of the hand starts there: no jump of the
            // pointer, and no distance for a program that reads movements.
            // The warps of a program that recentres are as old as before
            // the pause, so it still counts as holding (mouse_recentres).
            for (auto &at : mouse_hold.warps)
                if (at.time_since_epoch().count()) at += std::chrono::steady_clock::now() - pause_started;
            if (pause_took_mouse && mouse_hold.released && last_mouse_x >= 0 && last_mouse_y >= 0) {
                mouse_capture("the pause is over");
                int wx = 0, wy = 0;
                CanvasToWindow(LayoutFor(window, renderer), last_mouse_x, last_mouse_y, wx, wy);
                SDL_WarpMouseInWindow(window, wx, wy);
                printf("NEO98 HOST pause pointer back canvas=%d,%d window=%d,%d\n", last_mouse_x, last_mouse_y, wx, wy);
            }
            pause_took_mouse = false;
            // The time it stood is no hang and no stall.
            last_call_time = last_input_time = waiting_since = std::chrono::steady_clock::now();
            was_waiting = false;
            pacing.Resumed();
            if (released) nudge();
            printf("NEO98 HOST pause keys settled released=%u\n", released);
        }
        menu_state.paused = on;
        HostMenu::Update(menu_state);
        title_stale = true;
        // fast-gates: the calls answered inside the engine so far, the way
        // GetTickCount, timeGetTime and QueryPerformanceCounter take.
        unsigned dos_machines = 0;
        const unsigned dos_sound = HostDosVm::AudioQueued(&dos_machines) + X86::Application::DosVmSoundQueued();
        printf("NEO98 HOST pause %s (%s) stood=%.3f s tick=%u fast-gates=%llu dos-machines=%u dos-sound=%u\n",
               on ? "on" : "off", why, X86::Application::PausedMicroseconds() / 1e6, on ? X86::Application::TickCount() : tick_standing,
               a->fast_gate_calls, dos_machines, dos_sound);
        fflush(stdout);
    };
    auto serve_single = [&]() {
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(250)) return;
        last = now;
        std::vector<std::string> lines;
        if (!HostSingle::Take(lines)) return;
        for (const auto &line : lines) {
            printf("NEO98 HOST second start: %s\n", line.c_str());
            fflush(stdout);
            // A program that is to run as a game machine.
            if (!line.compare(0, 5, "game ")) { game_opens.push_back(line.substr(5)); continue; }
            if (line.compare(0, 5, "open ")) continue;
            const std::string path = line.substr(5);
            if (path.size() > 2 && path[1] == ':' && path[2] == '\\') {
                printf("NEO98 HOST open %s\n", path.c_str());
                fflush(stdout);
                pending_opens.push_back(path);
            } else open_path(path, false, true);
        }
        front_requested = "second start";
    };
    // --- Machine > Game machines ---
    // The program in front, for "New game machine for...": by guest path.
    // The shell itself is no game.
    auto front_program = [&]() {
        char path[260] = {};
        unsigned front = User::GetForegroundWindow();
        if (!front || !X86::Application::HostWindowProgramPath(front, path, sizeof path)) return std::string();
        const std::string leaf = HostGameMachine::Leaf(path);
        return !strcasecmp(leaf.c_str(), "EXPLORER.EXE") ? std::string() : std::string(path);
    };
    // Starts the game machine of a folder as a host of its own, through the
    // link of its launcher bundle: macOS then shows it under the name and
    // with the icon of the game. One that runs comes to the front: its lock
    // hands the start over.
    auto start_game_machine = [&](const std::string &folder) {
        const bool okay = HostGameMachine::Spawn(HostGameMachine::StartLine(folder, HostSettings::Executable()), {});
        printf("NEO98 HOST game machine start name=%s %s\n", HostGameMachine::Leaf(folder).c_str(), okay ? "ok" : "failed");
        fflush(stdout);
        return okay;
    };
    // Makes a game machine for make.program (and make.program_file) from
    // this machine as it is now; asks for the name where make.name is empty.
    // `notice` tells the person where it is (not when it starts at once).
    auto make_game_machine = [&](HostGameMachine::Request make, bool notice, std::string &folder) {
        make.machine = games_of;
        // The suggestion is a name that is free: a second game machine for
        // the same program is "Name 2".
        if (make.name.empty()) {
            const std::string suggested = HostGameMachine::FreeName(HostGameMachine::Directory(games_of),
                                                                    HostGameMachine::SuggestedName(make.program, make.program_file));
            if (!HostGameMenu::AskName(make.program, suggested, make.name)) {
                printf("NEO98 HOST game machine for %s: cancelled\n", make.program.c_str());
                fflush(stdout);
                return false;
            }
        }
        // The view, the cable and the drives as they are now.
        if (app_mode) make.source = settings;
        make.source.scale = scale;
        make.source.fullscreen = fullscreen;
        make.source.seamless = seamless;
        make.source.seamless_windows = seamless_windows;
        make.source.online = menu_state.online;
        make.source.net_listen = menu_state.net_listen;
        make.source.pause_unfocused = pause_unfocused;
        make.source.cdrom = cdrom_source;
        for (int i = 3; i < 26; ++i)
            make.source.drives[i] = Volume::Exists(char('A' + i)) && Volume::Type(char('A' + i)) != Volume::Cdrom ? host_roots[i]
                                                                                                            : std::string();
        // The registry changes this machine still holds in memory
        // belong into the copy of the journal.
        Files::System().Flush();
        Registry::Flush();
        const auto before = std::chrono::steady_clock::now();
        HostGameMachine::Result result;
        const bool made = HostGameMachine::Create(make, HostSettings::Executable(), result);
        const long ms = long(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count());
        if (!made) {
            printf("NEO98 HOST game machine create failed: %s\n", result.error.c_str());
            HostMenu::Notice("Game machine not created", result.error.c_str());
        } else {
            folder = result.folder;
            printf("NEO98 HOST game machine created name=%s program=%s ms=%ld folder=%s\n", result.name.c_str(),
                   make.program.c_str(), ms, result.folder.c_str());
            // The icon of the game, from its program file.
            if (int width = 0, height = 0; GameMachineIcon(result.folder, make.program_file, width, height))
                printf("NEO98 HOST game machine icon %dx%d from the file %s\n", width, height, make.program_file.c_str());
            HostGameMenu::Refresh(HostGameMachine::Leaf(front_program()));
            if (notice)
                HostMenu::Notice(("Game machine \"" + result.name + "\" created").c_str(),
                                 ("Start: Machine > Game machines > " + result.name + " > Start, or double-click in Finder: " +
                                  HostGameMachine::Launcher(result.folder) + ".").c_str());
        }
        fflush(stdout);
        return made;
    };
    // --- Programs opened from the Mac (docs/BUILDING.md, "Double-click") ---
    // The game machine of a program file starts: the only one, or the one
    // the person chooses among several. False when the program has none.
    auto run_in_game_machine = [&](const std::string &file) {
        std::string guest;
        GuestPathOf(file, guest);
        const auto machines = HostGameMachine::FindFor(games_of, file, guest);
        if (machines.empty()) return false;
        std::string chosen = machines[0].name;
        if (machines.size() > 1) {
            std::vector<std::string> names;
            for (const auto &machine : machines) names.push_back(machine.name);
            if (!HostGameMenu::Choose(HostGameMachine::Leaf(file), names, chosen)) {
                printf("NEO98 HOST game machine: %zu for %s, none chosen\n", machines.size(), file.c_str());
                fflush(stdout);
                return true;
            }
            printf("NEO98 HOST game machine: %zu for %s, chosen %s\n", machines.size(), file.c_str(), chosen.c_str());
        }
        for (const auto &machine : machines)
            if (machine.name == chosen) start_game_machine(machine.folder);
        return true;
    };
    // A new game machine for a program file, started at once. A file outside
    // the drives gets its folder mounted as for a double click; the game
    // machine keeps that drive in its settings like every drive mounted now.
    auto open_new_game_machine = [&](const std::string &file) {
        HostGameMachine::Request make;
        make.program_file = file;
        if (!guest_path(file, HostSettings::Parent(file), make.program)) {
            printf("NEO98 HOST game machine: no drive for %s\n", file.c_str());
            HostMenu::Notice("Game machine not created", "No drive letter is available for the program's folder.");
            return;
        }
        std::string folder;
        if (make_game_machine(make, false, folder)) start_game_machine(folder);
    };
    auto serve_mac_opens = [&]() {
        if (finder_opens.empty() && game_opens.empty()) return;
        std::vector<std::string> finder, games;
        finder.swap(finder_opens);
        games.swap(game_opens);
        // The entry of the Finder "Open as game machine": the game
        // machine the program has, else a new one.
        for (const auto &given : games) {
            const std::string file = RealPath(given);
            struct stat found = {};
            if (stat(file.c_str(), &found) || !S_ISREG(found.st_mode) || !HostGameMachine::ProgramName(file)) {
                printf("NEO98 HOST game machine: %s is no program file\n", file.c_str());
                HostMenu::Notice("Game machine not created", "A game machine requires the game's executable file (.exe or .com).");
                continue;
            }
            printf("NEO98 HOST open as game machine %s\n", file.c_str());
            if (!run_in_game_machine(file)) open_new_game_machine(file);
        }
        // A double click in the Finder. A program that has a game machine
        // runs in it. Any other is asked about once, unless the question is
        // off, the program was answered "normally" before, its name says it
        // installs something, or it is a program of Windows itself.
        for (const auto &file : finder) {
            if (run_in_game_machine(file)) continue;
            std::string guest;
            GuestPathOf(file, guest);
            bool known = false;
            for (const auto &program : settings.open_normal) known = known || HostGameMachine::SameFile(program, file);
            const bool windows = !strncasecmp(guest.c_str(), "C:\\WINDOWS\\", 11);
            const bool installer = HostGameMachine::InstallerName(file);
            // The command line of development keeps no settings and so no
            // answers: it does not ask.
            if (!app_mode) {
                open_path(file);
                continue;
            }
            if (settings.open_ask && !known && !installer && !windows) {
                bool never = false;
                const auto answer = HostGameMenu::AskOpen(HostGameMachine::Leaf(file), never);
                using HostGameMenu::OpenAs;
                if (answer != OpenAs::NotAsked)
                    printf("NEO98 HOST open %s asked: %s%s\n", file.c_str(),
                           answer == OpenAs::Game ? "game machine" : answer == OpenAs::Normal ? "normal" : "cancelled",
                           never && answer != OpenAs::Cancel ? ", never ask again" : "");
                if (answer == OpenAs::Cancel) continue;
                if (answer != OpenAs::NotAsked) {
                    if (never) {
                        settings.open_ask = false;
                        HostGameMenu::SetAsk(false);
                    }
                    if (answer == OpenAs::Normal) settings.open_normal.push_back(file);
                    if (never || answer == OpenAs::Normal) remember();
                    if (answer == OpenAs::Game) {
                        open_new_game_machine(file);
                        continue;
                    }
                }
            } else if (settings.open_ask)
                printf("NEO98 HOST open %s not asked: %s\n", file.c_str(), known ? "answered before" : installer ? "installer name" : "program of Windows");
            fflush(stdout);
            open_path(file);
        }
        fflush(stdout);
    };
    auto serve_game_machines = [&]() {
        if (games_of.empty()) return;
        static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(2);
        static std::string front_shown;
        if (const auto now = std::chrono::steady_clock::now(); now - last >= std::chrono::seconds(1)) {
            last = now;
            if (const std::string front = HostGameMachine::Leaf(front_program()); front != front_shown) {
                front_shown = front;
                HostGameMenu::Refresh(front);
            }
        }
        HostGameMenu::Request request;
        while (HostGameMenu::Next(request)) {
            using Kind = HostGameMenu::Request::Kind;
            if (request.kind == Kind::Open) {
                game_opens.push_back(request.program);
                continue;
            }
            if (request.kind == Kind::Ask) {
                settings.open_ask = !settings.open_ask;
                HostGameMenu::SetAsk(settings.open_ask);
                printf("NEO98 HOST open asks about game machines: %d\n", settings.open_ask ? 1 : 0);
                remember();
                continue;
            }
            if (request.kind == Kind::Create || request.kind == Kind::CreateFront) {
                HostGameMachine::Request make;
                // The program by its guest path and as a file of this Mac.
                if (request.kind == Kind::CreateFront) make.program = front_program();
                else if (request.program.size() > 3 && request.program[1] == ':' && request.program[2] == '\\') make.program = request.program;
                else {
                    make.program_file = RealPath(request.program);
                    GuestPathOf(make.program_file, make.program);
                }
                if (make.program_file.empty() && !make.program.empty()) HostPathOf(make.program, make.program_file);
                if (make.program.empty() || (request.kind == Kind::Create && !HostSettings::Exists(make.program_file))) {
                    printf("NEO98 HOST game machine: no program on a drive of the machine (%s)\n", request.program.c_str());
                    HostMenu::Notice("Game machine not created",
                                     request.kind == Kind::CreateFront
                                         ? "No program is in the foreground."
                                         : "The program must be on one of the machine's drives, such as drive C:.");
                    continue;
                }
                make.name = request.name;
                std::string folder;
                make_game_machine(make, true, folder);
                continue;
            }
            const std::string folder = HostGameMachine::Resolve(request.name, games_of);
            if (folder.empty()) {
                printf("NEO98 HOST game machine %s: not found\n", request.name.c_str());
                fflush(stdout);
                HostGameMenu::Refresh(front_shown);
                continue;
            }
            if (request.kind == Kind::Start) start_game_machine(folder);
            else if (request.kind == Kind::Reveal) {
                HostGameMenu::Reveal(HostGameMachine::Launcher(folder));
                printf("NEO98 HOST game machine reveal %s\n", HostGameMachine::Launcher(folder).c_str());
            } else if (request.kind == Kind::Desktop) {
                // An alias as the Finder makes one: it follows the launcher,
                // and the launcher is written again when NEO98 moves.
                std::string made, error;
                if (HostGameMenu::MakeAlias(HostGameMachine::Launcher(folder), request.program, made, error))
                    printf("NEO98 HOST game machine alias %s -> %s\n", made.c_str(), HostGameMenu::AliasTarget(made).c_str());
                else {
                    printf("NEO98 HOST game machine alias failed: %s\n", error.c_str());
                    HostMenu::Notice("Launcher not created", error.c_str());
                }
            } else if (request.kind == Kind::Delete) {
                std::string error;
                if (HostGameMachine::Running(folder)) error = "The game machine is still running. Close it first.";
                else if (const unsigned states = HostSaveState::Count(folder);
                         printf("NEO98 HOST game machine delete name=%s asks: %s\n", request.name.c_str(), HostGameMenu::DeleteQuestion(states).c_str()),
                         !HostGameMenu::ConfirmDelete(request.name, states)) { printf("NEO98 HOST game machine delete name=%s cancelled\n", request.name.c_str()); fflush(stdout); continue; }
                if (!error.empty() || !HostGameMachine::Delete(folder, error)) {
                    printf("NEO98 HOST game machine delete failed: %s\n", error.c_str());
                    HostMenu::Notice("Game machine not deleted", error.c_str());
                } else printf("NEO98 HOST game machine deleted name=%s folder=%s\n", request.name.c_str(), folder.c_str());
                HostGameMenu::Refresh(front_shown);
            }
            fflush(stdout);
        }
        serve_mac_opens();
    };
    // --- Saved states of a game machine -----------------
    // The supplier of the machine's part of a state: by what the game is.
    auto state_supplier = [&]() {
        return a->DosVmProcess() ? (X86::Application::DosSettingsStateOpen() ? HostMachineState::DosUi() : HostMachineState::Dos())
                                 : HostMachineState::Windows();
    };
    // Why no state can be taken right now; empty when one can. `machine`
    // gets what the supplier says about its machine.
    auto state_blocked = [&](std::vector<std::pair<std::string, std::string>> *machine = nullptr) {
        std::string reason;
        if (game_machine_folder.empty()) return std::string("States are available only in a game machine.");
        if (state_start.waiting || state_start.settling) return std::string("A state is being loaded.");
        if ((a->Status() == X86::Application::Exited || a->Status() == X86::Application::Failed) && !game_goes_on())
            return std::string("The game has exited.");
        const auto *supplier = state_supplier();
        // A DOS game with a Windows program beside it is more than its supplier holds.
        if ((supplier == HostMachineState::Dos() || supplier == HostMachineState::DosUi()) && X86::Application::LiveChildren())
            return std::string("Cannot save a state while DOS and Windows programs are running together.");
        if (!supplier->Ready(reason, machine)) return reason;
        return std::string();
    };
    // What the person is told: a line in the log now, and a note in the
    // picture for a few seconds (longer for a reason that takes reading).
    // While the window "States" is open it covers the picture, so what did
    // not work comes as a message there.
    struct StateNote {
        std::string title, detail;
        bool bad = false;
        double seconds = 0;
        unsigned serial = 0;
        std::chrono::steady_clock::time_point at;
    } state_note;
    auto state_say = [&](const std::string &title, const std::string &detail, bool bad) {
        printf("NEO98 HOST state %s: %s%s%s\n", bad ? "refused" : "notice", title.c_str(), detail.empty() ? "" : " ", detail.c_str());
        fflush(stdout);
        if (bad && HostStateWindow::IsOpen()) {
            HostMenu::Notice(title.c_str(), detail.c_str());
            return;
        }
        state_note.title = title;
        state_note.detail = detail;
        state_note.bad = bad;
        state_note.seconds = bad ? std::min(12.0, 4.0 + 0.05 * double(detail.size())) : 2.5;
        state_note.at = std::chrono::steady_clock::now();
        ++state_note.serial;
    };
    auto state_context = [&]() {
        HostSaveState::Context context;
        HostSaveState::ContextOf(game_machine_folder, StateRevision(), context);
        context.engines = HostMachineState::Engines();
        std::vector<std::pair<std::string, std::string>> machine;
        if (state_blocked(&machine).empty()) {
            context.game = state_supplier()->kind;
            context.machine = machine;
        }
        return context;
    };
    // A save in progress: the supplier was asked and has not answered yet.
    struct StateJob {
        bool active = false, was_paused = false, nudged = false;
        const HostMachineState::Supplier *supplier = nullptr;
        std::string kind, name;
        std::string then_load; // the state to load when this one is written
        std::chrono::steady_clock::time_point since;
        std::vector<std::pair<std::string, std::string>> machine;
    } state_job;
    bool state_reboot = false;
    // The process starts over with the state: the way of every restart of the
    // machine (execv with the lock kept), with --load-state. `kept` is the
    // state that holds what the machine was until now.
    auto state_restart = [&](const std::string &id, const std::string &kept) {
        setenv("NEO98_STATE_KEPT", kept.c_str(), 1);
        SetOption("--load-state", id);
        // The window comes back where it stands.
        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
            int x = 0, y = 0, w = 0, h = 0;
            SDL_GetWindowPosition(window, &x, &y);
            SDL_GetWindowSize(window, &w, &h);
            char place[96];
            snprintf(place, sizeof place, "%d,%d,%d,%d,%d,%d", x, y, w, h, canvas_w, canvas_h);
            setenv("NEO98_HOST_WINDOW_AT", place, 1);
        }
        printf("NEO98 HOST state load id=%s: the machine starts over with it, before=%s\n", id.c_str(), kept.c_str());
        fflush(stdout);
        state_reboot = true;
        reboot_reason = "load state";
        reboot = true;
        running = false;
    };
    // The picture of the game as it is: what the window of a DOS game shows,
    // in the pixels of its machine; the game's own where its supplier has
    // one; else the screen of the guest.
    auto state_picture = [&](std::vector<unsigned> &picture, unsigned &width, unsigned &height,
                             const HostMachineState::Supplier *capturing = nullptr) {
        const auto *supplier = capturing ? capturing : state_supplier();
        if (const unsigned *shown = nullptr; supplier == HostMachineState::Dos() && a->DosVmPicture(shown, width, height)) {
            picture.assign(shown, shown + size_t(width) * height);
            for (auto &pixel : picture) pixel &= 0xffffff;
        } else if (!supplier->Picture || !supplier->Picture(picture, width, height)) {
            int w = 0, h = 0;
            if (const auto *pixels = frame_pixels(a, w, h); pixels && w > 0 && h > 0) {
                width = unsigned(w);
                height = unsigned(h);
                picture.resize(size_t(w) * h);
                for (size_t i = 0; i < picture.size(); ++i)
                    picture[i] = unsigned(pixels[i * 4 + 2]) << 16 | unsigned(pixels[i * 4 + 1]) << 8 | pixels[i * 4];
            }
        }
    };
    // What the window "States" shows (tools/host_state_window.h).
    auto state_model = [&]() {
        HostStateWindow::Model model;
        model.game = HostGameMachine::Leaf(game_machine_folder);
        for (const auto &state : HostSaveState::List(state_context())) {
            HostStateWindow::Row row;
            row.id = state.id;
            row.name = state.name;
            row.parent = state.parent;
            row.kind = state.kind;
            row.game = state.game;
            row.time = state.time;
            row.usable = state.usable;
            row.damaged = state.damaged;
            row.current = state.current;
            row.reason = state.reason;
            row.revision = state.revision;
            if (state.has_picture) row.picture = HostSaveState::PictureFile(state.folder);
            model.rows.push_back(row);
        }
        model.save_reason = state_blocked();
        model.can_save = model.save_reason.empty();
        state_picture(model.live, model.live_width, model.live_height);
        model.keep_automatic = HostSaveState::KeepAutomatic;
        return model;
    };
    // Writes the state: the machine stands, everything it held in memory is
    // on the disk, and the layers are copied as they are at this moment.
    auto state_write = [&](const std::string &kind, const std::string &name, const std::vector<unsigned char> *bytes,
                           const std::vector<std::pair<std::string, std::string>> &machine, std::string &id, std::string &error,
                           const HostMachineState::Supplier *capturing = nullptr) {
        const auto *supplier = capturing ? capturing : state_supplier();
        Files::System().Flush();
        Registry::Flush();
        HostSaveState::Request request;
        request.name = name;
        request.kind = kind;
        request.revision = StateRevision();
        request.engine = supplier->Engine();
        request.machine = bytes;
        request.game = bytes ? supplier->kind : "off";
        request.machine_info = machine;
        if (bytes && a->DosVmProcess()) request.view.emplace_back("dos-vollbild", a->DosVmFullscreenNow() ? "1" : "0");
        // The files of the normal drives the state depends on: what its
        // supplier's machine read. The state of a DOS machine holds nothing
        // of the Windows side, which is built anew when it is loaded.
        const bool dos_alone = bytes && supplier == HostMachineState::Dos();
        for (char letter = 'C'; letter <= 'Z'; ++letter)
            if (auto *layer = HostVolume::Layer(letter))
                for (const auto &file : layer->BelowFiles()) {
                    if (dos_alone && !file.foreign) continue;
                    HostSaveState::BaseFile read;
                    read.drive = letter;
                    read.path = file.path;
                    read.size = file.size;
                    read.seconds = file.seconds;
                    read.nanoseconds = file.nanoseconds;
                    request.files.push_back(read);
                }
        std::vector<unsigned> picture;
        unsigned width = 0, height = 0;
        state_picture(picture, width, height, supplier);
        if (!picture.empty()) {
            request.picture = picture.data();
            request.picture_width = width;
            request.picture_height = height;
        }
        const auto before = std::chrono::steady_clock::now();
        const bool okay = HostSaveState::Save(game_machine_folder, request, id, error);
        printf("NEO98 HOST state %s id=%s kind=%s game=%s machine=%zu bytes files=%zu picture=%ux%u in %lld ms%s%s\n",
               okay ? "saved" : "save failed", id.empty() ? "-" : id.c_str(), kind.c_str(), request.game.c_str(), bytes ? bytes->size() : size_t(0),
               request.files.size(), width, height,
               (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count(),
               okay ? "" : ": ", okay ? "" : error.c_str());
        fflush(stdout);
        return okay;
    };
    // Machine > Save state. `then_load`: the state before a load.
    auto state_save = [&](const std::string &kind, const std::string &name, const std::string &then_load = std::string()) {
        if (state_job.active) return false;
        std::vector<std::pair<std::string, std::string>> machine;
        if (const std::string reason = state_blocked(&machine); !reason.empty()) {
            state_say("Not saved.", reason, true);
            return false;
        }
        state_job = StateJob();
        state_job.active = true;
        state_job.supplier = state_supplier();
        state_job.was_paused = machine_paused;
        state_job.kind = kind;
        state_job.name = name;
        state_job.then_load = then_load;
        state_job.machine = machine;
        state_job.since = std::chrono::steady_clock::now();
        state_job.supplier->Save();
        return true;
    };
    // Machine > Load state: what the machine is now is saved first (with
    // the machine where that can be done, its files alone else), then the
    // process starts over with the state.
    auto state_load = [&](const std::string &id) {
        if (state_job.active || state_start.waiting || state_start.settling) return false;
        HostSaveState::Entry wanted;
        for (const auto &state : HostSaveState::List(state_context()))
            if (state.id == id) wanted = state;
        if (wanted.id.empty()) {
            state_say("Not loaded.", "The state " + id + " does not exist.", true);
            return false;
        }
        if (!wanted.usable) {
            state_say("„" + wanted.name + "” could not be loaded.", wanted.reason, true);
            return false;
        }
        if (state_blocked().empty()) return state_save("auto", "", id);
        const bool was_paused = machine_paused;
        machine_pause(true, "a state is loaded");
        std::string kept, error;
        if (!state_write("auto", "", nullptr, {}, kept, error)) {
            state_say("Not loaded.", "The current state could not be saved, so nothing was loaded. " + error, true);
            if (!was_paused) machine_pause(false, "the load was given up");
            return false;
        }
        state_restart(id, kept);
        return true;
    };
    // The newest state that can be loaded, for "Load latest state": the
    // one the machine stands on when it has one, else the newest.
    auto state_last = [&]() {
        std::string found;
        long long newest = -1;
        for (const auto &state : HostSaveState::List(state_context())) {
            if (!state.usable || state.kind == "auto") continue;
            if (state.current) return state.id;
            if (state.time >= newest) { newest = state.time; found = state.id; }
        }
        return found;
    };
    // Machine > States …: the game stands while the window is open and
    // goes on when it closes, unless it stood before.
    bool state_list_paused = false;
    auto state_list_open = [&]() {
        if (game_machine_folder.empty() || state_job.active || state_start.waiting || state_start.settling) return;
        if (HostStateWindow::IsOpen()) {
            HostStateWindow::Open(state_model());
            return;
        }
        const bool was_paused = machine_paused;
        machine_pause(true, "the list of states");
        if (!HostStateWindow::Open(state_model())) {
            if (!was_paused) machine_pause(false, "the list of states could not be shown");
            return;
        }
        state_list_paused = !was_paused;
    };
    auto state_list_close = [&](bool go_on) {
        HostStateWindow::Close();
        if (state_list_paused && go_on && machine_paused) machine_pause(false, "the list of states was closed");
        state_list_paused = false;
        printf("NEO98 HOST state-window closed, the machine %s\n", machine_paused ? "stands" : "goes on");
        fflush(stdout);
    };
    auto serve_state_window = [&]() {
        HostStateWindow::Request request;
        while (HostStateWindow::Next(request)) {
            std::string error;
            using Kind = HostStateWindow::Request::Kind;
            if (request.kind == Kind::Close) {
                state_list_close(true);
            } else if (request.kind == Kind::Save) {
                state_save("manual", request.name);
            } else if (request.kind == Kind::Load) {
                // The window goes, the game stays as it stands; the load
                // starts the machine over.
                const bool resume = state_list_paused;
                state_list_paused = false;
                HostStateWindow::Close();
                if (!state_load(request.id) && resume && machine_paused) machine_pause(false, "the load was refused");
            } else if (request.kind == Kind::Delete) {
                if (!HostSaveState::Delete(game_machine_folder, request.id, error)) HostMenu::Notice("Not deleted.", error.c_str());
                else printf("NEO98 HOST state deleted id=%s\n", request.id.c_str());
                HostStateWindow::Update(state_model());
            } else if (request.kind == Kind::Rename) {
                if (!HostSaveState::Rename(game_machine_folder, request.id, request.name, error)) HostMenu::Notice("Not renamed.", error.c_str());
                else printf("NEO98 HOST state renamed id=%s name=\"%s\"\n", request.id.c_str(), HostSaveState::CleanName(request.name).c_str());
                HostStateWindow::Update(state_model());
            } else if (request.kind == Kind::Keep) {
                if (!HostSaveState::Keep(game_machine_folder, request.id, error)) HostMenu::Notice("Not changed.", error.c_str());
                else printf("NEO98 HOST state kept id=%s\n", request.id.c_str());
                HostStateWindow::Update(state_model());
            }
            fflush(stdout);
        }
    };
    // The note the picture shows now: what was said last, for its seconds,
    // fading at the end; and for as long as the machine stands after a load,
    // which state it is and how to go on. Zero for none, else a number that
    // changes with the text.
    auto state_note_now = [&](std::string &title, std::string &detail, bool &bad, double &alpha) -> unsigned {
        alpha = 1;
        if (state_start.loaded && machine_paused) {
            title = "Loaded: " + state_start.name;
            detail = "Press a key or click to continue. The previous state is listed as “Before loading” in States (⇧⌘Z).";
            bad = false;
            return 0x40000000u;
        }
        if (!state_note.serial) return 0;
        const double age = std::chrono::duration<double>(std::chrono::steady_clock::now() - state_note.at).count();
        if (age >= state_note.seconds) return 0;
        alpha = std::min(1.0, (state_note.seconds - age) / 0.4);
        title = state_note.title;
        detail = state_note.detail;
        bad = state_note.bad;
        return state_note.serial;
    };
    // After a load the machine stands until the person goes on: any key or a
    // click, or Pause in the menu.
    auto state_go_on = [&](const char *why) {
        if (!state_start.loaded || !machine_paused || HostStateWindow::IsOpen()) return false;
        machine_pause(false, why);
        return true;
    };
    // Every turn of the loop: a save that waits for its supplier, and a state
    // this start began with that waits to be put in.
    auto serve_states = [&]() {
        serve_state_window();
        // What the menu shows: the items of a game machine, grey with the
        // reason while no state can be taken.
        if (!game_machine_folder.empty()) {
            static auto looked = std::chrono::steady_clock::now() - std::chrono::seconds(2);
            if (const auto now = std::chrono::steady_clock::now(); now - looked >= std::chrono::milliseconds(500)) {
                looked = now;
                const std::string blocked = state_blocked();
                const bool last = HostSaveState::Count(game_machine_folder) != 0;
                if (!menu_state.states || blocked != menu_state.states_blocked || last != menu_state.states_last) {
                    menu_state.states = true;
                    menu_state.states_blocked = blocked;
                    menu_state.states_last = last;
                    HostMenu::Update(menu_state);
                }
            }
        }
        // A state this start was to begin with and did not take.
        if (static bool told = false; !told && getenv("NEO98_STATE_NOT_LOADED")) {
            told = true;
            const std::string error = getenv("NEO98_STATE_NOT_LOADED");
            unsetenv("NEO98_STATE_NOT_LOADED");
            HostMenu::Notice("Could not load the state",
                             (error + "\n\nThe state's files have been restored and the game will restart. The previous state is in the "
                                      "States list as \"Before loading\".").c_str());
        }
        if (static bool told = false; !told && !state_start.id.empty() && !state_start.error.empty()) {
            told = true;
            HostMenu::Notice("Could not load the state",
                             (state_start.error + "\n\nThe game machine will start with its previous state.").c_str());
        }
        if (state_job.active) {
            const auto *supplier = state_job.supplier;
            std::vector<unsigned char> bytes;
            std::string error;
            const int answer = supplier->Poll(&bytes, error);
            const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - state_job.since).count();
            if (!answer) {
                // Ready admitted a transient wait. Give a paused machine
                // its next turn immediately; Poll has already asked each
                // thread to stop as soon as its call can be resumed.
                if (machine_paused && !state_job.nudged && !HostDosUiState::HoldUi()) {
                    state_job.nudged = true;
                    machine_pause(false, "a state waits for the current call");
                }
                if (waited < 5) return;
                error = "The game is busy and cannot save a state right now. Try again shortly.";
            }
            const StateJob job = state_job;
            state_job.active = false;
            if (answer <= 0) {
                supplier->Release();
                machine_pause(job.was_paused, "a state was not taken");
                state_say(job.then_load.empty() ? "Not saved." : "Not loaded.",
                          (job.then_load.empty() ? "" : "The current state could not be saved, so nothing was loaded. ") + error, true);
                return;
            }
            machine_pause(true, "a state is saved");
            std::string id;
            const bool okay = state_write(job.kind, job.name, &bytes, job.machine, id, error, supplier);
            supplier->Release();
            if (!okay) {
                if (!job.was_paused) machine_pause(false, "the state was not saved");
                state_say(job.then_load.empty() ? "Not saved." : "Not loaded.",
                          (job.then_load.empty() ? "" : "The current state could not be saved, so nothing was loaded. ") + error, true);
                return;
            }
            if (!job.then_load.empty()) {
                state_restart(job.then_load, id);
                return;
            }
            if (!job.was_paused) machine_pause(false, "the state is saved");
            state_say("State saved", job.kind == "quick" ? "Press ⇧⌘L to load it." : job.name.empty() ? "" : "„" + HostSaveState::CleanName(job.name) + "“", false);
            // The window lists it and chooses it.
            if (HostStateWindow::IsOpen()) HostStateWindow::Update(state_model(), id);
        }
        if (state_start.settling && std::chrono::steady_clock::now() >= state_start.settle_until) {
            state_start.settling = false;
            machine_pause(true, "a state was loaded");
            HostMachineState::ByKind(state_start.game)->Release();
            state_start.loaded = true;
            printf("NEO98 HOST state loaded id=%s name=\"%s\" after %.2f s; the machine stands\n", state_start.id.c_str(),
                   state_start.name.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - host_start).count());
            fflush(stdout);
        }
        if (state_start.waiting) {
            const auto *supplier = HostMachineState::ByKind(state_start.game);
            std::string error;
            int answer = supplier->Poll(nullptr, error);
            if (!answer && std::chrono::steady_clock::now() - host_start < std::chrono::seconds(40)) return;
            state_start.waiting = false;
            if (!answer) error = "The game did not reach the point where the state was saved.";
            if (answer > 0) {
                // The supplier's machine stands on the state. The window gets
                // the picture the state was made with (a 3dfx Voodoo has
                // drawn nothing yet in this host), and the rest of the
                // machine runs a moment longer so the window paints it.
                std::vector<unsigned> picture;
                unsigned width = 0, height = 0;
                if (state_start.game != "dos-ui" && a->DosVmProcess() &&
                    HostSaveState::ReadPicture(HostSaveState::PictureFile(HostSaveState::StateFolder(game_machine_folder, state_start.id)),
                                               picture, width, height))
                    a->DosVmShowPicture(picture.data(), width, height);
                // The DOS window has the full screen or not, as it had.
                for (const auto &pair : state_start.view)
                    if (state_start.game != "dos-ui" && pair.first == "dos-vollbild" && a->DosVmProcess()) a->DosVmWantFullscreen(pair.second == "1");
                state_start.settling = true;
                state_start.settle_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
                return;
            }
            // The files are those of the state; its machine is not. The game
            // starts anew on them, and what the machine was before the load
            // is in the list as the state "Before loading". The supplier's
            // machine is not let go: it may hold half a state.
            printf("NEO98 HOST state load id=%s failed: %s\n", state_start.id.c_str(), error.c_str());
            fflush(stdout);
            HostMenu::Notice("Could not load the state",
                             (error + "\n\nThe state's files have been restored and the game will restart. The previous state is in the "
                                      "States list as \"Before loading\".").c_str());
            state_reboot = true;
            reboot_reason = "state not loaded";
            reboot = true;
            running = false;
        }
    };
    auto serve_menu = [&]() {
        HostMenu::Pump();
        HostMenu::FullScreenBars((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0);
        MenuScript();
        serve_game_machines();
        serve_single();
        serve_states();
        // The registry of Win98 does not wait for the shutdown: its changes
        // reach the disk a few seconds after they were made. The journal was
        // written only when the host ended in order, so after a crash or a
        // forced end the files of D:\RECYCLED\INFO2 were on the disk while
        // the per-drive count of SHELL32 in HKLM\...\Explorer\BitBucket was
        // not, and "Empty Recycle Bin" stayed grey. Flush is a
        // no-op while nothing changed.
        {
            static auto last_flush = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - last_flush >= std::chrono::seconds(3)) {
                last_flush = now;
                Registry::Flush();
            }
        }
        // Another machine folder chosen in the settings window: kept, and
        // in effect from the next restart of the machine.
        {
            std::string folder;
            bool restart = false;
            if (HostSetup::TakeMachineChoice(folder, restart)) {
                if (app_mode) {
                    settings.machine = folder;
                    RemoveOption("--machine");
                    remember();
                    printf("NEO98 HOST machine %s ready=%d restart=%d\n", folder.c_str(),
                           HostSettings::Ready(folder) ? 1 : 0, restart ? 1 : 0);
                    if (restart) { reboot = true; running = false; }
                } else
                    printf("NEO98 HOST machine: the command line names drive C:, settings unchanged\n");
                fflush(stdout);
            }
        }
        const char *active_dos = X86::Application::DosSettingsPath(User::GetForegroundWindow());
        if (!active_dos) {
            unsigned windows[User::WindowCapacity], count = 0;
            if (User::WindowSnapshot(0, windows, User::WindowCapacity, count))
                for (unsigned i = 0; i < count; ++i)
                    if (User::IsVisible(windows[i]) && (active_dos = X86::Application::DosSettingsPath(windows[i]))) break;
        }
        const std::string active_path = active_dos ? active_dos : "";
        if (menu_state.dos_program != active_path) {
            printf("NEO98 HOST DOS settings target=%s\n", active_path.c_str());
            menu_state.dos_program = active_path;
            HostMenu::Update(menu_state);
        }
        std::vector<std::pair<std::string, std::string>> failures;
        failures.swap(dos_failures);
        for (const auto &failure : failures) {
            const int action = HostMenu::DosFailure(failure.first.c_str(), failure.second.c_str());
            if (action == 1) a->RequestDosSettings(failure.first.c_str());
            if (action == 2) {
                X86::Application::RequestOpen(failure.first.c_str());
                // The failed DOS window may have vanished, leaving no
                // foreground window for RequestOpen's usual wake-up.
                unsigned windows[User::WindowCapacity], count = 0;
                if (User::WindowSnapshot(0, windows, User::WindowCapacity, count))
                    for (unsigned i = 0; i < count; ++i)
                        if (User::WindowProcess(windows[i]) == a->process_id) {
                            User::PostMessage(windows[i], 0, 0, 0);
                            break;
                        }
            }
        }
        HostMenu::Request request;
        while (HostMenu::Next(request)) {
            switch (request.action) {
            case HostMenu::Action::Reboot: reboot = true; running = false; break;
            case HostMenu::Action::Report: request_report("host-request"); break;
            case HostMenu::Action::Quit: running = false; break;
            case HostMenu::Action::Open: open_path(request.path); break;
            case HostMenu::Action::DosSettings: open_path(request.path, true); break;
            case HostMenu::Action::Preferences: {
                // With a path (the test switch settings=PATH) the folder is
                // chosen as the machine folder; else the window shows.
                if (!request.path.empty()) { HostSetup::ChooseMachine(request.path); break; }
                HostSetup::Info info;
                info.settings_file = HostSettings::File();
                info.machine = app_mode ? settings.machine : std::string();
                info.drive_c = drive_root ? drive_root : "RAM";
                info.logs = app_mode ? HostSettings::LogDirectory() : std::string("build");
                info.cdrom = cdrom_source;
                info.cdrom_letter = menu_state.cdrom_letter;
                for (int i = 3; i < 26; ++i)
                    if (Volume::Exists(char('A' + i)) && Volume::Type(char('A' + i)) != Volume::Cdrom) info.drives[i] = host_roots[i];
                info.scale = scale;
                info.fullscreen = fullscreen;
                info.online = menu_state.online;
                info.net_listen = menu_state.net_listen;
                HostSetup::ShowPreferences(info);
                break;
            }
            case HostMenu::Action::Scale: {
                scale = request.value == 2 ? 2 : 1;
                seamless_off("scale chosen");
                if (fullscreen) { SDL_SetWindowFullscreen(window, 0); fullscreen = false; }
                int ww = 0, wh = 0;
                WindowSizeFor(window, scale, ww, wh);
                SDL_SetWindowSize(window, ww, wh);
                SetOption("--scale", std::to_string(scale));
                RemoveFlag("--fullscreen");
                view_changed();
                remember();
                break;
            }
            case HostMenu::Action::NetworkListen:
                net.SetListen(request.value ? "any" : "127.0.0.1");
                menu_state.net_listen = net.ListenPolicy();
                SetOption("--net-listen", menu_state.net_listen);
                HostMenu::Update(menu_state);
                remember();
                break;
            case HostMenu::Action::Network:
                // The cable: the guest keeps its card, the network goes or
                // comes back; a restart keeps the choice.
                net.SetOnline(request.value != 0);
                RemoveFlag("--offline");
                if (!request.value) host_arguments.push_back("--offline");
                menu_state.online = request.value != 0;
                menu_state.address = card_address();
                HostMenu::Update(menu_state);
                printf("NEO98 HOST network %s address=%s\n", request.value ? "online" : "offline",
                       menu_state.address.empty() ? "-" : menu_state.address.c_str());
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::Wifi:
                // The Mac's wireless adapter in Windows; a restart keeps the choice.
                wifi = request.value != 0;
                hardware.SetWifi(wifi);
                RemoveFlag("--wifi");
                if (wifi) host_arguments.push_back("--wifi");
                menu_state.wifi = wifi;
                HostMenu::Update(menu_state);
                printf("NEO98 HOST wifi %s\n", wifi ? "on" : "off");
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::Microphone:
                // The recording device of Windows; a restart keeps the choice.
                microphone = request.value != 0;
                Sound::Microphone::SetEnabled(microphone);
                RemoveFlag("--no-microphone");
                if (!microphone) host_arguments.push_back("--no-microphone");
                menu_state.microphone = microphone;
                HostMenu::Update(menu_state);
                printf("NEO98 HOST microphone %s\n", microphone ? "on" : "off");
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::Seamless:
                zoom_end("Nahtlos chosen");
                // On: the state machine asks for the desktop and lays the
                // window over the screen once the guest has it. Off: the
                // desktop goes back to the size of the registry.
                if (seamless) seamless_off("menu");
                else {
                    seamless = true;
                    seamless_arguments();
                    printf("NEO98 HOST seamless on\n");
                    fflush(stdout);
                }
                remember();
                break;
            case HostMenu::Action::SeamlessWindows:
                seamless_windows = !seamless_windows;
                if (!seamless_windows) HostSeamless::CloseItems();
                pointer_known = false;
                seamless_arguments();
                printf("NEO98 HOST seamless windows=%d\n", seamless_windows ? 1 : 0);
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::SeamlessDock:
                seamless_dock = !seamless_dock;
                seamless_arguments();
                printf("NEO98 HOST seamless dock=%d\n", seamless_dock ? 1 : 0);
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::WheelFocus:
                // The wheel for the window with the focus, as under Win98,
                // instead of the window under the pointer.
                wheel_focus = !wheel_focus;
                User::SetWheelToPointer(!wheel_focus);
                menu_state.wheel_focus = wheel_focus;
                seamless_arguments();
                printf("NEO98 HOST wheel focus=%d\n", wheel_focus ? 1 : 0);
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::SeamlessIcons:
                seamless_icons = !seamless_icons;
                seamless_arguments();
                printf("NEO98 HOST seamless icons=%d\n", seamless_icons ? 1 : 0);
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::StartMenu: {
                // The Start menu without the taskbar under the Mac's mouse:
                // a press on the Start button, which stands at the left top
                // corner of the taskbar wherever the taskbar is docked. (The
                // Windows key and CTRL+ESC do not open it yet.)
                const unsigned tray = User::FindWindow("Shell_TrayWnd", nullptr);
                host_activation(true, "the Start menu chosen in the menu");
                Gdi::RECT bar{};
                const unsigned start = tray ? User::FindWindowEx(tray, 0, "Button", nullptr) : 0;
                if (seamless && !seamless_taskbar && start) {
                    // The omitted taskbar does not take screen input. Its
                    // Start button still processes a direct button click.
                    User::PostMessage(start, 0x0201, 1, 10u | (10u << 16));
                    User::PostMessage(start, 0x0202, 0, 10u | (10u << 16));
                } else if (tray && User::GetWindowRect(tray, &bar)) {
                    const int x = (bar.left < 0 ? 0 : bar.left) + 20, y = (bar.top < 0 ? 0 : bar.top) + 14;
                    a->MouseButtons(x, y, 1);
                    a->MouseButtons(x, y, 0);
                    last_mouse_x = x; last_mouse_y = y;
                }
                printf("NEO98 HOST start menu %s\n", tray ? "asked" : "without a shell");
                fflush(stdout);
                break;
            }
            case HostMenu::Action::GuestWindow:
                // A guest window picked in the menu "Window": it comes to
                // the foreground, restored if it was minimized.
                X86::Application::RequestForeground((unsigned)request.value);
                nudge();
                host_activation(true, "a guest window chosen in the menu");
                printf("NEO98 HOST seamless window %x chosen\n", (unsigned)request.value);
                fflush(stdout);
                break;
            case HostMenu::Action::MouseRelease:
                mouse_release("menu");
                break;
            case HostMenu::Action::Pause:
                machine_pause(!machine_paused, "menu");
                break;
            // Machine > Save state, Load latest state.
            case HostMenu::Action::StateSave:
                state_save(request.path.empty() ? "quick" : "manual", request.path);
                break;
            case HostMenu::Action::StateLoad:
                if (const std::string id = request.path.empty() ? state_last() : request.path; !id.empty()) state_load(id);
                else state_say("Nothing to load.", "No loadable state yet. Press ⇧⌘S to save one.", true);
                break;
            case HostMenu::Action::StateList:
                state_list_open();
                break;
            case HostMenu::Action::PauseUnfocused:
                pause_unfocused = !pause_unfocused;
                RemoveFlag("--pause-unfocused");
                if (pause_unfocused) host_arguments.push_back("--pause-unfocused");
                // Switched off while it holds the machine: go on.
                if (!pause_unfocused && pause_auto) machine_pause(false, "the setting was switched off");
                menu_state.pause_unfocused = pause_unfocused;
                HostMenu::Update(menu_state);
                printf("NEO98 HOST pause unfocused=%d\n", pause_unfocused ? 1 : 0);
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::Edit:
                guest_chord(unsigned(request.value));
                nudge();
                break;
            // The game shelf belongs to the phone shell (neo98_linux.cpp);
            // the Mac menu never sends these.
            case HostMenu::Action::GameCreate:
            case HostMenu::Action::GameList:
            case HostMenu::Action::GameLaunch:
                break;
            case HostMenu::Action::CtrlAltDelete: {
                host_activation(true, "Strg+Alt+Entf from the menu");
                X86::Application::RequestCtrlAltDelete();
                printf("NEO98 HOST Strg+Alt+Entf sent\n");
                fflush(stdout);
                break;
            }
            case HostMenu::Action::Minimize: {
                // Cmd+M. In the seamless view the guest's foreground window
                // is minimized as by its own button (SC_MINIMIZE), and its
                // Mac window goes into the Dock; otherwise the host's window.
                unsigned target = overlay_on ? User::GetForegroundWindow() : 0;
                while (target && (User::Style(target) & 0x40000000u) && User::Parent(target)) target = User::Parent(target);
                if (target && !(User::Style(target) & 0x20000u)) target = 0; // no WS_MINIMIZEBOX: Win98 has no way either
                if (target) { User::PostMessage(target, 0x112, 0xf020, 0); nudge(); }
                else if (!overlay_on) SDL_MinimizeWindow(window);
                printf("NEO98 HOST minimize hwnd=%x\n", target);
                fflush(stdout);
                break;
            }
            case HostMenu::Action::SeamlessTaskbar:
                seamless_taskbar = !seamless_taskbar;
                seamless_arguments();
                printf("NEO98 HOST seamless taskbar=%d\n", seamless_taskbar ? 1 : 0);
                fflush(stdout);
                remember();
                break;
            case HostMenu::Action::Fullscreen:
                if (zoomed) { zoom_end("menu"); break; }
                if (seamless) {
                    // Full screen in the seamless view is the program in
                    // front, for as long as it lasts; the desktop of Windows
                    // never shows, and nothing is stored.
                    unsigned front = User::GetForegroundWindow();
                    while (front && (User::Style(front) & 0x40000000u) && User::Parent(front)) front = User::Parent(front);
                    const unsigned shell = User::RegisteredShellWindow();
                    if (!overlay_on || !front || front == shell || !User::IsVisible(front) ||
                        (User::Style(front) & 0x20000000u)) {
                        printf("NEO98 HOST full screen: no program window in front, nothing changes\n");
                        fflush(stdout);
                        break;
                    }
                    zoomed = front;
                    set_overlay(false, UsableArea(window), "full screen of a program");
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    serve_zoom();
                    printf("NEO98 HOST full screen of a program hwnd=%x\n", zoomed);
                    fflush(stdout);
                    break;
                }
                seamless_off("full screen chosen");
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                // Back in a window, the window has the canvas size again
                // (the canvas may have changed while in full screen).
                if (!fullscreen) {
                    int ww = 0, wh = 0;
                    WindowSizeFor(window, scale, ww, wh);
                    SDL_SetWindowSize(window, ww, wh);
                }
                RemoveFlag("--fullscreen");
                if (fullscreen) host_arguments.push_back("--fullscreen");
                view_changed();
                remember();
                break;
            case HostMenu::Action::InsertCdrom: {
                // A folder or a CUE sheet goes to the drive as it is; any other
                // file is a disk image for hdiutil, and its mount point becomes
                // the drive.
                struct stat info = {};
                if (stat(request.path.c_str(), &info) != 0) {
                    printf("NEO98 HOST cdrom %s: not found\n", request.path.c_str());
                    fflush(stdout);
                    HostMenu::Notice("Could not mount CD-ROM", "The selected file or folder is inaccessible. The CD was not changed.");
                    break;
                }
                std::string drive = request.path;
                const bool cue = drive.size() > 4 && !strcasecmp(drive.c_str() + drive.size() - 4, ".cue");
                std::string mounted;
                bool owned_mount = false;
                if (!S_ISDIR(info.st_mode) && !cue) {
                    std::string error;
                    if (!HostMenu::Attach(request.path.c_str(), mounted, owned_mount, error)) {
                        printf("NEO98 HOST hdiutil attach %s failed: %s\n", request.path.c_str(), error.c_str());
                        fflush(stdout);
                        HostMenu::Notice("Could not mount CD-ROM", ("The CD was not changed.\n\n" + error).c_str());
                        break;
                    }
                    printf("NEO98 HOST hdiutil %s %s -> %s\n", owned_mount ? "attach" : "reuse", request.path.c_str(), mounted.c_str());
                    drive = mounted;
                }
                // The same image again answers the mount point it already has.
                const char *previous = getenv(AttachedEnv);
                const std::string previous_mount = previous ? previous : "";
                char letter = menu_state.cdrom_letter ? menu_state.cdrom_letter : FreeLetter();
                if (!letter || !SwapMedium(*a, letter, drive, true)) {
                    if (owned_mount && !mounted.empty() && mounted != previous_mount) {
                        std::string error;
                        HostMenu::Detach(mounted.c_str(), error);
                    }
                    HostMenu::Notice("Could not mount CD-ROM", letter ?
                        "Could not open the filesystem. The CD was not changed." :
                        "No drive letter is available. The CD was not changed.");
                    break;
                }
                if (!previous_mount.empty() && mounted != previous_mount) DetachAttached("replaced");
                if (owned_mount) setenv(AttachedEnv, mounted.c_str(), 1);
                SetOption("--cdrom", drive);
                menu_state.cdrom = drive;
                menu_state.cdrom_letter = letter;
                HostMenu::Update(menu_state);
                refresh_dos_drives();
                cdrom_source = request.path;
                remember();
                break;
            }
            case HostMenu::Action::EjectCdrom:
                // The drive stays, without a disc, until the next start.
                if (menu_state.cdrom_letter) SwapMedium(*a, menu_state.cdrom_letter, "", true);
                RemoveOption("--cdrom");
                DetachAttached("ejected");
                menu_state.cdrom.clear();
                HostMenu::Update(menu_state);
                cdrom_source.clear();
                remember();
                break;
            case HostMenu::Action::ComPort: {
                // Machine > Serial ports: where COMn leads from now
                // on. A port somebody holds keeps its cable until it is
                // closed.
                const unsigned port = unsigned(request.value - 1);
                if (port >= 4) break;
                if (!serial_ports.Attach(port, request.path.c_str())) {
                    char title[64];
                    snprintf(title, sizeof title, "COM%u is open", port + 1);
                    HostMenu::Notice(title, "A program is using this port. Close the program or its "
                                            "port connection before changing the endpoint.");
                    break;
                }
                // A restart of the machine (execv) keeps the environment; the
                // choice of the menu must win over what it held at the start.
                char variable[16];
                snprintf(variable, sizeof variable, "NEO98_COM%u", port + 1);
                unsetenv(variable);
                if (!app_mode && !request.path.empty()) setenv(variable, request.path.c_str(), 1);
                menu_state.com[port] = request.path;
                settings.com[port] = request.path;
                printf("NEO98 HOST COM%u -> %s\n", port + 1, request.path.empty() ? "(disconnected)" : request.path.c_str());
                fflush(stdout);
                HostMenu::Update(menu_state);
                remember();
                break;
            }
            case HostMenu::Action::ChooseDriveD: {
                // D: of the program collection or of an earlier choice gives
                // way; a CD-ROM on D: moves on to the next free letter.
                struct stat info = {};
                if (stat(request.path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
                    printf("NEO98 HOST drive D: %s: not a folder\n", request.path.c_str());
                    fflush(stdout);
                    break;
                }
                const bool cd_on_d = menu_state.cdrom_letter == 'D';
                if (!SwapMedium(*a, 'D', request.path, false)) break;
                dos_directories['D' - 'A'] = request.path;
                if (cd_on_d) {
                    const char moved = FreeLetter();
                    if (moved && (menu_state.cdrom.empty() ? (Volume::Mount(moved, nullptr, Volume::Cdrom, false),
                                                               a->ChangeMedium(moved), true)
                                                            : SwapMedium(*a, moved, menu_state.cdrom, true)))
                        menu_state.cdrom_letter = moved;
                    else menu_state.cdrom_letter = 0;
                }
                RemoveOption("--drive", "D");
                RemoveOption("--drive-readonly", "D");
                host_arguments.push_back("--drive");
                host_arguments.push_back("D=" + request.path);
                menu_state.drive_d = request.path;
                drive_d_session = false;
                HostMenu::Update(menu_state);
                refresh_dos_drives();
                remember();
                break;
            }
            }
        }
    };
    // The key equivalents of the menu bar reach SDL as well. With the menu in
    // place they are the menu's; without one (no AppKit) the host still acts
    // on Cmd+Shift+R and Cmd+Shift+B itself. No Win98 program sees them.
    auto host_shortcut = [&](const SDL_KeyboardEvent &key) {
        const bool command = (key.keysym.mod & KMOD_GUI) != 0, shift = (key.keysym.mod & KMOD_SHIFT) != 0,
                   control = (key.keysym.mod & KMOD_CTRL) != 0;
        const auto sym = key.keysym.sym;
        // Cmd+X, Cmd+C, Cmd+V: the menu "Edit" where there is one.
        if (command && !shift && !control && !(key.keysym.mod & KMOD_ALT) && (sym == SDLK_x || sym == SDLK_c || sym == SDLK_v)) {
            if (key.type == SDL_KEYDOWN && !HostMenu::Installed()) guest_chord(sym == SDLK_x ? 'X' : sym == SDLK_c ? 'C' : 'V');
            return true;
        }
        const bool state_key = !game_machine_folder.empty() && shift && (sym == SDLK_s || sym == SDLK_l || sym == SDLK_z);
        const bool menu_key = command && ((shift && (sym == SDLK_r || sym == SDLK_b || sym == SDLK_p)) || state_key || sym == SDLK_q ||
                                          sym == SDLK_o || sym == SDLK_COMMA || sym == SDLK_m ||
                                          sym == SDLK_1 || sym == SDLK_2 || (control && sym == SDLK_f) ||
                                          (control && sym == SDLK_s));
        if (!menu_key) return false;
        if (key.type == SDL_KEYDOWN && !HostMenu::Installed() && shift) {
            if (sym == SDLK_b) request_report("host-request");
            else if (sym == SDLK_r) { reboot = true; running = false; }
            else if (sym == SDLK_p && !key.repeat) machine_pause(!machine_paused, "key");
        }
        return true;
    };
    // --- A Dock icon for every program of the guest (tools/host_dock.h) ---
    // NEO98_DOCK_HELPERS=off|bundles|silent|dock; without it the icons show
    // where there is a screen and nothing happens under another driver.
    HostDock::Mode dock_mode = HostDock::Mode::Off;
    {
        const char *driver = SDL_GetCurrentVideoDriver();
        if (driver && !strcmp(driver, "cocoa")) dock_mode = HostDock::Mode::Dock;
        if (const char *wanted = getenv("NEO98_DOCK_HELPERS")) {
            dock_mode = !strcmp(wanted, "dock") ? HostDock::Mode::Dock : !strcmp(wanted, "silent") ? HostDock::Mode::Silent
                        : !strcmp(wanted, "bundles") ? HostDock::Mode::Bundles : HostDock::Mode::Off;
        }
    }
    const std::string dock_directory = HostSettings::Directory() + "/Programme";
    const std::string dock_helper = HostSettings::Parent(HostSettings::Executable()) + "/neo98-dock-helper";
    const std::string dock_fallback = HostSettings::Exists(HostSettings::Resource("Neo98.icns")) ? HostSettings::Resource("Neo98.icns") : std::string();
    bool dock_running = false;
    auto last_dock = std::chrono::steady_clock::now();
    // When the host last took the front by itself (its start, a second
    // start). What a helper reports right after that is not the user's
    // choice and moves nothing in the guest.
    auto dock_own_front = std::chrono::steady_clock::now() - std::chrono::hours(1);
    // The windows of a program that would have a button on the taskbar, front
    // to back.
    auto program_windows = [&](unsigned process) {
        std::vector<unsigned> found;
        unsigned handles[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, handles, User::WindowCapacity, count)) return found;
        const unsigned shell = User::RegisteredShellWindow();
        for (unsigned i = 0; i < count; ++i) {
            const unsigned h = handles[i];
            if (h == shell || !User::IsVisible(h) || !HostSeamless::TaskbarWindow(h)) continue;
            if (HostSeamless::WindowTitle(h).empty()) continue;
            if (process && User::WindowProcess(h) != process) continue;
            found.push_back(h);
        }
        return found;
    };
    // The icon of a game machine is the icon of its game: the first time the
    // game shows a window with a button for the taskbar, its icon becomes
    // Symbol.icns in the folder and the icon of the launcher bundle, and the
    // Dock shows it for this host.
    auto serve_game_icon = [&]() {
        static bool done = game_machine_folder.empty() || HostSettings::Exists(game_machine_folder + "/Symbol.icns");
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (done || now - last < std::chrono::milliseconds(500)) return;
        last = now;
        static unsigned picture[128 * 128];
        for (const unsigned h : program_windows(0)) {
            unsigned process = 0;
            char name[128] = {};
            int width = 0, height = 0;
            // False: the process loads the icon of its file first.
            if (!X86::Application::HostWindowProgram(h, process, name, sizeof name, width, height, picture, 128 * 128)) return;
            done = true;
            if (width <= 0) { printf("NEO98 HOST game machine icon: %s has none\n", name); fflush(stdout); return; }
            const std::vector<unsigned> argb(picture, picture + size_t(width) * size_t(height));
            const std::string icon = game_machine_folder + "/Symbol.icns";
            const bool okay = HostDock::WriteIcon(icon, width, height, argb) &&
                              HostDock::WriteIcon(HostGameMachine::LauncherIcon(game_machine_folder), width, height, argb);
            // The Finder reads the icon of a bundle again when the bundle changed.
            utimes(HostGameMachine::Launcher(game_machine_folder).c_str(), nullptr);
            if (okay) HostGameMenu::SetApplicationIcon(icon);
            printf("NEO98 HOST game machine icon %dx%d of %s %s\n", width, height, name, okay ? "written" : "not written");
            fflush(stdout);
            return;
        }
    };
    auto serve_dock = [&]() {
        if (dock_mode == HostDock::Mode::Off) return;
        const auto now = std::chrono::steady_clock::now();
        if (!seamless || !seamless_dock) {
            if (dock_running) { HostDock::Shutdown(); dock_running = false; printf("NEO98 HOST dock off\n"); fflush(stdout); }
            return;
        }
        if (now - last_dock < std::chrono::milliseconds(250)) return;
        last_dock = now;
        // The programs: every process with a window the Mac shows, or has
        // shown and keeps minimized. A window that has not painted yet is not
        // on the Mac, and its program has no icon yet.
        std::vector<HostDock::Program> programs;
        static unsigned picture[128 * 128];
        for (const unsigned h : program_windows(0)) {
            if (overlay_on && !(User::Style(h) & 0x20000000u)) {
                bool shown = false;
                for (const auto &window : mask.windows) shown = shown || window.handle == h;
                // A window that is wholly covered keeps its program's icon:
                // the icon is the way back to it.
                for (const unsigned covered : mask.covered) shown = shown || covered == h;
                if (!shown) continue;
            }
            HostDock::Program program;
            char name[128] = {};
            if (!X86::Application::HostWindowProgram(h, program.process, name, sizeof name, program.width, program.height,
                                                     picture, 128 * 128) || !program.process)
                continue;
            bool known = false;
            for (const auto &other : programs) known = known || other.process == program.process;
            if (known) continue;
            program.name = name;
            if (program.width > 0) program.argb.assign(picture, picture + size_t(program.width) * size_t(program.height));
            programs.push_back(program);
        }
        HostSettings::MakeDirectories(dock_directory);
        for (const auto &line : HostDock::Sync(programs, dock_directory, dock_helper, dock_fallback, dock_mode))
            printf("NEO98 HOST %s\n", line.c_str());
        dock_running = true;
        HostDock::Event event;
        while (HostDock::Next(event)) {
            const auto windows = program_windows(event.process);
            const char *what = event.kind == HostDock::Event::Quit ? "quit" : event.kind == HostDock::Event::Reopen ? "reopen" : "activate";
            printf("NEO98 HOST dock %s pid=%u windows=%zu\n", what, event.process, windows.size());
            if (windows.empty()) continue;
            if (event.kind == HostDock::Event::Quit) {
                // Quit in the Dock: every window of the program is asked to
                // close, as by its close button (WM_CLOSE).
                for (const unsigned h : windows) User::PostMessage(h, 0x10, 0, 0);
                continue;
            }
            if (now - dock_own_front < std::chrono::seconds(1)) {
                printf("NEO98 HOST dock %s pid=%u ignored (the host took the front itself)\n", what, event.process);
                continue;
            }
            // The program comes to the front: its front window gets the
            // foreground (a minimized one is restored), and its Mac windows
            // come in front of the Mac's.
            X86::Application::RequestForeground(windows.front());
            nudge();
            host_activation(true, "a program chosen in the Dock");
            if (overlay_on && seamless_windows) {
                std::vector<HostSeamless::Item> front;
                for (const auto &item : items)
                    if (!item.through && User::WindowProcess(item.id) == event.process) front.push_back(item);
                HostSeamless::RaiseItems(front);
            }
        }
        fflush(stdout);
    };
    // The minimized window the user took out of the Dock, or chose there.
    auto restore_window = [&](unsigned hwnd, const char *why) {
        X86::Application::RequestForeground(hwnd);
        nudge();
        host_activation(true, why);
        printf("NEO98 HOST seamless restore hwnd=%x (%s)\n", hwnd, why);
        fflush(stdout);
    };
    item_mouse = [&](const HostSeamless::MouseEvent &mouse) {
        int mx = mouse.x < 0 ? 0 : mouse.x >= canvas_w ? canvas_w - 1 : mouse.x;
        int my = mouse.y < 0 ? 0 : mouse.y >= canvas_h ? canvas_h - 1 : mouse.y;
        if (mouse.kind == HostSeamless::MouseEvent::Restore) { restore_window(mouse.window, "the Dock"); return; }
        if (mouse_hold.released) {
            if (mouse.kind == HostSeamless::MouseEvent::Button && mouse.buttons) mouse_capture("press");
            else if (mouse.kind != HostSeamless::MouseEvent::Chosen) return;
        }
        switch (mouse.kind) {
        case HostSeamless::MouseEvent::Move:
        case HostSeamless::MouseEvent::Button: {
            const bool changed = mx != last_mouse_x || my != last_mouse_y || mouse.buttons != last_mouse_buttons;
            hover = mouse.window;
            last_mouse_x = mx; last_mouse_y = my; last_mouse_buttons = mouse.buttons;
            seamless_buttons = mouse.buttons;
            a->MouseButtons(mx, my, mouse.buttons);
            if (changed) { last_input_time = std::chrono::steady_clock::now(); pacing.Input(); }
            break;
        }
        case HostSeamless::MouseEvent::Wheel:
            if (mouse.wheel || mouse.wheel_x) {
                HostScroll out;
                out.y = mouse.wheel, out.x = mouse.wheel_x, out.flags = mouse.wheel_flags;
                const unsigned long long now = SDL_GetTicks64();
                if (scroll_log.StartsUnits(out, now)) scroll_log_line();
                scroll_log.AddUnits(out, now);
                a->MouseScroll(mouse.wheel, mouse.wheel_x, mouse.wheel_flags);
            }
            break;
        case HostSeamless::MouseEvent::Leave:
            // Outside the guest's windows the pointer is the Mac's. The
            // guest sees the mouse go to where it left the window, as it
            // sees it go onto a window of another program under Win98: a
            // tooltip of the window it left closes.
            if (mouse.window == hover && !mouse.buttons) {
                hover = 0;
                if (mx != last_mouse_x || my != last_mouse_y) {
                    last_mouse_x = mx; last_mouse_y = my;
                    a->MouseButtons(mx, my, 0);
                }
            }
            break;
        case HostSeamless::MouseEvent::Chosen:
            X86::Application::RequestForeground(mouse.window);
            nudge();
            break;
        case HostSeamless::MouseEvent::Restore: break;
        }
    };
    // A machine out of a saved state stands paused until someone lets it go
    // on (tools/host_state.h).
    if (HostState::loaded) machine_pause(true, "state loaded");
    pacing.Start();
    while (running) {
        hardware.Pump(*a,game_machine_folder.empty());
#ifdef NEO98_WITH_HOST_MEDIA
        media.Pump();
        // Kodi's window is recorded while the guest shows it somewhere.
        if (media.WantsCapture()) HostMediaCapture::Start(); else HostMediaCapture::Stop();
        HostMediaCapture::Aspect(media.VideoAspect());
#endif
        // Run the guest for one frame's worth of time, then service the window.
        // A paused machine is not run at all : Pump is the
        // only way into the Windows threads and the ARM64 processors. The
        // window, the menu and the script below go on.
        const auto slice_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
        if (!machine_paused) {
            pacing.BeginRun();
            while (std::chrono::steady_clock::now() < slice_end) {
                // Pump of a first process that has ended still runs the others.
                if ((a->Status() == X86::Application::Exited || a->Status() == X86::Application::Failed) && !game_goes_on()) break;
                a->Pump();
                // Nothing to run anywhere: every thread of every process waits.
                if (!a->SystemRunnable()) break;
            }
            // Retained offscreen work must also reach the GPU when the guest
            // yields without a DirectDraw present. Keep logical batches open.
            if (gpu_backend) gpu_backend->Flush();
            pacing.EndRun(*a);
            pacing.Turn(*a);
            pacing.Stall(*a);
            HostState::ProbeTurn(*a);
        }
        if (!announced && User::LiveWindows()) {
            announced = true;
            printf("NEO98 HOST running calls=%u callbacks=%u windows=%u\n", a->ApiCalls(), a->Callbacks(), User::LiveWindows());
            fflush(stdout);
        }
        if (session_end_requested) {
            // EWX_SHUTDOWN and EWX_POWEROFF switch the machine off: the host
            // ends. EWX_REBOOT and EWX_LOGOFF (NEO98 has no login) bring it
            // back up the way the reboot control does.
            session_end_requested = false;
            printf("NEO98 HOST exitwindows flags=%08x code=%u\n", session_end_flags, session_end_code);
            fflush(stdout);
            if (session_powers_off(session_end_flags)) quit_requested = 1;
            else {
                reboot_reason = "exitwindows";
                reboot = true;
            }
            running = false;
        }
        if (dump_requested) { dump_requested = 0; host_dump_all(a); }
        if (report_requested) { report_requested = 0; request_report("signal"); }
        if (quit_requested) running = false;
        serve_menu();
        if (front_requested) {
            const char *reason = front_requested;
            front_requested = nullptr;
            // Nothing of this reaches the screen under the dummy driver.
            const bool screen = HostMenu::Activate();
            dock_own_front = std::chrono::steady_clock::now();
            if (screen) {
                if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(window);
                SDL_RaiseWindow(window);
                if (overlay_on && seamless_windows) HostSeamless::RaiseItems(items);
            }
            printf("NEO98 HOST activation (%s): %s\n", reason, screen ? "requested" : "no screen");
            if (!strcmp(reason, "second start")) printf("NEO98 HOST brought to front\n");
            fflush(stdout);
        }
        serve_opens();
        // The clipboard of the Mac is offered to the guest when it runs again.
        if (!machine_paused) serve_clipboard();
        // Without the taskbar in the view maximized windows take the whole
        // area.
        if (const bool whole = overlay_on && !seamless_taskbar; whole != X86::Application::HostWorkWhole()) {
            X86::Application::SetHostWorkArea(whole);
            nudge();
        }
        seamless_pointer();
        seamless_cursor();
        seamless_activation();
        serve_mouse_hold();
        serve_dock();
        serve_game_icon();
        // NEO98_HOST_DEADLINE=seconds ends the host after that time whatever
        // the guest or a script does: a run in a real window on the user's
        // screen never stays there.
        {
            static const char *deadline = getenv("NEO98_HOST_DEADLINE");
            if (deadline && *deadline &&
                std::chrono::steady_clock::now() - host_start >= std::chrono::seconds(strtol(deadline, nullptr, 10))) {
                printf("NEO98 HOST deadline %s s reached\n", deadline);
                fflush(stdout);
                running = false;
            }
        }
        // NEO98_HOST_STATS=1 writes a line a second: the pictures the
        // program has presented through DirectDraw (the x86 HAL and the
        // driver of ARM64 programs count in one place) and the frames the
        // mixer has rendered, all and those that were not silence. For
        // measurements without a screen and a loudspeaker.
        {
            static const char *stats = getenv("NEO98_HOST_STATS");
            static auto next = host_start + std::chrono::seconds(1);
            if (stats && *stats && std::chrono::steady_clock::now() >= next) {
                next += std::chrono::seconds(1);
                struct rusage used{};
                getrusage(RUSAGE_SELF, &used);
                printf("NEO98 HOST stats t=%.2f presents=%u sound=%llu audible=%llu cpu=%.2f\n",
                       std::chrono::duration<double>(std::chrono::steady_clock::now() - host_start).count(), a->HalPresents(),
                       Sound::Shared().RenderedFrames(), Sound::Shared().AudibleFrames(),
                       (double)(used.ru_utime.tv_sec + used.ru_stime.tv_sec) + (used.ru_utime.tv_usec + used.ru_stime.tv_usec) / 1e6);
                fflush(stdout);
#if defined(__APPLE__)
                HostMetal::LogStatistics();
#endif
            }
        }
        // NEO98_HOST_SHOT="seconds:path" writes one screenshot that many
        // seconds after the start, for hand checks that want a picture
        // without a key press (F12 takes them by hand).
        {
            static const auto started = std::chrono::steady_clock::now();
            static bool shot_done = false;
            const char *shot = getenv("NEO98_HOST_SHOT");
            if (shot && !shot_done) {
                const char *colon = strchr(shot, ':');
                const long seconds = strtol(shot, nullptr, 10);
                if (colon && colon[1] && std::chrono::steady_clock::now() - started >= std::chrono::seconds(seconds)) {
                    shot_done = true;
                    if (screenshot(colon + 1, a)) printf("NEO98 HOST screenshot %s\n", colon + 1);
                    fflush(stdout);
                }
            }
        }
        // NEO98_HOST_INPUT="seconds:action;..." plays pointer input and takes
        // screenshots without a person, for long hand checks under
        // SDL_VIDEODRIVER=dummy that must not touch the user's screen.
        // Actions in canvas pixels, the pixels of the screenshots:
        // move=x,y click=x,y dblclick=x,y rclick=x,y down=x,y up=x,y shot=path,
        // and motion=x,y for an SDL motion event in window points.
        // wait=x,y,rrggbb holds the script until the canvas pixel x,y shows
        // that colour (each channel within 48); the steps after it count from
        // the moment it did, so a script waits for a program's screen instead
        // of a fixed time that a loaded machine overruns.
        // type=TEXT (UTF-8) types each character with the key and the shift
        // state VkKeyScan of the installed layout names for it;
        // key=VK presses and releases one virtual key, in hex (key=0d Enter).
        // Between down= and up= a move= keeps the button held, so a script
        // can drag (a band of the taskbar).
        // These steps wait for the guest instead of the clock: wait=CLASS
        // (or CLASS|TITLE, or |TITLE)
        // until a visible top-level window of that class and title exists;
        // waitchild=CLASS|TITLE also searches visible child controls, for
        // wizard pages that keep the same top-level caption throughout.
        // idle=MS waits until every turn of the last MS
        // milliseconds ended with no runnable thread anywhere, and
        // menu=ITEM runs an item of the menu bar (menu=quit ends the host).
        // The clock of the script stands while it waits, so the seconds of
        // later steps count from the moment the wait was over. A wait gives
        // up after 240 s and says so.
        {
            struct Step { double seconds; std::string action; int x, y; std::string path; std::string text; };
            static std::vector<Step> steps;
            static size_t next_step = 0;
            static unsigned held = 0;
            static bool parsed = false;
            static const auto started = std::chrono::steady_clock::now();
            static double paused = 0;          // seconds the script clock stood still
            static double waiting_since = -1;  // script-independent time the current wait began
            static auto busy_at = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (a->SystemRunnable()) busy_at = now;
            if (!parsed) {
                parsed = true;
                if (const char *once = getenv("NEO98_HOST_INPUT_ONCE"); once && *once == '1') host_input_once = true;
                if (const char *script = getenv("NEO98_HOST_INPUT")) {
                    std::string text = script;
                    size_t at = 0;
                    while (at < text.size()) {
                        size_t end = text.find(';', at);
                        if (end == std::string::npos) end = text.size();
                        const std::string item = text.substr(at, end - at);
                        at = end + 1;
                        const size_t colon = item.find(':'), equals = item.find('=');
                        if (colon == std::string::npos || equals == std::string::npos || equals < colon) continue;
                        Step s{strtod(item.c_str(), nullptr), item.substr(colon + 1, equals - colon - 1), 0, 0, "", item};
                        const std::string value = item.substr(equals + 1);
                        if (s.action == "wheel" || s.action == "scroll" || s.action == "shot" || s.action == "type" || s.action == "menu" || s.action == "open" || s.action == "guestopen" || s.action == "pbtext" || s.action == "pbfiles" || s.action == "pbimage" || s.action == "pbdump" || s.action == "button" || s.action == "mask" || s.action == "choose" || s.action == "dock-restore" || s.action == "close" || s.action == "touch" || s.action == "touchcancel" || s.action == "state" || s.action == "wstate") s.path = value;
                        else if (s.action == "wait" || s.action == "waitchild") {
                            // wait=x,y,rrggbb waits for a pixel, wait=CLASS|TITLE for a window.
                            char colour[16] = {};
                            if (sscanf(value.c_str(), "%d,%d,%15s", &s.x, &s.y, colour) == 3) { s.action = "pixel"; s.path = colour; }
                            else s.path = value;
                        }
                        else if (s.action == "idle" || s.action == "raise" || s.action == "windows" || s.action == "activation" || s.action == "hostkeys" || s.action == "native" || s.action == "redraw" || s.action == "closebox" || s.action == "tops" || s.action == "trees" || s.action == "lists" || s.action == "cmd" || s.action == "pause" || s.action == "focus" || s.action == "trace") s.x = atoi(value.c_str());
                        else if (s.action == "key" || s.action == "keydown" || s.action == "keyup") s.x = (int)strtol(value.c_str(), nullptr, 16);
                        else if (sscanf(value.c_str(), "%d,%d", &s.x, &s.y) != 2) continue;
                        if (s.action == "motion") s.path = value; // optional x,y,dx,dy: a queued SDL sample
                        steps.push_back(s);
                    }
                }
            }
            const double real = std::chrono::duration<double>(now - started).count();
            // No step while a state is being taken or put in: the next one
            // meets the machine as it is afterwards.
            while (running && !state_job.active && !state_start.waiting && !state_start.settling && next_step < steps.size() &&
                   real - paused >= steps[next_step].seconds) {
                const Step &s = steps[next_step];
                if (s.action == "wait" || s.action == "waitchild" || s.action == "idle" || s.action == "pixel") {
                    bool met = false;
                    if (waiting_since < 0) waiting_since = real;
                    if (s.action == "wait" || s.action == "waitchild") {
                        const auto bar = s.path.find('|');
                        const std::string klass = s.path.substr(0, bar);
                        const std::string title = bar == std::string::npos ? "" : s.path.substr(bar + 1);
                        const unsigned h = User::FindWindow(klass.empty() ? nullptr : klass.c_str(),
                                                            title.empty() ? nullptr : title.c_str());
                        met = h && User::IsVisible(h);
                        if (!met && s.action == "waitchild") {
                            auto find = [&](auto &&self, unsigned parent, unsigned depth) -> bool {
                                if (depth >= User::WindowCapacity) return false;
                                for (unsigned child = User::GetTopWindow(parent); child; child = User::GetWindow(child, 2)) {
                                    if (!User::IsVisible(child)) continue;
                                    char child_class[128] = {};
                                    User::GetClassName(child, child_class, sizeof child_class);
                                    if ((klass.empty() || klass == child_class) &&
                                        (title.empty() || title == HostSeamless::WindowTitle(child))) return true;
                                    if (self(self, child, depth + 1)) return true;
                                }
                                return false;
                            };
                            met = find(find, 0, 0);
                        }
                    } else if (s.action == "pixel") {
                        int fw = 800, fh = 600;
                        const auto *frame = frame_pixels(a, fw, fh);
                        const unsigned want = (unsigned)strtoul(s.path.c_str(), nullptr, 16);
                        if (frame && s.x >= 0 && s.x < fw && s.y >= 0 && s.y < fh) {
                            const unsigned char *q = frame + (s.y * fw + s.x) * 4;
                            const int r = q[2], g = q[1], b = q[0];
                            met = abs(r - (int)((want >> 16) & 255)) <= 48 && abs(g - (int)((want >> 8) & 255)) <= 48 &&
                                  abs(b - (int)(want & 255)) <= 48;
                        }
                    } else {
                        // Quiet since the wait began, not only before it: the
                        // input of the step before may not have run yet.
                        const double quiet = std::min(std::chrono::duration<double>(now - busy_at).count(),
                                                      real - waiting_since);
                        met = quiet * 1000 >= s.x;
                    }
                    const bool expired = real - waiting_since > 240;
                    if (!met && !expired) {
                        // Hold the clock of the script at this step.
                        paused = real - s.seconds;
                        break;
                    }
                    printf("NEO98 HOST input %s=%s %s after %.1f s\n", s.action.c_str(), s.path.empty() ? std::to_string(s.x).c_str() : s.path.c_str(),
                           met ? "reached" : "timed out", real - waiting_since);
                    fflush(stdout);
                    waiting_since = -1;
                    ++next_step;
                    continue;
                }
                ++next_step;
                if (host_input_once) {
                    host_input_remaining.clear();
                    for (size_t i = next_step; i < steps.size(); ++i) host_input_remaining += steps[i].text + ";";
                }
                // Enable API tracing at a specific wizard page, including
                // child processes, without tracing its entire installation.
                if (s.action == "trace") {
                    for (unsigned i = 0; i < 16; ++i)
                        if (auto *process = static_cast<X86::Application *>(User::ProcessContextAt(i))) {
                            process->trace_calls = s.x != 0;
                            process->trace_from = 0;
                        }
                    printf("NEO98 HOST input trace=%d\n", s.x);
                    fflush(stdout);
                    continue;
                }
                if (s.action == "menu") {
                    // The request is served at the top of the next turn; a
                    // restart or the end must not see later steps taken.
                    MenuCommand(s.path);
                    break;
                }
                if (s.action == "touchcancel") {
                    SDL_Event event = {}; event.type = SDL_USEREVENT; event.user.code = 0x98544301u;
                    SDL_PushEvent(&event); continue;
                }
                if (s.action == "touch") {
                    unsigned long long finger = 0;
                    char phase[16] = {};
                    int x = 0, y = 0, pressure = -1;
                    if (sscanf(s.path.c_str(), "%llu,%15[^,],%d,%d,%d", &finger, phase, &x, &y, &pressure) >= 4) {
                        unsigned kind = !strcmp(phase,"down") ? NEO98_TOUCH_DOWN : !strcmp(phase,"move") ? NEO98_TOUCH_MOVE :
                                        !strcmp(phase,"up") ? NEO98_TOUCH_UP : !strcmp(phase,"cancel") ? NEO98_TOUCH_CANCEL : 0;
                        if (kind) {
                            SDL_Event event = {};
                            event.type = kind == NEO98_TOUCH_DOWN ? SDL_FINGERDOWN :
                                         kind == NEO98_TOUCH_UP || kind == NEO98_TOUCH_CANCEL ? SDL_FINGERUP : SDL_FINGERMOTION;
                            int wx = 0, wy = 0, width = 0, height = 0;
                            CanvasToWindow(LayoutFor(window, renderer), x, y, wx, wy);
                            SDL_GetWindowSize(window, &width, &height);
                            event.tfinger.touchId = SDL_TouchID(-1); event.tfinger.fingerId = SDL_FingerID(finger);
                            event.tfinger.windowID = SDL_GetWindowID(window);
                            event.tfinger.x = width ? float((wx + .5) / width) : 0;
                            event.tfinger.y = height ? float((wy + .5) / height) : 0;
                            event.tfinger.pressure = pressure < 0 ? -1.f : float(pressure) / 1024.f;
                            // SDL has no per-finger CANCEL event; its UP here
                            // carries a scripted cancellation marker.
                            if (kind == NEO98_TOUCH_CANCEL) event.tfinger.dx = -100000.f;
                            SDL_PushEvent(&event);
                        }
                    }
                    continue;
                }
                // open=PATH is a double click on that file in the Finder.
                if (s.action == "open") {
                    open_path(s.path, false, true);
                    continue;
                }
                // cmd=1 holds the Command key of the Mac as far as the guest
                // can tell (its Windows key goes down), cmd=0 lets it go.
                if (s.action == "cmd") {
                    SDL_SetModState(s.x ? SDL_Keymod(SDL_GetModState() | KMOD_LGUI) : SDL_Keymod(SDL_GetModState() & ~KMOD_GUI));
                    a->KeyboardModifiers(guest_modifiers());
                    printf("NEO98 HOST input cmd %d\n", s.x);
                    fflush(stdout);
                    continue;
                }
                // The pasteboard of the host for the tests (only a private
                // one, NEO98_HOST_PASTEBOARD=unique): pbtext=FILE copies the
                // UTF-8 text of FILE as another Mac program would, pbfiles=FILE
                // the files FILE names line by line, pbimage=FILE the PNG
                // file FILE, pbdump=FILE writes what the pasteboard holds.
                if (s.action == "pbtext" || s.action == "pbfiles" || s.action == "pbimage" || s.action == "pbdump") {
                    bool okay = HostPasteboard::Private();
                    if (okay && s.action == "pbdump") {
                        const std::string dump = HostPasteboard::Describe();
                        FILE *file = fopen(s.path.c_str(), "wb");
                        okay = file && fwrite(dump.data(), 1, dump.size(), file) == dump.size();
                        if (file) fclose(file);
                    } else if (okay) {
                        std::string content;
                        if (FILE *file = fopen(s.path.c_str(), "rb")) {
                            char block[4096];
                            for (size_t n; (n = fread(block, 1, sizeof block, file)) > 0;) content.append(block, n);
                            fclose(file);
                        } else okay = false;
                        if (okay && s.action == "pbimage") {
                            HostPasteboard::WritePng(std::vector<unsigned char>(content.begin(), content.end()));
                        } else if (okay && s.action == "pbtext") {
                            std::u16string text;
                            for (size_t i = 0; i < content.size();) {
                                unsigned c = (unsigned char)content[i++];
                                const int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
                                if (more) c &= 0x3f >> more;
                                for (int n = 0; n < more && i < content.size(); ++n) c = (c << 6) | ((unsigned char)content[i++] & 0x3f);
                                if (c >= 0x10000) { c -= 0x10000; text += char16_t(0xd800 | (c >> 10)); text += char16_t(0xdc00 | (c & 0x3ff)); }
                                else text += char16_t(c);
                            }
                            HostPasteboard::Write(&text, nullptr);
                        } else if (okay) {
                            std::vector<std::string> files;
                            for (size_t from = 0; from < content.size();) {
                                const size_t end = std::min(content.find('\n', from), content.size());
                                if (end > from) files.push_back(content.substr(from, end - from));
                                from = end + 1;
                            }
                            // The Finder adds the names as the string.
                            std::u16string names;
                            for (const auto &file : files) {
                                if (!names.empty()) names += u'\n';
                                for (unsigned char c : HostVolume::ToGuest(file.substr(file.rfind('/') + 1))) names += char16_t(c);
                            }
                            HostPasteboard::Write(&names, &files);
                        }
                    }
                    printf("NEO98 HOST input %s %s %s\n", s.action.c_str(), s.path.c_str(), okay ? "ok" : "refused");
                    fflush(stdout);
                    continue;
                }
                if (s.action == "guestopen") {
                    a->RequestOpen(s.path.c_str());
                    continue;
                }
                // Invoke a named button through BM_CLICK for installer probes;
                // a native control subclass still runs its own click handler.
                if (s.action == "button") {
                    auto find = [&](auto &&self, unsigned parent, unsigned depth) -> unsigned {
                        if (depth >= User::WindowCapacity) return 0;
                        for (unsigned h = User::GetTopWindow(parent); h; h = User::GetWindow(h, 2)) {
                            if (!User::IsVisible(h) || !User::IsEnabled(h)) continue;
                            std::string title = HostSeamless::WindowTitle(h);
                            title.erase(std::remove(title.begin(), title.end(), '&'), title.end());
                            if (User::IsButton(h) && title == s.path) return h;
                            if (const unsigned child = self(self, h, depth + 1)) return child;
                        }
                        return 0;
                    };
                    const unsigned h = find(find, 0, 0);
                    if (h) { User::NoteHostPress(h); User::PostMessage(h, 0xf5, 0, 0); }
                    printf("NEO98 HOST input button=%s hwnd=%x\n", s.path.c_str(), h);
                    fflush(stdout);
                    continue;
                }
                if (s.action == "shot") {
                    if (screenshot(s.path.c_str(), a)) printf("NEO98 HOST screenshot %s\n", s.path.c_str());
                    if (RenderProfile::Enabled()) RenderProfile::Counters().Report();
                    if (getenv("NEO98_HOST_PROFILE")) {
                        struct rusage used{};
                        getrusage(RUSAGE_SELF, &used);
                        printf("EXPLORER PROFILE checkpoint=%s wall=%.6f cpu=%.6f frames=%u calls=%u callbacks=%u fast=%llu slow=%llu guest=%llu minor_faults=%ld major_faults=%ld\n",
                               s.path.c_str(), real,
                               double(used.ru_utime.tv_sec + used.ru_stime.tv_sec) +
                                   (used.ru_utime.tv_usec + used.ru_stime.tv_usec) / 1e6,
                               User::FrameSerial(), a->ApiCalls(), a->Callbacks(),
                               a->fast_gate_calls, a->slow_gate_calls, a->guest_gate_calls,
                               used.ru_minflt, used.ru_majflt);
                        printf("EXPLORER CPU checkpoint=%s instance=%p\n", s.path.c_str(), static_cast<void *>(a->cpu));
                        // Root-process counters include every Explorer thread.
                        for (unsigned i = 0; i < a->import_count; ++i) {
                            const auto &entry = a->imports[i];
                            if (entry.calls) printf("EXPLORER API checkpoint=%s calls=%u api=%s!%s\n",
                                                    s.path.c_str(), entry.calls, entry.dll, entry.name);
                        }
                    }
                    fflush(stdout);
                    continue;
                }
                // The seamless view without a screen :
                // mask=PATH writes the alpha of the picture the overlay would
                // show as a PGM; pointer=x,y puts the Mac pointer at that
                // point of the overlay; press=x,y and release=x,y are the
                // Mac's mouse button there, through the same path as a real
                // SDL event.
                if (s.action == "mask") {
                    int fw = 0, fh = 0;
                    const auto *frame = frame_pixels(a, fw, fh);
                    HostSeamless::Collect(mask, seamless_view(frame));
                    const bool okay = overlay_on && frame && fw == mask.width && fh == mask.height;
                    if (okay && s.path.size() > 4 && s.path.substr(s.path.size() - 4) == ".ppm") {
                        // mask=X.ppm: the pixels the Mac windows of the guest
                        // windows get (ComposeItem), laid over each other
                        // from back to front; magenta where none shows.
                        HostSeamless::Plan(mask, items);
                        std::vector<unsigned> all(size_t(fw) * size_t(fh), 0x00ff00ffu), one;
                        for (size_t n = items.size(); n-- > 0;) {
                            const auto &item = items[n];
                            HostSeamless::ComposeItem(item, frame, fw, fh, one);
                            const int iw = item.frame.right - item.frame.left;
                            const auto visible = HostSeamless::Clipped(item.frame, fw, fh);
                            for (int y = visible.top; y < visible.bottom; ++y)
                                for (int x = visible.left; x < visible.right; ++x)
                                    if (const unsigned p = one[size_t(y - item.frame.top) * iw + (x - item.frame.left)]; p >> 24)
                                        all[size_t(y) * fw + x] = p;
                        }
                        if (FILE *file = fopen(s.path.c_str(), "wb")) {
                            fprintf(file, "P6\n%d %d\n255\n", fw, fh);
                            for (unsigned p : all) { const unsigned char rgb[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p}; fwrite(rgb, 1, 3, file); }
                            fclose(file);
                        }
                    } else if (okay) {
                        HostSeamless::Compose(mask, frame, seamless_picture);
                        HostSeamless::WriteAlpha(s.path.c_str(), seamless_picture, fw, fh);
                    }
                    printf("NEO98 HOST seamless mask %s %s windows=%zu rects=%zu\n", s.path.c_str(),
                           okay ? "written" : "refused", mask.windows.size(), mask.rects.size());
                    fflush(stdout);
                    continue;
                }
                // raise=1 brings the window to the front although the host
                // was started by a process in the background, for the proof
                // runs with screencapture.
                if (s.action == "raise") {
                    // raise=2 brings only the front guest window forward, as
                    // a click on it does.
                    if (s.x == 2 && overlay_on && seamless_windows) {
                        std::vector<HostSeamless::Item> front;
                        for (const auto &item : items)
                            if (!item.through) { front.push_back(item); break; }
                        HostSeamless::RaiseItems(front);
                    } else {
                        SDL_RaiseWindow(window);
                        if (overlay_on && seamless_windows) HostSeamless::RaiseItems(items);
                    }
                    printf("NEO98 HOST raised %d\n", s.x);
                    fflush(stdout);
                    continue;
                }
                // tops=1 lists the top-level windows of the guest, front to
                // back, with class, title, frame, styles, process, whether
                // the window is visible and has properties, and names the
                // foreground window: what a run without a screen has to say
                // about a shell.
                // trees=1: the rows of the native tree views of the first program.
                if (s.action == "trees") {
                    a->DumpTrees([](const char *line) { printf("NEO98 HOST %s\n", line); });
                    fflush(stdout);
                    continue;
                }
                // lists=1: the rows of the list views and list boxes.
                if (s.action == "lists") {
                    printf("NEO98 HOST input lists\n");
                    a->DumpLists([](const char *line) { printf("NEO98 HOST %s\n", line); });
                    a->DumpListBoxes([](const char *line) { printf("NEO98 HOST %s\n", line); });
                    fflush(stdout);
                    continue;
                }
                if (s.action == "tops") {
                    unsigned nodes[192], count = 0;
                    User::WindowSnapshot(User::GetDesktopWindow(), nodes, 192, count);
                    printf("NEO98 HOST tops foreground=%x focus=%x\n", User::ForegroundActiveWindow(), User::GetFocus());
                    for (unsigned i = 0; i < count; ++i) {
                        const unsigned h = nodes[i];
                        // tops=2: the child windows as well.
                        const bool child = (User::Parent(h) && User::Parent(h) != User::GetDesktopWindow()) || (User::Style(h) & 0x40000000u);
                        if (child && s.x != 2) continue;
                        char name[128] = {}, title[128] = {};
                        Gdi::RECT r{};
                        User::GetClassName(h, name, sizeof name);
                        User::GetWindowText(h, title, sizeof title);
                        User::GetWindowRect(h, &r);
                        printf(child ? "NEO98 HOST child id=%x visible=%d frame=%d,%d,%d,%d style=%08x process=%u props=%d class=%s title=%s\n" : "NEO98 HOST top id=%x visible=%d frame=%d,%d,%d,%d style=%08x process=%u props=%d class=%s title=%s\n", h,
                               User::IsVisible(h) ? 1 : 0, r.left, r.top, r.right, r.bottom, User::Style(h), User::WindowProcess(h),
                               User::PropertyList(h) ? 1 : 0, name, title);
                    }
                    fflush(stdout);
                    continue;
                }
                // redraw=1 makes every window invalid with its frame and
                // its children, as RedrawWindow(NULL, NULL, NULL,
                // RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN)
                // does: a test compares the picture before with the one the
                // programs paint afterwards.
                if (s.action == "redraw") {
                    const bool done = User::RedrawWindow(User::GetDesktopWindow(), nullptr, 0, 0x485);
                    printf("NEO98 HOST redraw %d\n", done ? 1 : 0);
                    fflush(stdout);
                    continue;
                }
                // windows=1 lists the Mac windows the seamless view has or
                // would have, front to back; choose=TITLE is a guest window
                // picked in Mission Control or the window menu.
                if (s.action == "windows") {
                    HostSeamless::Collect(mask, seamless_view(User::Pixels()));
                    HostSeamless::Plan(mask, items);
                    if (Gdi::RECT work{}; User::WorkArea(&work))
                        printf("NEO98 HOST seamless work=%d,%d,%d,%d\n", work.left, work.top, work.right, work.bottom);
                    printf("NEO98 HOST seamless items %zu mode=%s native=%u\n", items.size(),
                           !overlay_on ? "off" : seamless_windows ? "windows" : "layer", HostSeamless::ItemWindows());
                    for (const auto &item : items)
                        printf(item.minimized ? "NEO98 HOST seamless item id=%x minimized frame=%d,%d,%d,%d rects=%zu listed=%d through=%d title=%s\n"
                                              : "NEO98 HOST seamless item id=%x frame=%d,%d,%d,%d rects=%zu listed=%d through=%d title=%s\n",
                               item.id, item.frame.left, item.frame.top, item.frame.right, item.frame.bottom,
                               item.rects.size(), item.listed ? 1 : 0, item.through ? 1 : 0, item.title.c_str());
                    for (const unsigned h : mask.covered)
                        printf("NEO98 HOST seamless covered id=%x title=%s\n", h, HostSeamless::WindowTitle(h).c_str());
                    for (const auto &entry : menu_state.guest_windows)
                        printf("NEO98 HOST menu window id=%x title=%s\n", entry.first, entry.second.c_str());
                    fflush(stdout);
                    continue;
                }
                if (s.action == "choose") {
                    const unsigned h = User::FindWindow(nullptr, s.path.c_str());
                    X86::Application::RequestForeground(h);
                    nudge();
                    printf("NEO98 HOST seamless choose %s hwnd=%x\n", s.path.c_str(), h);
                    fflush(stdout);
                    continue;
                }
                // activation=0 and activation=1: the Mac takes the activation
                // from NEO98 and gives it back. leave=x,y: the Mac pointer
                // leaves the guest's windows at that point.
                // dock-restore=TITLE: the click on a minimized window in the
                // Dock, by its title.
                if (s.action == "dock-restore") {
                    unsigned found = 0;
                    for (const unsigned h : mask.minimized)
                        if (HostSeamless::WindowTitle(h) == s.path) { found = h; break; }
                    printf("NEO98 HOST input dock-restore %s hwnd=%x\n", s.path.c_str(), found);
                    if (found) restore_window(found, "the Dock");
                    fflush(stdout);
                    continue;
                }
                // pause=1 stops the whole machine as the menu does, pause=0
                // lets it go on. The clock of this script is
                // the host's and runs through the pause. focus=0 and focus=1
                // are the window losing and getting the keyboard focus.
                if (s.action == "pause") {
                    machine_pause(s.x != 0, "script");
                    continue;
                }
                // wstate=save, wstate=scan, wstate=can: the test switches of tools/host_state.h.
                if (s.action == "wstate") {
                    HostState::Step(*a, s.path, machine_pause, machine_paused, [&] { a->Pump(); });
                    continue;
                }
                // state=save, state=save:NAME, state=quick, state=load:ID,
                // state=loadname:NAME, state=last: what the items of
                // Machine > state action.
                if (s.action == "state") {
                    const auto colon_at = s.path.find(':');
                    const std::string what = s.path.substr(0, colon_at), argument = colon_at == std::string::npos ? "" : s.path.substr(colon_at + 1);
                    if (what == "save") state_save("manual", argument);
                    else if (what == "quick") state_save("quick", "");
                    else if (what == "load") state_load(argument);
                    else if (what == "loadname") {
                        // By its name, for a script that cannot know the id.
                        std::string id;
                        for (const auto &state : HostSaveState::List(state_context()))
                            if (state.name == argument) id = state.id;
                        state_load(id.empty() ? argument : id);
                    }
                    else if (what == "last") {
                        if (const std::string id = state_last(); !id.empty()) state_load(id);
                        else state_say("Nothing to load.", "No loadable state yet. Press ⇧⌘S to save one.", true);
                    }
                    continue;
                }
                if (s.action == "focus") {
                    SDL_Event focus{};
                    focus.type = SDL_WINDOWEVENT;
                    focus.window.windowID = SDL_GetWindowID(window);
                    focus.window.event = s.x ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
                    SDL_PushEvent(&focus);
                    printf("NEO98 HOST input focus=%d\n", s.x ? 1 : 0);
                    fflush(stdout);
                    continue;
                }
                // sdlkey=USAGE,1 and sdlkey=USAGE,0: a key of the Mac's
                // keyboard goes down or up, as an SDL event with that USB
                // usage. Unlike key= it takes the way of a real key, so the
                // host knows it as held.
                if (s.action == "sdlkey") {
                    SDL_Event key{};
                    key.type = s.y ? SDL_KEYDOWN : SDL_KEYUP;
                    key.key.windowID = SDL_GetWindowID(window);
                    key.key.state = s.y ? SDL_PRESSED : SDL_RELEASED;
                    key.key.keysym.scancode = SDL_Scancode(s.x);
                    key.key.keysym.sym = SDL_GetKeyFromScancode(SDL_Scancode(s.x));
                    SDL_PushEvent(&key);
                    printf("NEO98 HOST input sdlkey %d %s\n", s.x, s.y ? "down" : "up");
                    fflush(stdout);
                    continue;
                }
                if (s.action == "activation") {
                    script_pointer = true;
                    if (!overlay_on && !s.x) mouse_release("another application is active");
                    host_activation(s.x != 0, "script");
                    continue;
                }
                // native=1 prints the native window with its views and layers;
                // native=2 makes it the overlay's window once more before.
                if (s.action == "native") {
                    if (s.x == 2) { HostSeamless::Transparent(window, true); HostSeamless::KeepClear(window); }
                    std::string text = HostSeamless::Describe(window);
                    for (size_t at = 0; at < text.size();) {
                        const size_t end = text.find('\n', at);
                        printf("NEO98 HOST native %s\n", text.substr(at, end - at).c_str());
                        at = end == std::string::npos ? text.size() : end + 1;
                    }
                    fflush(stdout);
                    continue;
                }
                // hostkeys=ctrl+alt: the two keys go down on the Mac keyboard
                // (an SDL key event with both modifiers) and up again.
                // close=TITLE: the close button of the guest window of that
                // title (SC_CLOSE), as Quit in the Dock sends it.
                if (s.action == "close") {
                    unsigned found = 0;
                    for (const unsigned h : program_windows(0))
                        if (HostSeamless::WindowTitle(h) == s.path) { found = h; break; }
                    // close=* closes the active window, whatever its title.
                    if (s.path == "*") found = User::ForegroundActiveWindow();
                    if (found) { User::PostMessage(found, 0x112, 0xf060, 0); nudge(); }
                    printf("NEO98 HOST input close %s hwnd=%x\n", s.path.c_str(), found);
                    fflush(stdout);
                    continue;
                }
                // closebox=1: the close button of the host's window.
                if (s.action == "closebox") {
                    SDL_Event closing{};
                    closing.type = SDL_WINDOWEVENT;
                    closing.window.windowID = SDL_GetWindowID(window);
                    closing.window.event = SDL_WINDOWEVENT_CLOSE;
                    SDL_PushEvent(&closing);
                    SDL_Event quit{};
                    quit.type = SDL_QUIT;
                    SDL_PushEvent(&quit);
                    printf("NEO98 HOST input closebox\n");
                    fflush(stdout);
                    continue;
                }
                if (s.action == "hostkeys") {
                    for (const Uint32 type : {SDL_KEYDOWN, SDL_KEYUP}) {
                        SDL_Event key{};
                        key.type = type;
                        key.key.windowID = SDL_GetWindowID(window);
                        key.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
                        key.key.keysym.scancode = SDL_SCANCODE_LALT;
                        key.key.keysym.sym = SDLK_LALT;
                        key.key.keysym.mod = type == SDL_KEYDOWN ? Uint16(KMOD_LCTRL | KMOD_LALT) : Uint16(KMOD_LCTRL);
                        SDL_PushEvent(&key);
                    }
                    printf("NEO98 HOST input hostkeys ctrl+alt\n");
                    fflush(stdout);
                    continue;
                }
                if (s.action == "leave") {
                    HostSeamless::MouseEvent mouse;
                    mouse.kind = HostSeamless::MouseEvent::Leave;
                    mouse.x = s.x; mouse.y = s.y; mouse.window = hover;
                    item_mouse(mouse);
                    printf("NEO98 HOST seamless leave %d,%d\n", s.x, s.y);
                    fflush(stdout);
                    continue;
                }
                if (s.action == "pointer") {
                    script_pointer = true;
                    script_pointer_x = s.x;
                    script_pointer_y = s.y;
                    pointer_known = false;
                    if (overlay_on) HostSeamless::Collect(mask, seamless_view(User::Pixels()));
                    seamless_pointer();
                    continue;
                }
                // nspress=x,y and nsrelease=x,y: the left button in the Mac
                // window the window server has at that point of the guest's
                // screen, for the proof in a real window.
                if (s.action == "nspress" || s.action == "nsrelease" || s.action == "nsmove" || s.action == "nswheel" ||
                    s.action == "nswheeldown") {
                    using HostSeamless::TestMouse;
                    const unsigned taken = HostSeamless::PostMouse(
                        s.x, s.y, s.action == "nspress" ? TestMouse::Down : s.action == "nsrelease" ? TestMouse::Up
                                  : s.action == "nsmove" ? TestMouse::Move : s.action == "nswheel" ? TestMouse::WheelUp
                                  : TestMouse::WheelDown);
                    printf("NEO98 HOST seamless %s %d,%d window=%x\n", s.action.c_str(), s.x, s.y, taken);
                    fflush(stdout);
                    continue;
                }
                // scroll=UP,RIGHT[,begin|momentum] is a step of the trackpad
                // in points of the content's way; wheel=N are notches of a
                // wheel (a fraction adds up to notches).
                if (s.action == "wheel" || s.action == "scroll") {
                    // The text lives as long as the queued event needs it.
                    static std::deque<std::string> scrolls;
                    const char *text = s.path.c_str();
                    if (s.action == "scroll") { scrolls.push_back("scroll:" + s.path); text = scrolls.back().c_str(); }
                    // sdl2-compat's SDL_PushEvent drops preciseY on the way
                    // through SDL3. Keep scripted fractions in a user event;
                    // decode it before the normal SDL_MOUSEWHEEL handler.
                    SDL_Event event{};
                    event.type = SDL_USEREVENT;
                    event.user.code = ScriptMouse;
                    event.user.data1 = const_cast<char *>(text);
                    SDL_PushEvent(&event);
                    continue;
                }
                if (s.action == "press" || s.action == "release") {
                    SDL_Event button{};
                    button.type = s.action == "press" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                    button.button.windowID = SDL_GetWindowID(window);
                    button.button.which = ScriptMouse;
                    button.button.button = SDL_BUTTON_LEFT;
                    button.button.state = s.action == "press" ? SDL_PRESSED : SDL_RELEASED;
                    button.button.x = s.x;
                    button.button.y = s.y;
                    SDL_PushEvent(&button);
                    continue;
                }
                // keydown=VK and keyup=VK hold a key over several steps, as a
                // hand holds ALT while it presses F.
                if (s.action == "keydown" || s.action == "keyup") {
                    unsigned scan = 0;
                    bool extended = false;
                    for (unsigned usage = 1; usage < 128; ++usage) {
                        const auto mapped = UsbKeys::Map(usage, german, true);
                        if (mapped.vk == unsigned(s.x)) { scan = mapped.scan; extended = mapped.extended; break; }
                    }
                    switch (s.x) {
                    case 0xa0: scan = 0x2a; break; case 0xa1: scan = 0x36; break;
                    case 0xa2: case 0xa3: scan = 0x1d; break; case 0xa4: case 0xa5: scan = 0x38; break;
                    default: break;
                    }
                    a->RawKey((unsigned)s.x, 0, s.action == "keydown", scan, extended || s.x == 0xa3 || s.x == 0xa5);
                    printf("NEO98 HOST input %s %x\n", s.action.c_str(), (unsigned)s.x);
                    fflush(stdout);
                    last_input_time = std::chrono::steady_clock::now();
                    pacing.Input();
                    continue;
                }
                if (s.action == "key" || s.action == "type") {
                    // The modifiers the layout needs go down around the key,
                    // and USER translates the key with that key state.
                    auto press = [&](unsigned vk, unsigned character, unsigned modifiers) {
                        unsigned scan = 0;
                        bool extended = false;
                        for (unsigned usage = 1; usage < 128; ++usage) {
                            const auto mapped = UsbKeys::Map(usage, german, true);
                            if (mapped.vk == vk) { scan = mapped.scan; extended = mapped.extended; break; }
                        }
                        if (modifiers & 1) a->RawKey(0xa0, 0, true, 0x2a, false);
                        if (modifiers & 2) a->RawKey(0xa2, 0, true, 0x1d, false);
                        if (modifiers & 4) a->RawKey(0xa4, 0, true, 0x38, false);
                        a->RawKey(vk, character, true, scan, extended);
                        a->RawKey(vk, 0, false, scan, extended);
                        if (modifiers & 4) a->RawKey(0xa4, 0, false, 0x38, false);
                        if (modifiers & 2) a->RawKey(0xa2, 0, false, 0x1d, false);
                        if (modifiers & 1) a->RawKey(0xa0, 0, false, 0x2a, false);
                    };
                    if (s.action == "key") press((unsigned)s.x, s.x == 0x0d ? '\r' : 0, 0);
                    for (size_t i = 0; i < s.path.size(); ++i) {
                        // The script is UTF-8; the guest page is Windows-1252,
                        // which agrees with Latin-1 above A0h and has the euro at 80h.
                        unsigned c = (unsigned char)s.path[i];
                        if (c >= 0xc0 && c < 0xe0 && i + 1 < s.path.size())
                            c = ((c & 0x1f) << 6) | ((unsigned char)s.path[++i] & 0x3f);
                        else if (c == 0xe2 && i + 2 < s.path.size() && (unsigned char)s.path[i + 1] == 0x82 &&
                                 (unsigned char)s.path[i + 2] == 0xac)
                            c = 0x80, i += 2;
                        if (c > 255) continue;
                        // VkKeyScan of the installed layout.
                        const unsigned scanned = a->KeyScan(c);
                        if (scanned != 0xffff) press(scanned & 255, c, (scanned >> 8) & 7);
                    }
                    printf("NEO98 HOST input %s %s\n", s.action.c_str(), s.path.c_str());
                    fflush(stdout);
                    last_input_time = std::chrono::steady_clock::now();
                    pacing.Input();
                    continue;
                }
                // motion=x,y is the hand on the Mac mouse: an SDL motion event
                // in window points, through the same path as a real one, so
                // the pointer of the host moves and a guest that recentres it
                // warps it back. The other actions go to the guest
                // directly.
                // hand=dx,dy moves the Mac pointer by that much from where SDL
                // has it, which is where the last warp of the host put it: the
                // event carries the new place and the distance, as a real one
                // does (tools/test_pause_mouse_host.py).
                if (s.action == "hand") {
                    int px = 0, py = 0;
                    SDL_GetMouseState(&px, &py);
                    SDL_WarpMouseInWindow(window, px + s.x, py + s.y);
                    SDL_Event motion{};
                    motion.type = SDL_MOUSEMOTION;
                    motion.motion.windowID = SDL_GetWindowID(window);
                    motion.motion.which = ScriptMouse;
                    motion.motion.x = px + s.x;
                    motion.motion.y = py + s.y;
                    motion.motion.xrel = s.x;
                    motion.motion.yrel = s.y;
                    SDL_PushEvent(&motion);
                    printf("NEO98 HOST input hand %d,%d -> %d,%d\n", s.x, s.y, px + s.x, py + s.y);
                    fflush(stdout);
                    continue;
                }
                if (s.action == "motion") {
                    SDL_Event motion{};
                    motion.type = SDL_MOUSEMOTION;
                    motion.motion.windowID = SDL_GetWindowID(window);
                    motion.motion.which = ScriptMouse;
                    motion.motion.x = s.x;
                    motion.motion.y = s.y;
                    int wx = 0, wy = 0;
                    CanvasToWindow(LayoutFor(window, renderer), last_mouse_x, last_mouse_y, wx, wy);
                    motion.motion.xrel = s.x - wx;
                    motion.motion.yrel = s.y - wy;
                    sscanf(s.path.c_str(), "%d,%d,%d,%d", &motion.motion.x, &motion.motion.y,
                           &motion.motion.xrel, &motion.motion.yrel);
                    SDL_PushEvent(&motion);
                    if (cursor_trace) {
                        printf("NEO98 HOST CURSOR queued sdl=%d,%d ticks=%u late_ms=%.3f\n",
                               s.x, s.y, SDL_GetTicks(), (real - paused - s.seconds) * 1000);
                        fflush(stdout);
                    }
                    continue;
                }
                int mx = s.x, my = s.y;
                CanvasToGuest(mx, my);
                const unsigned button = s.action == "rclick" ? 2 : 1;
                const int presses = s.action == "dblclick" ? 2 : s.action == "move" ? 0 : 1;
                // down= and up= split a click over time, as a hand does: the
                // guest decides between the two where the release goes
                // (a menu takes the capture on the press).
                if (s.action == "down" || s.action == "up" || (s.action == "move" && held)) {
                    if (s.action != "move") held = s.action == "down" ? 1 : 0;
                    a->MouseButtons(mx, my, held);
                    printf("NEO98 HOST input %s %d,%d\n", s.action.c_str(), s.x, s.y);
                    fflush(stdout);
                    last_input_time = std::chrono::steady_clock::now();
                    pacing.Input();
                    continue;
                }
                a->MouseButtons(mx, my, 0);
                for (int n = 0; n < presses; ++n) {
                    a->MouseButtons(mx, my, button);
                    a->MouseButtons(mx, my, 0);
                }
                printf("NEO98 HOST input %s %d,%d\n", s.action.c_str(), s.x, s.y);
                fflush(stdout);
                last_input_time = std::chrono::steady_clock::now();
                pacing.Input();
            }
        }
        SDL_Event ev;
        User::TouchPump();
        if (scroll_log.Due(SDL_GetTicks64())) scroll_log_line();
        static bool close_asked = false;
        watch_pointer(window);
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_USEREVENT && ev.user.code == int(ScriptMouse) && ev.user.data1 &&
                !strncmp(static_cast<const char *>(ev.user.data1), "scroll:", 7)) {
                // A scripted step of the trackpad.
                const char *text = static_cast<const char *>(ev.user.data1) + 7;
                char *end = nullptr;
                const double up = strtod(text, &end);
                const double right = *end == ',' ? strtod(end + 1, &end) : 0;
                const bool first = strstr(end, "begin") != nullptr, momentum = strstr(end, "momentum") != nullptr;
                const auto layout = LayoutFor(window, renderer);
                const HostScroll out = host_wheel.Points(up, right, layout.canvas_w > 0 ? double(layout.guest.w) / layout.canvas_w : 1, first, momentum);
                printf("NEO98 HOST input scroll up=%g right=%g units=%d,%d flags=%u pointer=%d\n", up, right, out.y, out.x,
                       out.flags, User::WheelToPointer() ? 1 : 0);
                fflush(stdout);
                if (out.y || out.x) {
                    a->MouseScroll(out.y, out.x, out.flags);
                    last_input_time = std::chrono::steady_clock::now();
                    pacing.Input();
                }
                continue;
            }
            if (ev.type == SDL_USEREVENT && ev.user.code == int(ScriptMouse) && ev.user.data1) {
                const double delta = strtod(static_cast<const char *>(ev.user.data1), nullptr);
                ev = {};
                ev.type = SDL_MOUSEWHEEL;
                ev.wheel.windowID = SDL_GetWindowID(window);
                ev.wheel.which = ScriptMouse;
                ev.wheel.y = std::isfinite(delta) ? int(std::max(-2048.0, std::min(2048.0, delta))) : 0;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                ev.wheel.preciseY = float(delta);
#endif
            }
            switch (ev.type) {
            case SDL_USEREVENT:
                if (unsigned(ev.user.code) == 0x98544301u) User::TouchCancelAll(true);
                break;
            case SDL_QUIT:
                // The close button of the window of a program mode closes the
                // program, as its own close button would (SC_CLOSE); NEO98
                // goes on. Cmd+Q and the menu end NEO98 (Action::Quit).
                if (close_asked) { close_asked = false; break; }
                running = false;
                break;
            case SDL_MOUSEMOTION: {
                if (ev.motion.which == SDL_TOUCH_MOUSEID) break;
                // A paused machine has no mouse and no keyboard.
                if (machine_paused) { host_pointer(false); break; }
                // With a Mac window per guest window the mouse comes from
                // those windows (HostSeamless::NextMouse), not from SDL's.
                if (overlay_on && seamless_windows && ev.motion.which != ScriptMouse) break;
                // Let go (Ctrl+Option), the mouse is the Mac's until a press.
                if (mouse_hold.released) { host_pointer(false); break; }
                // Ordinary absolute input stops at the guest area. A game that
                // recentres instead receives the hand's distance from its warp.
                int gx = 0, gy = 0, mx = 0, my = 0;
                const auto mouse_layout = LayoutFor(window, renderer);
                const bool relative = !overlay_on && (mouse_recentres() || X86::Application::DinputMouseExclusive());
                const Gdi::POINT motion{ev.motion.xrel, ev.motion.yrel};
                if (relative) {
                    // SDL computes xrel/yrel before queuing the event. Its absolute
                    // position may predate a guest SetCursorPos, or be a focus-entry
                    // sample with zero movement. Rebase the hand's distance on the
                    // latest warp instead of reporting that old position as a jump.
                    // The hand's distance is in mickeys, independent of the
                    // display mode and of the size of the host window.
                    Gdi::POINT at{};
                    User::CursorPosition(&at);
                    mx = at.x + motion.x; my = at.y + motion.y;
                    User::ConfinePointer(mx, my);
                    gx = mx; gy = my;
                    GuestToCanvas(gx, gy);
                } else {
                    if (!WindowToCanvas(mouse_layout, ev.motion.x, ev.motion.y, gx, gy)) {
                        host_pointer(false);
                        break;
                    }
                    mx = gx; my = gy;
                    CanvasToGuest(mx, my);
                }
                // In the seamless view a point outside the guest's windows
                // is the Mac's (seamless_pointer shows whose pointer it is).
                if (overlay_on && !seamless_takes(gx, gy)) break;
                host_pointer(!overlay_on);
                const unsigned buttons = (ev.motion.state & SDL_BUTTON_LMASK ? 1 : 0) |
                                         (ev.motion.state & SDL_BUTTON_RMASK ? 2 : 0) |
                                         (ev.motion.state & SDL_BUTTON_MMASK ? 4 : 0);
                // Only a packet that changes the guest pointer position or a
                // button is input the guest can answer. A jiggle below one
                // canvas pixel changes nothing, and counting it as input made
                // the hang indicator below claim a hang on an idle desktop.
                const bool changed = gx != last_mouse_x || gy != last_mouse_y || buttons != last_mouse_buttons;
                const unsigned guest_buttons = HostMouseButtons::Motion(User::PhysicalMouseButtons(), last_mouse_buttons, buttons);
                last_mouse_x = gx; last_mouse_y = gy; last_mouse_buttons = buttons;
                // The guest sees its own current screen: a program in
                // a smaller DirectDraw mode gets pointer positions in that
                // mode's pixels, not in this window's fixed 800x600 canvas.
                if (cursor_trace) {
                    printf("NEO98 HOST CURSOR motion sdl=%d,%d rel=%d,%d canvas=%d,%d guest=%d,%d\n",
                           ev.motion.x, ev.motion.y, ev.motion.xrel, ev.motion.yrel, gx, gy, mx, my);
                    printf("NEO98 HOST CURSOR delivered ticks=%u queue_ms=%u\n",
                           SDL_GetTicks(), SDL_GetTicks() - ev.motion.timestamp);
                    fflush(stdout);
                }
                a->MouseButtons(mx, my, guest_buttons, &motion);
                if (changed) { last_input_time = std::chrono::steady_clock::now(); pacing.Input(); }
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                if (ev.button.which == SDL_TOUCH_MOUSEID) break;
                if (ev.type == SDL_MOUSEBUTTONDOWN && state_go_on("a click after the load")) break;
                if (machine_paused) break;
                int gx = 0, gy = 0;
                if (!WindowToCanvas(LayoutFor(window, renderer), ev.button.x, ev.button.y, gx, gy) &&
                    ev.type == SDL_MOUSEBUTTONDOWN)
                    break;
                const bool scripted = ev.button.which == ScriptMouse;
                if (overlay_on && seamless_windows && !scripted) break;
                if (mouse_hold.released) {
                    if (ev.type != SDL_MOUSEBUTTONDOWN || (overlay_on && !seamless_takes(gx, gy))) break;
                    mouse_capture("press");
                }
                if (overlay_on && ev.type == SDL_MOUSEBUTTONDOWN && !seamless_takes(gx, gy)) {
                    if (scripted) {
                        printf("NEO98 HOST seamless press %d,%d goes through to the Mac\n", gx, gy);
                        fflush(stdout);
                        // What it does on the Mac: another application, or
                        // the Finder's desktop, comes to the front.
                        host_activation(false, "a press on the Mac");
                    }
                    break;
                }
                // A scripted press on a guest window brings NEO98 back, and
                // the press activates the window.
                if (scripted && overlay_on && ev.type == SDL_MOUSEBUTTONDOWN) guest_active = true;
                const unsigned buttons = HostMouseButtons::Apply(
                    last_mouse_buttons, ev.button.button, ev.type == SDL_MOUSEBUTTONDOWN);
                const unsigned guest_buttons = HostMouseButtons::Apply(
                    User::PhysicalMouseButtons(), ev.button.button, ev.type == SDL_MOUSEBUTTONDOWN);
                seamless_buttons = buttons;
                if (scripted && overlay_on) {
                    printf("NEO98 HOST seamless %s %d,%d reaches the guest\n",
                           ev.type == SDL_MOUSEBUTTONDOWN ? "press" : "release", gx, gy);
                    fflush(stdout);
                }
                last_mouse_buttons = buttons;
                int mx = gx, my = gy;
                CanvasToGuest(mx, my);
                // SDL reports movement separately. Button coordinates must
                // not turn a relative packet back into scaled movement.
                if (!overlay_on && (mouse_recentres() || X86::Application::DinputMouseExclusive())) {
                    Gdi::POINT at{}; User::CursorPosition(&at);
                    mx = at.x; my = at.y;
                    gx = mx; gy = my; GuestToCanvas(gx, gy);
                }
                last_mouse_x = gx; last_mouse_y = gy;
                const Gdi::POINT motion{};
                a->MouseButtons(mx, my, guest_buttons, &motion);
                last_input_time = std::chrono::steady_clock::now();
                pacing.Input();
                break;
            }
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP: {
                // An indirect trackpad is still the Mac's mouse, not an
                // absolute touchscreen over the guest. -1 is our script ID.
                if (ev.tfinger.touchId != -1 && SDL_GetTouchDeviceType(ev.tfinger.touchId) != SDL_TOUCH_DEVICE_DIRECT) break;
                if (machine_paused) break;
                int width = 0, height = 0, x = 0, y = 0;
                SDL_GetWindowSize(window, &width, &height);
                const bool inside = WindowToCanvas(LayoutFor(window, renderer), int(ev.tfinger.x * width),
                                                    int(ev.tfinger.y * height), x, y);
                // Keep an outside beginning tracked but ignored; subsequent
                // movement into the guest must not turn it into a new finger.
                if (!inside && ev.type == SDL_FINGERDOWN) { x = -100000; y = -100000; }
                const unsigned phase = ev.tfinger.touchId == -1 && ev.tfinger.dx == -100000.f ? NEO98_TOUCH_CANCEL :
                                       ev.type == SDL_FINGERDOWN ? NEO98_TOUCH_DOWN :
                                       ev.type == SDL_FINGERUP ? NEO98_TOUCH_UP : NEO98_TOUCH_MOVE;
                if (mouse_hold.released && phase == NEO98_TOUCH_DOWN) break;
                deliver_touch((unsigned long long)ev.tfinger.touchId, (unsigned long long)ev.tfinger.fingerId,
                              phase, x, y, int(ev.tfinger.pressure * 1024), ev.tfinger.timestamp);
                break;
            }
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) host_wheel.Reset();
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) User::TouchCancelAll(true);
                if (ev.window.event == SDL_WINDOWEVENT_LEAVE) host_pointer(false);
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE && seamless && !overlay_on) {
                    unsigned front = User::GetForegroundWindow();
                    while (front && (User::Style(front) & 0x40000000u) && User::Parent(front)) front = User::Parent(front);
                    if (front) {
                        User::PostMessage(front, 0x112, 0xf060, 0); // WM_SYSCOMMAND SC_CLOSE
                        nudge();
                        close_asked = true; // the SDL_QUIT that follows is this close
                        printf("NEO98 HOST program window closed: SC_CLOSE to %x\n", front);
                        fflush(stdout);
                    }
                }
                // Cmd+Tab, a press on another application.
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !overlay_on)
                    mouse_release("another application is active");
                // In the seamless view the activation of the application
                // decides (host_activation), here the focus of the window.
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST && !overlay_on) focus_pause(false);
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && !overlay_on) focus_pause(true);
                break;
            case SDL_MOUSEWHEEL: {
                if (overlay_on && seamless_windows) break;
                if (mouse_hold.released || machine_paused) break;
                // The sign already follows the Mac's natural-scroll setting.
                // Do not invert it again because SDL calls it FLIPPED.
                double delta = ev.wheel.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                if (ev.wheel.preciseY) delta = ev.wheel.preciseY;
#endif
                double sideways = ev.wheel.x;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                if (ev.wheel.preciseX) sideways = ev.wheel.preciseX;
#endif
                // The record of the monitor (HostSeamless::WatchScroll) and
                // the units for the guest: tools/host_wheel.h.
                const bool script = ev.wheel.which == ScriptMouse, held = !script && mouse_recentres();
                const auto wheel_layout = LayoutFor(window, renderer);
                const double wheel_scale = wheel_layout.canvas_w > 0 ? double(wheel_layout.guest.w) / wheel_layout.canvas_w : 1;
                const HostWheelStep step = HostWheelFromSdl(host_wheel, delta, sideways, script, held, User::GetFocus(), wheel_scale, SDL_GetTicks64());
                if (script) {
                    printf("NEO98 HOST input wheel delta=%.6g steps=%d focus=%x\n", delta, step.steps, User::GetFocus());
                    fflush(stdout);
                } else {
                    // A gesture a line in the session log (scroll_log_line below).
                    const unsigned long long now = SDL_GetTicks64();
                    if (scroll_log.Starts(step, now)) scroll_log_line();
                    scroll_log.Add(step, delta, sideways, held, now);
                }
                if (step.kind == HostWheelStep::Notches) a->MouseWheel(step.steps);
                else if (step.kind == HostWheelStep::Scroll) a->MouseScroll(step.out.y, step.out.x, step.out.flags);
                if (step.kind != HostWheelStep::Nothing) {
                    last_input_time = std::chrono::steady_clock::now();
                    pacing.Input();
                }
                break;
            }
            case SDL_DROPFILE:
                // application:openURLs: of the Finder and a file dropped
                // onto the window arrive the same way.
                if (ev.drop.file) {
                    open_path(ev.drop.file, false, true);
                    SDL_free(ev.drop.file);
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                const unsigned usage = (unsigned)ev.key.keysym.scancode;
                // Cmd+Shift+R reboots, Cmd+Shift+B only writes a report, and
                // the other key equivalents of the menu bar; see host_shortcut.
                if (host_shortcut(ev.key)) break;
                // A machine that stands after a load goes on with any key;
                // that key is the person's word to the host, not the game's.
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat && state_start.loaded && machine_paused) {
                    const auto sym = ev.key.keysym.sym;
                    const bool modifier = sym == SDLK_LSHIFT || sym == SDLK_RSHIFT || sym == SDLK_LCTRL || sym == SDLK_RCTRL ||
                                          sym == SDLK_LALT || sym == SDLK_RALT || sym == SDLK_LGUI || sym == SDLK_RGUI ||
                                          sym == SDLK_CAPSLOCK;
                    if (!modifier && !(ev.key.keysym.mod & KMOD_GUI) && state_go_on("a key after the load")) break;
                }
                // Ctrl+Option lets a held mouse go. The keys still reach the
                // guest as the modifiers they are.
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                    const auto sym = ev.key.keysym.sym;
                    const unsigned mod = ev.key.keysym.mod | SDL_GetModState();
                    if ((sym == SDLK_LCTRL || sym == SDLK_RCTRL || sym == SDLK_LALT || sym == SDLK_RALT) &&
                        (mod & KMOD_CTRL) && (mod & KMOD_ALT) && !(mod & (KMOD_SHIFT | KMOD_GUI)))
                        mouse_release("ctrl+option");
                }
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F11) { host_dump_all(a); break; }
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F12) {
                    char name[64];
                    snprintf(name, sizeof name, "neo98-host-%u.ppm", ++shots);
                    if (screenshot(name,a)) printf("NEO98 HOST screenshot %s\n", name);
                    break;
                }
                // Keys of a pause reach nobody; machine_pause settles the
                // ones that changed when the machine goes on.
                if (machine_paused) break;
                a->KeyboardModifiers(guest_modifiers());
                last_input_time = std::chrono::steady_clock::now();
                if (usage >= 0xe0 || usage >= 128) break; // modifiers travel through the mask
                if (ev.type == SDL_KEYDOWN) {
                    if (ev.key.repeat && !held_vk[usage]) break;
                    if (!held_vk[usage]) {
                        const auto key = UsbKeys::Map(usage, german, (SDL_GetModState() & KMOD_NUM) != 0);
                        held_vk[usage] = key.vk; held_scan[usage] = key.scan; held_extended[usage] = key.extended;
                    }
                    if (held_vk[usage]) a->RawKey(held_vk[usage], character_for(ev.key.keysym), true, held_scan[usage], held_extended[usage]);
                } else {
                    if (held_vk[usage]) a->RawKey(held_vk[usage], 0, false, held_scan[usage], held_extended[usage]);
                    held_vk[usage] = 0;
                }
                break;
            }
            default: break;
            }
        }
        // The mouse in the Mac windows of the guest's windows, in screen
        // pixels of the guest.
        if (overlay_on && seamless_windows) {
            HostSeamless::MouseEvent mouse;
            while (HostSeamless::NextMouse(mouse))
                if (!machine_paused) item_mouse(mouse);
                else if (mouse.kind == HostSeamless::MouseEvent::Button && mouse.buttons) state_go_on("a click after the load");
        }
        const auto now = std::chrono::steady_clock::now();
        // Hang observation. An API call is the only progress the host can see
        // from outside; GetMessage and PeekMessage are API calls too, so a
        // message loop that spins still counts as alive. The calls of the child
        // processes count as well: an autostart child that answers the mouse
        // leaves the shell without a call for seconds without anything hanging.
        // A DOS machine's loop counts by its turns (Application::Progress).
        if (const unsigned calls = HostPacing::TotalCalls(*a); calls != last_calls) {
            last_calls = calls;
            last_call_time = now;
        }
        // The clock starts when the guest has something to do and no progress
        // since: the first input it has not answered, or a runnable thread.
        // The first move after a quiet minute is no hang of a minute; it gets
        // its three seconds like any other.
        const bool input_waiting = last_input_time > last_call_time;
        const bool waiting = !machine_paused && (input_waiting || a->SystemRunnable());
        HostHang::Observe(waiting_since, was_waiting, now, last_call_time, last_input_time,
                          input_waiting, waiting);
        const bool stalled = waiting && now - waiting_since >= HangSeconds;
        const unsigned stalled_ms = (unsigned)std::chrono::duration_cast<std::chrono::milliseconds>(
                                        now - waiting_since).count();
        // The title follows the program in a program mode.
        {
            std::string wanted;
            if (seamless && !overlay_on) {
                unsigned front = User::GetForegroundWindow();
                while (front && (User::Style(front) & 0x40000000u) && User::Parent(front)) front = User::Parent(front);
                if (front) wanted = HostSeamless::WindowTitle(front);
            }
            if (wanted != shown_program_title) title_stale = true;
        }
        if (stalled != hang_now || (stalled && stalled_ms / 1000 != hang_ms_now / 1000) || title_stale) {
            const bool say = title_stale;
            title_stale = false;
            // While the guest holds the mouse the title names the way out.
            // In a program mode of the seamless view the window is the
            // program's: it carries the title of the guest's foreground window.
            std::string program_title;
            if (seamless && !overlay_on) {
                unsigned front = User::GetForegroundWindow();
                while (front && (User::Style(front) & 0x40000000u) && User::Parent(front)) front = User::Parent(front);
                if (front) program_title = HostSeamless::WindowTitle(front);
            }
            shown_program_title = program_title;
            const char *mouse = mouse_hold.shown == 1 ? " - Ctrl+Option releases the mouse"
                                : mouse_hold.shown == 2 ? " - Mouse released, click the window to capture it" : "";
            char title[256];
            const char *name = program_title.empty() ? window_name.c_str() : program_title.c_str();
            if (machine_paused)
                snprintf(title, sizeof title, "%s [Pause]", name);
            else if (stalled)
                snprintf(title, sizeof title, "%s [unresponsive for %u s]%s", name, stalled_ms / 1000, mouse);
            else
                snprintf(title, sizeof title, "%s%s", name, mouse);
            SDL_SetWindowTitle(window, title);
            if (say) { printf("NEO98 HOST title %s\n", title); fflush(stdout); }
        }
        if (stalled != hang_now) {
            menu_state.hang = stalled; HostMenu::Update(menu_state);
            fprintf(stderr, "NEO98 HOST hang %s\n", stalled ? "begin" : "end");
        }
        hang_now = stalled;
        hang_ms_now = stalled ? stalled_ms : 0;
        if (now - last_present >= std::chrono::milliseconds(16) ||
            a->Status()==X86::Application::Exited || a->Status()==X86::Application::Failed) {
            last_present = now;
            int frame_w = 800, frame_h = 600;
            bool text_view = false;
            // In a program mode the window has the size of that mode, and so
            // has the frame USER hands out: one conversion at most.
            User::SetHostFrameFollowsMode(seamless);
            X86::Application::SetHostKeepsDosWindows(seamless);
            User::SetHostHiddenWindow(seamless && !seamless_taskbar && !zoomed &&
                                      !User::ScreenModeActive() && !User::ExclusiveMode()
                                          ? User::FindWindow("Shell_TrayWnd", nullptr) : 0);
            const auto *pixels = frame_pixels(a, frame_w, frame_h, &text_view);
            // Alt+Enter or "Full screen" of a DOS window in the seamless
            // view: that window alone over the display, and back.
            unsigned asked = 0;
            int asked_strip = 0;
            if (X86::Application::TakeHostDosFullscreen(asked, asked_strip)) {
                if (zoomed == asked) zoom_end("the DOS window asked");
                else if (seamless && !zoomed && User::IsWindow(asked) && User::IsVisible(asked)) {
                    zoomed = asked;
                    set_overlay(false, UsableArea(window), "full screen of a program");
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                    printf("NEO98 HOST full screen of a program hwnd=%x (asked by the DOS window)\n", zoomed);
                    fflush(stdout);
                }
            }
            serve_zoom();
            // A DOS window with a 3dfx Voodoo draws for the whole output
            // while it is the one shown alone (User::SetOverlay).
            X86::Application::SetHostZoomedWindow(zoomed);
            serve_seamless(text_view, text_view ? nullptr : pixels);
            // The canvas is the desktop's frame; the text view of a console
            // shows in the same window, stretched onto it. With the seamless
            // view wanted and a program mode in its place, the window has the
            // size of that mode instead of the desktop's, which is the Mac's
            // screen.
            if (zoomed) {
                // The canvas is the client area of the window (serve_zoom).
            } else if (seamless && !overlay_on) {
                int view_w = frame_w, view_h = frame_h, view_depth = 32;
                if (!text_view) User::ScreenMode(view_w, view_h, view_depth);
                follow_canvas(view_w, view_h);
            } else if (!text_view) follow_canvas(frame_w, frame_h);
            if (pixels && (frame_w != texture_w || frame_h != texture_h)) {
                SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, frame_w, frame_h);
                if (texture) SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
                texture_w = frame_w;
                texture_h = frame_h;
                if (!texture) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); pixels = nullptr; running = false; }
            }
            if (pixels) {
                // NEO98_FRAME_LOG=<folder>: every presented frame that differs
                // from the one before goes there as a 200x150 PPM, with one
                // FRAMELOG line (time, frame, file). A flicker between two
                // screenshots shows up this way.
                static const char *frame_log = getenv("NEO98_FRAME_LOG");
                if (frame_log) {
                    static unsigned long long last_hash = 0;
                    static unsigned frames = 0, dumped = 0;
                    unsigned long long hash = 1469598103934665603ull;
                    for (unsigned i = 0; i < unsigned(frame_w * frame_h); i += 13) { hash ^= *(const unsigned *)(pixels + i * 4); hash *= 1099511628211ull; }
                    ++frames;
                    if (hash != last_hash && dumped < 3000) {
                        last_hash = hash;
                        char path[512];
                        snprintf(path, sizeof path, "%s/f%05u.ppm", frame_log, dumped++);
                        if (FILE *f = fopen(path, "wb")) {
                            fprintf(f, "P6\n%d %d\n255\n", frame_w / 4, frame_h / 4);
                            for (int y = 0; y + 3 < frame_h; y += 4) for (int x = 0; x + 3 < frame_w; x += 4) {
                                const unsigned char *q = pixels + (y * frame_w + x) * 4;
                                unsigned char rgb[] = {q[2], q[1], q[0]}; fwrite(rgb, 1, 3, f);
                            }
                            fclose(f);
                        }
                        printf("FRAMELOG t=%.3f frame=%u file=%s\n",
                               std::chrono::duration<double>(now - host_start).count(), frames, path);
                        fflush(stdout);
                    }
                }
                // The seamless view shows the guest's windows and nothing
                // of its desktop: alpha from USER's visible regions.
                const bool shaped = overlay_on && !text_view && mask.width == frame_w && mask.height == frame_h;
                // A paused machine keeps its last picture, dimmed and with
                // the mark. In the seamless view the picture is the guest's
                // windows alone, so the copy handed to them is dimmed and the
                // mark sits in the window in front; the window of the other
                // views is dimmed as a whole further down.
                static std::vector<unsigned char> pause_picture;
                static bool pause_drawn = false;
                if (!machine_paused) pause_drawn = false;
                if (machine_paused && shaped) {
                    const size_t bytes = size_t(frame_w) * size_t(frame_h) * 4;
                    if (!pause_drawn || pause_picture.size() != bytes) {
                        pause_picture.assign(pixels, pixels + bytes);
                        for (size_t i = 0; i < bytes; i += 4)
                            for (int c = 0; c < 3; ++c) pause_picture[i + c] = (unsigned char)(pause_picture[i + c] * 2 / 5);
                        int cx = frame_w / 2, cy = frame_h / 2;
                        unsigned front = User::GetForegroundWindow();
                        while (front && (User::Style(front) & 0x40000000u) && User::Parent(front)) front = User::Parent(front);
                        Gdi::RECT place{};
                        if (front && front != User::RegisteredShellWindow() && User::IsVisible(front) &&
                            User::GetWindowRect(front, &place) && place.right > place.left && place.bottom > place.top) {
                            cx = (place.left + place.right) / 2;
                            cy = (place.top + place.bottom) / 2;
                        }
                        const int unit = std::max(2, frame_h / 300);
                        PauseMark(cx, cy, unit, [&](int x, int y, int w, int h, bool light) {
                            for (int py = std::max(0, y); py < std::min(frame_h, y + h); ++py)
                                for (int px = std::max(0, x); px < std::min(frame_w, x + w); ++px) {
                                    unsigned char *q = pause_picture.data() + (size_t(py) * frame_w + px) * 4;
                                    q[0] = q[1] = q[2] = light ? 255 : 24;
                                    q[3] = 255;
                                }
                        });
                        pause_drawn = true;
                        printf("NEO98 HOST pause picture seamless %dx%d mark=%d,%d\n", frame_w, frame_h, cx, cy);
                        fflush(stdout);
                    }
                    pixels = pause_picture.data();
                }
                // A note about a saved state. In the
                // seamless view the picture is the guest's windows alone,
                // so the note is drawn into the copy handed to them, at the
                // top of the window in front; the other views draw it over
                // the picture further down.
                std::string note_title, note_detail;
                bool note_bad = false;
                double note_alpha = 1;
                const unsigned note = state_note_now(note_title, note_detail, note_bad, note_alpha);
                if (note && shaped) {
                    static std::vector<unsigned char> plate, noted;
                    static int plate_w = 0, plate_h = 0;
                    static unsigned plate_note = 0;
                    static const unsigned char *noted_from = nullptr;
                    if (plate_note != note) {
                        plate_note = note;
                        noted_from = nullptr;
                        if (!HostStateWindow::RenderNote(note_title, note_detail, note_bad, 1.0, frame_w * 9 / 10, plate, plate_w, plate_h))
                            plate_w = plate_h = 0;
                    }
                    // A picture that stands is drawn once; one that moves,
                    // or a note that fades, every frame.
                    if (plate_w && (noted_from != pixels || !machine_paused || note_alpha < 1)) {
                        noted_from = pixels;
                        noted.assign(pixels, pixels + size_t(frame_w) * size_t(frame_h) * 4);
                        int cx = frame_w / 2, top = 12;
                        unsigned front = User::GetForegroundWindow();
                        while (front && (User::Style(front) & 0x40000000u) && User::Parent(front)) front = User::Parent(front);
                        Gdi::RECT place{};
                        if (front && front != User::RegisteredShellWindow() && User::IsVisible(front) &&
                            User::GetWindowRect(front, &place) && place.right > place.left) {
                            cx = (place.left + place.right) / 2;
                            top = place.top + 34;
                        }
                        const int left = cx - plate_w / 2;
                        for (int y = 0; y < plate_h; ++y) {
                            const int py = top + y;
                            if (py < 0 || py >= frame_h) continue;
                            for (int x = 0; x < plate_w; ++x) {
                                const int px = left + x;
                                if (px < 0 || px >= frame_w) continue;
                                const unsigned char *from = plate.data() + (size_t(y) * plate_w + x) * 4;
                                unsigned char *to = noted.data() + (size_t(py) * frame_w + px) * 4;
                                const unsigned alpha = unsigned(from[3] * note_alpha);
                                for (int c = 0; c < 3; ++c) to[c] = (unsigned char)((from[c] * alpha + to[c] * (255 - alpha)) / 255);
                                to[3] = (unsigned char)std::max<unsigned>(to[3], alpha);
                            }
                        }
                    }
                    if (plate_w) pixels = noted.data();
                }
                if (shaped) {
                    if (seamless_windows) {
                        // Every guest window in a Mac window of its own; the
                        // window of SDL stays empty behind them.
                        HostSeamless::Plan(mask, items);
                        HostSeamless::Present(window, items, pixels, frame_w, frame_h);
                        seamless_picture.assign(size_t(frame_w) * size_t(frame_h), 0u);
                        // NEO98_SEAMLESS_TRACE=1: how much of each window is
                        // still the colour of the desktop, whenever it changes
                        // by more than a few percent.
                        static const bool paint_trace = getenv("NEO98_SEAMLESS_TRACE") != nullptr;
                        if (paint_trace) {
                            static std::unordered_map<unsigned, int> shares;
                            for (const auto &item : items) {
                                if (item.through || item.desktop) continue;
                                const int share = HostSeamless::DesktopShare(item, pixels, frame_w, frame_h);
                                auto found = shares.find(item.id);
                                if (found != shares.end() && abs(found->second - share) < 5) continue;
                                shares[item.id] = share;
                                printf("NEO98 HOST seamless paint t=%.3f hwnd=%x %dx%d desktop=%d%% awaits=%d title=%s\n",
                                       std::chrono::duration<double>(now - host_start).count(), item.id,
                                       item.frame.right - item.frame.left, item.frame.bottom - item.frame.top, share,
                                       User::AwaitsPaint(item.id) ? 1 : 0, item.title.c_str());
                            }
                            fflush(stdout);
                        }
                        // NEO98_SEAMLESS_PROBE=x,y: which window's picture is
                        // opaque at that point of the screen and what colour
                        // the frame has there, at every frame that changes
                        // either. A change of the foreground between two
                        // windows that overlap there must never show "none"
                        // (the Mac's desktop would shine through).
                        static const char *probe_spec = getenv("NEO98_SEAMLESS_PROBE");
                        if (probe_spec) {
                            static int px = -1, py = -1;
                            static unsigned last_owner = ~0u, last_pixel = ~0u;
                            if (px < 0 && sscanf(probe_spec, "%d,%d", &px, &py) != 2) px = py = 0;
                            if (px < frame_w && py < frame_h) {
                                unsigned owner = 0;
                                for (const auto &item : items) {
                                    if (item.minimized) continue;
                                    bool holds = false;
                                    for (const auto &r : item.rects) if (r.Holds(px, py)) { holds = true; break; }
                                    if (holds) { owner = item.id; break; }
                                }
                                const unsigned char *q = pixels + (py * frame_w + px) * 4;
                                const unsigned pixel = unsigned(q[2]) << 16 | unsigned(q[1]) << 8 | q[0];
                                if (owner != last_owner || pixel != last_pixel) {
                                    last_owner = owner; last_pixel = pixel;
                                    printf("NEO98 HOST seamless probe t=%.3f owner=%x pixel=%06x\n",
                                           std::chrono::duration<double>(now - host_start).count(), owner, pixel);
                                    fflush(stdout);
                                }
                            }
                        }
                        // Which windows the Mac shows, whenever the set changes:
                        // a log tells what covered the screen.
                        static std::vector<unsigned> shown_ids;
                        std::vector<unsigned> ids;
                        for (const auto &item : items) if (!item.through) ids.push_back(item.id);
                        if (ids != shown_ids) {
                            shown_ids = ids;
                            std::string line;
                            for (const auto &item : items) {
                                if (item.through) continue;
                                char one[400];
                                snprintf(one, sizeof one, " [%x %d,%d,%dx%d %s]", item.id, item.frame.left, item.frame.top,
                                         item.frame.right - item.frame.left, item.frame.bottom - item.frame.top,
                                         item.title.c_str());
                                line += one;
                            }
                            printf("NEO98 HOST seamless shows %zu t=%.3f:%s\n", ids.size(),
                                   std::chrono::duration<double>(now - host_start).count(), line.c_str());
                            fflush(stdout);
                        }
                    } else HostSeamless::Compose(mask, pixels, seamless_picture);
                    HostSeamless::KeepClear(window);
                } else if (overlay_on && HostSeamless::ItemWindows()) {
                    // No picture for the Mac windows this turn: none of them
                    // stays on the screen with an old one.
                    HostSeamless::CloseItems();
                }
                // NEO98_POINTER_TRACE: a frame that goes to the screen without
                // the pointer USER should have drawn on it is a blink.
                if (pointer_trace) {
                    static unsigned presented = 0, bare = 0;
                    ++presented;
                    if (User::PointerComposited() && User::PointerVisible() && !User::PointerDrawn(nullptr)) {
                        ++bare;
                        printf("NEO98 HOST POINTER missing in frame %u (%u so far)\n", presented, bare);
                        fflush(stdout);
                    }
                }
                // The video in Media Center 98's window :
                // the pixels of the key colour in its area become clear, and
                // Kodi's picture goes below the guest's picture.
                static std::vector<unsigned> keyed;
                static SDL_Texture *video_texture = nullptr;
                static int video_tw = 0, video_th = 0;
#ifdef NEO98_WITH_HOST_MEDIA
                static unsigned video_serial = 0;
                const auto video_areas = shaped ? std::vector<HostMediaService::Service::Area>{} : media.VideoAreas();
                const bool video_on = !video_areas.empty();
                if (video_on) {
                    keyed.resize(size_t(frame_w) * frame_h);
                    const unsigned *source = (const unsigned *)pixels;
                    for (size_t i = 0; i < keyed.size(); ++i) keyed[i] = source[i] | 0xff000000u;
                    for (const auto &a : video_areas) {
                        const int x0 = std::max(0, a.x), y0 = std::max(0, a.y);
                        const int x1 = std::min(frame_w, a.x + a.width), y1 = std::min(frame_h, a.y + a.height);
                        for (int y = y0; y < y1; ++y)
                            for (int x = x0; x < x1; ++x)
                                if ((keyed[size_t(y) * frame_w + x] & 0xffffffu) == HM_VIDEO_KEY) keyed[size_t(y) * frame_w + x] = 0;
                    }
                    HostMediaCapture::Latest([&](const unsigned *picture, int w, int h) {
                        if (!video_texture || w != video_tw || h != video_th) {
                            if (video_texture) SDL_DestroyTexture(video_texture);
                            video_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
                            if (video_texture) SDL_SetTextureScaleMode(video_texture, SDL_ScaleModeLinear);
                            video_tw = w; video_th = h;
                        }
                        if (video_texture) SDL_UpdateTexture(video_texture, nullptr, picture, w * 4);
                        static const char *kodi_shot = getenv("NEO98_MEDIA_KODI_SHOT");
                        static auto last_kodi_shot = std::chrono::steady_clock::time_point{};
                        if (kodi_shot && std::chrono::steady_clock::now() - last_kodi_shot > std::chrono::seconds(4))
                            if (FILE *f = fopen(kodi_shot, "wb")) {
                                last_kodi_shot = std::chrono::steady_clock::now();
                                fprintf(f, "P6\n%d %d\n255\n", w, h);
                                for (int i = 0; i < w * h; ++i) { const unsigned c = picture[i]; const unsigned char rgb[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c}; fwrite(rgb, 1, 3, f); }
                                fclose(f);
                            }
                    }, video_serial);
                }
#else
                struct VideoArea { int x, y, width, height; bool fill; };
                const std::vector<VideoArea> video_areas;
                const bool video_on = false;
#endif
                SDL_UpdateTexture(texture, nullptr, video_on ? (const void *)keyed.data()
                                  : shaped ? (const void *)seamless_picture.data() : (const void *)pixels,
                                  frame_w * 4);
                SDL_SetTextureBlendMode(texture, video_on ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, shaped ? 0 : 255);
                SDL_RenderClear(renderer);
                // Window coordinates to renderer pixels (a Retina output is
                // larger than the window's points).
                const auto layout = LayoutFor(window, renderer);
                const double sx = (double)layout.output_w / (layout.window_w ? layout.window_w : 1),
                             sy = (double)layout.output_h / (layout.window_h ? layout.window_h : 1);
                const SDL_Rect screen{(int)(layout.guest.x * sx), (int)(layout.guest.y * sy),
                                      (int)(layout.guest.w * sx), (int)(layout.guest.h * sy)};
                // The Glide driver picks its resolution by this size.
                User::SetHostOutput(screen.w, screen.h);
                int width = 0, height = 0;
                unsigned serial = 0;
                const unsigned *full = User::Overlay(width, height, serial);
                if (full && width > 0 && height > 0) {
                    if (!overlay || width != overlay_width || height != overlay_height) {
                        if (overlay) SDL_DestroyTexture(overlay);
                        overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
                        if (overlay) SDL_SetTextureScaleMode(overlay, SDL_ScaleModeLinear);
                        overlay_width = width;
                        overlay_height = height;
                        overlay_serial = serial - 1;
                    }
                    if (overlay && serial != overlay_serial) {
                        SDL_UpdateTexture(overlay, nullptr, full, width * 4);
                        overlay_serial = serial;
                    }
                }
                if (full && overlay) SDL_RenderCopy(renderer, overlay, nullptr, &screen);
                else {
                    // Whole device pixels per guest pixel stay sharp (nearest);
                    // a fitted or stretched picture is smoothed.
                    SDL_SetTextureScaleMode(texture, screen.w % frame_w == 0 && screen.h % frame_h == 0
                                                         ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
                    const SDL_Rect part{canvas_crop_x, canvas_crop_y, canvas_w, canvas_h};
                    const bool crop = canvas_crop && part.x + part.w <= frame_w && part.y + part.h <= frame_h;
                    if (crop) SDL_SetTextureScaleMode(texture, screen.w % part.w == 0 && screen.h % part.h == 0
                                                                   ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
                    for (const auto &a : video_areas) {
                        if (crop) break;
                        // The area in renderer pixels, black, and Kodi's
                        // picture in it with its own proportions.
                        // In Media Center's full screen the whole output.
                        const SDL_Rect area = a.fill ? SDL_Rect{0, 0, layout.output_w, layout.output_h}
                                                     : SDL_Rect{screen.x + (int)((long long)a.x * screen.w / frame_w),
                                                                screen.y + (int)((long long)a.y * screen.h / frame_h),
                                                                (int)((long long)a.width * screen.w / frame_w), (int)((long long)a.height * screen.h / frame_h)};
                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                        SDL_RenderFillRect(renderer, &area);
                        if (video_texture && video_tw > 0 && video_th > 0) {
                            SDL_Rect fit = area;
                            if ((long long)area.w * video_th > (long long)area.h * video_tw) fit.w = (int)((long long)area.h * video_tw / video_th);
                            else fit.h = (int)((long long)area.w * video_th / video_tw);
                            fit.x = area.x + (area.w - fit.w) / 2; fit.y = area.y + (area.h - fit.h) / 2;
                            SDL_RenderCopy(renderer, video_texture, nullptr, &fit);
                        }
                    }
                    SDL_RenderCopy(renderer, texture, crop ? &part : nullptr, &screen);
                }
                if (machine_paused && !shaped) {
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 153);
                    SDL_RenderFillRect(renderer, &screen);
                    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                    const int unit = std::max(2, screen.h / 150), cx = screen.x + screen.w / 2, cy = screen.y + screen.h / 2;
                    PauseMark(cx, cy, unit, [&](int x, int y, int w, int h, bool light) {
                        const SDL_Rect part{x, y, w, h};
                        const Uint8 shade = light ? 255 : 24;
                        SDL_SetRenderDrawColor(renderer, shade, shade, shade, 255);
                        SDL_RenderFillRect(renderer, &part);
                    });
                    // NEO98_HOST_PAUSE_PROBE=1 reads the picture back, once a
                    // pause: a corner of the guest area as the guest drew it
                    // and as the window shows it, and the left bar.
                    static const bool probe = getenv("NEO98_HOST_PAUSE_PROBE") != nullptr;
                    if (probe && !pause_drawn) {
                        unsigned corner = 0, bar = 0;
                        const SDL_Rect at_corner{screen.x + 2, screen.y + 2, 1, 1};
                        const SDL_Rect at_bar{cx - 22 * unit + 16 * unit, cy - 20 * unit + 13 * unit, 1, 1};
                        SDL_RenderReadPixels(renderer, &at_corner, SDL_PIXELFORMAT_ARGB8888, &corner, 4);
                        SDL_RenderReadPixels(renderer, &at_bar, SDL_PIXELFORMAT_ARGB8888, &bar, 4);
                        const unsigned guest = *(const unsigned *)(pixels + (size_t(2 * frame_h / std::max(1, screen.h)) * frame_w +
                                                                           size_t(2 * frame_w / std::max(1, screen.w))) * 4);
                        printf("NEO98 HOST pause picture guest=%06x shown=%06x bar=%06x\n", guest & 0xffffff, corner & 0xffffff,
                               bar & 0xffffff);
                        // And a grid of five by five points without the
                        // middle one, where the mark is: the place in the
                        // guest's picture, what the guest drew there and what
                        // the window shows. A 3D picture lies under the
                        // dimming like any other (tools/test_pause_graphics_host.py).
                        const bool cut = canvas_crop && canvas_crop_x + canvas_w <= frame_w && canvas_crop_y + canvas_h <= frame_h;
                        const SDL_Rect from = cut ? SDL_Rect{canvas_crop_x, canvas_crop_y, canvas_w, canvas_h}
                                                  : SDL_Rect{0, 0, frame_w, frame_h};
                        printf("NEO98 HOST pause grid");
                        for (int gy = 0; gy < 5; ++gy)
                            for (int gx = 0; gx < 5; ++gx) {
                                if (gx == 2 && gy == 2) continue;
                                const SDL_Rect at{screen.x + (2 * gx + 1) * screen.w / 10, screen.y + (2 * gy + 1) * screen.h / 10, 1, 1};
                                const int px = from.x + (2 * gx + 1) * from.w / 10, py = from.y + (2 * gy + 1) * from.h / 10;
                                unsigned seen = 0;
                                SDL_RenderReadPixels(renderer, &at, SDL_PIXELFORMAT_ARGB8888, &seen, 4);
                                printf(" %d,%d=%06x>%06x", px, py,
                                       *(const unsigned *)(pixels + (size_t(py) * frame_w + px) * 4) & 0xffffff, seen & 0xffffff);
                            }
                        printf("\n");
                        fflush(stdout);
                    }
                    pause_drawn = true;
                }
                // The note about a saved state, over the picture and at the
                // resolution of the screen.
                if (note && !shaped) {
                    static SDL_Texture *plate_texture = nullptr;
                    static int plate_w = 0, plate_h = 0, plate_room = 0;
                    static unsigned plate_note = 0;
                    static double plate_scale = 0;
                    const double scale = std::max(1.0, sx);
                    if (plate_note != note || plate_scale != scale || plate_room != screen.w) {
                        plate_note = note;
                        plate_scale = scale;
                        plate_room = screen.w;
                        if (plate_texture) SDL_DestroyTexture(plate_texture);
                        plate_texture = nullptr;
                        std::vector<unsigned char> plate;
                        if (HostStateWindow::RenderNote(note_title, note_detail, note_bad, scale, screen.w * 9 / 10, plate, plate_w, plate_h) &&
                            (plate_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, plate_w, plate_h))) {
                            SDL_UpdateTexture(plate_texture, nullptr, plate.data(), plate_w * 4);
                            SDL_SetTextureBlendMode(plate_texture, SDL_BLENDMODE_BLEND);
                        }
                    }
                    if (plate_texture) {
                        SDL_SetTextureAlphaMod(plate_texture, Uint8(255 * note_alpha));
                        const SDL_Rect at{screen.x + (screen.w - plate_w) / 2, screen.y + int(16 * scale), plate_w, plate_h};
                        SDL_RenderCopy(renderer, plate_texture, nullptr, &at);
                        // NEO98_HOST_STATE_PROBE=1 says once a note where it is drawn.
                        static const bool probe = getenv("NEO98_HOST_STATE_PROBE") != nullptr;
                        static unsigned said = 0;
                        if (probe && said != note) {
                            said = note;
                            unsigned inside = 0;
                            const SDL_Rect middle{at.x + plate_w / 2, at.y + 3, 1, 1};
                            SDL_RenderReadPixels(renderer, &middle, SDL_PIXELFORMAT_ARGB8888, &inside, 4);
                            printf("NEO98 HOST state note drawn %dx%d at %d,%d plate=%06x title=\"%s\"\n", plate_w, plate_h, at.x, at.y,
                                   inside & 0xffffff, note_title.c_str());
                            fflush(stdout);
                        }
                    }
                }
                // NEO98_MEDIA_SHOT=file: the composed picture with the video,
                // every few seconds while it plays in the window (tests; the
                // guest's own screenshot has no video in it).
                if (video_on) {
                    static const char *media_shot = getenv("NEO98_MEDIA_SHOT");
                    static auto last_media_shot = std::chrono::steady_clock::time_point{};
                    if (media_shot && std::chrono::steady_clock::now() - last_media_shot > std::chrono::seconds(4)) {
                        last_media_shot = std::chrono::steady_clock::now();
                        int ow = 0, oh = 0;
                        SDL_GetRendererOutputSize(renderer, &ow, &oh);
                        std::vector<unsigned char> rgb(size_t(ow) * oh * 4);
                        if (ow > 0 && oh > 0 && !SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ABGR8888, rgb.data(), ow * 4))
                            if (FILE *f = fopen(media_shot, "wb")) {
                                fprintf(f, "P6\n%d %d\n255\n", ow, oh);
                                for (size_t i = 0; i < size_t(ow) * oh; ++i) fwrite(&rgb[i * 4], 1, 3, f);
                                fclose(f);
                            }
                    }
                }
                SDL_RenderPresent(renderer);
                pacing.Frame();
            }
        }
        // The shell ended on WM_ENDSESSION of an ExitWindowsEx another
        // process runs (DXSETUP's restart): the session end is agreed, so the
        // machine goes the way the hook would have taken it.
        if (unsigned flags = 0, code = 0;
            a->Status() == X86::Application::Exited && X86::Application::SessionCommitted(flags, code)) {
            printf("NEO98 HOST exitwindows flags=%08x code=%u (shell ended first)\n", flags, code);
            fflush(stdout);
            if (!session_powers_off(flags)) {
                reboot_reason = "exitwindows";
                reboot = true;
            }
            running = false;
            continue;
        }
        if ((a->Status() == X86::Application::Exited || a->Status() == X86::Application::Failed) && game_goes_on()) {
            // The game machine runs on with what the first process started.
            static bool said = false;
            if (!said) {
                said = true;
                printf("NEO98 HOST game machine: first process %s code=%u, %u more running\n",
                       a->Status() == X86::Application::Exited ? "exited" : "failed", a->ExitCode(), X86::Application::LiveChildren());
                fflush(stdout);
            }
            if (machine_paused) SDL_Delay(10);
            else if (!a->SystemRunnable()) SDL_Delay(2);
        } else if (a->Status() == X86::Application::Exited || a->Status() == X86::Application::Failed) {
            printf("NEO98 HOST %s code=%u calls=%u error=%s\n",
                   a->Status() == X86::Application::Exited ? "exited" : "failed", a->ExitCode(), a->ApiCalls(), a->Error());
            fflush(stdout);
            char title[512];
            snprintf(title, sizeof title, "NEO98 host: %s (%s)", a->Status() == X86::Application::Exited ? "exited" : "FAILED", a->Error() ? a->Error() : "");
            SDL_SetWindowTitle(window, title);
            // Leave the last frame on screen until the window is closed. The
            // reboot control stays live: a failed run is exactly when one wants
            // to start over without leaving the window.
            // Nothing draws a Win98 pointer into the last frame any more:
            // the Mac pointer comes back over it.
            host_pointer(false);
            // Without a screen nobody can close the window, and the deadline
            // of a run counts here as well.
            if (const char *video = SDL_GetCurrentVideoDriver(); video && !strcmp(video, "dummy")) running = false;
            // A game machine ends with its game; nobody has to close a last
            // picture. After a failure the person learns where the report is.
            if (!game_machine_folder.empty()) {
                printf("NEO98 HOST game machine ends (%s)\n", a->Status() == X86::Application::Exited ? "the game has ended" : "the game failed");
                fflush(stdout);
                if (a->Status() == X86::Application::Failed)
                    HostMenu::Notice("The game exited unexpectedly",
                                     (std::string(a->Error() ? a->Error() : "") + "\n\nThe crash report is in " + crash_report_dir + ".").c_str());
                running = false;
            }
            const char *deadline = getenv("NEO98_HOST_DEADLINE");
            while (running) {
                if (deadline && *deadline &&
                    std::chrono::steady_clock::now() - host_start >= std::chrono::seconds(strtol(deadline, nullptr, 10))) {
                    printf("NEO98 HOST deadline %s s reached\n", deadline);
                    fflush(stdout);
                    break;
                }
                watch_pointer(window);
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) running = false;
                    if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) host_shortcut(ev.key);
                }
                if (quit_requested) running = false;
                if (report_requested) { report_requested = 0; request_report("signal"); }
                serve_menu();
                SDL_Delay(50);
            }
        } else if (machine_paused) {
            SDL_Delay(10);
        } else if (!a->SystemRunnable()) {
            SDL_Delay(2);
        }
    }
    if (state_job.active) {
        state_job.supplier->Release();
        state_job.active = false;
    }
    if (pacing.enabled) pacing.Report("host");
    // The Dock icons of the guest's programs go with the machine.
    HostDock::Shutdown();
    if (reboot) {
        // Reboot = report, then start over with the command line this host was
        // launched with. It re-execs instead of tearing the runtime down in
        // place: USER, GDI, the shared icon and child-process tables and the
        // Unicorn engine slots all keep process-wide state, and a teardown path
        // that is only exercised on a hang is the wrong thing to trust while
        // diagnosing one. Re-exec always gives the same clean start as the
        // first launch.
        char path[1024] = {};
        // A state that is loaded is no reason for a report.
        if (report_on_reboot && !state_reboot) write_report(*a, reboot_reason, path, sizeof path);
        // A restart through ExitWindowsEx is Windows 98's own end of the
        // session, where VMM swaps a replaced hive; a restart after
        // a hang is the reset button and does not.
        if (!strcmp(reboot_reason, "exitwindows")) X86::Application::SystemExitHives();
        Files::System().Flush();
        Registry::Flush();
        if (report_on_reboot && !state_reboot) printf("NEO98 HOST reboot report=%s\n", path);
        else printf("NEO98 HOST restart reason=%s\n", reboot_reason);
        fflush(stdout);
        X86::Application::SetCrashHook(nullptr, nullptr);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        host_pointer(false);
        SDL_Quit();
        // The command line the host was started with, plus what the menu
        // changed: scale, full screen, CD-ROM, D:.
        std::vector<char *> arguments;
        for (auto &argument : host_arguments) arguments.push_back(argument.data());
        arguments.push_back(nullptr);
        if (host_input_once) setenv("NEO98_HOST_INPUT", host_input_remaining.c_str(), 1);
        const char *program = restart_program.empty() ? real_argv[0] : restart_program.c_str();
        HostSingle::KeepAcrossExec();
        // The processes are not torn down here, their machine is: no thread
        // may carry a virtual processor into the new image.
        Arm64::ShutdownMachines();
        execv(program, arguments.data());
        fprintf(stderr, "reboot failed: cannot re-exec %s\n", program);
        return 11;
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    host_pointer(false);
    SDL_Quit();
    X86::Application::SetCrashHook(nullptr, nullptr);
    host_app = nullptr;
    X86::Application::SetDosFailureHook(nullptr, nullptr);
    HostState::ProbeReport();
    HostState::ExitTrace();
    delete a;
    X86::Application::SystemExitHives();
    Files::System().Flush();
    Registry::Flush();
    DetachAttached("quit");
    // A DOS machine that still runs keeps its thread, and the destructors of
    // the static objects would end the process with terminate and a crash
    // dialog. Everything is written; leave without them, as the
    // restart does through execv.
    fflush(stdout);
    fflush(stderr);
    Arm64::ShutdownMachines();
#if defined(__APPLE__)
    HostMetal::LogStatistics();
#endif
    _exit(0);
}
