// SPDX-License-Identifier: GPL-2.0-or-later
// The property sheet of a DOS program, both ways Win98 opens it:
//
// 1. Explorer. EXPLORER.EXE runs as the shell of the imported installation
// with TEST.PIF on the desktop. A click selects the icon, the context
// menu's Properties opens the sheet: SHELL32's page collector loads
// PIFMGR.DLL, calls PIFPROPGETPAGES through the SHELL forwarder and hands
// the 16-bit page handles to COMCTL32's CreateProxyPage. The sheet must
// carry General and the pages of PIFMGR (and the machine's own pages);
// every page is shown once, values are changed on four of them, OK
// writes the PIF.
// 2. The MS-DOS window. A double click on the icon starts the PIF; the
// window must carry the changed title and no toolbar strip, which is the
// "next start uses it". Then WINSHEET.PIF, which keeps its toolbar,
// starts; the Properties button of the strip opens the window's own
// sheet (COMCTL32 loaded into the MS-DOS process), which must carry the
// same pages; a title is applied there with OK.
// test-pif-pages EXPLORER.EXE FONT --system-tree T --drive-c DIR --drive D=DIR
// [--icon X,Y] [--menu X,Y] [--second-icon X,Y] [--output DIR]
// Without --icon and --second-icon the icons "Test" and "Winsheet" are
// selected by typing their labels (select_icon), Shift+F10's WM_CONTEXTMENU
// opens the context menu and Enter starts them. Without --menu the click goes
// to the last item of the context menu, Properties.
#define main original_probe_main
#include "probe_application.cpp"
#undef main
#include "host_dos_vm.h"
using App = X86::Application;
static App *shell;
static const char *out_dir = "build/pif-pages";
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "PIF PAGES FAILED: %s\n", why);
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
static unsigned find_top(const char *klass_want, const char *text_part)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[128], text[256];
        User::GetClassName(nodes[i], klass, sizeof klass);
        User::GetWindowText(nodes[i], text, sizeof text);
        if (User::IsVisible(nodes[i]) && !User::Parent(nodes[i]) && !strcmp(klass, klass_want) &&
            (!text_part || strstr(text, text_part)))
            return nodes[i];
    }
    return 0;
}
static void click(int x, int y, bool twice = false)
{
    for (int i = 0; i < (twice ? 2 : 1); ++i) {
        shell->MouseButtons(x, y, 0);
        pump(30);
        shell->MouseButtons(x, y, 1);
        pump(30);
        shell->MouseButtons(x, y, 0);
        pump(twice ? 60 : 200);
    }
}
static void right_click(int x, int y)
{
    shell->MouseButtons(x, y, 0);
    pump(30);
    shell->MouseButtons(x, y, 2);
    pump(30);
    shell->MouseButtons(x, y, 0);
    pump(300);
}
// The child dialogs of a sheet, the pages it created, with their titles.
static unsigned pages_of(unsigned sheet, unsigned *out, char (*titles)[64], unsigned capacity)
{
    unsigned n = 0;
    for (unsigned child = User::GetWindow(sheet, 5); child && n < capacity; child = User::GetWindow(child, 2)) {
        char klass[64];
        User::GetClassName(child, klass, sizeof klass);
        if (strcmp(klass, "#32770")) continue;
        out[n] = child;
        User::GetWindowText(child, titles[n], 64);
        ++n;
    }
    return n;
}
static unsigned page_titled(unsigned sheet, const char *title)
{
    unsigned pages[16];
    char titles[16][64];
    const unsigned n = pages_of(sheet, pages, titles, 16);
    for (unsigned i = 0; i < n; ++i)
        if (!strcmp(titles[i], title)) return pages[i];
    return 0;
}
// PSM_SETCURSEL: the sheet shows (and creates) the page of that tab.
static unsigned show_page(unsigned sheet, unsigned index, const char *title)
{
    User::PostMessage(sheet, 0x0465, index, 0);
    unsigned page = 0;
    until([&] { page = page_titled(sheet, title); return page && User::IsVisible(page); }, title, 20000);
    printf("PIF PAGES page=%s hwnd=%u\n", title, page);
    return page;
}
static void set_check(unsigned page, unsigned id, bool on)
{
    const unsigned button = User::GetDlgItem(page, id);
    require(button != 0, "a check box of the page");
    User::PostMessage(button, 0x00f1, on ? 1 : 0, 0);
    pump(100);
}
static void set_text(unsigned page, unsigned id, const char *text)
{
    const unsigned edit = User::GetDlgItem(page, id);
    require(edit != 0, "an edit field of the page");
    require(User::SetWindowText(edit, text), "set the text of the field");
    pump(50);
}
// The first descendant of `parent` with the class name.
static unsigned descendant(unsigned parent, const char *klass_want)
{
    unsigned nodes[256], n = 0;
    User::WindowSnapshot(parent, nodes, 256, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[64];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!strcmp(klass, klass_want)) return nodes[i];
    }
    return 0;
}
// Selects the desktop icon with this label the way the keyboard does: a
// click on free wallpaper, then the label typed into the desktop's list view
// (its incremental search selects the item). A host call into the list view
// would be a send to another thread of Explorer from outside its pump, and
// the place of an icon depends on what else the desktop of the tree carries.
// Answers the list view.
static unsigned select_icon(const char *label)
{
    const unsigned progman = find_top("Progman", nullptr);
    const unsigned list = progman ? descendant(progman, "SysListView32") : 0;
    require(list != 0, "the list view of the desktop");
    click(760, 300);
    pump(600); // past the double-click time, so the typing starts a new search
    for (const char *c = label; *c; ++c) {
        User::PostMessage(list, 0x0102 /* WM_CHAR */, (unsigned char)*c, 1);
        pump(60);
    }
    pump(500);
    printf("PIF PAGES selected %s\n", label);
    return list;
}
// Enter on the selected icon: the list view tells the view to open it.
static void open_selected(unsigned list)
{
    User::PostMessage(list, 0x0100 /* WM_KEYDOWN */, 0x0d, 0x001c0001);
    User::PostMessage(list, 0x0101 /* WM_KEYUP */, 0x0d, 0xc01c0001u);
    pump(300);
}
static unsigned lit_rows(unsigned hwnd, Gdi::RECT &client)
{
    client = {};
    if (!User::ClientScreenRect(hwnd, &client)) return 0;
    return (unsigned)(client.bottom - client.top);
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    int icon_x = -1, icon_y = -1, menu_x = -1, menu_y = -1;
    for (int i = 3; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--output") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--icon") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &icon_x, &icon_y);
        else if (!strcmp(argv[i], "--menu") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &menu_x, &menu_y);
    }
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C:");
    require(HostVolume::Mount(drive_options), "mount the writable C: and the program drive");
    require(RegistryFile::Attach(argc, argv, drive_options.drive_c), "attach the registry journal");
    std::vector<HostDosVm::Drive> drives;
    drives.push_back({'C', tree_options.root ? tree_options.root : SystemTreeHost::DefaultRoot(), true, drive_options.drive_c});
    for (unsigned i = 0; i < drive_options.count; ++i)
        if (drive_options.extra[i].letter && drive_options.extra[i].letter != 'C' && !drive_options.extra[i].cdrom)
            drives.push_back({drive_options.extra[i].letter, drive_options.extra[i].directory, false});
    HostDosVm::Install(logger, drives.data(), (unsigned)drives.size());
    auto exe = load(argv[1]), font = load(argv[2]);
    shell = new App(logger);
    require(shell->Start(exe.data(), exe.size(), font.data(), font.size(), "EXPLORER.EXE"), "explorer start");
    until([&] {
        if (auto h = visible_dialog("RUNDLL")) User::PostMessage(h, 0x0010, 0, 0);
        return find_top("Shell_TrayWnd", nullptr) && find_top("Progman", nullptr) && !visible_dialog("RUNDLL");
    // The first start runs every Active Setup stub before the tray comes;
    // since the Web Folders stub runs through that takes longer
    // than 90 seconds under the sanitizers.
    }, "Explorer builds the shell", 300000);
    pump(4000);
    char path[512];
    snprintf(path, sizeof path, "%s/desktop.ppm", out_dir);
    screenshot(path);
    // 1. The sheet of the icon, through the context menu.
    if (icon_x < 0) {
        // Shift+F10: the list view's keyboard context menu, WM_CONTEXTMENU at -1,-1.
        const unsigned list = select_icon("Test");
        User::PostMessage(list, 0x007b /* WM_CONTEXTMENU */, list, 0xffffffffu);
        pump(300);
    } else {
        click(icon_x, icon_y);
        right_click(icon_x, icon_y);
    }
    if (menu_x < 0) {
        // Properties is the last item of the context menu.
        unsigned menu = 0;
        until([&] { return (menu = find_top("#32768", nullptr)) != 0; }, "the context menu of the icon", 20000);
        pump(300);
        Gdi::RECT box{};
        require(User::GetWindowRect(menu, &box), "the rectangle of the context menu");
        menu_x = box.left + 40;
        menu_y = box.bottom - 12;
    }
    shell->MouseButtons(menu_x, menu_y, 0);
    pump(300);
    click(menu_x, menu_y);
    unsigned sheet = 0;
    until([&] { return (sheet = find_top("#32770", "Properties")) != 0; }, "the context menu opens the property sheet", 60000);
    char caption[256] = {};
    User::GetWindowText(sheet, caption, sizeof caption);
    printf("PIF PAGES sheet=%u caption=%s\n", sheet, caption);
    // The tabs: General of the shell, then Program, Font, Memory, Screen,
    // Misc of PIFMGR, then the pages of the machine's options. Every page
    // is shown once, which creates its dialog through COMMCTRL.
    static const char *const original[] = {"Program", "Font", "Memory", "Screen", "Misc"};
    // Pictures of General (the read-only fields on the dialog face)
    // and Program (Change Icon) as the sheet first shows them.
    pump(1500);
    snprintf(path, sizeof path, "%s/general.ppm", out_dir);
    screenshot(path);
    unsigned index = 1;
    for (const char *title : original)
    {
        show_page(sheet, index++, title);
        if (!strcmp(title, "Program"))
        {
            pump(1000);
            snprintf(path, sizeof path, "%s/program.ppm", out_dir);
            screenshot(path);
        }
    }
    const char *extra_titles[8] = {};
    unsigned extras = 0;
    for (int i = 3; i < argc; ++i)
        if (!strcmp(argv[i], "--extra-page") && i + 1 < argc && extras < 8) extra_titles[extras++] = argv[++i];
    for (unsigned i = 0; i < extras; ++i) show_page(sheet, index++, extra_titles[i]);
    // Every control of a machine page lies inside the page, as on PIFMGR's
    // own pages (the Volume group of Sound ran past the bottom).
    for (unsigned i = 0; i < extras; ++i)
    {
        const unsigned page = page_titled(sheet, extra_titles[i]);
        Gdi::RECT area = {};
        User::GetWindowRect(page, &area);
        unsigned nodes[128], count = 0, outside = 0;
        if (page && User::WindowSnapshot(page, nodes, 128, count))
            for (unsigned n = 0; n < count; ++n)
            {
                Gdi::RECT r = {};
                if (nodes[n] == page || !User::GetWindowRect(nodes[n], &r)) continue;
                if (r.bottom > area.bottom || r.right > area.right) ++outside;
            }
        printf("PIF PAGES page %s bottom=%d controls outside=%u\n", extra_titles[i], area.bottom, outside);
        require(page && outside == 0, "every control of a machine page lies inside the page");
    }
    {
        unsigned pages[16];
        char titles[16][64];
        const unsigned n = pages_of(sheet, pages, titles, 16);
        printf("PIF PAGES pages=%u:", n);
        for (unsigned i = 0; i < n; ++i) printf(" %s", titles[i]);
        printf("\n");
        require(n == 6 + extras, "General, the five pages of PIFMGR and the machine's pages");
    }
    // Program: the title and the batch file.
    const unsigned program = page_titled(sheet, "Program");
    {
        char text[128] = {};
        User::GetWindowText(User::GetDlgItem(program, 4002), text, sizeof text);
        printf("PIF PAGES program cmdline=%s\n", text);
        require(strstr(text, "COMMAND.COM") != nullptr, "the Program page shows the command line of the PIF");
        User::GetWindowText(User::GetDlgItem(program, 4001), text, sizeof text);
        printf("PIF PAGES program title=%s\n", text);
    }
    // Change Icon (4013): SHELL32's PickIconDlg (ordinal 62) over PIFMGR.DLL;
    // the fourth icon of its list, OK, and the page shows it.
    {
        User::PostMessage(program, 0x0111, 4013, User::GetDlgItem(program, 4013));
        unsigned picker = 0;
        until([&] { return (picker = find_top("#32770", "Change Icon")) != 0; }, "Change Icon opens its dialog", 30000);
        pump(300);
        unsigned list = 0;
        {
            unsigned nodes[64], count = 0;
            if (User::WindowSnapshot(picker, nodes, 64, count))
                for (unsigned i = 0; i < count && !list; ++i)
                {
                    char klass[32] = {};
                    User::GetClassName(nodes[i], klass, sizeof klass);
                    if (!strcmp(klass, "ListBox")) list = nodes[i];
                }
        }
        require(list != 0, "the icon list of the Change Icon dialog");
        User::PostMessage(list, 0x0186, 3, 0); // LB_SETCURSEL
        pump(200);
        snprintf(path, sizeof path, "%s/change-icon.ppm", out_dir);
        screenshot(path);
        User::PostMessage(picker, 0x0111, 1, User::GetDlgItem(picker, 1));
        until([&] { return !User::IsWindow(picker); }, "OK closes the Change Icon dialog", 20000);
        pump(300);
        printf("PIF PAGES change icon done\n");
    }
    // Advanced (4008), MS-DOS mode (4009), "Specify a new MS-DOS
    // configuration" (6010) with both fields empty: PIFMGR starts the
    // configuration wizard of APPWIZ.CPL. Its first option list
    // is confirmed with Enter, the wizard's lines land in the two fields;
    // Cancel leaves the program as it was.
    {
        User::PostMessage(program, 0x0111, 4008, User::GetDlgItem(program, 4008));
        unsigned advanced = 0;
        until([&] { return (advanced = find_top("#32770", "Advanced")) != 0; }, "Advanced opens its dialog", 30000);
        pump(200);
        User::PostMessage(User::GetDlgItem(advanced, 4009), 0x00f1, 1, 0);
        User::PostMessage(advanced, 0x0111, 4009, User::GetDlgItem(advanced, 4009));
        pump(200);
        User::PostMessage(User::GetDlgItem(advanced, 6009), 0x00f1, 0, 0);
        User::PostMessage(User::GetDlgItem(advanced, 6010), 0x00f1, 1, 0);
        if (getenv("PIF_WIZARD_TRACE")) { shell->trace_calls = true; shell->trace_from = 1; }
        User::PostMessage(advanced, 0x0111, 6010, User::GetDlgItem(advanced, 6010));
        unsigned wizard = 0;
        until([&] {
            unsigned nodes[User::WindowCapacity], n = 0;
            User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
            for (unsigned i = 0; i < n; ++i)
                if (nodes[i] != advanced && User::IsVisible(nodes[i]) && User::Owner(nodes[i]) == advanced) { wizard = nodes[i]; return true; }
            return false;
        }, "the configuration wizard opens over Advanced", 60000);
        pump(500);
        char caption[128] = {};
        User::GetWindowText(wizard, caption, sizeof caption);
        printf("PIF PAGES wizard=%u caption=%s\n", wizard, caption);
        shell->trace_calls = false;
        // The check box of the second option, Disk Cache: a click on its
        // state image toggles it (NM_CLICK, LVM_HITTEST).
        unsigned options = 0;
        {
            unsigned nodes[64], count = 0;
            if (User::WindowSnapshot(wizard, nodes, 64, count))
                for (unsigned i = 0; i < count && !options; ++i)
                {
                    char klass[32] = {};
                    User::GetClassName(nodes[i], klass, sizeof klass);
                    if (!strcmp(klass, "SysListView32")) options = nodes[i];
                }
        }
        require(options != 0, "the option list of the wizard");
        Gdi::RECT list_rect = {};
        require(User::ClientScreenRect(options, &list_rect), "the option list's place");
        click(list_rect.left + 8, list_rect.top + 18 + 9);
        pump(500);
        snprintf(path, sizeof path, "%s/wizard.ppm", out_dir);
        screenshot(path);
        // The wizard is a property sheet in wizard form: its "OK" is the
        // Finish button, 3025h.
        User::PostMessage(wizard, 0x0111, 0x3025, User::GetDlgItem(wizard, 0x3025));
        until([&] { return !User::IsWindow(wizard) || !User::IsVisible(wizard); }, "OK closes the wizard", 30000);
        pump(500);
        char config[512] = {}, autoexec[512] = {};
        User::GetWindowText(User::GetDlgItem(advanced, 6003), config, sizeof config);
        User::GetWindowText(User::GetDlgItem(advanced, 6005), autoexec, sizeof autoexec);
        printf("PIF PAGES wizard config=%zu autoexec=%zu\n", strlen(config), strlen(autoexec));
        printf("PIF PAGES wizard config text: %.200s\n", config);
        snprintf(path, sizeof path, "%s/advanced.ppm", out_dir);
        screenshot(path);
        require(config[0] || autoexec[0], "the wizard fills in the MS-DOS configuration");
        char upper[512] = {};
        for (unsigned i = 0; autoexec[i] && i + 1 < sizeof upper; ++i) upper[i] = (char)toupper((unsigned char)autoexec[i]);
        printf("PIF PAGES wizard autoexec text: %.300s\n", autoexec);
        require(strstr(upper, "SMARTDRV") != nullptr, "the checked Disk Cache adds SmartDrive to AUTOEXEC.BAT");
        User::PostMessage(advanced, 0x0111, 2, User::GetDlgItem(advanced, 2));
        until([&] { return !User::IsWindow(advanced); }, "Cancel closes Advanced", 20000);
        pump(200);
    }
    set_text(program, 4001, "Changed Title");
    set_text(program, 4007, "C:\\SETUP.BAT");
    // Memory: 8192 KB of extended memory (Auto, None, then 1024 to 16384
    // in steps of 1024: item 9), DPMI 4096 (Auto, then 1024 steps: item 4),
    // chosen from the lists the way a click in the drop-down does.
    const unsigned memory = page_titled(sheet, "Memory");
    User::PostMessage(User::GetDlgItem(memory, 1026), 0x014e, 9, 0);
    User::PostMessage(User::GetDlgItem(memory, 1088), 0x014e, 4, 0);
    pump(200);
    // Screen: no toolbar, 50 lines.
    const unsigned screen = page_titled(sheet, "Screen");
    set_check(screen, 1049, false);
    User::PostMessage(User::GetDlgItem(screen, 1047), 0x014e, 3, 0);
    // Misc: "Always suspend" on, fast pasting off, Alt+Enter reserved.
    const unsigned misc = page_titled(sheet, "Misc");
    set_check(misc, 1033, true);
    set_check(misc, 1081, false);
    set_check(misc, 1076, false);
    snprintf(path, sizeof path, "%s/sheet.ppm", out_dir);
    screenshot(path);
    // The machine's pages: a Sound Blaster Pro 2, a Hercules card and the
    // interpreter as the processor core, as a pick in the drop-down would
    // set them; a picture of each page.
    if (extras >= 2)
    {
        const unsigned sound = show_page(sheet, 6, extra_titles[0]);
        User::PostMessage(User::GetDlgItem(sound, 101), 0x014e, 1, 0);
        pump(200);
        snprintf(path, sizeof path, "%s/sound.ppm", out_dir);
        screenshot(path);
        const unsigned graphics = show_page(sheet, 7, extra_titles[1]);
        User::PostMessage(User::GetDlgItem(graphics, 201), 0x014e, 5, 0);
        pump(200);
        snprintf(path, sizeof path, "%s/graphics.ppm", out_dir);
        screenshot(path);
        if (extras >= 3)
        {
            const unsigned hardware = show_page(sheet, 8, extra_titles[2]);
            User::PostMessage(User::GetDlgItem(hardware, 306), 0x014e, 2, 0);
            pump(200);
            snprintf(path, sizeof path, "%s/hardware.ppm", out_dir);
            screenshot(path);
        }
    }
    // OK, as the button sends it: PSN_KILLACTIVE and PSN_APPLY to every page.
    User::PostMessage(sheet, 0x0111, 1, User::GetDlgItem(sheet, 1));
    until([&] { return !User::IsWindow(sheet); }, "OK closes the sheet", 20000);
    printf("PIF PAGES applied\n");
    pump(500);
    // 2. The next start: a double click on the icon.
    if (icon_x < 0) open_selected(select_icon("Test"));
    else click(icon_x, icon_y, true);
    unsigned tty = 0;
    until([&] { return (tty = find_top("tty", nullptr)) != 0; }, "the icon starts the MS-DOS window", 90000);
    pump(2000);
    User::GetWindowText(tty, caption, sizeof caption);
    Gdi::RECT client = {};
    const unsigned height = lit_rows(tty, client);
    printf("PIF PAGES tty=%u title=%s client=%d,%d,%d,%d\n", tty, caption, client.left, client.top, client.right,
           client.bottom);
    snprintf(path, sizeof path, "%s/next-start.ppm", out_dir);
    screenshot(path);
    require(!strcmp(caption, "Changed Title"), "the next start carries the title of the Program page");
    printf("PIF PAGES client_height=%u\n", height);
    User::PostMessage(tty, 0x0010, 0, 0);
    until([&] { return !User::IsWindow(tty); }, "the MS-DOS window closes", 30000);
    // 3. The sheet of the MS-DOS window: the second icon's PIF keeps its
    // toolbar; the Properties button of the strip opens the native sheet.
    int second_x = -1, second_y = -1;
    for (int i = 3; i < argc; ++i)
        if (!strcmp(argv[i], "--second-icon") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &second_x, &second_y);
    if (second_x < 0) open_selected(select_icon("Winsheet"));
    else click(second_x, second_y, true);
    until([&] { return (tty = find_top("tty", nullptr)) != 0; }, "the second icon starts the MS-DOS window", 90000);
    pump(2000);
    require(User::ClientScreenRect(tty, &client), "the client area of the window");
    // The strip: Properties is the fifth button, x 184 of the client area.
    const int bx = client.left + 184 + 11, by = client.top + 14;
    click(bx, by);
    unsigned window_sheet = 0;
    until([&] { return (window_sheet = find_top("#32770", "Properties")) != 0; }, "the Properties button opens the sheet", 30000);
    User::GetWindowText(window_sheet, caption, sizeof caption);
    printf("PIF PAGES window sheet=%u caption=%s\n", window_sheet, caption);
    index = 0;
    for (const char *title : original) show_page(window_sheet, index++, title);
    for (unsigned i = 0; i < extras; ++i) show_page(window_sheet, index++, extra_titles[i]);
    {
        unsigned pages[16];
        char titles[16][64];
        const unsigned n = pages_of(window_sheet, pages, titles, 16);
        printf("PIF PAGES window pages=%u\n", n);
        require(n == 5 + extras, "the window's sheet has the pages of PIFMGR and the machine's pages");
    }
    snprintf(path, sizeof path, "%s/window-sheet.ppm", out_dir);
    screenshot(path);
    const unsigned second_program = page_titled(window_sheet, "Program");
    set_text(second_program, 4001, "Window Sheet");
    User::PostMessage(window_sheet, 0x0111, 1, User::GetDlgItem(window_sheet, 1));
    until([&] { return !User::IsWindow(window_sheet); }, "OK closes the window's sheet", 20000);
    printf("PIF PAGES window applied\n");
    User::PostMessage(tty, 0x0010, 0, 0);
    until([&] { return !User::IsWindow(tty); }, "the second MS-DOS window closes", 30000);
    App::TerminateChildren(0);
    for (unsigned i = 0; i < 64 && App::LiveChildren(); ++i) App::PumpChildren(8);
    HostDosVm::Uninstall();
    puts("PIF PAGES PASS");
    return 0;
}
