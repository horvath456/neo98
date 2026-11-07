// SPDX-License-Identifier: GPL-2.0-or-later
// The existing host adapter inspects the runtime's host service methods.
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/usb_keys.h"
#include "host_audio.h"
#include "host_clipboard_linux.h"
#include "host_gpu.h"
#include "host_input_linux.h"
#include "host_menu_linux.h"
#include "host_mouse_buttons.h"
#include "host_scroll_linux.h"
#include "host_seamless_linux.h"
#include "host_settings_linux.h"
#include "guest_screen_scale.h"
#include "linux_session.h"
#include "host_states_linux.h"
#include "host_pause_mark.h"
#include "host_setup_linux.h"
#include "host_devices/service.h"
#include "host_hang.h"
#include "host_pacing.h"
#ifdef NEO98_WITH_HOST_MEDIA
#include "host_media/service.h"
#include "host_media/seed.h"
#include "host_wayland/compositor.h"
#include "host_wayland/video.h"
#endif
#include <algorithm>
#include <sys/resource.h>
#include <cerrno>
#include <filesystem>
#include <map>
#include <thread>
namespace
{
volatile sig_atomic_t interrupted = 0;
// SIGTERM and SIGHUP end the host in order, as a quit from the menu does and
// as the macOS host ends on them (SDL_QUIT): exit code 0. SIGINT (Ctrl+C in a
// terminal) stays an interruption with 130.
volatile sig_atomic_t quit_requested = 0;
// The guest holds the mouse, as on the Mac (tools/neo98_host.cpp, MouseHold):
// a pointer clip smaller than the screen, the system mouse of DirectInput
// taken exclusively, or a program that puts the pointer back after every
// movement (four SetCursorPos within a second, the last of them not three
// seconds ago). A pause lets such a mouse go and takes it back when it ends.
struct MouseHold
{
    std::chrono::steady_clock::time_point warps[4]{};
    unsigned next = 0;
    bool released = false;
    unsigned released_for = 0; // the foreground window at that moment
    bool released_clip = false; // a pointer clip held it
    int shown = 0;             // 0 free, 1 held, 2 let go
};
MouseHold mouse_hold;
// Where the guest's pointer stands in canvas pixels, as the last motion or
// SetCursorPos put it.
int last_mouse_x = -1, last_mouse_y = -1;
// NEO98_POINTER_TRACE=1 prints each warp of the pointer for a guest.
const bool pointer_trace = getenv("NEO98_POINTER_TRACE") && *getenv("NEO98_POINTER_TRACE") == '1';
// The mouse of the input script (hand=, press=, release=) in SDL's events.
constexpr Uint32 ScriptMouse = 0x5c51;
bool MouseRecentres()
{
    if (mouse_hold.next < 4) return false;
    const auto oldest = mouse_hold.warps[mouse_hold.next & 3], newest = mouse_hold.warps[(mouse_hold.next + 3) & 3];
    return newest - oldest < std::chrono::seconds(1) && std::chrono::steady_clock::now() - newest < std::chrono::seconds(3);
}
bool MouseHeld()
{
    if (X86::Application::DinputMouseExclusive()) return true;
    if (User::PointerClipActive())
    {
        Gdi::RECT clip{};
        int sw = 0, sh = 0, depth = 0;
        User::ScreenMode(sw, sh, depth);
        if (User::PointerClip(&clip) && (clip.right - clip.left < sw || clip.bottom - clip.top < sh)) return true;
    }
    return MouseRecentres();
}
bool MouseRelease(const char *why)
{
    if (mouse_hold.released || !MouseHeld()) return false;
    mouse_hold.released = true;
    mouse_hold.released_for = User::GetForegroundWindow();
    mouse_hold.released_clip = User::PointerClipActive();
    printf("NEO98 HOST mouse released (%s)\n", why);
    fflush(stdout);
    return true;
}
void MouseCapture(const char *why)
{
    if (!mouse_hold.released) return;
    mouse_hold.released = false;
    printf("NEO98 HOST mouse captured (%s)\n", why);
    fflush(stdout);
}
void ServeMouseHold()
{
    // The program that held the mouse is gone: nothing is left to take back.
    if (mouse_hold.released && mouse_hold.released_for && !User::IsWindow(mouse_hold.released_for) && !MouseHeld())
        MouseCapture("the window that held it is gone");
    if (mouse_hold.released && mouse_hold.released_clip && !MouseHeld()) MouseCapture("the pointer clip is gone");
    const int now = mouse_hold.released ? 2 : MouseHeld() ? 1 : 0;
    if (now == mouse_hold.shown) return;
    mouse_hold.shown = now;
    printf("NEO98 HOST mouse hold=%d released=%d\n", now ? 1 : 0, now == 2 ? 1 : 0);
    fflush(stdout);
}
volatile sig_atomic_t dump_requested = 0;
void Signal(int)
{
    interrupted = 1;
}
void QuitSignal(int)
{
    quit_requested = 1;
}
void DumpSignal(int)
{
    dump_requested = 1;
}
void DescribeProcess(X86::Application &app, unsigned slot)
{
    fprintf(stderr, "NEO98 Linux process=%u slot=%u name=%s state=%u calls=%u input=%u\n", app.ProcessId(),
            slot, app.program_name, app.Status(), app.ApiCalls(), app.input_count);
    for (unsigned i = 0; i < 16; ++i)
    {
        const auto &thread = app.threads[i];
        if (!thread.fiber)
            continue;
        const unsigned eip = app.ThreadEip(i);
        fprintf(
            stderr,
            "NEO98 Linux thread=%u block=%u wait=%08x done=%u waiting=%u pending=%08x eip=%08x runnable=%u\n",
            thread.id, thread.block, thread.wait_handle, thread.done, thread.waiting, thread.pending, eip,
            app.Runnable(i));
        if (eip >= X86::Application::GateBase && eip < X86::Application::GateBase + app.import_count * 16 &&
            !((eip - X86::Application::GateBase) & 15))
            fprintf(stderr, "NEO98 Linux gate=%s!%s\n",
                    app.imports[(eip - X86::Application::GateBase) / 16].dll,
                    app.imports[(eip - X86::Application::GateBase) / 16].name);
    }
}
struct SessionEnd
{
    bool requested = false;
    unsigned flags = 0, code = 0;
};
void EndSession(void *context, X86::Application &, unsigned flags, unsigned code)
{
    auto &end = *static_cast<SessionEnd *>(context);
    end.requested = true;
    end.flags = flags;
    end.code = code;
}
std::string GuestPath(const std::string &path, const std::map<char, std::string> &roots)
{
    char real[PATH_MAX];
    if (!realpath(path.c_str(), real))
        return {};
    std::filesystem::path absolute(real);
    for (const auto &drive : roots)
    {
        char root[PATH_MAX];
        if (!realpath(drive.second.c_str(), root))
            continue;
        auto relative = absolute.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..")
            continue;
        std::string guest(1, drive.first);
        guest += ":\\";
        guest += HostVolume::ToGuest(relative.string());
        std::replace(guest.begin(), guest.end(), '/', '\\');
        return guest;
    }
    return {};
}
} // namespace
#ifdef NEO98_WITH_HOST_MEDIA
// One frame with Kodi's video under the guest's picture :
// the key colour in the video areas becomes clear, Kodi's picture goes
// below it in the window's own pixels, fitted with its proportions. A
// `fill` area is Media Center's full screen and takes the whole window.
static void PresentVideo(SDL_Renderer *renderer, SDL_Texture *texture, const uint32_t *pixels, int width, int height,
                         const std::vector<HostMediaService::Service::Area> &areas, HostWayland::Video &video)
{
    static std::vector<uint32_t> keyed;
    static SDL_Texture *picture = nullptr;
    static int picture_w = 0, picture_h = 0;
    static unsigned picture_serial = 0;
    keyed.resize(size_t(width) * height);
    for (size_t i = 0; i < keyed.size(); ++i) keyed[i] = pixels[i] | 0xff000000u;
    for (const auto &a : areas)
        for (int y = std::max(0, a.y); y < std::min(height, a.y + a.height); ++y)
            for (int x = std::max(0, a.x); x < std::min(width, a.x + a.width); ++x)
                if ((keyed[size_t(y) * width + x] & 0xffffff) == HM_VIDEO_KEY) keyed[size_t(y) * width + x] = 0;
    SDL_UpdateTexture(texture, nullptr, keyed.data(), width * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    const bool fresh = video.Latest([&](const uint32_t *p, unsigned w, unsigned h) {
        if (!picture || int(w) != picture_w || int(h) != picture_h)
        {
            if (picture) SDL_DestroyTexture(picture);
            picture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, int(w), int(h));
            if (picture) { SDL_SetTextureScaleMode(picture, SDL_ScaleModeLinear); SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE); }
            picture_w = int(w); picture_h = int(h);
        }
        if (picture) SDL_UpdateTexture(picture, nullptr, p, int(w) * 4);
    }, picture_serial);
    if (!video.Serial()) picture_w = picture_h = 0;
    // NEO98_MEDIA_FPS=1: how many of Kodi's pictures reached the window
    // each second (tools/test_media_center_host.py).
    static const bool fps = getenv("NEO98_MEDIA_FPS") && !strcmp(getenv("NEO98_MEDIA_FPS"), "1");
    static unsigned shown = 0;
    static auto second = std::chrono::steady_clock::now();
    shown += fresh;
    if (fps && std::chrono::steady_clock::now() - second >= std::chrono::seconds(1))
    {
        if (shown) fprintf(stderr, "NEO98 HOST video fps=%u\n", shown);
        shown = 0; second = std::chrono::steady_clock::now();
    }
    // In the window's pixels: the guest's place as SDL's logical size puts
    // it, and the areas in it.
    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    SDL_RenderSetViewport(renderer, nullptr);
    const double scale = std::min(double(out_w) / width, double(out_h) / height);
    const SDL_Rect guest{int((out_w - width * scale) / 2), int((out_h - height * scale) / 2), int(width * scale), int(height * scale)};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (const auto &a : areas)
    {
        const SDL_Rect area = a.fill ? SDL_Rect{0, 0, out_w, out_h}
                                     : SDL_Rect{guest.x + int(a.x * scale), guest.y + int(a.y * scale), int(a.width * scale), int(a.height * scale)};
        if (!picture || picture_w <= 0 || picture_h <= 0) continue;
        SDL_Rect fit = area;
        if (int64_t(area.w) * picture_h > int64_t(area.h) * picture_w) fit.w = int(int64_t(area.h) * picture_w / picture_h);
        else fit.h = int(int64_t(area.w) * picture_h / picture_w);
        fit.x = area.x + (area.w - fit.w) / 2; fit.y = area.y + (area.h - fit.h) / 2;
        SDL_RenderCopy(renderer, picture, nullptr, &fit);
    }
    SDL_RenderCopy(renderer, texture, nullptr, &guest);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    // Back to the guest's size before SDL maps the next mouse event.
    SDL_RenderSetLogicalSize(renderer, width, height);
}
#endif

