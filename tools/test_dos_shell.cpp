// SPDX-License-Identifier: GPL-2.0-or-later
// A DOS program started by the shell, the way a double click starts it:
// EXPLORER.EXE runs as the shell of the imported installation, the Run key
// of this session names a PIF of the collection, and Explorer starts it
// through ShellExecute. SHELL32 asks PifMgr for the program's properties on
// that way, then calls CreateProcess, which opens the
// DOS machine. The test waits for the tty window with the
// PIF's title and a picture in it, presses a key, and closes it.
// test-dos-shell EXPLORER.EXE FONT --system-tree --drive-c DIR --drive D=DIR
// --pif "D:\OMF.PIF" --title "One Must Fall" [--output DIR]
#define main original_probe_main
#include "probe_application.cpp"
#undef main
#include "host_dos_vm.h"
using App = X86::Application;
static App *shell;
static const char *out_dir = "build/dos-shell";
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "DOS SHELL FAILED: %s\n", why);
    if (shell) sampling::stall_dump(*shell);
    snapshot_windows();
    char path[512];
    snprintf(path, sizeof path, "%s/failure.ppm", out_dir);
    screenshot(path);
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
static unsigned lit_client_pixels(unsigned hwnd, Gdi::RECT &client, unsigned &black)
{
    client = {};
    black = 0;
    if (!User::ClientScreenRect(hwnd, &client)) return 0;
    User::Render();
    const auto *pixels = User::Pixels();
    unsigned lit = 0;
    for (int y = client.top < 0 ? 0 : client.top; y < client.bottom && y < 600; ++y)
        for (int x = client.left < 0 ? 0 : client.left; x < client.right && x < 800; ++x)
        {
            const auto *p = pixels + (y * 800 + x) * 4;
            if (p[0] | p[1] | p[2]) ++lit;
            else ++black;
        }
    return lit;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const char *pif = nullptr, *title = nullptr;
    for (int i = 3; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--pif") && i + 1 < argc) pif = argv[++i];
        else if (!strcmp(argv[i], "--title") && i + 1 < argc) title = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc) out_dir = argv[++i];
    }
    require(pif && title, "--pif and --title name the program and the expected window title");
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C:");
    require(HostVolume::Mount(drive_options), "mount the writable C: and the program drive");
    require(RegistryFile::Attach(argc, argv, drive_options.drive_c),
            "attach the registry journal with the autostart cleanup");
    // The DOS machine sees the same drives as the Windows side.
    std::vector<HostDosVm::Drive> drives;
    drives.push_back({'C', tree_options.root ? tree_options.root : SystemTreeHost::DefaultRoot(), true, drive_options.drive_c});
    for (unsigned i = 0; i < drive_options.count; ++i)
        if (drive_options.extra[i].letter && drive_options.extra[i].letter != 'C' && !drive_options.extra[i].cdrom)
            drives.push_back({drive_options.extra[i].letter, drive_options.extra[i].directory, false});
    HostDosVm::Install(logger, drives.data(), (unsigned)drives.size());
    {
        auto &store = Registry::System();
        unsigned key = 0;
        bool created = false;
        require(!store.Open(store.roots[2], "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                            true, key, created), "the Run key of the imported hive is there");
        require(!store.Set(key, "DosProgram", 1, pif, (unsigned)strlen(pif) + 1), "write the Run entry");
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    shell = new App(logger);
    require(shell->Start(exe.data(), exe.size(), font.data(), font.size(), "EXPLORER.EXE"), "explorer start");
    unsigned tray = 0, tasks = 0;
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
        tasks = find("MSTaskSwClass");
        return tray && find("Progman") && !visible_dialog("RUNDLL");
    }, "Explorer builds the shell", 90000);
    (void)tasks;
    // The DOS window of the Run entry, with the title of the PIF.
    unsigned tty = 0;
    char caption[256] = {};
    until([&] {
        tty = find("tty");
        if (tty) User::GetWindowText(tty, caption, sizeof caption);
        return tty != 0;
    }, "the Run entry opens the MS-DOS window", 90000);
    printf("DOS SHELL tty=%u title=%s\n", tty, caption);
    Gdi::RECT client = {};
    unsigned lit = 0, black = 0;
    // A text screen lights a few hundred pixels on black; a graphics
    // program may fill the whole client area, so only the lit count counts.
    until([&] {
        lit = lit_client_pixels(tty, client, black);
        return lit >= 100;
    }, "the DOS program draws into the window", 60000);
    printf("DOS SHELL client=%d,%d,%d,%d lit=%u black=%u foreground=%u\n", client.left, client.top, client.right,
           client.bottom, lit, black, User::GetForegroundWindow() == tty);
    char path[512];
    snprintf(path, sizeof path, "%s/running.ppm", out_dir);
    screenshot(path);
    require(!strcmp(caption, title), "the window carries the title of the PIF");
    // Close it the way the user does: the close box, here WM_CLOSE.
    User::PostMessage(tty, 0x0010, 0, 0);
    until([&] { return !User::IsWindow(tty); }, "the MS-DOS window closes", 30000);
    snprintf(path, sizeof path, "%s/closed.ppm", out_dir);
    screenshot(path);
    App::TerminateChildren(0);
    for (unsigned i = 0; i < 64 && App::LiveChildren(); ++i) App::PumpChildren(8);
    HostDosVm::Uninstall();
    puts("DOS SHELL PASS");
    return 0;
}
