// SPDX-License-Identifier: GPL-2.0-or-later
// neo98-kms: NEO98 as the only graphics program of the computer, without X11
// or a Wayland desktop. It shows Windows through DRM/KMS
// (tools/host_kms/output.h), reads keyboard, mouse and touch through libinput
// (tools/host_kms/input.h) and plays sound through SDL without a window.
// Ctrl+Alt+Backspace ends it.
// The host adapter reaches the runtime's host service methods, as
// tools/neo98_linux.cpp does.
#define private public
#include "../src/x86_app.h"
#undef private
#include "linux_session.h"
#include "../src/usb_keys.h"
#include "host_audio.h"
#include "host_gpu.h"
#include "host_devices/service.h"
#include "../src/host_media_abi.h"
#ifdef NEO98_WITH_HOST_MEDIA
#include "host_media/service.h"
#include "host_media/seed.h"
#endif
#include "host_kms/input.h"
#include "host_kms/output.h"
#include "host_wayland/compositor.h"
#include "host_wayland/video.h"
#include <algorithm>
#include <poll.h>
#include <thread>
namespace
{
volatile sig_atomic_t interrupted = 0;
void Signal(int) { interrupted = 1; }
struct SessionEnd { bool requested = false; };
void EndSession(void *context, X86::Application &, unsigned, unsigned) { static_cast<SessionEnd *>(context)->requested = true; }

// Timed test input (NEO98_HOST_INPUT), the steps the other hosts know:
// wait=CLASS, click=X,Y, dblclick=X,Y, move=X,Y, drag=X1,Y1,X2,Y2 (guest
// coordinates), key=VK, ctrl=VK and alt=VK (hexadecimal), type=TEXT,
// shot=PATH (the screen as PNG), quit=1 (also menu=quit).
class Script
{
    struct Step { double time; std::string action, value; };
    std::vector<Step> steps;
    size_t at = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    static bool Window(const std::string &name)
    {
        unsigned windows[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count)) return false;
        for (unsigned i = 0; i < count; ++i)
        {
            char klass[256];
            User::GetClassName(windows[i], klass, sizeof klass);
            if (name == klass && User::IsVisible(windows[i])) return true;
        }
        return false;
    }

