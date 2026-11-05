// SPDX-License-Identifier: GPL-2.0-or-later
// Check a Delphi application's taskbar icon through Explorer. Its hidden
// TApplication owner receives WM_SETICON and is reported to the shell.
// Taskbar DrawCaptionTempA requests icon and text from that window; inspect
// the resulting icon pixels.
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *shell;
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "TASKBAR ICON FAILED: %s\n", why);
    if (shell) sampling::stall_dump(*shell);
    snapshot_windows();
    screenshot("build/taskbar-icon/failure.ppm");
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
// Every top-level window with the fields the taskbar reads: class, text,
// style, owner and the icons USER finds for it.
static void dump_top_level()
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        if (User::Parent(nodes[i]) && User::Parent(nodes[i]) != User::GetDesktopWindow()) continue;
        char klass[128] = {}, text[256] = {};
        User::GetClassName(nodes[i], klass, sizeof klass);
        User::GetWindowText(nodes[i], text, sizeof text);
        Gdi::RECT r{};
        User::GetWindowRect(nodes[i], &r);
        printf("TASKBAR TOP hwnd=%u class=%s text=\"%s\" style=%08x exstyle=%08x owner=%u visible=%u "
               "rect=%d,%d,%d,%d process=%u drag_icon=%u\n",
               nodes[i], klass, text, User::Style(nodes[i]), User::ExtendedStyle(nodes[i]), User::Owner(nodes[i]),
               User::IsVisible(nodes[i]) ? 1u : 0u, r.left, r.top, r.right, r.bottom,
               User::WindowProcess(nodes[i]), User::WindowDragIcon(nodes[i]));
    }
}
// Pyramid: VB3 registers every class with icon 0 and sets no icon. The window
// USER reports to the shell is ThunderRTMain, a popup with WS_SYSMENU and no
// caption; it answers WM_QUERYDRAGICON with zero, also after the
// forms have loaded, so the button carries USER's default application icon.
// The probe reads the icon cell and the title part of the first button: the
// user's host showed the button without title (23 September).
static int win16_button(unsigned tasks)
{
    unsigned form = 0, application = 0;
    until([&] {
        form = find("ThunderForm");
        application = any_window("ThunderRTMain");
        return form && application && User::IsVisible(application);
    }, "the Run entry starts Pyramid and it shows a form", 90000);
    // the shareware notice is a second ThunderForm with a button "OK".
    // After it is confirmed the main form stays open; the user's host showed
    // it minimized in the taskbar.
    unsigned notice = 0, ok = 0;
    until([&] {
        unsigned nodes[User::WindowCapacity], n = 0;
        User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
        for (unsigned i = 0; i < n && !ok; ++i) {
            char klass[64] = {};
            User::GetClassName(nodes[i], klass, sizeof klass);
            if (strcmp(klass, "ThunderForm") || !User::IsVisible(nodes[i])) continue;
            unsigned kids[User::WindowCapacity], k = 0;
            User::WindowSnapshot(nodes[i], kids, User::WindowCapacity, k);
            for (unsigned j = 0; j < k; ++j) {
                char text[16] = {};
                User::GetWindowText(kids[j], text, sizeof text);
                if (!strcmp(text, "OK") && User::IsVisible(kids[j])) { notice = nodes[i]; ok = kids[j]; break; }
            }
        }
        return ok != 0;
    }, "Pyramid shows its notice with an OK button", 60000);
    {
        Gdi::RECT r{};
        require(User::GetWindowRect(ok, &r), "the OK button has a rectangle");
        const int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
        shell->MouseButtons(x, y, 0);
        shell->MouseButtons(x, y, 1);
        shell->MouseButtons(x, y, 0);
    }
    until([&] { return !User::IsWindow(notice) || !User::IsVisible(notice); },
          "the notice closes on OK", 30000);
    pump(3000);
    form = find("ThunderForm");
    require(form != 0, "the main form is still visible after the notice");
    printf("TASKBAR WIN16 form=%u style=%08x after the notice\n", form, User::Style(form));
    require(!(User::Style(form) & 0x20000000u), "the main form is not minimized after the notice");
    pump(3000);
    dump_top_level();
    char title[64] = {};
    User::GetWindowText(application, title, sizeof title);
    require(!strcmp(title, "PYRAMID"), "ThunderRTMain carries the program title");
    Gdi::RECT bar{};
    require(User::GetWindowRect(tasks, &bar), "the task button band has a rectangle");
    printf("TASKBAR ICON band %d,%d %d,%d\n", bar.left, bar.top, bar.right, bar.bottom);
    unsigned blue = 0, ink = 0;
    auto probe = [&] {
        const unsigned char *pixels = User::Pixels();
        if (!pixels) return;
        blue = ink = 0;
        for (int y = bar.top + 2; y < bar.bottom - 2 && y < 600; ++y)
            for (int x = bar.left + 2; x < bar.left + 150 && x < 800; ++x) {
                const unsigned char *p = pixels + (unsigned(y) * 800 + unsigned(x)) * 4;
                // The generic application icon has a blue title bar; its
                // label is black text to the right.
                if (x < bar.left + 22 && p[0] > 100 && p[1] < 60 && p[2] < 60) ++blue;
                if (x >= bar.left + 22 && p[0] == 0 && p[1] == 0 && p[2] == 0) ++ink;
            }
    };
    auto good = [&] { probe(); return blue >= 4 && ink >= 60; };
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!good() && std::chrono::steady_clock::now() < end) pump(50);
    screenshot("build/taskbar-icon/win16-desktop.ppm");
    printf("TASKBAR WIN16 cell blue=%u text_ink=%u\n", blue, ink);
    require(blue >= 4, "the button of the 16-bit program carries an icon");
    require(ink >= 60, "the button of the 16-bit program carries its title");
    App::TerminateChildren(0);
    for (unsigned i = 0; i < 64 && App::LiveChildren(); ++i) App::PumpChildren(8);
    puts("TASKBAR WIN16 PASS");
    return 0;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    // --win16 starts Pyramid (Visual Basic 3) instead of Clickomania: the task
    // button of a 16-bit program (reported by a user).
    bool win16 = false;
    for (int i = 3; i < argc; ++i) if (!strcmp(argv[i], "--win16")) win16 = true;
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C:");
    require(HostVolume::Mount(drive_options), "mount the writable C: and the program drive");
    require(RegistryFile::Attach(argc, argv, drive_options.drive_c),
            "attach the registry journal with the autostart cleanup");
    // One autostart entry of this session's own: the program whose task button
    // the test reads. Explorer starts it the way it starts every Run entry.
    {
        auto &store = Registry::System();
        unsigned key = 0;
        bool created = false;
        require(!store.Open(store.roots[2], "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                            true, key, created), "the Run key of the imported hive is there");
        static const char command[] = "D:\\Click.exe", pyramid[] = "\"D:\\GAMES\\ALTEPC\\Spiele von gerda oma\\PYRAMID\\PYRAMID.EXE\"";
        if (win16) require(!store.Set(key, "Pyramid", 1, pyramid, sizeof pyramid), "write the Run entry");
        else require(!store.Set(key, "Clickomania", 1, command, sizeof command), "write the Run entry");
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    shell = new App(nullptr);
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
        tasks = find("MSTaskSwWClass");
        return tray && tasks && find("Progman") && !visible_dialog("RUNDLL");
    }, "Explorer builds the shell", 300000);
    if (win16) return win16_button(tasks);
    // The Delphi program of the Run entry, and the window USER hands the shell.
    unsigned application = 0, form = 0;
    until([&] {
        application = any_window("TApplication");
        form = any_window("TfrmGame");
        return application && form && User::IsVisible(form);
    }, "the Run entry starts Clickomania and it reaches its main form", 90000);
    printf("TASKBAR ICON application=%u form=%u owner=%u icon_large=%u icon_small=%u "
           "class_small=%u class_big=%u\n",
           application, form, User::Owner(form),
           User::WindowIcon(application, true), User::WindowIcon(application, false),
           User::ClassAccess(application, -34, 0, 2), User::ClassAccess(application, -14, 0, 2));
    require(User::Owner(form) == application, "the form belongs to the TApplication window");
    require(User::WindowIcon(application, true) || User::WindowIcon(application, false),
            "WM_SETICON left an icon on the window the taskbar shows");
    pump(1500);   // let the taskbar lay its buttons out and paint them
    Gdi::RECT bar{};
    require(User::GetWindowRect(tasks, &bar), "the task button band has a rectangle");
    printf("TASKBAR ICON band %d,%d %d,%d\n", bar.left, bar.top, bar.right, bar.bottom);
    require(bar.right - bar.left > 40 && bar.bottom - bar.top > 10,
            "the taskbar gave the program a button band");
    // The button of the only running program starts at the left edge of the
    // band and carries its icon in the first cell.
    unsigned colours = 0, painted = 0, coloured = 0;
    auto probe = [&] {
        const unsigned char *pixels = User::Pixels();
        if (!pixels) return;
        unsigned seen[64] = {};
        colours = painted = coloured = 0;
        for (int y = bar.top + 2; y < bar.bottom - 2 && y < 600; ++y)
            for (int x = bar.left + 2; x < bar.left + 22 && x < 800; ++x)
            {
                const unsigned char *p = pixels + (unsigned(y) * 800 + unsigned(x)) * 4;
                const unsigned value = unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16;
                ++painted;
                // Gray pixels have equal RGB channels; the application icon
                // contributes non-gray colors.
                const int low = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
                const int high = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
                if (high - low > 60) ++coloured;
                bool known = false;
                for (unsigned i = 0; i < colours; ++i) if (seen[i] == value) { known = true; break; }
                if (!known && colours < 64) seen[colours++] = value;
            }
    };
    // Retry until the taskbar has had time to paint instead of relying on a
    // fixed delay.
    until([&] { probe(); return coloured >= 30; },
          "the button cell carries the coloured pixels of the program icon", 60000);
    screenshot("build/taskbar-icon/desktop.ppm");
    printf("TASKBAR ICON cell pixels=%u colours=%u coloured=%u\n", painted, colours, coloured);
    // An empty button uses gray shading. Clickomania's colored blocks must
    // produce multiple colors and non-gray pixels, unlike the generic
    // fallback icon.
    require(painted > 200, "the icon cell has pixels");
    require(colours >= 8, "the button cell carries more than the button face and its edges");
    App::TerminateChildren(0);
    for (unsigned i = 0; i < 64 && App::LiveChildren(); ++i) App::PumpChildren(8);
    puts("TASKBAR ICON PASS");
    return 0;
}