int main(int argc, char **argv)
{
    // The game machines and their states from the command line, as on
    // macOS; no machine runs for these.
    if (const int code = LinuxHost::GameMachineCommand(argc, argv); code >= 0) return code;
    if (const int code = LinuxHost::GameMachineStateCommand(argc, argv); code >= 0) return code;
    LinuxHost::UseSettingsMachine(argc, argv);
    if (!LinuxSetup::FirstRun(argc, argv)) return 1;
    LinuxHost::Options options;
    LinuxHost::Preferences preferences;
    if (!preferences.Load(argc, argv, options))
        return 2;
    if (!preferences.Game().empty())
        HostSettings::RedirectLog();
    if (!LinuxHost::Parse(argc, argv, options))
    {
        LinuxHost::Help(true);
        return 2;
    }
    HostState::save_path = options.state_save;
    HostState::load_path = options.state_load;
    if (options.help)
    {
        LinuxHost::Help(true);
        return 0;
    }
    if (options.open_host.compare(0, 5, "file:") == 0)
    {
        GError *error = nullptr;
        char *host = nullptr;
        char *path = g_filename_from_uri(options.open_host.c_str(), &host, &error);
        const bool local = !host || !*host || !strcasecmp(host, "localhost");
        if (!path || !local) { fprintf(stderr, "Only local file URLs are supported.\n"); g_free(path); g_free(host); if (error) g_error_free(error); return 2; }
        options.open_host = path; g_free(path); g_free(host);
    }
    if (options.open_host.find_first_of("\r\n") != std::string::npos) return 2;
    if (!options.program_action.empty()) options.open_host = HostVolume::ToGuest(options.open_host);
    if (options.program_action == "quit")
    {
        const auto drives = HostVolume::Scan(argc, argv);
        const char *root = drives.drive_c ? drives.drive_c : HostVolume::DefaultDriveC();
        long owner = 0;
        const auto lock = HostSingle::Acquire(root, owner);
        if (lock == HostSingle::Result::Running)
            return HostSingle::Hand(root, options.open_host, "quit") ? 0 : 1;
        HostSingle::Release(root);
        return 0;
    }
    LinuxGtk::persistent_settings = preferences.Enabled();
    const bool restore_seamless_after_fullscreen = options.seamless && options.fullscreen;
    if (options.fullscreen || options.desktop_width)
        options.seamless = false;
#ifndef NEO98_WITH_VULKAN
    if (getenv("NEO98_GPU") && !strcmp(getenv("NEO98_GPU"), "vulkan"))
    {
        fprintf(stderr, "This build has no Vulkan support. Build with python3 neo98.py build --gpu vulkan.\n");
        return 1;
    }
#endif
    signal(SIGINT, Signal);
    signal(SIGTERM, QuitSignal);
    signal(SIGHUP, QuitSignal);
    signal(SIGUSR1, DumpSignal);
    // Use the same desktop and seamless path on X11 and Wayland sessions.
    // Native Wayland remains an explicit, deprecated compatibility option.
    if (!getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "x11", 1);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        if (!strcmp(getenv("SDL_VIDEODRIVER"), "x11") && getenv("WAYLAND_DISPLAY"))
            fprintf(stderr, "Neo98 needs XWayland in a Wayland session. Check that XWayland is running and DISPLAY is set.\n");
        return 1;
    }
    if (!strcmp(SDL_GetCurrentVideoDriver(), "wayland"))
        fprintf(stderr, "Neo98: native Wayland output is deprecated. Use SDL_VIDEODRIVER=x11 for XWayland.\n");
    struct Sdl
    {
        ~Sdl()
        {
            SDL_Quit();
        }
    } sdl;
    SDL_Rect usable = {};
    if (options.seamless)
    {
        if (SDL_GetDisplayUsableBounds(0, &usable))
            SDL_GetDisplayBounds(0, &usable);
        X86::Application::SetHostDisplay(usable.w, usable.h, false);
        X86::Application::SetHostWorkArea(!options.seamless_taskbar);
    }
    std::vector<std::pair<std::string, std::string>> dos_failures;
    X86::Application::SetDosFailureHook(&dos_failures, [](void *context, const char *path, const char *error) {
        static_cast<std::vector<std::pair<std::string, std::string>> *>(context)->emplace_back(path, error);
    });
    struct DosFailures { ~DosFailures() { X86::Application::SetDosFailureHook(nullptr, nullptr); } } dos_failure_hook;
    HostDeviceService::Service hardware; hardware.Install();
#ifdef NEO98_WITH_HOST_MEDIA
    // Kodi's catalogue for Media Center 98, only in a
    // build with NEO98_WITH_HOST_MEDIA=1. A Kodi this host starts draws into
    // a Wayland compositor of NEO98 (tools/host_wayland), and the video
    // plays in Media Center's video area; NEO98_MEDIA_WINDOW=0 keeps the
    // old way (Kodi takes the screen). The compositor comes before the
    // provider and ends after it: the provider quits Kodi first.
    HostWayland::Compositor compositor;
    HostWayland::Video video;
    const bool media_window = !getenv("NEO98_MEDIA_WINDOW") || strcmp(getenv("NEO98_MEDIA_WINDOW"), "0");
    if (media_window)
    {
        // Kodi draws at the size and rate of the screen.
        SDL_DisplayMode mode{};
        if (SDL_GetDesktopDisplayMode(0, &mode)) mode = {0, 1920, 1080, 60, nullptr};
        std::string error;
        if (!compositor.Start("", unsigned(mode.w), unsigned(mode.h), unsigned(mode.refresh_rate ? mode.refresh_rate : 60) * 1000, error))
            fprintf(stderr, "NEO98 HOST media: no compositor for Kodi: %s\n", error.c_str());
        else if (!video.Start(compositor.RenderNode(), error))
            fprintf(stderr, "NEO98 HOST media: Kodi's picture is copied without the GPU: %s\n", error.c_str());
    }
    HostMediaService::Service media; media.Install();
    if (!compositor.Socket().empty()) media.UseCompositor(compositor.Socket(), compositor.RuntimeDir());
    // Whether the video can show in the window: Kodi is connected to the
    // compositor and the window shows the guest through the SDL renderer.
    std::atomic<bool> media_embeds{false};
    media.embeds = [&] { return media_embeds.load(); };
#endif
    static LinuxHost::Session session(argc, argv, options);
    struct SessionCleanup { LinuxHost::Session &session; ~SessionCleanup() { session.Stop(); } } session_cleanup{session};
    LinuxHost::States states(preferences.Game());
    static SessionEnd session_end;
    X86::Application::SetSessionEndHook(&session_end, EndSession);
    // Sound::Output keeps this provider by symbol across a machine load.
    static HostAudio::Device audio;
    audio.Install();
    auto *gpu_backend = HostGpu::Install();
    X86::Application::SetGpuBackend(gpu_backend);
    const auto mounted = HostVolume::Scan(argc, argv);
    if (mounted.drive_c) printf("NEO98 HOST drive C: %s\n", mounted.drive_c);
    for (unsigned i = 0; i < mounted.count; ++i) {
        const auto &drive = mounted.extra[i];
        if (!drive.directory) continue;
        printf("NEO98 HOST drive %c: %s %s\n", drive.letter,
               drive.cdrom ? "cdrom" : drive.readonly ? "readonly" : drive.below ? "layer" : "fixed", drive.directory);
        if (drive.below) printf("NEO98 HOST drive %c: below %s\n", drive.letter, drive.below);
    }
    fflush(stdout);
    DosVm::EndWithProgram(!preferences.Game().empty());
    if (!session.Start(true, [&]() { return states.Prepare(options.load_state); },
        [&](X86::Application &app, const unsigned short *clock) {
#ifdef NEO98_WITH_HOST_MEDIA
            // Media Center 98 on C: before the program starts, which may be it.
            if (preferences.Game().empty() && !HostMediaSeed::Seed())
                fprintf(stderr, "NEO98 HOST media: Media Center 98 is missing beside the host program\n");
#endif
            // The machine of a game-machine state, or of --state-load FILE
            // (tools/host_state.h), in the place of the started program.
            if (const int made = states.BeforeStart(app, clock)) return made;
            return HostState::LoadMachine(app) ? 1 : 0;
        }))
    {
        if (states.LoadRefused())
        {
            std::vector<std::string> arguments;
            for (int i = 0; i < argc; ++i)
            {
                if (!strcmp(argv[i], "--load-state") && i + 1 < argc) { ++i; continue; }
                arguments.emplace_back(argv[i]);
            }
            std::vector<char *> again;
            for (auto &argument : arguments) again.push_back(argument.data());
            again.push_back(nullptr);
            // No destructor may see the half-loaded machine; the lock of
            // drive C: goes over to the new image.
            printf("NEO98 HOST restart reason=state not loaded\n");
            session.PrepareRestart();
            fflush(stdout);
            fflush(stderr);
            execvp(again[0], again.data());
            fprintf(stderr, "Restart failed: %s\n", strerror(errno));
            _exit(23);
        }
        return session.forwarded ? 0 : 1;
    }
    // Windows starts here, not a program: MSGSRV32 plays the sound of the
    // scheme's "SystemStart" before the shell, as on macOS. A program
    // started on its own, and a machine out of a saved state, has none.
    if (!states.Rebuilt() && !HostState::loaded && options.command.empty() && !strcasecmp(options.exe.c_str(), "C:\\WINDOWS\\EXPLORER.EXE"))
        session.app->PlayStartupSound();
    std::string launch_directory = preferences.StartDirectory();
    if (launch_directory.empty() && HostSystemFont::GuestPath(options.exe.c_str()))
    {
        const auto slash = options.exe.rfind('\\');
        if (slash != std::string::npos)
            launch_directory = options.exe.substr(0, slash == 2 ? 3 : slash);
    }
    if (!states.Rebuilt() && !HostState::loaded && !launch_directory.empty() && session.app->files.SetDirectory(launch_directory.c_str()))
        fprintf(stderr, "Could not open guest working directory: %s\n", launch_directory.c_str());
    int width, height;
    User::PixelsSize(width, height);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Neo98 Linux x64 | Ctrl+Shift+M: Machine", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, width * options.scale, height * options.scale,
                         SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI),
        SDL_DestroyWindow);
    if (!window)
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *made = SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED);
    if (!made)
        made = SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_SOFTWARE);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(made, SDL_DestroyRenderer);
    if (!renderer)
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    if (!LinuxGtk::Create(window.get()))
    {
        fprintf(stderr, "Could not initialize GTK.\n");
        return 1;
    }
    struct GtkEnd
    {
        ~GtkEnd()
        {
            LinuxGtk::Close();
        }
    } gtk_end;
#ifdef NEO98_WITH_VULKAN
    bool vulkan_desktop = false;
    if (gpu_backend &&
        !(getenv("NEO98_VULKAN_PRESENT") && !strcmp(getenv("NEO98_VULKAN_PRESENT"), "copy")))
    {
        SDL_SysWMinfo wm; SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(window.get(), &wm) && wm.subsystem == SDL_SYSWM_X11)
            vulkan_desktop = HostVulkan::AttachX11(wm.info.x11.display, wm.info.x11.window);
    }
    bool scanout_enabled = vulkan_desktop;
    if (vulkan_desktop)
        User::SetHostScanoutProvider(&scanout_enabled, [](void *context, const unsigned char *bits, bool read) {
            if (read) { HostVulkan::ReadScanout(bits); return true; }
            return *static_cast<bool *>(context) && HostVulkan::CanPresent(bits);
        });
    struct VulkanEnd { ~VulkanEnd() { User::SetHostScanoutProvider(nullptr, nullptr); HostVulkan::Detach(); } } vulkan_end;
#endif
#ifdef NEO98_WITH_HOST_MEDIA
    media.show_screen = [](unsigned to) { LinuxGtk::Yield(to == HM_SCREEN_KODI); };
#endif
    LinuxHost::Seamless seamless;
    if (options.seamless && !seamless.Supported(window.get()))
    {
        fprintf(stderr, "The active window driver does not support seamless windows.\n");
        return 1;
    }
    struct CursorMove
    {
        SDL_Renderer *renderer;
        LinuxHost::Seamless *seamless;
        bool *enabled;
    };
    bool seamless_enabled = options.seamless;
    User::SetHostWindowSurfaces(seamless_enabled && LinuxGtk::wayland_canvas);
    CursorMove cursor_move{renderer.get(), &seamless, &seamless_enabled};
    X86::Application::SetHostCursorProvider(&cursor_move, [](void *context, int x, int y) {
        auto &move = *static_cast<CursorMove *>(context);
        if (*move.enabled)
        {
            move.seamless->Warp(x, y);
            return;
        }
        auto *renderer = move.renderer;
        int guest_width = 0, guest_height = 0, depth = 0, canvas_width = 0, canvas_height = 0;
        User::ScreenMode(guest_width, guest_height, depth);
        User::PixelsSize(canvas_width, canvas_height);
        GuestScreenScale::GuestToCanvas(x, y, guest_width, guest_height, canvas_width, canvas_height);
        // As on the Mac: each SetCursorPos counts for the hold; a mouse that
        // was let go is the hand's alone, and a warp to where the pointer
        // stands moves nothing.
        mouse_hold.warps[mouse_hold.next++ & 3] = std::chrono::steady_clock::now();
        if (mouse_hold.released) return;
        if (last_mouse_x == x && last_mouse_y == y) return;
        last_mouse_x = x;
        last_mouse_y = y;
        int window_x = 0, window_y = 0;
        SDL_RenderLogicalToWindow(renderer, float(x), float(y), &window_x, &window_y);
        if (pointer_trace)
        {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(SDL_RenderGetWindow(renderer), &ww, &wh);
            const bool centre = window_x >= ww / 2 && window_x <= (ww + 1) / 2 && window_y >= wh / 2 && window_y <= (wh + 1) / 2;
            printf("NEO98 HOST POINTER warp window=%d,%d size=%dx%d centre=%d\n", window_x, window_y, ww, wh, centre ? 1 : 0);
            fflush(stdout);
        }
        SDL_WarpMouseInWindow(SDL_RenderGetWindow(renderer), window_x, window_y);
    });
    struct CursorProvider
    {
        ~CursorProvider()
        {
            X86::Application::SetHostCursorProvider(nullptr, nullptr);
        }
    } cursor_provider;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(renderer.get(), width, height);
    std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture(nullptr, SDL_DestroyTexture);
    static LinuxHost::Clipboard clipboard;
    HostMenu::Install();
    LinuxHost::Script input;
    // A person sits in front of this host: an unhandled exception shows the
    // error box of Win98 and ends only its process.
    X86::Application::SetFaultBox(true);
    // Every program the box ended leaves a report, as on macOS.
    X86::Application::SetCrashHook(&session, [](void *context, X86::Application &process, const char *reason) {
        static_cast<LinuxHost::Session *>(context)->Report(reason, process);
    });
    LinuxHost::native_input = [&](const std::string &action,const std::string &value) { return seamless.Input(action,value); };
    User::ShowPointer(true);
    const bool german = !(getenv("NEO98_KEYMAP") && !strcmp(getenv("NEO98_KEYMAP"), "US"));
    HostMenu::State menu;
    LinuxScroll::Wheel host_wheel;
    LinuxHost::scroll_input = [&](bool trackpad, double up, double right, bool first, bool momentum) {
        if (trackpad)
        {
            const HostScroll out = host_wheel.wheel.Points(up, right, double(menu.scale), first, momentum);
            printf("NEO98 HOST input scroll up=%g right=%g units=%d,%d flags=%u pointer=%d\n", up, right, out.y, out.x,
                   out.flags, User::WheelToPointer() ? 1 : 0);
            fflush(stdout);
            if (out.y || out.x) session.app->MouseScroll(out.y, out.x, out.flags);
            return;
        }
        const auto step = host_wheel.Convert(up, right, User::CaptureOperation(0), User::GetForegroundWindow(),
                                             double(menu.scale), SDL_GetTicks64());
        if (step.kind == HostWheelStep::Notches) session.app->MouseWheel(step.steps);
        else if (step.kind == HostWheelStep::Scroll) session.app->MouseScroll(step.out.y, step.out.x, step.out.flags);
    };
    if (!options.open_host.empty() && (preferences.Game().empty() || options.program_action.empty()))
        HostMenu::Perform(options.program_action.empty() ? "open" : "open-normal", options.open_host.c_str());
    menu.scale = options.scale;
    menu.fullscreen = options.fullscreen;
