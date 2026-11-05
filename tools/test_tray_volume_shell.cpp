// SPDX-License-Identifier: GPL-2.0-or-later
// Test the notification-area volume icon with Explorer as shell and
// SYSTRAY.EXE started through the session Run key. SysTray must find the
// mixer device, register through Shell_NotifyIcon and display its icon beside
// the clock.
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *shell;
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "TRAY VOLUME FAILED: %s\n", why);
    if (shell) sampling::stall_dump(*shell);
    snapshot_windows();
    screenshot("build/tray-volume/failure.ppm");
    fflush(stdout);
    exit(1);
}
static void pump(unsigned milliseconds)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        shell->Pump(32);
        App::PumpChildren(8);
        require(shell->Status() != App::Failed, shell->Error());
    } while (std::chrono::steady_clock::now() < end);
}
template <class F> static void until(F condition, const char *why, unsigned limit = 5000)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit);
    while (!condition() && std::chrono::steady_clock::now() < end) pump(20);
    require(condition(), why);
}
static unsigned find(const char *name)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[128];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (User::IsVisible(nodes[i]) && !strcmp(klass, name)) return nodes[i];
    }
    return 0;
}
// Any top-level window of the named class, visible or not, over every process.
// The desktop holds the top-level windows of every process, not only the
// shell's own, so one snapshot is the whole list.
static unsigned any_window(const char *name)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[128];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!strcmp(klass, name)) return nodes[i];
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C:");
    require(HostVolume::Mount(drive_options), "mount the writable C:");
    require(RegistryFile::Attach(argc, argv, drive_options.drive_c),
            "attach the registry journal with the autostart cleanup");
    // This session's only startup entry is SysTray.
    {
        auto &store = Registry::System();
        unsigned key = 0;
        bool created = false;
        require(!store.Open(store.roots[2], "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                            true, key, created), "the Run key of the imported hive is there");
        static const char command[] = "C:\\WINDOWS\\SYSTEM\\SYSTRAY.EXE";
        require(!store.Set(key, "SystemTray", 1, command, sizeof command), "write the Run entry");
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    // --trace-calls enables probe call tracing. Otherwise the run is silent.
    bool trace = false;
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--trace-calls")) trace = true;
    shell = new App(trace ? logger : nullptr);
    shell->trace_calls = trace;
    require(shell->Start(exe.data(), exe.size(), font.data(), font.size(), "EXPLORER.EXE"), "explorer start");
    unsigned tray = 0, notify = 0;
    until([&] {
        if (auto h = visible_dialog("RUNDLL")) {
            auto b = User::GetDlgItem(h, 2);
            if (!b) b = User::GetDlgItem(h, 1);
            if (b) {
                Gdi::RECT r{};
                if (User::GetWindowRect(b, &r)) {
                    const int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
                    shell->MouseButtons(x, y, 0);
                    shell->MouseButtons(x, y, 1);
                    shell->MouseButtons(x, y, 0);
                }
            }
        }
        tray = find("Shell_TrayWnd");
        notify = find("TrayNotifyWnd");
        return tray && notify && find("Progman") && !visible_dialog("RUNDLL");
    }, "Explorer builds the shell with its notification area", 90000);
    // SysTray must stay running when a mixer is available.
    unsigned systray = 0;
    until([&] { systray = any_window("SystemTray_Main"); return systray != 0; },
          "the Run entry starts SYSTRAY.EXE and it keeps its window", 90000);
    Gdi::RECT area{};
    require(User::GetWindowRect(notify, &area), "the notification area has a rectangle");
    printf("TRAY VOLUME systray=%u notify=%u area %d,%d %d,%d\n", systray, notify,
           area.left, area.top, area.right, area.bottom);
    // An icon widens the clock-only notification area and adds pixels in its
    // leftmost sixteen columns.
    unsigned colours = 0, painted = 0;
    auto probe = [&] {
        Gdi::RECT now{};
        if (!User::GetWindowRect(notify, &now)) return;
        area = now;
        const unsigned char *pixels = User::Pixels();
        if (!pixels) return;
        unsigned seen[64] = {};
        colours = painted = 0;
        for (int y = area.top + 2; y < area.bottom - 2 && y < 600; ++y)
            for (int x = area.left + 2; x < area.left + 20 && x < 800; ++x)
            {
                const unsigned char *p = pixels + (unsigned(y) * 800 + unsigned(x)) * 4;
                const unsigned value = unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16;
                ++painted;
                bool known = false;
                for (unsigned i = 0; i < colours; ++i) if (seen[i] == value) { known = true; break; }
                if (!known && colours < 64) seen[colours++] = value;
            }
    };
    until([&] { probe(); return colours >= 4; },
          "the notification area shows an icon beside the clock", 60000);
    screenshot("build/tray-volume/desktop.ppm");
    printf("TRAY VOLUME cell pixels=%u colours=%u area %d,%d %d,%d\n", painted, colours,
           area.left, area.top, area.right, area.bottom);
    require(painted > 100, "the icon cell has pixels");
    // Double-clicking the tray icon starts SNDVOL32.EXE, whose main window
    // class is SNDVOL32.
    {
        const int x = area.left + 9, y = (area.top + area.bottom) / 2;
        for (unsigned i = 0; i < 2; ++i)
        {
            shell->MouseButtons(x, y, 0);
            shell->MouseButtons(x, y, 1);
            shell->MouseButtons(x, y, 0);
            pump(60);
        }
    }
    // SNDVOL32 keeps a hidden SNDVOL32 window and displays its mixer in a
    // Volume Control window.
    unsigned volume_window = 0;
    until([&] {
        volume_window = find("Volume Control");
        return any_window("SNDVOL32") && volume_window != 0;
    }, "the double click opens the mixer window of SNDVOL32", 90000);
    pump(2000);
    screenshot("build/tray-volume/volume.ppm");
    Gdi::RECT window{};
    require(User::GetWindowRect(volume_window, &window), "the volume window has a rectangle");
    printf("TRAY VOLUME sndvol=%u %d,%d %d,%d\n", volume_window, window.left, window.top,
           window.right, window.bottom);
    require(window.right - window.left > 100 && window.bottom - window.top > 100,
            "the volume window has a size");
    App::TerminateChildren(0);
    for (unsigned i = 0; i < 64 && App::LiveChildren(); ++i) App::PumpChildren(8);
    puts("TRAY VOLUME PASS");
    return 0;
}
