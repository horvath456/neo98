// SPDX-License-Identifier: GPL-2.0-or-later
// The Font page of the MS-DOS window's property sheet.
// NOTEPAD.EXE starts HELLO.COM on drive D:; the Properties button of the
// window's toolbar opens the sheet, PSM_SETCURSEL shows the Font page.
//
// 1. The font preview: every size named with --size (its index in the
// sorted list and its item data: width | height << 8, 0x8000 for
// TrueType, 0xffff for Auto) is selected the way the list tells its page
// (LB_SETCURSEL and LBN_SELCHANGE), and a picture of the screen is taken.
// The page logs the cell and the rectangle of the preview it drew
// ("DOSVM fontpreview"); the script reads that rectangle out of the
// picture.
// 2. The scroll bar of the size list: a click on the lower arrow, a click
// into the trough below the thumb, a drag of the thumb and a click on the
// upper arrow, each through the pointer of the machine (press, a pause,
// release). After each the thumb is logged and a picture taken.
//
// test-pif-font-page NOTEPAD.EXE FONT --system-tree T --drive-c DIR
// --drive D=DIR --output DIR [--size INDEX:DATA]...
#define main probe_dos_vm_main
#include "probe_dos_vm.cpp"
#undef main

static Session *session;
static const char *shots = "build/pif-font-page";
static void wait_ms(unsigned milliseconds)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end && session->pump()) {}
}
static unsigned page_titled(unsigned sheet, const char *title)
{
    for (unsigned child = User::GetWindow(sheet, 5); child; child = User::GetWindow(child, 2))
    {
        char klass[64] = {}, text[64] = {};
        User::GetClassName(child, klass, sizeof klass);
        User::GetWindowText(child, text, sizeof text);
        if (!strcmp(klass, "#32770") && !strcmp(text, title)) return child;
    }
    return 0;
}
static void press(int x, int y, unsigned hold_ms)
{
    session->app->MouseButtons(x, y, 0);
    wait_ms(80);
    session->app->MouseButtons(x, y, 1);
    wait_ms(hold_ms);
    session->app->MouseButtons(x, y, 0);
    wait_ms(250);
}
// The thumb of the list's vertical bar and the first item it shows.
static void report_bar(unsigned list, const char *step)
{
    User::ScrollBarLayoutView view;
    User::ScrollBarLayout(list, -5, view);
    printf("PIFFONT bar step=%s present=%u bar=%d,%d,%d,%d line=%d thumb=%d,%d\n", step, view.present ? 1u : 0u,
           view.bar.left, view.bar.top, view.bar.right, view.bar.bottom, view.line, view.thumb_start, view.thumb_end);
    char path[512];
    snprintf(path, sizeof path, "scroll-%s", step);
    screenshot(shots, path);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    std::vector<std::pair<unsigned, unsigned>> sizes; // list index, item data
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    if (!tree_options.tree || !SystemTreeHost::Mount(tree_options.root)) { fprintf(stderr, "the imported Win98 tree is needed\n"); return 2; }
    const auto drive_options = HostVolume::Scan(argc, argv);
    if (!HostVolume::Mount(drive_options)) { fprintf(stderr, "cannot mount the host drives\n"); return 13; }
    for (int i = 3; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--system-tree")) { if (i + 1 < argc && argv[i + 1][0] != '-') ++i; continue; }
        if (!strcmp(argv[i], "--drive") || !strcmp(argv[i], "--drive-c")) { ++i; continue; }
        if (!strcmp(argv[i], "--output") && i + 1 < argc) { shots = argv[++i]; continue; }
        if (!strcmp(argv[i], "--size") && i + 1 < argc)
        {
            unsigned index = 0, data = 0;
            if (sscanf(argv[++i], "%u:%x", &index, &data) != 2) return 2;
            sizes.push_back({index, data});
            continue;
        }
        fprintf(stderr, "unknown argument %s\n", argv[i]);
        return 2;
    }
    std::vector<HostDosVm::Drive> drives;
    const char *tree_root = tree_options.root ? tree_options.root : SystemTreeHost::DefaultRoot();
    drives.push_back({'C', tree_root, true, drive_options.drive_c});
    for (unsigned i = 0; i < drive_options.count; ++i)
        if (drive_options.extra[i].letter && drive_options.extra[i].letter != 'C' && !drive_options.extra[i].cdrom)
            drives.push_back({drive_options.extra[i].letter, drive_options.extra[i].directory, false});
    HostDosVm::Install(logger, drives.data(), (unsigned)drives.size());
    Sound::SetCapture(sound_capture.data(), (unsigned)(sound_capture.size() / 2));
    auto exe = load(argv[1]), font = load(argv[2]);
    if (exe.empty() || font.empty()) return 3;
    {
        char guest[260];
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\%s", leaf(argv[1]));
        Files::System().Seed(guest, exe.data(), exe.size());
    }
    auto *a = new X86::Application(logger);
    if (!a->Start(exe.data(), exe.size(), font.data(), font.size(), leaf(argv[1]), nullptr)) return 3;
    Session s{a, std::chrono::steady_clock::now() + std::chrono::seconds(300)};
    session = &s;
    bool okay = false;
    auto finish = [&](const char *why) {
        printf("PIFFONT result=%s%s%s\n", okay ? "PASS" : "FAIL", why ? " " : "", why ? why : "");
        fflush(stdout);
        a->TerminateSelf(0);
        X86::Application::TerminateChildren(0);
        delete a;
        HostDosVm::Uninstall();
        return okay ? 0 : 5;
    };
    while (s.pump() && !a->WaitingForInput()) {}
    unsigned process = 0;
    if (!s.create_process("D:\\HELLO.COM", process) || !process) return finish("create");
    unsigned tty = 0;
    while (s.pump() && !(tty = find_tty())) {}
    if (!tty) return finish("no window");
    Gdi::RECT client = {};
    unsigned black = 0;
    const auto picture = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (s.pump() && std::chrono::steady_clock::now() < picture)
        if (lit_client_pixels(tty, client, black) >= 200 && User::GetForegroundWindow() == tty) break;
    // The Properties button of the strip.
    press(client.left + 184 + 11, client.top + 14, 60);
    unsigned sheet = 0;
    const auto sheet_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (s.pump() && std::chrono::steady_clock::now() < sheet_deadline && !(sheet = find_class("#32770", 0))) {}
    if (!sheet) return finish("no sheet");
    wait_ms(500);
    unsigned page = 0;
    for (unsigned index = 0; index < 4 && !page; ++index)
    {
        User::PostMessage(sheet, 0x0465, index, 0);
        wait_ms(600);
        page = page_titled(sheet, "Font");
        if (page && !User::IsVisible(page)) page = 0;
    }
    if (!page) return finish("no Font page");
    // The sheet in the middle of the screen, away from the DOS window's strip.
    wait_ms(600);
    const unsigned list = User::GetDlgItem(page, 1055), label = User::GetDlgItem(page, 0x425);
    Gdi::RECT list_rect = {}, label_rect = {}, page_rect = {};
    User::GetWindowRect(list, &list_rect);
    User::GetWindowRect(label, &label_rect);
    User::GetWindowRect(page, &page_rect);
    printf("PIFFONT page=%u list=%u rect=%d,%d,%d,%d label=%d,%d,%d,%d page=%d,%d,%d,%d\n", page, list, list_rect.left,
           list_rect.top, list_rect.right, list_rect.bottom, label_rect.left, label_rect.top, label_rect.right,
           label_rect.bottom, page_rect.left, page_rect.top, page_rect.right, page_rect.bottom);
    fflush(stdout);
    // 1. The previews. The script knows the order of the list (Auto, the
    // raster sizes of the WOAFont, the TrueType sizes) and names the index.
    for (const auto &size : sizes)
    {
        const unsigned want = size.second;
        printf("PIFFONT size data=0x%04x index=%u\n", want, size.first);
        User::PostMessage(list, 0x0186 /* LB_SETCURSEL */, size.first, 0);
        wait_ms(200);
        User::PostMessage(page, 0x0111, 1055 | 1u << 16, list);
        wait_ms(900);
        char name[64];
        snprintf(name, sizeof name, "preview-%04x", want);
        screenshot(shots, name);
        fflush(stdout);
    }
    // 2. The scroll bar: back to the first item, then the four presses.
    User::PostMessage(list, 0x0186, 0, 0);
    User::PostMessage(page, 0x0111, 1055 | 1u << 16, list);
    wait_ms(800);
    report_bar(list, "start");
    User::ScrollBarLayoutView view;
    User::ScrollBarLayout(list, -5, view);
    if (!view.present) return finish("the size list has no vertical bar");
    const int x = (view.bar.left + view.bar.right) / 2;
    press(x, view.bar.bottom - view.line / 2 - 1, 60);
    report_bar(list, "arrow-down");
    User::ScrollBarLayout(list, -5, view);
    press(x, view.bar.top + (view.thumb_end + view.bar.bottom - view.bar.top - view.line) / 2, 60);
    report_bar(list, "page-down");
    User::ScrollBarLayout(list, -5, view);
    {
        // Drag the thumb back up by 40 pixels or to the top.
        const int from = view.bar.top + (view.thumb_start + view.thumb_end) / 2;
        int to = from - 40;
        if (to < view.bar.top + view.line) to = view.bar.top + view.line;
        a->MouseButtons(x, from, 0);
        wait_ms(80);
        a->MouseButtons(x, from, 1);
        wait_ms(120);
        for (int step = 1; step <= 4; ++step)
        {
            a->MouseButtons(x, from + (to - from) * step / 4, 1);
            wait_ms(80);
        }
        a->MouseButtons(x, to, 0);
        wait_ms(300);
    }
    report_bar(list, "drag-up");
    User::ScrollBarLayout(list, -5, view);
    press(x, view.bar.bottom - view.line / 2 - 1, 60);
    press(x, view.bar.bottom - view.line / 2 - 1, 60);
    report_bar(list, "arrow-down-2");
    press(x, view.bar.top + view.line / 2, 60);
    report_bar(list, "arrow-up");
    okay = true;
    if (User::IsWindow(sheet)) User::PostMessage(sheet, 0x0111, 2, User::GetDlgItem(sheet, 2));
    wait_ms(800);
    return finish(nullptr);
}