#ifdef NEO98_WITH_HOST_MEDIA
    // Media Center 98's full screen: the window too, unless it already is;
    // back afterwards. NEO98_MEDIA_HOST_FULLSCREEN=0 (tests) leaves it.
    bool media_full_before = false;
    media.video_fullscreen = [&](bool on) {
        if (getenv("NEO98_MEDIA_HOST_FULLSCREEN") && !strcmp(getenv("NEO98_MEDIA_HOST_FULLSCREEN"), "0")) return;
        if (on) { media_full_before = menu.fullscreen; if (!menu.fullscreen) LinuxGtk::Fullscreen(true); }
        else if (!media_full_before) LinuxGtk::Fullscreen(false);
    };
#endif
    menu.online = !options.offline;
    menu.wifi = options.wifi;
    hardware.SetWifi(menu.wifi);
    // Maschine > Mikrofon in Windows.
    menu.microphone = options.microphone;
    Sound::Microphone::SetEnabled(menu.microphone);
    menu.net_listen = session.network.ListenPolicy();
    printf("NEO98 network listen=%s\n", menu.net_listen.c_str());
    menu.seamless = options.seamless;
    menu.seamless_taskbar = options.seamless_taskbar;
    menu.seamless_windows = options.seamless_windows;
    menu.seamless_icons = options.seamless_icons;
    menu.seamless_dock = options.seamless_dock;
    LinuxDock::machine = HostSettings::DriveC(preferences.Game().empty() ? HostVolume::DefaultDriveC() : preferences.SourceMachine());
    for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "--drive-c")) LinuxDock::machine = argv[i + 1];
    LinuxDock::game = preferences.Game();
    menu.pause_unfocused = options.pause_unfocused;
    menu.wheel_focus = options.wheel_focus;
    // NEO98_WHEEL_FOCUS=1 is --wheel-focus for a test, as on the Mac.
    if (const char *wanted = getenv("NEO98_WHEEL_FOCUS"); wanted && *wanted == '1') menu.wheel_focus = true;
    LinuxGtk::Check("open-ask", preferences.AskEnabled());
    for (unsigned port = 0; port < 4; ++port)
        if (const char *value = getenv(("NEO98_COM" + std::to_string(port + 1)).c_str()))
            menu.com[port] = value;
    User::SetWheelToPointer(!menu.wheel_focus);
    if (menu.seamless)
    {
        LinuxGtk::SetSeamless(true);
        User::ShowPointer(false);
        if (!menu.seamless_dock)
            LinuxGtk::Show();
    }
    if (options.fullscreen)
        LinuxGtk::Fullscreen(true);
    if (!preferences.Game().empty())
    {
        gtk_window_set_title(GTK_WINDOW(LinuxGtk::shell), preferences.GameName().c_str());
    }
    std::unique_ptr<HostCdImage::Image> cd;
    std::vector<std::unique_ptr<HostVolume::Drive>> clipboard_drives;
    std::string added_drive_d;
    std::map<char, std::string> changed_cdroms;
    std::map<char, std::string> roots{{'C', mounted.drive_c ? mounted.drive_c : HostVolume::DefaultDriveC()}};
    for (unsigned i = 0; i < mounted.count; ++i)
        if (mounted.extra[i].directory)
        {
            const auto &drive = mounted.extra[i];
            roots[drive.letter] = drive.directory;
            if (mounted.extra[i].letter == 'D' && !mounted.extra[i].cdrom)
                menu.drive_d = mounted.extra[i].directory;
            if (mounted.extra[i].cdrom)
            {
                menu.cdrom = mounted.extra[i].directory;
                menu.cdrom_letter = mounted.extra[i].letter;
            }
        }
    HostMenu::Update(menu);
    hardware.media_changed=[&](char letter,const std::string &path) {
        if(path.empty())roots.erase(letter);else roots[letter]=path;
        session.RefreshDos(&roots);
    };
    session.RefreshDos(&roots);
    auto map_host_file = [&](const std::string &path, std::string &guest, bool writable) {
        guest = GuestPath(path, roots);
        if (guest.empty())
            for (char letter = 'C'; letter <= 'Z'; ++letter)
                if (auto *layer = HostVolume::Layer(letter); layer && Volume::Mounted(letter) == layer)
                {
                    guest = GuestPath(path, {{letter, layer->BelowRoot()}});
                    if (!guest.empty())
                        break;
                }
        if (!guest.empty())
            return true;
        char real[PATH_MAX];
        if (!realpath(path.c_str(), real))
            return false;
        const auto parent = std::filesystem::path(real).parent_path().string();
        for (char letter = 'F'; letter <= 'Z'; ++letter)
            if (!Volume::Mounted(letter))
            {
                auto drive = std::make_unique<HostVolume::Drive>();
                if (!drive->Open(parent.c_str(), writable, "HOST"))
                    return false;
                Volume::Mount(letter, drive.get(), Volume::Fixed, writable);
                roots[letter] = parent;
                clipboard_drives.push_back(std::move(drive));
                session.app->ChangeMedium(letter);
                session.RefreshDos(&roots);
                guest = GuestPath(path, roots);
                return !guest.empty();
            }
        return false;
    };
    clipboard.Paths(
        [&](const std::string &path, std::string &guest) { return map_host_file(path, guest, false); },
        [&](const std::string &guest, std::string &host) {
            if (guest.size() < 3 || guest[1] != ':')
                return false;
            const char letter = char(HostVolume::Fold(guest[0]));
            if (auto *layer = HostVolume::Layer(letter); layer && Volume::Mounted(letter) == layer)
            {
                host = layer->HostFile(guest.c_str());
                char real[PATH_MAX];
                if (host.empty() || !realpath(host.c_str(), real))
                    return false;
                for (const auto &directory : {layer->Root(), layer->BelowRoot()})
                {
                    char base[PATH_MAX];
                    if (!realpath(directory.c_str(), base))
                        continue;
                    const auto relative = std::filesystem::path(real).lexically_relative(base);
                    if (!relative.empty() && *relative.begin() != "..")
                    {
                        host = real;
                        return true;
                    }
                }
                return false;
            }
            auto root = roots.find(letter);
            if (root == roots.end())
                return false;
            host = root->second;
            std::string prefix = guest.substr(0, 2);
            for (size_t at = 3; at < guest.size();)
            {
                size_t end = guest.find('\\', at);
                if (end == std::string::npos)
                    end = guest.size();
                std::string part = guest.substr(at, end - at);
                at = end + 1;
                if (part.empty())
                    continue;
                prefix += "\\" + part;
                Volume::Entry entry;
                if ((Volume::Mounted(letter) && Volume::Mounted(letter)->Find(prefix.c_str(), entry)) ||
                    (letter == 'C' && HostVolume::TreeDrive() &&
                     HostVolume::TreeDrive()->Find(prefix.c_str(), entry)))
                    part = entry.name;
                if (part == ".." || part == ".")
                    return false;
                host += "/" + HostVolume::ToHost(part);
            }
            char real[PATH_MAX], base[PATH_MAX];
            if (!realpath(host.c_str(), real) || !realpath(root->second.c_str(), base))
                return false;
            auto relative = std::filesystem::path(real).lexically_relative(base);
            if (relative.empty() || *relative.begin() == "..")
                return false;
            host = real;
            return true;
        });
    struct StopSession
    {
        LinuxHost::Session &session;
        ~StopSession()
        {
            session.Stop();
        }
    } stop_session{session};
    bool running = true, reboot = false, expired = false;
    std::string state_restart;
    unsigned buttons = 0;
    int texture_width = 0, texture_height = 0;
    if (options.desktop_width)
    {
        int offered_width = 0, offered_height = 0;
        X86::Application::HostDisplay(offered_width, offered_height);
        printf("NEO98 HOST desktop %ux%u %s\n", options.desktop_width, options.desktop_height,
               offered_width ? "offered" : "refused");
        fflush(stdout);
    }
    // The window for a canvas, as on the Mac (tools/neo98_host.cpp,
    // WindowSizeFor): the canvas at the chosen scale, a smaller scale where
    // that does not fit the usable area of the display, and else as large as
    // the area allows with the proportions of the canvas. The dummy driver of
    // the tests has a 1024x768 display; NEO98_HOST_SCREEN=WxH names another.
    auto frame_window = [&](int canvas_width, int canvas_height, bool view_changed) {
        if (menu.fullscreen || seamless_enabled)
            return;
        SDL_Rect usable{0, 0, 0, 0};
        const int display = SDL_GetWindowDisplayIndex(window.get());
        if (SDL_GetDisplayUsableBounds(display < 0 ? 0 : display, &usable))
            usable = {0, 0, 0, 0};
        if (const char *screen = getenv("NEO98_HOST_SCREEN"); screen && *screen)
            sscanf(screen, "%dx%d", &usable.w, &usable.h);
        int window_width = 0, window_height = 0;
        GuestScreenScale::WindowForCanvas(canvas_width, canvas_height, int(menu.scale), usable.w, usable.h,
                                          window_width, window_height);
        LinuxGtk::Resize(window_width, window_height);
        if (!LinuxGtk::embedded)
            SDL_GetWindowSize(window.get(), &window_width, &window_height);
        printf("NEO98 HOST window framed %dx%d for %dx%d resizable=0\n", window_width, window_height,
               canvas_width, canvas_height);
        if (view_changed)
        {
            const double fit = std::min(double(window_width) / canvas_width, double(window_height) / canvas_height);
            const int guest_width = int(canvas_width * fit), guest_height = int(canvas_height * fit);
            printf("NEO98 HOST view scale=%u fullscreen=0 window=%dx%d guest=%d,%d,%dx%d\n", unsigned(menu.scale),
                   window_width, window_height, (window_width - guest_width) / 2, (window_height - guest_height) / 2,
                   guest_width, guest_height);
        }
        fflush(stdout);
    };
    auto start = std::chrono::steady_clock::now();
    auto last_present = start - std::chrono::milliseconds(16);
    bool texture_dirty = true, display_dirty = true;
    std::string host_shot_path;
    LinuxHost::host_shot = [&](const std::string &path) { host_shot_path = path; display_dirty = true; };
    const unsigned char *last_console = nullptr;
    unsigned long long last_console_hash = 0, last_frame = User::FrameSerial();
    unsigned long long uploads = 0, presentations = 0;
    unsigned long long gpu_presentations = 0;
    const bool frame_stats = getenv("NEO98_FRAME_STATS") != nullptr;
    const bool uncapped = getenv("NEO98_UNCAPPED") != nullptr;
    auto frame_sample = start;
    unsigned frame_presents = session.app->HalPresents();
    unsigned long long frame_displayed = 0;
    double frame_pump_ms = 0, frame_render_ms = 0, frame_menu_ms = 0;
    auto next_handoff = start;
    bool auto_paused = false;
    bool resize_after_frame = false;
    bool fullscreen_from_seamless = restore_seamless_after_fullscreen;
    bool machine_paused = false;
    // The machine stands after the load of a game machine's state until the
    // person goes on (states.stand, state_go_on).
    bool standing = false;
    // Whether the picture of the current pause was drawn (its probe line).
    bool pause_drawn = false;
    // The focus as the script step focus= gives it; a window without a
    // screen (SDL's dummy driver) has it until a step takes it away.
    int focus_script = -1;
    if (const char *video = SDL_GetCurrentVideoDriver(); video && !strcmp(video, "dummy")) focus_script = 1;
    LinuxHost::focus_input = [&](bool value) { focus_script = value ? 1 : 0; };
    LinuxHost::describe_menu = [&] {
        if (LinuxGtk::menu_bar) LinuxGtk::DescribeItems(LinuxGtk::menu_bar, "");
        else {
            // The dummy driver has no native menu, but its host menu model
            // and shortcuts are the same as those of the windowed host.
            printf("NEO98 HOST menu-item Machine > Pause [pause] %s%s\n",
                   LinuxGtk::ItemKeys("pause"), menu.paused ? " (enabled)" : "");
            fflush(stdout);
        }
    };
    // The keys of the window the guest holds down, by SDL scancode: what the
    // end of a pause releases when the hand let them go meanwhile.
    struct HeldKey { unsigned vk = 0, scan = 0; bool extended = false; };
    std::map<int, HeldKey> held_keys;
    // pause_took_mouse: the pause let go a mouse the guest held, and gives it
    // back when it ends (as on the Mac). `why` goes into the line the macOS
    // host writes as well; the tests of the saved states read it.
    bool pause_took_mouse = false;
    auto pause_started = std::chrono::steady_clock::now();
    auto pause_machine = [&](bool value, const char *why = "the host") {
        if (machine_paused == value) return;
        const unsigned tick_standing = value ? 0 : X86::Application::TickCount();
        if (value)
        {
            pause_took_mouse = MouseRelease("pause");
            pause_started = std::chrono::steady_clock::now();
        }
        machine_paused = value;
        pause_drawn = false;
        texture_dirty = display_dirty = true;
        X86::Application::PauseMachine(value);
        HostDosVm::Pause(value);
        if (!value)
        {
            standing = false;
            // A key that went up during the pause goes up for the guest
            // now; one that went down during it is not pressed afterwards.
            int count = 0;
            const Uint8 *down = SDL_GetKeyboardState(&count);
            unsigned released = 0;
            for (auto at = held_keys.begin(); at != held_keys.end();)
            {
                if (at->first < count && down[at->first]) { ++at; continue; }
                session.app->RawKey(at->second.vk, 0, false, at->second.scan, at->second.extended);
                at = held_keys.erase(at);
                ++released;
            }
            printf("NEO98 HOST pause keys settled released=%u\n", released);
            session.app->KeyboardModifiers(0);
            session.app->MouseButtons(session.app->mouse_x, session.app->mouse_y, 0);
            buttons = 0;
            // The warps of a program that recentres are as old as before the
            // pause, so it still counts as holding. The mouse the pause let
            // go is the guest's again, and the pointer of the window returns
            // to where the guest's stands: no jump, and no distance for a
            // program that reads movements.
            for (auto &at : mouse_hold.warps)
                if (at.time_since_epoch().count()) at += std::chrono::steady_clock::now() - pause_started;
            if (pause_took_mouse && mouse_hold.released && last_mouse_x >= 0 && last_mouse_y >= 0)
            {
                MouseCapture("the pause is over");
                int window_x = 0, window_y = 0;
                SDL_RenderLogicalToWindow(renderer.get(), float(last_mouse_x), float(last_mouse_y), &window_x, &window_y);
                SDL_WarpMouseInWindow(window.get(), window_x, window_y);
                printf("NEO98 HOST pause pointer back canvas=%d,%d window=%d,%d\n", last_mouse_x, last_mouse_y,
                       window_x, window_y);
            }
            pause_took_mouse = false;
        }
        unsigned dos_machines = 0;
        const unsigned dos_sound = HostDosVm::AudioQueued(&dos_machines) + X86::Application::DosVmSoundQueued();
        printf("NEO98 HOST pause %s (%s) stood=%.3f s tick=%u fast-gates=%llu dos-machines=%u dos-sound=%u\n",
               value ? "on" : "off", why, X86::Application::PausedMicroseconds() / 1e6,
               value ? X86::Application::TickCount() : tick_standing, session.app->fast_gate_calls, dos_machines, dos_sound);
        fflush(stdout);
    };
    // The steps of the input script the macOS host takes through its own
    // mouse (tools/neo98_host.cpp; pause= is a machine step, below): hand=dx,dy moves
    // the pointer of the window by that much and sends the movement as a
    // real mouse does; press= and release= are the left button at a point of
    // the window.
    // A movement of the mouse: the position in canvas pixels and the
    // distance in points of the window (deliver_motion, below).
    std::function<void(int, int, int, int)> deliver_motion;
    LinuxHost::host_step = [&](const std::string &action, int x, int y) {
        if (machine_paused && (action == "key" || action == "type" || action == "ctrl" ||
                               action == "keydown" || action == "keyup" || action == "click" ||
                               action == "move" || action == "wheel" || action == "scroll")) return true;
        if (action == "hand")
        {
            // The distance reaches the guest as it is: an event through SDL
            // would carry it rounded to canvas pixels. The pointer of the
            // window follows (its event has no distance).
            int px = 0, py = 0;
            SDL_GetMouseState(&px, &py);
            printf("NEO98 HOST input hand %d,%d -> %d,%d\n", x, y, px + x, py + y);
            fflush(stdout);
            if (!menu.paused && !auto_paused && !machine_paused && !mouse_hold.released && !seamless_enabled)
            {
                float logical_x = 0, logical_y = 0;
                SDL_RenderWindowToLogical(renderer.get(), px + x, py + y, &logical_x, &logical_y);
                deliver_motion(int(logical_x), int(logical_y), x, y);
            }
            SDL_WarpMouseInWindow(window.get(), px + x, py + y);
            return true;
        }
        if (action == "windows")
        {
            // windows=1: the work area, the windows of the seamless view
            // (which this host shows in X11 and lists none of here), and the
            // guest windows of the Window menu, as the macOS host prints them.
            if (Gdi::RECT work{}; User::WorkArea(&work))
                printf("NEO98 HOST seamless work=%d,%d,%d,%d\n", work.left, work.top, work.right, work.bottom);
            printf("NEO98 HOST seamless items 0 mode=off native=0\n");
            for (const auto &entry : menu.guest_windows)
                printf("NEO98 HOST menu window id=%x title=%s\n", entry.first, entry.second.c_str());
            fflush(stdout);
            return true;
        }
        if (action == "press" || action == "release")
        {
            SDL_Event button{};
            button.type = action == "press" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            button.button.windowID = SDL_GetWindowID(window.get());
            button.button.which = ScriptMouse;
            button.button.button = SDL_BUTTON_LEFT;
            button.button.state = action == "press" ? SDL_PRESSED : SDL_RELEASED;
            button.button.x = x;
            button.button.y = y;
            SDL_PushEvent(&button);
            return true;
        }
        return false;
    };
    struct HostStepEnd { ~HostStepEnd() { LinuxHost::host_step = nullptr; } } host_step_end;
    states.Attach(*session.app, [&](bool value, const char *why) {
        pause_machine(value || menu.paused || auto_paused || states.ListOpen(), why);
    }, [&](const std::string &id) { state_restart = id; reboot = true; running = false; });
    states.stand = [&] {
        standing = true;
        menu.paused = true;
        pause_machine(true, "a state was loaded");
    };
    // After a load the machine stands until the person goes on: any key or a
    // click, or Pause in the menu.
    auto state_go_on = [&](const char *why) {
        if (!standing || !machine_paused || states.ListOpen()) return false;
        menu.paused = false;
        pause_machine(false, why);
        return true;
    };
    // A machine out of --state-load stands paused until a step or the menu
    // lets it go on, as on macOS (tools/host_state.h).
    if (HostState::loaded)
    {
        standing = true;
        menu.paused = true;
        pause_machine(true, "state loaded");
    }
    LinuxHost::hold_steps = [&] { return !running || states.NeedsRun(); };
    LinuxHost::machine_step = [&](const std::string &action, const std::string &value) {
        if (action == "pause")
        {
            // pause=1 stops the whole machine as the menu does, pause=0 lets
            // it go on.
            menu.paused = atoi(value.c_str()) != 0;
            pause_machine(menu.paused || auto_paused, "script");
        }
        else if (action == "wstate")
            HostState::Step(*session.app, value, [&](bool on, const char *why) {
                menu.paused = on;
                pause_machine(on || auto_paused, why);
            }, machine_paused, [&] { session.app->Pump(8); });
        else if (action == "state")
        {
            // state=save[:NAME], state=quick, state=load:ID,
            // state=loadname:NAME, state=last: what the items of Machine >
            // states do in a game machine.
            const auto colon = value.find(':');
            const std::string what = value.substr(0, colon), argument = colon == std::string::npos ? "" : value.substr(colon + 1);
            if (what == "save") states.Save(argument, {}, "manual");
            else if (what == "quick") states.Save("");
            else if (what == "load") states.Load(argument);
            else if (what == "loadname") states.Load(states.IdByName(argument));
            else if (what == "last") states.Load("");
        }
    };
    auto show_seamless = [&](bool value) {
        seamless_enabled = value;
        User::SetHostWindowSurfaces(value && LinuxGtk::wayland_canvas);
        LinuxGtk::SetSeamless(value);
        if (value && !menu.seamless_dock)
            LinuxGtk::Show();
        User::ShowPointer(!value);
        if (!value)
        {
            seamless.Close();
            display_dirty = true;
        }
    };
    auto set_seamless = [&](bool value) {
        menu.seamless = value;
        const auto area = seamless.Area(window.get());
        X86::Application::SetHostDisplay(value ? area.w : 0, value ? area.h : 0, true);
        X86::Application::SetHostWorkArea(value && !menu.seamless_taskbar);
        show_seamless(value);
        fprintf(stderr, "NEO98 seamless=%d\n", value);
    };
    auto mouse_point = [&](unsigned window_id, int &x, int &y) {
        if (seamless_enabled)
        {
            unsigned guest = 0;
            seamless.Translate(window_id, x, y, guest);
            return;
        }
        int guest_width = 0, guest_height = 0, depth = 0;
        User::ScreenMode(guest_width, guest_height, depth);
        GuestScreenScale::CanvasToGuest(x, y, guest_width, guest_height, width, height);
    };
    // NEO98_HOST_MENU="seconds:item[=argument];..." runs items of the menu
    // as the macOS host takes them: each step waits its seconds after the
    // previous one, and the steps not yet taken stay in the environment for
    // the execv of a restart. A step without seconds runs at once.
    auto menu_previous = start;
    const auto menu_script = [&] {
        const char *script = getenv("NEO98_HOST_MENU");
        if (!script || !*script) return;
        const std::string text = script;
        const auto end = text.find(';');
        const std::string step = text.substr(0, end), rest = end == std::string::npos ? "" : text.substr(end + 1);
        const auto colon = step.find(':');
        const long seconds = colon == std::string::npos ? 0 : strtol(step.c_str(), nullptr, 10);
        if (std::chrono::steady_clock::now() - menu_previous < std::chrono::seconds(seconds)) return;
        menu_previous = std::chrono::steady_clock::now();
        setenv("NEO98_HOST_MENU", rest.c_str(), 1);
        const std::string command = colon == std::string::npos ? step : step.substr(colon + 1);
        const auto split = command.find('=');
        HostMenu::Perform(command.substr(0, split).c_str(),
                          split == std::string::npos ? nullptr : command.substr(split + 1).c_str());
    };
    // NEO98_HOST_DEADLINE=seconds ends the host after that time whatever the
    // guest or a script does.
    const char *deadline = getenv("NEO98_HOST_DEADLINE");
    const char *stats = getenv("NEO98_HOST_STATS");
    auto next_stats = start + std::chrono::seconds(1);
    auto next_state_menu = start;
    // Hang observation, as on macOS: the guest has had input it did not
    // answer, or a runnable thread, and made no API call for three seconds.
    unsigned last_calls = HostPacing::TotalCalls(*session.app);
    auto last_call_time = start, last_input_time = start, waiting_since = start;
    // As on the Mac: the hand's distance goes with the position, in
    // mickeys, whatever the display mode and the size of the window. A
    // program that recentres, or DirectInput's exclusive mouse, receives that
    // distance from where the guest's pointer stands; SDL's own position may
    // predate the guest's last SetCursorPos.
    deliver_motion = [&](int canvas_x, int canvas_y, int distance_x, int distance_y) {
        const Gdi::POINT motion{distance_x, distance_y};
        int guest_width = 0, guest_height = 0, depth = 0;
        User::ScreenMode(guest_width, guest_height, depth);
        int gx = canvas_x, gy = canvas_y, mx = 0, my = 0;
        if (MouseRecentres() || X86::Application::DinputMouseExclusive())
        {
            Gdi::POINT at{};
            User::CursorPosition(&at);
            mx = at.x + motion.x;
            my = at.y + motion.y;
            User::ConfinePointer(mx, my);
            gx = mx;
            gy = my;
            GuestScreenScale::GuestToCanvas(gx, gy, guest_width, guest_height, width, height);
        }
        else
        {
            mx = gx;
            my = gy;
            GuestScreenScale::CanvasToGuest(mx, my, guest_width, guest_height, width, height);
        }
        const bool changed = gx != last_mouse_x || gy != last_mouse_y || motion.x || motion.y;
        last_mouse_x = gx;
        last_mouse_y = gy;
        session.app->MouseButtons(mx, my, buttons, &motion);
        if (changed) last_input_time = std::chrono::steady_clock::now();
    };
    bool was_waiting = false, hang_now = false;
    unsigned hang_seconds_shown = 0;
    const bool headless = SDL_GetCurrentVideoDriver() && !strcmp(SDL_GetCurrentVideoDriver(), "dummy");
    while (running && !interrupted && !quit_requested && !session.Finished())
    {
        if (deadline && *deadline &&
            std::chrono::steady_clock::now() - start >= std::chrono::seconds(strtol(deadline, nullptr, 10)))
        {
            printf("NEO98 HOST deadline %s s reached\n", deadline);
            fflush(stdout);
            break;
        }
        // Without a screen nobody can close the window: as on the Mac, the
        // host ends when its first program has, whatever that started (the
        // dummy driver of the tests). A game machine runs on with it.
        if (headless && preferences.Game().empty() &&
            (session.app->Status() == X86::Application::Exited || session.app->Status() == X86::Application::Failed))
            break;
        menu_script();
        ServeMouseHold();
        // NEO98_HOST_STATS=1 writes a line a second, as the macOS host does:
        // the pictures presented through DirectDraw and the frames the mixer
        // rendered, all and those that were not silence.
        if (stats && *stats && std::chrono::steady_clock::now() >= next_stats)
        {
            next_stats += std::chrono::seconds(1);
            rusage used{}; getrusage(RUSAGE_SELF, &used);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            const long long cpu = (used.ru_utime.tv_sec + used.ru_stime.tv_sec) * 100ll + (used.ru_utime.tv_usec + used.ru_stime.tv_usec) / 10000;
            printf("NEO98 HOST stats t=%lld.%02lld presents=%u sound=%llu audible=%llu cpu=%lld.%02lld\n",
                   (long long)elapsed / 1000, ((long long)elapsed % 1000) / 10, session.app->HalPresents(),
                   Sound::Shared().RenderedFrames(), Sound::Shared().AudibleFrames(), cpu / 100, cpu % 100);
            fflush(stdout);
        }
        hardware.Pump(*session.app,preferences.Game().empty());
#ifdef NEO98_WITH_HOST_MEDIA
        compositor.Dispatch();
        media_embeds = !compositor.Socket().empty() && compositor.Clients() > 0 && !LinuxGtk::wayland_canvas && !seamless_enabled;
        media.Pump();
#endif
        if (dump_requested)
        {
            dump_requested = 0;
            DescribeProcess(*session.app, 0);
            for (unsigned i = 0; i < X86::Application::ChildCapacity; ++i)
            {
                bool done = false;
                if (auto *child = session.app->ChildApplication(i, done); child && !done)
                    DescribeProcess(*child, i + 1);
            }
        }
        if (std::chrono::steady_clock::now() >= next_handoff)
        {
            std::vector<std::string> requests;
            HostSingle::Take(requests);
            for (const auto &request : requests)
            {
                if (seamless_enabled)
                {
                    if (!seamless.Raise())
                        LinuxGtk::Show();
                }
                else
                    LinuxGtk::Show();
                if (request.compare(0, 5, "open ") == 0)
                    HostMenu::Perform("open", request.substr(5).c_str());
                else if (request.compare(0, 9, "activate ") == 0 || request.compare(0, 5, "quit ") == 0)
                {
                    const bool quit = request.compare(0, 5, "quit ") == 0;
                    const auto path = request.substr(quit ? 5 : 9);
                    unsigned windows[User::WindowCapacity], count = 0; bool found = false;
                    if (User::WindowSnapshot(0, windows, User::WindowCapacity, count))
                        for (unsigned i = 0; i < count; ++i)
                        {
                            char program[PATH_MAX] = {};
                            if (!X86::Application::HostWindowProgramPath(windows[i], program, sizeof program) || strcasecmp(path.c_str(), program)) continue;
                            if (quit) User::PostMessage(windows[i], 0x10, 0, 0);
                            else if (!found) X86::Application::RequestForeground(windows[i]);
                            found = true;
                        }
                    if (!quit && !found) HostMenu::Perform("open-normal", path.c_str());
                    fprintf(stderr, "NEO98 dock %s found=%u\n", quit ? "quit" : "activate", unsigned(found));
                }
            }
            next_handoff = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        }
        const auto menu_begin = frame_stats ? std::chrono::steady_clock::now() : start;
        unsigned guest_windows[User::WindowCapacity], guest_count = 0;
        menu.guest_windows.clear();
        if (User::WindowSnapshot(0, guest_windows, User::WindowCapacity, guest_count))
            for (unsigned i = 0; i < guest_count; ++i)
            {
                unsigned h = guest_windows[i];
                if (User::IsVisible(h) && HostSeamless::TaskbarWindow(h))
                {
                    auto title = HostSeamless::WindowTitle(h);
                    if (!title.empty())
                        menu.guest_windows.emplace_back(h, title);
                }
            }
        const char *active_dos = X86::Application::DosSettingsPath(User::GetForegroundWindow());
        if (!active_dos)
            for (unsigned i = 0; i < guest_count; ++i)
                if (User::IsVisible(guest_windows[i]) && (active_dos = X86::Application::DosSettingsPath(guest_windows[i]))) break;
        menu.dos_program = active_dos ? active_dos : "";
        if (std::chrono::steady_clock::now() >= next_state_menu)
        {
            menu.states = !preferences.Game().empty();
            menu.states_blocked = states.Blocked();
            menu.states_last = menu.states && !states.NeedsRun() && !states.Last().empty();
            next_state_menu = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        }
        HostMenu::Update(menu);
        HostMenu::Pump();
        if (frame_stats)
            frame_menu_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - menu_begin).count();
        if (LinuxGtk::redraw)
        {
            display_dirty = true;
            LinuxGtk::redraw = false;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_KEYDOWN && !event.key.repeat && standing && machine_paused)
            {
                const auto sym = event.key.keysym.sym;
                const bool modifier = sym == SDLK_LSHIFT || sym == SDLK_RSHIFT || sym == SDLK_LCTRL || sym == SDLK_RCTRL ||
                                      sym == SDLK_LALT || sym == SDLK_RALT || sym == SDLK_LGUI || sym == SDLK_RGUI ||
                                      sym == SDLK_CAPSLOCK;
                // That key is the person's word to the host, not the game's.
                if (!modifier && !(event.key.keysym.mod & KMOD_CTRL) && state_go_on("a key after the load")) continue;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && state_go_on("a click after the load")) continue;
            if (getenv("NEO98_INPUT_TRACE") &&
                (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_WINDOWEVENT ||
                 event.type == SDL_MOUSEBUTTONDOWN))
                fprintf(stderr, "NEO98 SDL input type=%x window=%u code=%d focus=%u\n", event.type,
                        event.window.windowID,
                        event.type == SDL_WINDOWEVENT                          ? event.window.event
                        : event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ? event.key.keysym.sym
                                                                               : event.button.button,
                        SDL_GetKeyboardFocus() ? SDL_GetWindowID(SDL_GetKeyboardFocus()) : 0);
            if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                                                  event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                                                  event.window.event == SDL_WINDOWEVENT_RESTORED))
                display_dirty = true;
            if (event.type == SDL_QUIT)
                running = false;
            else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            {
                session.app->KeyboardModifiers(0);
                session.app->MouseButtons(0, 0, 0);
                buttons = 0;
                X86::Application::HostActivation(false);
            }
            else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            {
                X86::Application::HostActivation(true);
                if (seamless_enabled)
                    if (const unsigned h = seamless.Guest(event.window.windowID); h && h < 0xffff0000u)
                        X86::Application::RequestForeground(h);
            }
            else if (event.type == SDL_WINDOWEVENT && seamless_enabled &&
                     LinuxGtk::wayland_canvas && seamless.Contains(event.window.windowID) &&
                     event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                X86::Application::RequestWindowSize(seamless.Guest(event.window.windowID),event.window.data1,event.window.data2);
            else if (event.type == SDL_WINDOWEVENT && seamless_enabled &&
                     seamless.Contains(event.window.windowID) &&
                     (event.window.event == SDL_WINDOWEVENT_CLOSE ||
                      event.window.event == SDL_WINDOWEVENT_MINIMIZED ||
                      event.window.event == SDL_WINDOWEVENT_RESTORED))
            {
                const unsigned h = seamless.Guest(event.window.windowID);
                if (h && h < 0xffff0000u)
                    User::PostMessage(h, event.window.event == SDL_WINDOWEVENT_CLOSE ? 0x10 : 0x112,
                                      event.window.event == SDL_WINDOWEVENT_MINIMIZED ? 0xf020 : 0xf120, 0);
            }
            else if (event.type == SDL_KEYDOWN && (event.key.keysym.mod & KMOD_CTRL) &&
                     (event.key.keysym.mod & KMOD_SHIFT) && event.key.keysym.sym == SDLK_m)
            {
                HostMenu::ShowLinux();
                session.app->KeyboardModifiers(0);
                session.app->MouseButtons(0, 0, 0);
                buttons = 0;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F11)
                HostMenu::Perform("fullscreen", nullptr);
            else if (event.type == SDL_KEYDOWN && (event.key.keysym.mod & KMOD_CTRL) &&
                     (event.key.keysym.mod & KMOD_SHIFT) && event.key.keysym.sym == SDLK_s)
                HostMenu::Perform("seamless", nullptr);
            else if (event.type == SDL_KEYDOWN && (event.key.keysym.mod & KMOD_CTRL) &&
                     (event.key.keysym.mod & KMOD_SHIFT) && event.key.keysym.sym == SDLK_p)
                HostMenu::Perform("pause", nullptr);
            else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            {
                const auto mods = event.key.keysym.mod;
                unsigned usb = (mods & KMOD_LCTRL ? 1 : 0) | (mods & KMOD_LSHIFT ? 2 : 0) |
                               (mods & KMOD_LALT ? 4 : 0) | (mods & KMOD_LGUI ? 8 : 0) |
                               (mods & KMOD_RCTRL ? 16 : 0) | (mods & KMOD_RSHIFT ? 32 : 0) |
                               (mods & KMOD_RALT ? 64 : 0) | (mods & KMOD_RGUI ? 128 : 0);
                if (german && (mods & KMOD_RALT))
                    usb |= 1;
                if (!machine_paused) session.app->KeyboardModifiers(usb);
                auto key = UsbKeys::Map(event.key.keysym.scancode, german, mods & KMOD_NUM);
                // Keys during a pause reach nobody; one held when it began is
                // released when it ends (pause_machine).
                if (key.vk && !machine_paused)
                {
                    if (event.type == SDL_KEYDOWN) held_keys[event.key.keysym.scancode] = {key.vk, key.scan, key.extended};
                    else held_keys.erase(event.key.keysym.scancode);
                    session.app->RawKey(key.vk, 0, event.type == SDL_KEYDOWN, key.scan, key.extended);
                    last_input_time = std::chrono::steady_clock::now();
                }
            }
            else if (event.type == SDL_MOUSEMOTION && !menu.paused && !auto_paused && !machine_paused &&
                     !mouse_hold.released)
            {
                if (seamless_enabled)
                {
                    int x = event.motion.x, y = event.motion.y;
                    mouse_point(event.motion.windowID, x, y);
                    session.app->MouseButtons(x, y, buttons);
                    last_input_time = std::chrono::steady_clock::now();
                    continue;
                }
                // SDL gives the distance in canvas pixels; the hand's is in
                // points of the window.
                float scale_x = 1, scale_y = 1;
                SDL_RenderGetScale(renderer.get(), &scale_x, &scale_y);
                deliver_motion(event.motion.x, event.motion.y, int(std::lround(event.motion.xrel * scale_x)),
                               int(std::lround(event.motion.yrel * scale_y)));
            }
            else if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && !machine_paused)
            {
                if (event.type == SDL_MOUSEBUTTONDOWN && !seamless_enabled && LinuxGtk::embedded)
                {
                    LinuxGtk::Show();
                    LinuxGtk::FocusCanvas();
                }
                // Let go, the mouse is the window's again with a press.
                if (mouse_hold.released)
                {
                    if (event.type != SDL_MOUSEBUTTONDOWN) continue;
                    MouseCapture("press");
                }
                buttons =
                    HostMouseButtons::Apply(buttons, event.button.button, event.type == SDL_MOUSEBUTTONDOWN);
                int x = event.button.x, y = event.button.y;
                mouse_point(event.button.windowID, x, y);
                if (!seamless_enabled)
                {
                    // SDL reports movement separately: for a program that
                    // recentres or DirectInput's exclusive mouse the press is
                    // where the guest's pointer stands, without movement.
                    int guest_width = 0, guest_height = 0, depth = 0;
                    User::ScreenMode(guest_width, guest_height, depth);
                    if (MouseRecentres() || X86::Application::DinputMouseExclusive())
                    {
                        Gdi::POINT at{};
                        User::CursorPosition(&at);
                        x = at.x;
                        y = at.y;
                    }
                    int gx = x, gy = y;
                    GuestScreenScale::GuestToCanvas(gx, gy, guest_width, guest_height, width, height);
                    last_mouse_x = gx;
                    last_mouse_y = gy;
                    const Gdi::POINT motion{};
                    session.app->MouseButtons(x, y, buttons, &motion);
                }
                else
                    session.app->MouseButtons(x, y, buttons);
                last_input_time = std::chrono::steady_clock::now();
            }
            else if (event.type == SDL_MOUSEWHEEL && !machine_paused)
            {
                const double sign = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
                const auto step = host_wheel.Convert(sign * event.wheel.preciseY,
                    sign * event.wheel.preciseX, User::CaptureOperation(0), User::GetForegroundWindow(),
                    double(menu.scale), SDL_GetTicks64());
                if (step.kind == HostWheelStep::Notches) session.app->MouseWheel(step.steps);
                else if (step.kind == HostWheelStep::Scroll) session.app->MouseScroll(step.out.y, step.out.x, step.out.flags);
            }
            else if (event.type == SDL_DROPFILE)
            {
                HostMenu::Perform("open", event.drop.file);
                SDL_free(event.drop.file);
            }
        }
        HostMenu::Request request;
        while (HostMenu::Next(request))
        {
            using Action = HostMenu::Action;
            if (getenv("NEO98_MENU_TRACE"))
                fprintf(stderr, "NEO98 menu action=%u value=%d path=%s\n", unsigned(request.action),
                        request.value, request.path.c_str());
            if (request.action == Action::Quit)
                running = false;
            else if (request.action == Action::StateSave) states.Save(request.path);
            else if (request.action == Action::StateLoad) states.Load(request.path);
            else if (request.action == Action::StateList) states.Open();
            else if (request.action == Action::DosSettings)
            {
                std::string path = request.path.empty() ? menu.dos_program : request.path;
                if (!path.empty() && !HostSystemFont::GuestPath(path.c_str())) {
                    std::string guest;
                    if (!map_host_file(path, guest, true)) continue;
                    path = std::move(guest);
                }
                const unsigned owner = User::GetForegroundWindow();
                const char *active = X86::Application::DosSettingsPath(owner);
                session.app->RequestDosSettings(path.c_str(), active && path == active ? owner : 0);
            }
            else if (request.action == Action::Reboot)
            {
                reboot = true;
                running = false;
            }
            else if (request.action == Action::Pause)
            {
                menu.paused = !menu.paused;
                pause_machine(menu.paused || auto_paused, "menu");
            }
            else if (request.action == Action::NetworkListen)
            {
                session.network.SetListen(request.value ? "any" : "127.0.0.1");
                menu.net_listen = session.network.ListenPolicy();
                printf("NEO98 network listen=%s (new listeners)\n", menu.net_listen.c_str());
            }
            else if (request.action == Action::Network)
            {
                menu.online = request.value;
                session.network.SetOnline(menu.online);
            }
            else if (request.action == Action::Wifi)
            {
                menu.wifi = request.value;
                hardware.SetWifi(menu.wifi);
            }
            else if (request.action == Action::Microphone)
            {
                menu.microphone = request.value;
                Sound::Microphone::SetEnabled(menu.microphone);
                printf("NEO98 HOST microphone %s\n", menu.microphone ? "on" : "off");
                fflush(stdout);
            }
            else if (request.action == Action::Report)
                session.Report("host-request");
            else if (request.action == Action::Preferences)
            {
                if (request.value == 1) { preferences.ToggleAsk(menu); LinuxGtk::Check("open-ask", preferences.AskEnabled()); }
                else LinuxGtk::Preferences();
            }
            else if (request.action == Action::GameList)
            {
                const std::string machine =
                    preferences.Game().empty() ? roots['C'] : preferences.SourceMachine();
                GtkWidget *dialog = gtk_dialog_new_with_buttons("Game machines", GTK_WINDOW(LinuxGtk::shell),
                                                                GTK_DIALOG_DESTROY_WITH_PARENT, "Close",
                                                                GTK_RESPONSE_CLOSE, nullptr);
                GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
                gtk_container_set_border_width(GTK_CONTAINER(box), 16);
                const auto games = HostGameMachine::List(machine);
                if (games.empty())
                    gtk_box_pack_start(
                        GTK_BOX(box),
                        gtk_label_new("No game machines yet. Choose File, Open as game machine."),
                        FALSE, FALSE, 8);
                for (const auto &game : games)
                {
                    GtkWidget *button =
                        gtk_button_new_with_label((game.name + (game.running ? " · running" : "")).c_str());
                    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 4);
                    gtk_widget_set_sensitive(button, !game.running);
                    g_signal_connect_data(
                        button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer folder) {
                            HostMenu::Perform("game-launch", static_cast<const char *>(folder));
                        }),
                        g_strdup(game.folder.c_str()), +[](gpointer folder, GClosure *) { g_free(folder); },
                        GConnectFlags(0));
                }
                g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, gint, gpointer) {
                                     gtk_widget_destroy(GTK_WIDGET(dialog));
                                 }),
                                 nullptr);
                gtk_widget_show_all(dialog);
            }
            else if (request.action == Action::GameLaunch)
            {
                const auto folder = HostGameMachine::Resolve(request.path, roots['C']);
                if (folder.empty() ||
                    !HostGameMachine::Spawn({HostSettings::Executable(), "--game-machine", folder}, {}))
                    LinuxGtk::Notice("Game machine", "Could not start the game machine.");
            }
            else if (request.action == Action::GameCreate)
            {
                if (!preferences.Game().empty())
                {
                    LinuxGtk::Notice("Game machine", "Create game machines from the main machine.");
                    continue;
                }
                HostGameMachine::Request make;
                std::string path = request.path;
                const auto separator = path.find('|');
                const bool scripted = separator != std::string::npos;
                if (scripted) { make.name = path.substr(separator + 1); path.resize(separator); }
                make.machine = HostSettings::DriveC(LinuxHost::MachineFolder()) == LinuxHost::RealPath(roots['C'])
                                   ? LinuxHost::MachineFolder() : roots['C'];
                const bool guest_path = HostSystemFont::GuestPath(path.c_str());
                make.program = guest_path ? path : GuestPath(path, roots);
                if (make.program.empty()) map_host_file(path, make.program, true);
                if (!guest_path) make.program_file = path;
                make.source.scale = menu.scale;
                make.source.seamless = menu.seamless;
                make.source.online = menu.online;
                make.source.net_listen = menu.net_listen;
                make.source.seamless_windows = menu.seamless_windows;
                make.source.pause_unfocused = menu.pause_unfocused;
                for (const auto &root : roots)
                    if (root.first > 'C' && Volume::Type(root.first) != Volume::Cdrom)
                        make.source.drives[root.first - 'A'] = root.second;
                for (unsigned port = 0; port < 4; ++port)
                    make.source.com[port] = menu.com[port];
                Registry::Flush();
                HostGameMachine::Result result;
                if (!HostGameMachine::Create(make, HostSettings::Executable(), result))
                    LinuxGtk::Notice("Game machine", result.error.c_str());
                else if (scripted) {
                    printf("NEO98 HOST game machine created name=%s program=%s folder=%s\n",
                           result.name.c_str(), make.program.c_str(), result.folder.c_str());
                    fflush(stdout);
                }
                else if (!HostGameMachine::Spawn(
                             {HostSettings::Executable(), "--game-machine", result.folder}, {}))
                    LinuxGtk::Notice("Game machine",
                                     "The game machine was created but could not be started.");
                else
                    fprintf(stderr, "NEO98 game machine created name=%s folder=%s\n", result.name.c_str(),
                            result.folder.c_str());
            }
            else if (request.action == Action::CtrlAltDelete)
                X86::Application::RequestCtrlAltDelete();
            else if (request.action == Action::Fullscreen)
            {
                menu.fullscreen = !menu.fullscreen;
                if (menu.fullscreen && seamless_enabled)
                {
                    fullscreen_from_seamless = true;
                    set_seamless(false);
                }
                LinuxGtk::Fullscreen(menu.fullscreen);
                if (!menu.fullscreen && fullscreen_from_seamless)
                {
                    fullscreen_from_seamless = false;
                    set_seamless(true);
                }
            }
            else if (request.action == Action::Scale)
            {
                if (menu.seamless)
                    set_seamless(false);
                menu.scale = request.value;
                resize_after_frame = true;
            }
            else if (request.action == Action::Seamless)
            {
                if (!menu.seamless && !seamless.Supported(window.get()))
                {
                    LinuxGtk::Notice(
                        "Nahtlose Ansicht",
                        "The active window driver does not support seamless windows.");
                    continue;
                }
                if (menu.fullscreen)
                {
                    menu.fullscreen = false;
                    LinuxGtk::Fullscreen(false);
                }
                fullscreen_from_seamless = false;
                set_seamless(!menu.seamless);
            }
            else if (request.action == Action::SeamlessTaskbar)
            {
                menu.seamless_taskbar = !menu.seamless_taskbar;
                X86::Application::SetHostWorkArea(menu.seamless && !menu.seamless_taskbar);
            }
            else if (request.action == Action::SeamlessWindows)
            {
                menu.seamless_windows = !menu.seamless_windows;
                seamless.Close();
            }
            else if (request.action == Action::SeamlessIcons)
                menu.seamless_icons = !menu.seamless_icons;
            else if (request.action == Action::SeamlessDock)
            {
                menu.seamless_dock = !menu.seamless_dock;
                seamless.Close();
            }
            else if (request.action == Action::WheelFocus)
            {
                menu.wheel_focus = !menu.wheel_focus;
                User::SetWheelToPointer(!menu.wheel_focus);
            }
            else if (request.action == Action::PauseUnfocused)
                menu.pause_unfocused = !menu.pause_unfocused;
            else if (request.action == Action::GuestWindow)
            {
                X86::Application::RequestForeground(request.value);
                if (menu.seamless)
                    seamless.Raise(request.value);
                else
                    LinuxGtk::Show();
            }
            else if (request.action == Action::Minimize)
            {
                if (menu.seamless)
                    User::PostMessage(User::GetForegroundWindow(), 0x112, 0xf020, 0);
                else if (LinuxGtk::shell)
                    gtk_window_iconify(GTK_WINDOW(LinuxGtk::shell));
            }
            else if (request.action == Action::StartMenu)
            {
                X86::Application::HostActivation(true);
                const unsigned tray = User::FindWindow("Shell_TrayWnd", nullptr);
                const unsigned button = tray ? User::FindWindowEx(tray, 0, "Button", nullptr) : 0;
                if (seamless_enabled && !menu.seamless_taskbar && button)
                {
                    User::PostMessage(button, 0x201, 1, 10 | (10 << 16));
                    User::PostMessage(button, 0x202, 0, 10 | (10 << 16));
                }
                else
                {
                    Gdi::RECT bar = {};
                    if (tray && User::GetWindowRect(tray, &bar))
                    {
                        session.app->MouseButtons(std::max(0, bar.left) + 20, std::max(0, bar.top) + 14, 1);
                        session.app->MouseButtons(std::max(0, bar.left) + 20, std::max(0, bar.top) + 14, 0);
                    }
                }
            }
            else if (request.action == Action::Edit)
            {
                X86::Application::HostActivation(true);
                session.app->Key(request.value, 0, false, true);
                session.app->KeyUp(request.value);
                session.app->KeyboardModifiers(0);
                if (menu.seamless)
                    seamless.Raise();
                else
                    LinuxGtk::FocusCanvas();
            }
            else if (request.action == Action::MouseRelease)
            {
                SDL_SetRelativeMouseMode(SDL_FALSE);
                SDL_CaptureMouse(SDL_FALSE);
                session.app->KeyboardModifiers(0);
                session.app->MouseButtons(0, 0, 0);
                buttons = 0;
            }
            else if (request.action == Action::ComPort)
            {
                const std::string name = "NEO98_COM" + std::to_string(request.value);
                if (request.path.empty())
                    unsetenv(name.c_str());
                else
                    setenv(name.c_str(), request.path.c_str(), 1);
                menu.com[request.value - 1] = request.path;
            }
            else if (request.action == Action::Open)
            {
                if (request.path.find_first_of("\r\n") != std::string::npos)
                { LinuxGtk::Notice("Open program", "The filename contains a line break."); continue; }
                const auto dot = request.path.rfind('.');
                const std::string extension = dot == std::string::npos ? "" : request.path.substr(dot);
                if (!strcasecmp(extension.c_str(), ".iso") || !strcasecmp(extension.c_str(), ".cue"))
                { HostMenu::Perform("cdrom", request.path.c_str()); continue; }
                if (!request.value && preferences.Game().empty() &&
                    HostGameMachine::ProgramName(request.path) && !HostGameMachine::InstallerName(request.path))
                {
                    const auto games = HostGameMachine::FindFor(roots['C'], request.path, request.path);
                    if (games.size() == 1) { HostMenu::Perform("game-launch", games.front().folder.c_str()); continue; }
                    if (games.size() > 1 && LinuxGtk::ready)
                    {
                        std::vector<std::pair<std::string,std::string>> choices;
                        for (const auto &game : games) choices.emplace_back(game.name, game.folder);
                        LinuxGtk::ChooseGame(choices); continue;
                    }
                    if (preferences.AskOpen(request.path) && LinuxGtk::ready)
                    {
                        LinuxGtk::AskOpen(request.path, [&, path = request.path](int answer, bool never) {
                            if (answer != 1 && answer != 2) return;
                            preferences.RememberOpen(answer == 1 ? path : "", never, menu);
                            LinuxGtk::Check("open-ask", preferences.AskEnabled());
                            HostMenu::Perform(answer == 1 ? "open-normal" : "game-create", path.c_str());
                        });
                        continue;
                    }
                }
                auto guest = HostSystemFont::GuestPath(request.path.c_str()) ? request.path
                                                                             : GuestPath(request.path, roots);
                if (guest.empty())
                    map_host_file(request.path, guest, true);
                if (!guest.empty())
                    X86::Application::RequestOpen(guest.c_str());
                else
                    LinuxGtk::Notice("Open program", "Could not open the file.");
            }
            else if (request.action == Action::ChooseDriveD)
            {
                auto drive = std::make_unique<HostVolume::Drive>();
                if (drive->Open(request.path.c_str(), true, "HOST"))
                {
                    CdAudio::Stop('D');
                    CdAudio::Insert('D', nullptr);
                    Volume::Mount('D', drive.get(), Volume::Fixed, true);
                    clipboard_drives.push_back(std::move(drive));
                    roots['D'] = request.path;
                    menu.drive_d = added_drive_d = request.path;
                    if (menu.cdrom_letter == 'D')
                    {
                        menu.cdrom.clear();
                        menu.cdrom_letter = 0;
                        changed_cdroms['D'].clear();
                    }
                    session.app->ChangeMedium('D');
                    session.RefreshDos(&roots);
                }
                else
                    LinuxGtk::Notice("Drive D:", "Could not mount the folder.");
            }
            else if (request.action == Action::InsertCdrom)
            {
                const char letter = menu.cdrom_letter ? menu.cdrom_letter : 'E';
                if (Volume::Mounted(letter) && !menu.cdrom_letter)
                {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "NEO98",
                                             "Drive E: is already in use.", window.get());
                    continue;
                }
                std::string error;
                auto image = std::make_unique<HostCdImage::Image>();
                auto folder = std::make_unique<HostVolume::Drive>();
                if (HostSettings::IsDirectory(request.path) &&
                    folder->Open(request.path.c_str(), false, "CDROM"))
                {
                    CdAudio::Stop(letter);
                    CdAudio::Insert(letter, nullptr);
                    Volume::Mount(letter, folder.get(), Volume::Cdrom, false);
                    cd.reset();
                    clipboard_drives.push_back(std::move(folder));
                    roots[letter] = request.path;
                    menu.cdrom = request.path;
                    menu.cdrom_letter = letter;
                    changed_cdroms[letter] = request.path;
                    session.app->ChangeMedium(letter);
                }
                else if (image->Open(request.path.c_str(), error))
                {
                    CdAudio::Stop(letter);
                    CdAudio::Insert(letter, nullptr);
                    Volume::Mount(letter, nullptr, Volume::Cdrom, false);
                    cd = std::move(image);
                    Volume::Mount(letter, cd.get(), Volume::Cdrom, false);
                    CdAudio::Insert(letter, cd.get());
                    roots.erase(letter);
                    menu.cdrom = request.path;
                    menu.cdrom_letter = letter;
                    changed_cdroms[letter] = request.path;
                    session.app->ChangeMedium(letter);
                }
                else
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "CD-ROM", error.c_str(), window.get());
            }
            else if (request.action == Action::EjectCdrom && menu.cdrom_letter)
            {
                const char letter = menu.cdrom_letter;
                CdAudio::Stop(letter);
                CdAudio::Insert(letter, nullptr);
                Volume::Mount(letter, nullptr, Volume::Cdrom, false);
                session.app->ChangeMedium(letter);
                cd.reset();
                roots.erase(letter);
                changed_cdroms[letter].clear();
                menu.cdrom.clear();
                menu.cdrom_letter = 0;
            }
        }
        const bool focused = focus_script >= 0 ? focus_script != 0 :
            LinuxGtk::Active() ||
            (seamless_enabled ? seamless.HasFocus() : SDL_GetKeyboardFocus() == window.get());
        const bool pause_now = menu.pause_unfocused && !focused;
        if (auto_paused != pause_now)
        {
            auto_paused = pause_now;
            pause_machine(menu.paused || auto_paused, pause_now ? "NEO98 lost the focus" : "NEO98 has the focus again");
        }
        {
            // A state that waits for the current call lets a paused machine
            // run on until its threads stand well (tools/host_states_linux.h).
            const bool hold = states.HoldUi() || (!states.NeedsRun() && (menu.paused || auto_paused || states.ListOpen()));
            pause_machine(hold, !hold && states.NeedsRun() ? "a state waits for the current call" : "the host");
        }
        const auto pump_begin = frame_stats ? std::chrono::steady_clock::now() : start;
        if (!states.HoldUi() && (states.NeedsRun() || (!menu.paused && !auto_paused && !states.ListOpen())))
            session.app->Pump(8);
        HostState::ProbeTurn(*session.app);
        if (frame_stats)
            frame_pump_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - pump_begin).count();
        input.Pump(session, window.get());
        {
            const auto now = std::chrono::steady_clock::now();
            if (input.last_input > last_input_time) last_input_time = input.last_input;
            if (const unsigned calls = HostPacing::TotalCalls(*session.app); calls != last_calls)
            {
                last_calls = calls;
                last_call_time = now;
            }
            // A deliberate pause is not time spent answering guest input.
            if (machine_paused) last_call_time = now;
            const bool input_waiting = last_input_time > last_call_time;
            const bool waiting = !machine_paused && (input_waiting || session.app->SystemRunnable());
            HostHang::Observe(waiting_since, was_waiting, now, last_call_time, last_input_time, input_waiting, waiting);
            const bool stalled = waiting && now - waiting_since >= std::chrono::seconds(3);
            const unsigned stalled_seconds =
                stalled ? unsigned(std::chrono::duration_cast<std::chrono::seconds>(now - waiting_since).count()) : 0;
            if (stalled != hang_now)
            {
                fprintf(stderr, "NEO98 HOST hang %s\n", stalled ? "begin" : "end");
                fflush(stderr);
            }
            // The window title says it, as on macOS.
            if (LinuxGtk::shell && (stalled != hang_now || stalled_seconds != hang_seconds_shown))
            {
                const std::string name = preferences.Game().empty() ? "Neo98" : preferences.GameName();
                const std::string title =
                    stalled ? name + " [unresponsive for " + std::to_string(stalled_seconds) + " s]" : name;
                gtk_window_set_title(GTK_WINDOW(LinuxGtk::shell), title.c_str());
            }
            hang_now = stalled;
            hang_seconds_shown = stalled_seconds;
        }
        states.Poll();
        std::vector<std::pair<std::string, std::string>> failures;
        failures.swap(dos_failures);
        for (const auto &failure : failures)
        {
            printf("NEO98 HOST DOS failure %s: %s\n", failure.first.c_str(), failure.second.c_str());
            fflush(stdout);
            if (const char *action = getenv("NEO98_DOS_FAILURE_ACTION")) {
                if (!strcmp(action, "settings")) session.app->RequestDosSettings(failure.first.c_str());
                if (!strcmp(action, "retry")) {
                    X86::Application::RequestOpen(failure.first.c_str());
                    unsigned windows[User::WindowCapacity], count = 0;
                    if (User::WindowSnapshot(0, windows, User::WindowCapacity, count))
                        for (unsigned i = 0; i < count; ++i)
                            if (User::WindowProcess(windows[i]) == session.app->process_id) {
                                User::PostMessage(windows[i], 0, 0, 0);
                                break;
                            }
                }
                continue;
            }
            GtkWidget *dialog = gtk_message_dialog_new(LinuxGtk::shell ? GTK_WINDOW(LinuxGtk::shell) : nullptr,
                GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE, "%s", failure.second.c_str());
            gtk_window_set_title(GTK_WINDOW(dialog), "DOS program");
            gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Close", GTK_RESPONSE_CLOSE,
                "Settings…", 1, "Restart", 2, nullptr);
            g_object_set_data_full(G_OBJECT(dialog), "dos-path", g_strdup(failure.first.c_str()), g_free);
            g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog *dialog, gint response, gpointer) {
                const char *path = static_cast<const char *>(g_object_get_data(G_OBJECT(dialog), "dos-path"));
                if (response == 1) HostMenu::Perform("dos-settings", path);
                if (response == 2) HostMenu::Perform("open", path);
                gtk_widget_destroy(GTK_WIDGET(dialog));
            }), nullptr);
            gtk_widget_show_all(dialog);
        }
        // The machine goes when ExitWindowsEx has run to its end (the hook),
        // or, as on macOS, when the shell ended on WM_ENDSESSION of an
        // ExitWindowsEx another process runs. The agreed session alone is not
        // the end: USER still plays "SystemExit" to its end after it.
        unsigned end_flags = 0, end_code = 0;
        if (session_end.requested || (session.app->Status() == X86::Application::Exited &&
                                      X86::Application::SessionCommitted(end_flags, end_code)))
        {
            printf("NEO98 HOST exitwindows flags=%08x code=%u\n", session_end.requested ? session_end.flags : end_flags,
                   session_end.requested ? session_end.code : end_code);
            fflush(stdout);
            reboot = !((session_end.requested ? session_end.flags : end_flags) & 9) ||
                     ((session_end.requested ? session_end.flags : end_flags) & 2);
            running = false;
        }
        clipboard.Pump();
        const bool mouse_in_guest = LinuxGtk::wayland_canvas ? LinuxGtk::canvas_pointer : SDL_GetMouseFocus() == window.get();
        if (!seamless_enabled)
        {
            User::ShowPointer(mouse_in_guest && !last_console);
            SDL_ShowCursor(mouse_in_guest && !last_console ? SDL_DISABLE : SDL_ENABLE);
            LinuxGtk::CanvasCursor(!(mouse_in_guest && !last_console));
        }
        const auto render_begin = frame_stats ? std::chrono::steady_clock::now() : start;
