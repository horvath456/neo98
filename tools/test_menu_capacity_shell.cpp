// SPDX-License-Identifier: GPL-2.0-or-later
// WordPad and the Control Panel, started out of the Start menu of the shell
// that runs the whole imported installation.
// The shell alone holds a few menus, the Start menu a few more, and every
// program that starts from it brings its own. Before the menu table grew, the
// 64 slots of the old handle encoding ran out here and LoadMenu answered zero,
// which stopped WordPad in its MFC frame creation with "Menu capacity
// exceeded". The test drives the real menu band of the installation with the
// mouse, so the count it reaches is the count of a real session.
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *shell;
static unsigned peak_menus = 0;
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "MENU CAPACITY FAILED: %s\n", why);
    if (shell) sampling::stall_dump(*shell);
    // A failure that starts in a child process (the second Explorer of the
    // /idlist hand-off) needs the child's threads too.
    for (unsigned i = 0; shell && i < X86::Application::ChildCapacity; ++i) {
        bool done = false;
        if (auto *child = shell->ChildApplication(i, done); child && !done) {
            printf("MENU CAPACITY child slot=%u pid=%u\n", i, child->ProcessId());
            sampling::stall_dump(*child);
        }
    }
    // The stall dump names the blocked threads; the crash report adds the API
    // ring buffer of every thread, for scheduler diagnostics.
    if (shell) write_crash_report(*shell, why);
    snapshot_windows();
    screenshot("build/menu-capacity-shell/failure.ppm");
    fflush(stdout);
    exit(1);
}
static void pump(unsigned milliseconds)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        shell->Pump(32);
        App::PumpChildren(8);
        if (User::LiveMenus() > peak_menus) peak_menus = User::LiveMenus();
        require(shell->Status() != App::Failed, shell->Error());
    } while (std::chrono::steady_clock::now() < end);
}
// Wait until the shell makes no API call for the given span, or give up.
static void settle(unsigned quiet_ms, unsigned limit_ms)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit_ms);
    unsigned last = shell->ApiCalls();
    auto quiet_since = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < end) {
        pump(50);
        const unsigned now = shell->ApiCalls();
        if (now != last) { last = now; quiet_since = std::chrono::steady_clock::now(); continue; }
        if (std::chrono::steady_clock::now() - quiet_since >= std::chrono::milliseconds(quiet_ms)) return;
    }
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
// The first visible window whose title ends in the given suffix, such as
// "- WordPad" for the document frame MFC opens.
static unsigned find_title(const char *suffix)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    const size_t want = strlen(suffix);
    for (unsigned i = 0; i < n; ++i) {
        char text[256] = {};
        if (!User::IsVisible(nodes[i]) || User::Parent(nodes[i])) continue;
        User::GetWindowText(nodes[i], text, sizeof text);
        const size_t have = strlen(text);
        if (have >= want && !strcmp(text + have - want, suffix)) return nodes[i];
    }
    return 0;
}
static void dump(const char *tag)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    printf("MENU CAPACITY %s menus=%u peak=%u capacity=%u windows=%u children=%u\n", tag,
           User::LiveMenus(), peak_menus, User::MenuCapacity(), n, App::LiveChildren());
    fflush(stdout);
}
static void shot(const char *tag)
{
    char path[160];
    snprintf(path, sizeof path, "build/menu-capacity-shell/%s.ppm", tag);
    screenshot(path);
}
// The Quick Launch toolbar is the first ToolbarWindow32 inside Shell_TrayWnd.
// Its first button is Show Desktop.scf.
static unsigned quick_launch()
{
    const unsigned tray = find("Shell_TrayWnd");
    if (!tray) return 0;
    Gdi::RECT t{};
    if (!User::GetWindowRect(tray, &t)) return 0;
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[128];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!User::IsVisible(nodes[i]) || strcmp(klass, "ToolbarWindow32")) continue;
        Gdi::RECT r{};
        if (!User::GetWindowRect(nodes[i], &r)) continue;
        if (r.left >= t.left && r.top >= t.top && r.right <= t.right && r.bottom <= t.bottom)
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
// The menu band of the shell is a chain of BaseBar windows, one per level.
static unsigned bars(unsigned *seen = nullptr, unsigned capacity = 0)
{
    unsigned nodes[User::WindowCapacity], n = 0, count = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[64];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!User::IsVisible(nodes[i]) || strcmp(klass, "BaseBar")) continue;
        if (seen && count < capacity) seen[count] = nodes[i];
        ++count;
    }
    return count;
}
// A menu band level is addressed by the distance of the row's middle from the
// top of its frame. The submenus of this installation draw 20 pixel rows with
// the first middle 14 below the frame; the Start menu itself draws the taller
// rows of its large icons, so its offsets are named where they are used.
static int row20(unsigned row) { return 14 + int(row) * 20; }
static void point_at(const Gdi::RECT &level, int dy, int &x, int &y)
{
    x = (level.left + level.right) / 2;
    y = level.top + dy;
}
// Hover a row until its submenu stands on screen, and answer that submenu's
// frame. Win98 opens the child of a menu band row on hover alone; a click is
// the fallback for a row that does not answer the hover in time.
static Gdi::RECT open_row(const Gdi::RECT &level, int dy, const char *tag)
{
    unsigned before[32], previous = bars(before, 32);
    int x = 0, y = 0;
    point_at(level, dy, x, y);
    shell->MouseButtons(x, y, 0);
    for (unsigned i = 0; i < 80 && bars() == previous; ++i) pump(50);
    if (bars() == previous) {
        shell->MouseButtons(x, y, 1);
        shell->MouseButtons(x, y, 0);
        for (unsigned i = 0; i < 60 && bars() == previous; ++i) pump(50);
    }
    require(bars() > previous, tag);
    until([&] { return shell->WaitingForInput() && !User::Rendering() && !User::PendingPaintWindows(nullptr); },
          "menu band level finishes its first paint", 20000);
    pump(300);
    unsigned now[32];
    const unsigned count = bars(now, 32);
    Gdi::RECT opened{};
    for (unsigned i = 0; i < count && i < 32; ++i) {
        bool known = false;
        for (unsigned j = 0; j < previous && j < 32; ++j) known = known || before[j] == now[i];
        if (!known) require(User::GetWindowRect(now[i], &opened), "submenu rect");
    }
    require(opened.right > opened.left, tag);
    dump(tag);
    shot(tag);
    return opened;
}
// Pick a row that starts a program: a plain click, no submenu to wait for.
static void choose_row(const Gdi::RECT &level, int dy)
{
    int x = 0, y = 0;
    point_at(level, dy, x, y);
    shell->MouseButtons(x, y, 0);
    pump(200);
    shell->MouseButtons(x, y, 1);
    pump(60);
    shell->MouseButtons(x, y, 0);
    pump(200);
}
// The rows of the Start menu itself, measured from the top of its frame:
// Windows Update, then Programs, Favorites, Documents, Settings, Find, Help,
// Run and Shut Down under it, 32 pixels apart.
constexpr int StartPrograms = 59, StartSettings = 155;
static Gdi::RECT open_start_menu(unsigned start)
{
    click(start);
    until([&] { return find("BaseBar") && !User::Rendering(); }, "Start menu opens");
    until([&] { return shell->WaitingForInput() && !User::Rendering() && !User::PendingPaintWindows(nullptr); },
          "Start menu finishes its first paint", 20000);
    pump(300);
    Gdi::RECT level{};
    require(User::GetWindowRect(find("BaseBar"), &level), "Start menu rect");
    return level;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    probe_argc = argc;
    probe_argv = argv;
    probe_start = std::chrono::steady_clock::now();
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--crash-report-dir") && i + 1 < argc) crash_report_dir = argv[i + 1];
    if (crash_report_dir) App::SetCrashHook(nullptr, on_crash);
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C: with the shell line");
    require(HostVolume::Mount(drive_options), "mount the writable C: drive");
    // The cleaned registry layer beside the drive directory, replayed by
    // Registry::Restore on the first use of the store; see
    // tools/win98_folder.py.
    require(RegistryFile::Attach(argc, argv, drive_options.drive_c),
            "attach the registry journal with the autostart cleanup");
    auto exe = load(argv[1]), font = load(argv[2]);
    // Diagnostics for NEO98_MENU_LOG logs every API call of the shell,
    // NEO98_MENU_TRACE starts the call trace at the given call count, once the
    // shell stands. Both are off in a normal run.
    shell = new App(getenv("NEO98_MENU_LOG") ? logger : nullptr);
    for (int i = 3; i < argc && strncmp(argv[i], "--", 2); ++i)
        require(shell->AddNativeModule(argv[i]), "native module override");
    require(shell->Start(exe.data(), exe.size(), font.data(), font.size(), "EXPLORER.EXE"), "explorer start");
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
    }, "Explorer builds the shell out of the mounted installation", 60000);
    dump("shell-at-rest");
    if (const char *from = getenv("NEO98_MENU_TRACE")) { shell->trace_calls = true; shell->trace_from = atoi(from); }
    // Start, Programs, Accessories, WordPad. The rows are those of this
    // installation's Start Menu folder; the Start menu itself starts with
    // Windows Update above Programs.
    Gdi::RECT root = open_start_menu(start);
    Gdi::RECT programs = open_row(root, StartPrograms, "programs");
    Gdi::RECT accessories = open_row(programs, row20(1), "accessories");
    choose_row(accessories, row20(9));
    until([&] { return App::LiveChildren() >= 1; }, "the shell starts a program for the WordPad row", 20000);
    unsigned frame = 0;
    until([&] { frame = find_title("- WordPad"); return frame != 0; }, "WordPad opens its frame window", 60000);
    until([&] { return !User::Rendering() && !User::PendingPaintWindows(nullptr); }, "WordPad paints", 30000);
    pump(500);
    dump("wordpad");
    shot("wordpad");
    // A child that died on the way says so here; without the line the missing
    // frame window below looks like a menu problem.
    for (unsigned i = 0; const char *error = App::FailedChild(i); ++i)
        printf("MENU CAPACITY child failed %u %s\n", i, error);
    printf("MENU CAPACITY children started=%u live=%u\n", App::ChildrenStarted(), App::LiveChildren());
    fflush(stdout);
    Gdi::RECT frame_rect{};
    require(User::GetWindowRect(frame, &frame_rect), "WordPad frame rect");
    require(frame_rect.right - frame_rect.left > 300 && frame_rect.bottom - frame_rect.top > 200,
            "the WordPad frame has a real size");
    const unsigned bar = User::GetMenu(frame);
    require(bar != 0, "the WordPad frame carries a menu");
    // File, Edit, View, Insert, Format, Help.
    require(User::MenuItemCount(bar) >= 6, "the WordPad menu bar holds its six popups");
    printf("MENU CAPACITY wordpad frame=%u menu=%u items=%u\n", frame, bar, User::MenuItemCount(bar));
    fflush(stdout);
    // Start, Settings, Control Panel.
    root = open_start_menu(start);
    Gdi::RECT settings = open_row(root, StartSettings, "settings");
    choose_row(settings, row20(0));
    unsigned panel = 0;
    until([&] { panel = find_title("Control Panel"); return panel != 0; }, "the Control Panel folder opens", 60000);
    until([&] { return !User::Rendering() && !User::PendingPaintWindows(nullptr); }, "the Control Panel paints", 30000);
    // The web view of the folder keeps working after its first paint: it runs
    // the script of the template, asks SHELL32 for the applets and draws their
    // icons. A fixed pause caught the window at a different point every run, so
    // this waits until the shell stops calling the API for a second.
    settle(1000, 40000);
    dump("control-panel");
    shot("control-panel");
    // The folder window of the shell hangs its File, Edit, View, Go, Favorites
    // and Help band in the rebar, not in a menu bar of the frame, so what this
    // checks is the window and the menu the frame does carry.
    const unsigned panel_bar = User::GetMenu(panel);
    require(panel_bar != 0, "the Control Panel window carries a menu");
    Gdi::RECT panel_rect{};
    require(User::GetWindowRect(panel, &panel_rect), "Control Panel rect");
    require(panel_rect.right - panel_rect.left > 300 && panel_rect.bottom - panel_rect.top > 200,
            "the Control Panel window has a real size");
    printf("MENU CAPACITY control-panel frame=%u menu=%u items=%u\n", panel, panel_bar,
           User::MenuItemCount(panel_bar));
    fflush(stdout);
    // Show Desktop must minimize all windows on the first click and restore
    // them on the second. Session-wide task identities prevent unrelated
    // WM_ACTIVATEAPP delivery from resetting that state.
    const unsigned quick = quick_launch();
    require(quick != 0, "the taskbar carries the Quick Launch toolbar");
    Gdi::RECT quick_rect{};
    require(User::GetWindowRect(quick, &quick_rect), "Quick Launch rect");
    const int quick_x = quick_rect.left + 11, quick_y = (quick_rect.top + quick_rect.bottom) / 2;
    auto minimized = [](unsigned handle) { return (User::Style(handle) & 0x20000000) != 0; };
    auto show_desktop = [&] {
        shell->MouseButtons(quick_x, quick_y, 0);
        shell->MouseButtons(quick_x, quick_y, 1);
        pump(30);
        shell->MouseButtons(quick_x, quick_y, 0);
        pump(200);
    };
    printf("MENU CAPACITY quick-launch hwnd=%u x=%d y=%d wordpad=%08x panel=%08x\n", quick, quick_x,
           quick_y, User::Style(frame), User::Style(panel));
    fflush(stdout);
    show_desktop();
    until([&] { return minimized(frame) && minimized(panel); },
          "Show Desktop minimizes the WordPad frame and the folder window", 30000);
    // Wait for composition; otherwise the screenshot still shows the state
    // before the click.
    until([&] { return !User::Rendering() && !User::PendingPaintWindows(nullptr); },
          "the desktop repaints after Show Desktop", 20000);
    dump("show-desktop");
    shot("show-desktop");
    printf("MENU CAPACITY show-desktop-hidden wordpad=%08x panel=%08x\n", User::Style(frame),
           User::Style(panel));
    fflush(stdout);
    require(minimized(frame) && minimized(panel),
            "the desktop stays shown while the shell waits for the second click");
    pump(700); // Two separate clicks, not a double-click on the toolbar.
    show_desktop();
    until([&] { return !minimized(frame) && !minimized(panel); },
          "Show Desktop restores the WordPad frame and the folder window", 30000);
    until([&] { return !User::Rendering() && !User::PendingPaintWindows(nullptr); },
          "the windows repaint after the second click", 20000);
    dump("show-desktop-restored");
    shot("show-desktop-restored");
    printf("MENU CAPACITY show-desktop wordpad=%08x panel=%08x\n", User::Style(frame),
           User::Style(panel));
    fflush(stdout);
    require(peak_menus > 64, "this session really passes the old ceiling of 64 menus");
    require(peak_menus < User::MenuCapacity(), "the menu table still has room");
    const unsigned running = App::LiveChildren();
    const unsigned terminated = App::TerminateChildren(0);
    for (unsigned i = 0; i < 64 && App::LiveChildren(); ++i) App::PumpChildren(8);
    printf("MENU CAPACITY autostart running=%u terminated=%u left=%u peak=%u capacity=%u\n", running,
           terminated, App::LiveChildren(), peak_menus, User::MenuCapacity());
    require(!App::LiveChildren(), "every child process ends with the session");
    puts("MENU CAPACITY SHELL PASS");
    return 0;
}