  public:
    Script()
    {
        const char *environment = getenv("NEO98_HOST_INPUT");
        if (!environment) return;
        std::string text = environment;
        for (size_t from = 0; from < text.size();)
        {
            auto end = text.find(';', from);
            if (end == std::string::npos) end = text.size();
            auto entry = text.substr(from, end - from);
            from = end + 1;
            auto colon = entry.find(':'), equals = entry.find('=');
            if (colon == std::string::npos || equals == std::string::npos || equals <= colon) continue;
            steps.push_back({strtod(entry.substr(0, colon).c_str(), nullptr), entry.substr(colon + 1, equals - colon - 1), entry.substr(equals + 1)});
        }
    }
    // Returns false when the script asks to quit.
    bool Pump(X86::Application &app, const std::function<bool(const std::string &)> &shot)
    {
        if (at == steps.size()) return true;
        const auto now = std::chrono::steady_clock::now();
        const auto &step = steps[at];
        if (std::chrono::duration<double>(now - start).count() < step.time) return true;
        if (step.action == "wait")
        {
            if (!Window(step.value)) return true;
            start = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(step.time));
        }
        else if (step.action == "shot") shot(step.value);
        else if (step.action == "quit" || (step.action == "menu" && step.value == "quit")) { ++at; return false; }
        else if (step.action == "type")
            for (unsigned char character : step.value) { app.Key(0, character, false, false); app.KeyUp(0); }
        else if (step.action == "key" || step.action == "ctrl" || step.action == "alt")
        {
            const unsigned key = strtoul(step.value.c_str(), nullptr, 16);
            unsigned scan = 0; bool extended = false;
            for (unsigned usage = 1; usage < 128; ++usage)
            {
                const auto mapped = UsbKeys::Map(usage, false, true);
                if (mapped.vk == key) { scan = mapped.scan; extended = mapped.extended; break; }
            }
            // Alt is a key of its own for Windows (WM_SYSKEYDOWN), not only a state.
            const bool alt = step.action == "alt";
            app.KeyboardModifiers(step.action == "ctrl" ? 1 : alt ? 4 : 0);
            if (alt) app.RawKey(0x12, 0, true, 0x38, false);
            app.RawKey(key, key == 0x0d && !alt ? '\r' : 0, true, scan, extended);
            app.RawKey(key, 0, false, scan, extended);
            if (alt) app.RawKey(0x12, 0, false, 0x38, false);
            app.KeyboardModifiers(0);
        }
        else if (step.action == "click" || step.action == "dblclick" || step.action == "move")
        {
            int x = 0, y = 0;
            if (sscanf(step.value.c_str(), "%d,%d", &x, &y) == 2)
            {
                app.MouseButtons(x, y, 0);
                const unsigned clicks = step.action == "dblclick" ? 2 : step.action == "click" ? 1 : 0;
                for (unsigned i = 0; i < clicks; ++i) { app.MouseButtons(x, y, 1); app.MouseButtons(x, y, 0); }
            }
        }
        else if (step.action == "drag")
        {
            // drag=X1,Y1,X2,Y2: press, move in steps, release.
            int x1, y1, x2, y2;
            if (sscanf(step.value.c_str(), "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4)
            {
                app.MouseButtons(x1, y1, 0); app.MouseButtons(x1, y1, 1);
                for (int i = 1; i <= 8; ++i) app.MouseButtons(x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8, 1);
                app.MouseButtons(x2, y2, 0);
            }
        }
        else fprintf(stderr, "Unknown test input: %s\n", step.action.c_str());
        fprintf(stderr, "NEO98 KMS input %s=%s\n", step.action.c_str(), step.value.c_str());
        ++at;
        return true;
    }
};

bool WritePng(const std::string &path, const HostKms::Frame &frame)
{
    std::vector<uint32_t> rows(size_t(frame.width) * frame.height);
    for (unsigned y = 0; y < frame.height; ++y)
        memcpy(&rows[size_t(y) * frame.width], frame.pixels + size_t(y) * frame.stride, frame.width * 4);
    FILE *file = fopen(path.c_str(), "wb");
    if (!file) return false;
    CrashFiles::FileSink sink(file);
    const bool okay = CrashReport::WritePng(sink, reinterpret_cast<const unsigned char *>(rows.data()), int(frame.width), int(frame.height));
    fclose(file);
    return okay;
}
} // namespace