#ifdef NEO98_WITH_VULKAN
        scanout_enabled = vulkan_desktop && !seamless_enabled && !LinuxGtk::wayland_canvas;
#endif
        texture_dirty |= User::Render();
        if (frame_stats)
            frame_render_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - render_begin).count();
        User::PixelsSize(width, height);
        texture_dirty |= last_frame != User::FrameSerial();
        last_frame = User::FrameSerial();
        const unsigned char *pixels = reinterpret_cast<const unsigned char *>(session.app->ConsolePixels());
        texture_dirty |=
            pixels != last_console || (pixels && last_console_hash != session.app->ConsoleFrameHash());
        last_console_hash = session.app->ConsoleFrameHash();
        last_console = pixels;
        if (pixels)
        {
            if (!User::DisplayPower())
            {
                width = 800;
                height = 600;
            }
        }
        else if (!User::DeferScreenRead())
            pixels = User::Pixels();
        if (resize_after_frame && !menu.paused && !auto_paused)
        {
            frame_window(width, height, true);
            resize_after_frame = false;
            display_dirty = true;
        }
        int overlay_width = 0, overlay_height = 0;
        unsigned overlay_serial = 0;
        const bool program_view = last_console || User::ScreenModeActive() || User::ExclusiveMode() ||
                                  User::Overlay(overlay_width, overlay_height, overlay_serial) != nullptr;
        if (frame_stats)
        {
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - frame_sample).count();
            if (seconds >= 1)
            {
                const unsigned presents = session.app->HalPresents();
                fprintf(stderr, "NEO98 FRAMES seconds=%.6f presents=%u fps=%.3f displayed=%llu "
                        "display_fps=%.3f calls=%u pump_ms=%.3f render_ms=%.3f menu_ms=%.3f\n",
                        seconds, presents - frame_presents, (presents - frame_presents) / seconds,
                        presentations - frame_displayed, (presentations - frame_displayed) / seconds,
                        session.app->ApiCalls(), frame_pump_ms, frame_render_ms, frame_menu_ms);
                frame_sample = now;
                frame_presents = presents;
                frame_displayed = presentations;
                frame_pump_ms = frame_render_ms = frame_menu_ms = 0;
            }
        }
        bool native_view = menu.seamless && !menu.fullscreen && !program_view;
        if (LinuxGtk::wayland_canvas) {
            int sw = 0, sh = 0, depth = 0; User::ScreenMode(sw,sh,depth);
            native_view &= depth == 32;
        }
        if (native_view)
        {
            int wanted_width = 0, wanted_height = 0;
            X86::Application::HostDisplay(wanted_width, wanted_height);
            const auto area = seamless.Area(window.get());
            if (!X86::Application::HostDisplayPending() && !wanted_width)
            {
                menu.seamless = native_view = false;
            }
            else
            {
                if (wanted_width != area.w || wanted_height != area.h)
                    X86::Application::SetHostDisplay(area.w, area.h, true);
                native_view = width == area.w && height == area.h;
            }
        }
        if (native_view != seamless_enabled)
        {
            show_seamless(native_view);
            fprintf(stderr, "NEO98 seamless presentation=%d program=%d\n", native_view, program_view);
        }
        if (seamless_enabled)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_present >= std::chrono::milliseconds(16))
            {
                seamless.Present(window.get(), menu, pixels, texture_dirty || display_dirty);
                last_present = now;
                texture_dirty = display_dirty = false;
                if (const char *path = getenv("NEO98_SEAMLESS_DUMP"))
                    seamless.Describe(path);
            }
            if (options.seconds &&
                std::chrono::steady_clock::now() - start >= std::chrono::seconds(options.seconds))
            {
                expired = true;
                break;
            }
            if (machine_paused || !session.app->SystemRunnable())
                SDL_Delay(1);
            continue;
        }
        if (texture_width != width || texture_height != height)
        {
            texture.reset(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING, width, height));
            if (!texture)
            {
                fprintf(stderr, "%s\n", SDL_GetError());
                return 1;
            }
            // A new canvas size (a display mode, the desktop's size): the
            // window follows it, as the Mac window does.
            frame_window(width, height, false);
            texture_width = width;
            texture_height = height;
            SDL_RenderSetLogicalSize(renderer.get(), width, height);
            texture_dirty = true;
        }
