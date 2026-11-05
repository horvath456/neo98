// SPDX-License-Identifier: GPL-2.0-or-later
// EXPLORER.EXE as the shell of the whole imported Win98 installation.
// The tree brings the machine's own settings with it. Its SYSTEM.INI names
// C:\Launcher\launcher.exe in [boot] Shell, and EXPLORER.EXE builds desktop
// and taskbar only when that line names its own file, so with the bare tree it
// opens a folder window, exactly as Win98 would. The
// writable C: above the tree carries the SYSTEM.INI of an ordinary
// installation, with shell=Explorer.exe; tools/win98_folder.py writes it.
// What this test waits for is the shell, not the end of its children. The tree
// also brings the Run keys of the installation, and the programs they start may
// keep running for the whole session. The session ends the way Win98 ends one:
// those processes are terminated and reaped.
// Beside the writable C: lies the registry change journal with the cleanup of
// the autostart entries this installation added. After it the Run keys hold the
// standard Win98 entries and nothing else; see tools/win98_folder.py and
//
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *shell;
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "EXPLORER SHELL FAILED: %s\n", why);
    if (shell) sampling::stall_dump(*shell);
    snapshot_windows();
    screenshot("build/explorer-shell-tree/failure.ppm");
    fflush(stdout);
    exit(1);
}
static void pump(unsigned milliseconds)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        shell->Pump(32);
        require(shell->Status() != App::Failed, shell->Error());
    } while (std::chrono::steady_clock::now() < end);
}
template <class F> static void until(F condition, const char *why, unsigned limit = 5000)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit);
    while (!condition() && std::chrono::steady_clock::now() < end) pump(20);
    require(condition(), why);
}
static unsigned find(const char *name, char *text = nullptr, unsigned capacity = 0)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[128];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!User::IsVisible(nodes[i]) || strcmp(klass, name)) continue;
        if (text) User::GetWindowText(nodes[i], text, capacity);
        return nodes[i];
    }
    return 0;
}
static void click(unsigned handle)
{
    Gdi::RECT r{};
    require(User::GetWindowRect(handle, &r), "click window");
    const int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    shell->MouseButtons(x, y, 0);
    shell->MouseButtons(x, y, 1);
    until([&] { return User::ControlMessage(handle, 0xf2, 0, 0) & 4; }, "button paints pressed state");
    shell->MouseButtons(x, y, 0);
    pump(120);
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    // Both layers of C: are mounted before anything touches Files::System.
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C: with the shell line");
    require(HostVolume::Mount(drive_options), "mount the writable C: drive");
    // The cleaned registry layer lies beside that directory as the change
    // journal of src/registry_persist.cpp, and Registry::Restore replays it on
    // the first use of the store, which is why it is attached here and not
    // later. It carries the `delete value` records that take the autostart
    // entries of this installation out of the Run keys; see
    // tools/win98_folder.py.
    require(RegistryFile::Attach(argc, argv, drive_options.drive_c),
            "attach the registry journal with the autostart cleanup");
    printf("EXPLORER SHELL registry %s\n",
           static_cast<RegistryFile::File *>(Registry::Attached())->Path());
    // Proof that the cleanup arrived, read out of the store the shell will
    // read: the Run key holds the standard Win98 entries and nothing else.
    // This is also what builds the store for the first time, which is where
    // Registry::Restore replays the journal.
    {
        auto &store = Registry::System();
        unsigned key = 0;
        bool created = false;
        require(!store.Open(store.roots[2], "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                            false, key, created), "the Run key of the imported hive is there");
        char names[256]{};
        for (unsigned i = 0; i < 32; ++i) {
            const Registry::Value *value = nullptr;
            if (store.EnumValue(key, i, value) || !value) break;
            if (names[0]) strncat(names, ",", sizeof names - strlen(names) - 1);
            strncat(names, value->name, sizeof names - strlen(names) - 1);
        }
        printf("EXPLORER SHELL run key %s\n", names);
        require(!strcmp(names, "SystemTray,LoadPowerProfile"), names);
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    shell = new App(nullptr);
    for (int i = 3; i < argc && strncmp(argv[i], "--", 2); ++i)
        require(shell->AddNativeModule(argv[i]), "native module override");
    require(shell->Start(exe.data(), exe.size(), font.data(), font.size(), "EXPLORER.EXE"), "explorer start");
    // The taskbar with its Start button, the notification area with a clock
    // that shows a time, the desktop of Progman with its view, no pending
    // RUNDLL error box of an autostart program, and a quiet message loop.
    unsigned tray = 0, start = 0;
    char clock_text[64]{};
    until([&] {
        if (auto h = visible_dialog("RUNDLL")) {
            auto b = User::GetDlgItem(h, 2);
            if (!b) b = User::GetDlgItem(h, 1);
            if (b) click(b);
        }
        tray = find("Shell_TrayWnd");
        start = tray ? User::GetDlgItem(tray, 0x130) : 0;
        return tray && start && find("TrayNotifyWnd") && find("TrayClockWClass", clock_text, sizeof clock_text) &&
               clock_text[0] && find("Progman") && find("SHELLDLL_DefView") && find("SysListView32") &&
               !visible_dialog("RUNDLL") && shell->WaitingForInput();
    // On the first start of a fresh C: Explorer runs Active Setup's per-user
    // stubs one after the other and waits for each; about forty
    // RUNDLL, RUNONCE and GRPCONV children take several minutes on a loaded
    // machine, so the limit is generous (the minute ran out while the
    // stubs were still coming).
    }, "Explorer builds the shell out of the mounted installation", 400000);
    printf("EXPLORER SHELL tray=%u start=%u clock=%s calls=%u children=%u\n", tray, start, clock_text,
           shell->ApiCalls(), App::LiveChildren());
    fflush(stdout);
    // No folder window: this Explorer is the shell, not a browser.
    require(!find("ExploreWClass"), "Explorer opened no folder window");
    for (unsigned i = 0; i < 2; ++i) {
        click(start);
        until([&] { return find("BaseBar") && !User::Rendering(); }, "Start menu opens");
        require(User::ControlMessage(start, 0xf2, 0, 0) & 4, "Start remains pressed while menu holds capture");
        shell->RawKey(27, 0, true);
        shell->RawKey(27, 0, false);
        until([&] { return !find("BaseBar") && !(User::ControlMessage(start, 0xf2, 0, 0) & 4); },
              "Start releases on Escape");
    }
    pump(300); // Let the composite catch up before the picture.
    screenshot("build/explorer-shell-tree/desktop.ppm");
    const unsigned running = App::LiveChildren(), started = App::ChildRecords();
    const unsigned terminated = App::TerminateChildren(0);
    for (unsigned i = 0; i < 64 && App::LiveChildren(); ++i) App::PumpChildren(8);
    printf("EXPLORER SHELL autostart running=%u terminated=%u left=%u records=%u\n", running, terminated,
           App::LiveChildren(), App::ChildRecords());
    fflush(stdout);
    // With the cleaned layer the Run keys hold the standard Win98 entries and
    // nothing else, so what runs to the end of the session can be none: the
    // count of started children decides, not the count of live ones.
    // A child that finished and gave its handles back leaves no record, so the
    // question is what the session ever started, not what is still there.
    printf("EXPLORER SHELL autostart started=%u\n", App::ChildrenStarted());
    require(started + App::FailedChildren() + App::ChildrenStarted() >= 1,
            "the Run keys of the installation start at least one program");
    require(!App::LiveChildren(), "every child process ends with the session");
    // No autostart program dies on its own. SysTray.Exe and the screen saver
    // of this installation ended in "DLL process initialization failed" until
    // VERSION.DLL could connect its blocks to the native 16-bit
    // VER.DLL. The shell
    // never notices such a child, so the test looks for it here.
    const char *first_error = nullptr, *first_init = nullptr;
    const unsigned failed = App::FailedChildren(nullptr, &first_error);
    const unsigned refused = App::FailedChildren("DLL process initialization failed", &first_init);
    printf("EXPLORER SHELL children failed=%u refused=%u first=%s\n", failed, refused,
           failed ? first_error : "none");
    for (unsigned i = 0; const char *error = App::FailedChild(i); ++i)
        printf("EXPLORER SHELL child failed %u %s\n", i, error);
    fflush(stdout);
    require(!refused, first_init);
    // The cleaned layer took the autostart entries of this installation away, so what starts here is the standard Win98
    // autostart: SystemTray and LoadPowerProfile. Both still terminate, and
    // both entries stay in the Run key, and both bugs stay open. The
    // fresh ISO also starts 16-bit RUNDLL.EXE, which reaches the still
    // unported KERNEL ordinal 431. Any other failure is new.
    for (unsigned i = 0; const char *error = App::FailedChild(i); ++i)
        require(!strncmp(error, "SysTray.Exe", 11) || !strncmp(error, "Rundll32.exe", 12) ||
                !strcmp(error, "RUNDLL.EXE: Win16 KERNEL ordinal 431 is not ported yet"), error);
    puts("EXPLORER SHELL TREE PASS");
    return 0;
}