int main(int argc, char **argv)
{
    LinuxHost::Options options;
    if (!LinuxHost::Parse(argc, argv, options)) { LinuxHost::Help(true); return 2; }
    if (options.help) { LinuxHost::Help(true); return 0; }
    signal(SIGINT, Signal);
    signal(SIGTERM, Signal);
    // The screen first: without it there is nothing to start.
    auto output = HostKms::Choose();
    std::string error;
    if (!output->Open(error)) { fprintf(stderr, "NEO98 KMS: %s\n", error.c_str()); return 1; }
    fprintf(stderr, "NEO98 KMS output %s\n", output->Describe().c_str());
    // Kodi draws for NEO98 through a Wayland socket of its own
    // (tools/host_wayland/compositor.h); NEO98 stays the only program on the screen.
    HostWayland::Compositor compositor;
    {
        const auto frame = output->Back();
        if (!compositor.Start(output->RenderNode(), frame.width, frame.height, output->Refresh(), error))
            fprintf(stderr, "NEO98 KMS: no compositor for Kodi: %s\n", error.c_str());
    }
    // Kodi's picture for the video area, copied by the GPU.
    HostWayland::Video video;
    if (!compositor.Socket().empty() && !video.Start(compositor.RenderNode(), error))
        fprintf(stderr, "NEO98 KMS: Kodi's picture is copied without the GPU: %s\n", error.c_str());
    // NEO98_TEST_COMPOSITOR=fullscreen shows the client's picture over the
    // whole screen without Kodi and Media Center (tools/test_compositor_host.py).
    const bool test_fullscreen = getenv("NEO98_TEST_COMPOSITOR") && !strcmp(getenv("NEO98_TEST_COMPOSITOR"), "fullscreen");
    // The keyboard and mouse belong to the screen: a picture in memory (the
    // tests) reads none, also not the keys of a desktop that runs beside it.
    HostKms::Input input;
    if (output->Fd() >= 0 && !input.Open(error)) { fprintf(stderr, "NEO98 KMS: %s\n", error.c_str()); return 1; }
    // Sound and controllers through SDL, without its video.
    if (SDL_Init(SDL_INIT_EVENTS)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    struct Sdl { ~Sdl() { SDL_Quit(); } } sdl;
    HostDeviceService::Service hardware;
    hardware.Install();
#ifdef NEO98_WITH_HOST_MEDIA
    // Kodi's catalogue for Media Center 98, only in a
    // build with NEO98_WITH_HOST_MEDIA=1. A Kodi this host starts draws into
    // the compositor, and its video shows in Media Center's video area or
    // over the whole screen; Kodi itself never takes the screen.
    HostMediaService::Service media;
    media.Install();
    if (!compositor.Socket().empty()) media.UseCompositor(compositor.Socket(), compositor.RuntimeDir());
    std::atomic<bool> kodi_inside{false};
    media.embeds = [&] { return kodi_inside.load(); };
#endif
    static LinuxHost::Session session(argc, argv, options);
    struct SessionCleanup { LinuxHost::Session &session; ~SessionCleanup() { session.Stop(); } } session_cleanup{session};
    static SessionEnd session_end;
    X86::Application::SetSessionEndHook(&session_end, EndSession);
    static HostAudio::Device audio;
    audio.Install();
    X86::Application::SetGpuBackend(HostGpu::Install());
    if (!session.Start(true, {}, [&](X86::Application &, const unsigned short *) {
#ifdef NEO98_WITH_HOST_MEDIA
            if (!HostMediaSeed::Seed()) fprintf(stderr, "NEO98 HOST media: Media Center 98 is missing beside the host program\n");
#endif
            return 0;
        }))
        return session.forwarded ? 0 : 1;
    User::ShowPointer(true);

    const bool german = !(getenv("NEO98_KEYMAP") && !strcmp(getenv("NEO98_KEYMAP"), "US"));
    unsigned modifiers = 0, buttons = 0;
    bool numlock = true, running = true;
    double pointer_x = 0, pointer_y = 0;
    HostKms::Placement placement;
    {
        const auto frame = output->Back();
        input.width = frame.width;
        input.height = frame.height;
        pointer_x = frame.width / 2.0;
        pointer_y = frame.height / 2.0;
    }
    auto send_pointer = [&] {
        if (!placement.width || !placement.height) return;
        pointer_x = std::clamp(pointer_x, double(placement.x), double(placement.x + placement.width - 1));
        pointer_y = std::clamp(pointer_y, double(placement.y), double(placement.y + placement.height - 1));
        session.app->MouseButtons(placement.GuestX(int(pointer_x)), placement.GuestY(int(pointer_y)), buttons);
    };
    HostKms::Sink sink;
    sink.key = [&](unsigned usage, bool down) {
        static const unsigned bits[8] = {1, 2, 4, 8, 16, 32, 64, 128};
        if (usage >= 0xe0 && usage <= 0xe7)
        {
            if (down) modifiers |= bits[usage - 0xe0];
            else modifiers &= ~bits[usage - 0xe0];
        }
        if (usage == 0x53 && down) numlock = !numlock;
        // Ctrl+Alt+Backspace ends NEO98, there is no window to close.
        if (down && usage == 0x2a && (modifiers & 0x11) && (modifiers & 0x44)) { running = false; return; }
        unsigned usb = modifiers;
        if (german && (modifiers & 64)) usb |= 1;
        session.app->KeyboardModifiers(usb);
        const auto key = UsbKeys::Map(usage, german, numlock);
        if (key.vk) session.app->RawKey(key.vk, 0, down, key.scan, key.extended);
    };
    sink.move = [&](double dx, double dy) {
        // One guest pixel per unit of the mouse, whatever the screen's size.
        const double scale = placement.guest_width ? double(placement.width) / placement.guest_width : 1;
        pointer_x += dx * scale;
        pointer_y += dy * scale;
        send_pointer();
    };
    sink.place = [&](double x, double y) { pointer_x = x; pointer_y = y; send_pointer(); };
    sink.buttons = [&](unsigned held) { buttons = held; send_pointer(); };
    sink.wheel = [&](int notches) { session.app->MouseWheel(notches); };

    Script script;
    unsigned last_serial = ~0u, last_video = ~0u;
    // Kodi's buffer on the screen without a copy, and the one before it,
    // which goes back to Kodi once the flip away from it is done.
    unsigned screen_serial = 0, release_serial = 0;
    const void *last_console = nullptr;
    auto last_present = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto render = [&](bool force) {
        if (!output->Ready()) return;
        if (release_serial) { compositor.Unhold(release_serial); release_serial = 0; }
        bool dirty = User::Render() || force;
        dirty |= last_serial != User::FrameSerial();
        last_serial = User::FrameSerial();
        const auto *console = session.app->ConsolePixels();
        dirty |= console != last_console || console;
        last_console = console;
        int width = 800, height = 600;
        const uint32_t *pixels = reinterpret_cast<const uint32_t *>(console);
        if (!pixels)
        {
            User::PixelsSize(width, height);
            pixels = reinterpret_cast<const uint32_t *>(User::Pixels());
        }
        const auto frame = output->Back();
        placement = HostKms::Place(unsigned(width), unsigned(height), frame.width, frame.height);
        // Where Kodi's picture shows : Media Center 98's
        // video area in the key colour, or with `fill` the whole screen.
        struct Area { int x, y, width, height; bool fill; };
        std::vector<Area> areas;
        double aspect = 0;
        if (test_fullscreen) { if (compositor.Serial()) areas.push_back({0, 0, width, height, true}); }
#ifdef NEO98_WITH_HOST_MEDIA
        else if (!console)
        {
            for (const auto &a : media.VideoAreas()) areas.push_back({a.x, a.y, a.width, a.height, a.fill});
            aspect = media.VideoAspect();
        }
#endif
        video.Feed(compositor, aspect, !areas.empty());
        const unsigned video_serial = areas.empty() ? 0 : video.Serial();
        dirty |= video_serial != last_video;
        last_video = video_serial;
        const auto now = std::chrono::steady_clock::now();
        if (!dirty && now - last_present < std::chrono::seconds(1)) return;
        // Kodi draws at the size of the screen; the area shows the video's
        // part of it.
        compositor.Configure(frame.width, frame.height);
        // The whole screen with nothing of Windows on it: Kodi's buffer
        // itself on the real screen, no copy.
        bool covered = !areas.empty() && areas[0].fill;
        if (covered && !test_fullscreen)
            for (size_t i = 0, n = size_t(width) * height; i < n && covered; ++i) covered = (pixels[i] & 0xffffff) == HM_VIDEO_KEY;
        HostWayland::Scanout buffer;
        if (covered && compositor.Buffer(buffer) &&
            (buffer.serial == screen_serial ||
             output->PresentBuffer({buffer.fd, buffer.offset, buffer.stride, buffer.width, buffer.height, buffer.format, buffer.modifier})))
        {
            if (buffer.serial != screen_serial)
            {
                compositor.Hold(buffer.serial);
                release_serial = screen_serial;
                screen_serial = buffer.serial;
            }
            last_present = now;
            return;
        }
        // Otherwise the GPU's copy of the video's part.
        static std::vector<uint32_t> picture;
        static unsigned picture_w = 0, picture_h = 0, picture_serial = 0;
        // Kodi's window gone: none of its pictures stays.
        if (areas.empty() || !compositor.Serial()) { picture.clear(); picture_w = picture_h = picture_serial = 0; }
        else video.Latest([&](const uint32_t *p, unsigned w, unsigned h) { picture.assign(p, p + size_t(w) * h); picture_w = w; picture_h = h; }, picture_serial);
        if (!areas.empty() && test_fullscreen) std::fill(frame.pixels, frame.pixels + size_t(frame.stride) * frame.height, HM_VIDEO_KEY);
        else HostKms::Draw(frame, pixels, placement);
        for (const auto &a : areas)
        {
            const HostKms::Box box = a.fill ? HostKms::Box{0, 0, int(frame.width), int(frame.height)}
                                            : HostKms::Box{placement.ScreenX(a.x), placement.ScreenY(a.y), placement.ScreenX(a.x + a.width), placement.ScreenY(a.y + a.height)};
            HostKms::DrawKeyed(frame, picture.empty() ? nullptr : picture.data(), picture_w, picture_h, picture_w, box, HM_VIDEO_KEY,
                               a.fill ? &placement : nullptr);
        }
        output->Present();
        if (screen_serial) { release_serial = screen_serial; screen_serial = 0; }
        last_present = now;
        // NEO98_MEDIA_FPS=1: how many of Kodi's pictures reached the screen
        // each second (tools/test_media_kms.py).
        static const bool fps = getenv("NEO98_MEDIA_FPS") && !strcmp(getenv("NEO98_MEDIA_FPS"), "1");
        static unsigned shown = 0, shown_serial = 0;
        static auto second = now;
        if (fps && picture_serial && picture_serial != shown_serial) { shown_serial = picture_serial; ++shown; }
        if (fps && now - second >= std::chrono::seconds(1))
        {
            if (shown) fprintf(stderr, "NEO98 HOST video fps=%u\n", shown);
            shown = 0; second = now;
        }
    };
    auto shot = [&](const std::string &path) {
        render(true);
        // The image output shows at once; the screen after its flip.
        while (!output->Ready()) { pollfd p{output->Fd(), POLLIN, 100}; poll(&p, 1, 100); }
        return WritePng(path, output->Front());
    };

    const auto start = std::chrono::steady_clock::now();
    bool expired = false;
    while (running && !interrupted && !session.Finished())
    {
        input.Dispatch(sink);
        compositor.Dispatch();
        if (getenv("NEO98_KMS_LOOP_STATS"))
        {
            static unsigned loops = 0; static auto second = std::chrono::steady_clock::now();
            ++loops;
            if (std::chrono::steady_clock::now() - second >= std::chrono::seconds(1))
            { fprintf(stderr, "NEO98 KMS loops=%u\n", loops); loops = 0; second = std::chrono::steady_clock::now(); }
        }
        hardware.Pump(*session.app, true);
#ifdef NEO98_WITH_HOST_MEDIA
        kodi_inside = compositor.Clients() > 0;
        media.Pump();
#endif
        const bool idle = session.app->Pump(8) == X86::Application::Idle;
        render(false);
        if (!script.Pump(*session.app, shot)) running = false;
        unsigned flags = 0, code = 0;
        if (session_end.requested || X86::Application::SessionCommitted(flags, code)) running = false;
        if (options.seconds && std::chrono::steady_clock::now() - start >= std::chrono::seconds(options.seconds)) { expired = true; break; }
        // Nothing to run: wait for input, a flip or Kodi, at most a few
        // milliseconds for the guest's timers.
        if (idle || X86::Application::SessionWaitingForInput())
        {
            pollfd fds[4] = {{input.Fd(), POLLIN, 0}, {output->Fd(), POLLIN, 0}, {compositor.Fd(), POLLIN, 0}, {video.Fd(), POLLIN, 0}};
            poll(fds, 4, 4);
        }
    }
    if (expired || !options.quiet) session.Describe();
    if (!options.screenshot.empty() && !shot(options.screenshot)) return 1;
    if (session.app->Status() == X86::Application::Failed) { fprintf(stderr, "%s\n", session.app->Error()); session.Report("guest-failed"); }
    if (interrupted) return 130;
    if (expired) return 124;
    // Ended by Ctrl+Alt+Backspace, the script or Windows' own shutdown: 0;
    // otherwise the program's code, as the other hosts answer.
    return running ? session.Result() : 0;
}