#ifdef NEO98_WITH_HOST_MEDIA
        // The video in Media Center 98's window : where
        // the program painted the key colour, Kodi's picture from the
        // compositor shows through, in the window's own resolution.
        bool video_new = false;
        std::vector<HostMediaService::Service::Area> video_areas;
        {
            if (media_embeds && !last_console) video_areas = media.VideoAreas();
#ifdef NEO98_WITH_VULKAN
            // The video goes below the guest's picture through the SDL
            // renderer: Vulkan's presentation leaves for good, as when it fails.
            if (!video_areas.empty() && vulkan_desktop)
            {
                fprintf(stderr, "NEO98 Vulkan presentation leaves for Kodi's video\n");
                scanout_enabled = vulkan_desktop = false;
                User::SetHostScanoutProvider(nullptr, nullptr);
                HostVulkan::Detach();
                display_dirty = true;
            }
#endif
            video.Feed(compositor, media.VideoAspect(), !video_areas.empty());
            static unsigned drawn_serial = 0;
            const unsigned newest = video_areas.empty() ? 0 : video.Serial();
            if (newest != drawn_serial) { drawn_serial = newest; video_new = texture_dirty = true; }
        }
#else
        const bool video_new = false;
#endif
        const auto now = std::chrono::steady_clock::now();
        if ((texture_dirty || display_dirty) && (uncapped || video_new || now - last_present >= std::chrono::milliseconds(16)))
        {
            bool gpu_shown = false;
#ifdef NEO98_WITH_VULKAN
            if (!pixels && vulkan_desktop && !machine_paused)
            {
                int output_width = 0, output_height = 0;
                SDL_GetWindowSize(window.get(), &output_width, &output_height);
                if (HostVulkan::Present(User::ScanoutBits(), output_width, output_height, width, height))
                {
                    ++gpu_presentations;
                    gpu_shown = true;
                }
                else
                {
                    fprintf(stderr, "NEO98 Vulkan presentation falls back to CPU image\n");
                    scanout_enabled = vulkan_desktop = false;
                    User::SetHostScanoutProvider(nullptr, nullptr);
                    HostVulkan::Detach();
                }
            }
#endif
            if (!gpu_shown)
            {
                if (!pixels) pixels = User::Pixels();
                // A machine that stands shows its picture dimmed, with the
                // mark of the pause over it, as on macOS.
                if (machine_paused && !seamless_enabled && pixels && width > 0 && height > 0)
                {
                    static std::vector<unsigned char> paused_copy;
                    const size_t bytes = size_t(width) * height * 4;
                    paused_copy.assign(pixels, pixels + bytes);
                    for (size_t i = 0; i < bytes; i += 4)
                        for (size_t c = 0; c < 3; ++c) paused_copy[i + c] = (unsigned char)(paused_copy[i + c] * 2 / 5);
                    const int unit = std::max(2, height / 150), cx = width / 2, cy = height / 2;
                    PauseMark(cx, cy, unit, [&](int x, int y, int w, int h, bool light) {
                        const unsigned char shade = light ? 255 : 24;
                        for (int row = std::max(0, y); row < std::min(height, y + h); ++row)
                            for (int column = std::max(0, x); column < std::min(width, x + w); ++column)
                            {
                                unsigned char *at = &paused_copy[(size_t(row) * width + column) * 4];
                                at[0] = at[1] = at[2] = shade;
                            }
                    });
                    // NEO98_HOST_PAUSE_PROBE=1 tells once a pause what the
                    // guest drew and what the window shows: a corner, the left
                    // bar of the mark and a grid of five by five points
                    // without the middle one (tools/test_pause_host.py).
                    static const bool probe = getenv("NEO98_HOST_PAUSE_PROBE") != nullptr;
                    if (probe && !pause_drawn)
                    {
                        const auto at = [&](const unsigned char *bits, int x, int y) {
                            return *reinterpret_cast<const unsigned *>(bits + (size_t(y) * width + x) * 4) & 0xffffff;
                        };
                        const int bar_x = cx - 22 * unit + 16 * unit, bar_y = cy - 20 * unit + 13 * unit;
                        printf("NEO98 HOST pause picture guest=%06x shown=%06x bar=%06x\n", at(pixels, 2, 2),
                               at(paused_copy.data(), 2, 2), at(paused_copy.data(), bar_x, bar_y));
                        printf("NEO98 HOST pause grid");
                        for (int gy = 0; gy < 5; ++gy)
                            for (int gx = 0; gx < 5; ++gx)
                            {
                                if (gx == 2 && gy == 2) continue;
                                const int px = (2 * gx + 1) * width / 10, py = (2 * gy + 1) * height / 10;
                                printf(" %d,%d=%06x>%06x", px, py, at(pixels, px, py), at(paused_copy.data(), px, py));
                            }
                        printf("\n");
                        fflush(stdout);
                    }
                    pause_drawn = true;
                    pixels = paused_copy.data();
                    texture_dirty = true;
                }
#ifdef NEO98_WITH_HOST_MEDIA
                if (!video_areas.empty())
                    PresentVideo(renderer.get(), texture.get(), reinterpret_cast<const uint32_t *>(pixels), width, height, video_areas, video);
                else
#endif
                {
                    if (texture_dirty && !LinuxGtk::wayland_canvas)
                    {
                        SDL_UpdateTexture(texture.get(), nullptr, pixels, width * 4);
                        ++uploads;
                    }
                    if (!LinuxGtk::wayland_canvas)
                    {
                        SDL_RenderClear(renderer.get());
                        SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
                    }
                }
                if (!host_shot_path.empty() && !LinuxGtk::wayland_canvas)
                {
                    // The whole window in its own pixels, bars included.
                    int out_w = 0, out_h = 0;
                    SDL_GetRendererOutputSize(renderer.get(), &out_w, &out_h);
                    std::vector<uint32_t> shot(size_t(std::max(out_w, 0)) * std::max(out_h, 0));
                    SDL_RenderSetLogicalSize(renderer.get(), 0, 0);
                    SDL_RenderSetViewport(renderer.get(), nullptr);
                    if (!shot.empty() && !SDL_RenderReadPixels(renderer.get(), nullptr, SDL_PIXELFORMAT_ARGB8888, shot.data(), out_w * 4))
                        if (FILE *file = fopen(host_shot_path.c_str(), "wb"))
                        {
                            CrashFiles::FileSink sink(file);
                            CrashReport::WritePng(sink, reinterpret_cast<const unsigned char *>(shot.data()), out_w, out_h);
                            fclose(file);
                        }
                    SDL_RenderSetLogicalSize(renderer.get(), width, height);
                    fprintf(stderr, "NEO98 Linux host shot %s %dx%d\n", host_shot_path.c_str(), out_w, out_h);
                    host_shot_path.clear();
                }
                LinuxGtk::Present(renderer.get(),pixels,width,height);
            }
            ++presentations;
            last_present = now;
            texture_dirty = display_dirty = false;
        }
        if (options.seconds &&
            std::chrono::steady_clock::now() - start >= std::chrono::seconds(options.seconds))
        {
            expired = true;
            break;
        }
        // Yield when the guest waits, as the macOS host does. Sleeping after
        // every pump adds several milliseconds to each frame of a busy game.
        if (machine_paused || !session.app->SystemRunnable())
            SDL_Delay(1);
    }
    HostState::ProbeReport();
    HostState::ExitTrace();
    if (menu.paused || auto_paused)
        pause_machine(false);
    auto saved_view = menu;
    saved_view.seamless |= fullscreen_from_seamless;
    preferences.Save(saved_view);
    seamless.Close();
    if (getenv("NEO98_PRESENT_STATS"))
        printf("NEO98 PRESENT uploads=%llu presentations=%llu seconds=%.3f gpu=%llu\n", uploads, presentations,
               std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), gpu_presentations);
    if (expired || !options.quiet)
        session.Describe();
    // The line of the macOS host when the first program has ended.
    if (session.app->Status() == X86::Application::Exited || session.app->Status() == X86::Application::Failed)
    {
        printf("NEO98 HOST %s code=%u calls=%u error=%s\n",
               session.app->Status() == X86::Application::Exited ? "exited" : "failed", session.app->ExitCode(),
               session.app->ApiCalls(), session.app->Error() ? session.app->Error() : "(null)");
        fflush(stdout);
        // A game machine ends with its game, as on macOS.
        if (!preferences.Game().empty() && session.Finished())
        {
            printf("NEO98 HOST game machine ends (%s)\n",
                   session.app->Status() == X86::Application::Exited ? "the game has ended" : "the game failed");
            fflush(stdout);
        }
    }
    if (!options.screenshot.empty() && !session.Screenshot(options.screenshot))
        return 1;
    // The report of a stop was written when it happened (the crash hook).
    if (session.app->Status() == X86::Application::Failed)
        fprintf(stderr, "%s\n", session.app->Error());
    if (reboot)
    {
        std::vector<std::string> arguments;
        for (int i = 0; i < argc; ++i)
        {
            if ((!strcmp(argv[i], "--load-state") || !strcmp(argv[i], "--open-host") ||
                 !strcmp(argv[i], "--activate-program") || !strcmp(argv[i], "--quit-program")) && i + 1 < argc)
            { ++i; continue; }
            if ((!strcmp(argv[i], "--drive") || !strcmp(argv[i], "--drive-readonly")) && i + 1 < argc &&
                !added_drive_d.empty() && (argv[i + 1][0] == 'D' || argv[i + 1][0] == 'd') &&
                argv[i + 1][1] == '=')
            {
                ++i;
                continue;
            }
            if (!strcmp(argv[i], "--cdrom") && i + 1 < argc)
            {
                const char *path = argv[i + 1];
                const char letter = path[0] && path[1] == '=' ? char(HostVolume::Fold(path[0])) : 'D';
                if (changed_cdroms.count(letter))
                {
                    ++i;
                    continue;
                }
            }
            arguments.emplace_back(argv[i]);
        }
        if (!added_drive_d.empty())
        {
            arguments.emplace_back("--drive");
            arguments.emplace_back("D=" + added_drive_d);
        }
        for (const auto &medium : changed_cdroms)
            if (!medium.second.empty())
            {
                arguments.emplace_back("--cdrom");
                arguments.emplace_back(std::string(1, medium.first) + "=" + medium.second);
            }
        arguments.emplace_back(menu.seamless ? "--seamless" : "--no-seamless");
        arguments.emplace_back(menu.fullscreen ? "--fullscreen" : "--no-fullscreen");
        arguments.emplace_back(menu.online ? "--online" : "--offline");
        arguments.emplace_back(menu.wifi ? "--wifi" : "--no-wifi");
        arguments.emplace_back(menu.microphone ? "--microphone" : "--no-microphone");
        arguments.insert(arguments.end(), {"--net-listen", menu.net_listen});
        arguments.emplace_back(menu.seamless_taskbar ? "--seamless-taskbar" : "--seamless-no-taskbar");
        arguments.emplace_back(menu.seamless_windows ? "--seamless-windows" : "--seamless-layer");
        arguments.emplace_back(menu.seamless_icons ? "--seamless-icons" : "--seamless-no-icons");
        arguments.emplace_back(menu.seamless_dock ? "--seamless-dock" : "--seamless-no-dock");
        arguments.emplace_back(menu.pause_unfocused ? "--pause-unfocused" : "--no-pause-unfocused");
        arguments.emplace_back(menu.wheel_focus ? "--wheel-focus" : "--wheel-pointer");
        arguments.emplace_back("--scale");
        arguments.emplace_back(std::to_string(menu.scale));
        if (!state_restart.empty()) { arguments.emplace_back("--load-state"); arguments.push_back(state_restart); }
        std::vector<char *> again;
        for (auto &argument : arguments)
            again.push_back(argument.data());
        again.push_back(nullptr);
        printf("NEO98 HOST restart reason=%s\n", state_restart.empty() ? (getenv("NEO98_LINUX_STATE_ERROR") ? "state not loaded" : "menu") : "load state");
        fflush(stdout);
        session.PrepareRestart();
        session.Stop();
        cd.reset();
        execvp(again[0], again.data());
        fprintf(stderr, "Restart failed: %s\n", strerror(errno));
        return 1;
    }
    if (interrupted)
        return 130;
    if (expired)
        return 124;
    // As on macOS: a host that ended in order ends with 0, whatever its
    // program returned or where it stopped (a fault box ends a program with
    // the code of the fault). The exit line above says which.
    return 0;
}
