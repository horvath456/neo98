// SPDX-License-Identifier: GPL-2.0-or-later
// Scrolling in an original Explorer folder window: dragging the thumb
// and repainting the rows that become visible.
//
// The list pane is the original COMCTL32 list view. It answers WM_VSCROLL
// SB_THUMBTRACK by asking GetScrollInfo for SIF_TRACKPOS and moves its content
// with ScrollWindowEx, so both bugs sit on the USER side of those two calls.
//
// The repaint check needs no reference image: after a scroll the pane has to
// look exactly the way it looks after a forced full repaint of the same view.
// A stripe that the scroll left behind differs from that image.
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *shell;
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "EXPLORER SCROLL FAILED: %s\n", why);
    if (shell) sampling::stall_dump(*shell);
    snapshot_windows();
    screenshot("build/explorer-scroll/failure.ppm");
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
template <class F> static void until(F condition, const char *why, unsigned limit = 8000)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit);
    while (!condition() && std::chrono::steady_clock::now() < end) pump(20);
    require(condition(), why);
}
static unsigned find(const char *name, const char *title = nullptr)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char c[128], t[128];
        User::GetClassName(nodes[i], c, sizeof c);
        User::GetWindowText(nodes[i], t, sizeof t);
        if (User::IsVisible(nodes[i]) && (!name || !strcmp(c, name)) && (!title || !strcmp(t, title)))
            return nodes[i];
    }
    return 0;
}
static void click(unsigned h)
{
    Gdi::RECT r{};
    require(User::GetWindowRect(h, &r), "click window");
    const int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    shell->MouseButtons(x, y, 0);
    shell->MouseButtons(x, y, 1);
    shell->MouseButtons(x, y, 0);
    pump(120);
}
static bool option_start(const char *text) { return text[0] == '-' && text[1] == '-'; }
static void quiet(const char *why, unsigned limit = 20000)
{
    until([&] { return shell->WaitingForInput() && !User::Rendering() &&
                       !User::PendingPaintWindows(nullptr); }, why, limit);
}
// The composed desktop surface inside one rectangle.
static std::vector<unsigned> capture(const Gdi::RECT &r)
{
    std::vector<unsigned> out;
    const unsigned char *pixels = User::Pixels();
    if (!pixels) return out;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x) {
            const unsigned char *p = pixels + 4 * (y * 800 + x);
            out.push_back(unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16));
        }
    return out;
}
static unsigned differing(const std::vector<unsigned> &a, const std::vector<unsigned> &b)
{
    if (a.size() != b.size()) return 0xffffffff;
    unsigned n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}
static unsigned position_of(unsigned list, unsigned bar)
{
    Win98Scroll::Info info{28, 0x17, 0, 0, 0, 0, 0};
    User::ScrollState(list, 0, bar, &info, 0);
    return info.position;
}
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    if (tree_options.tree) require(SystemTreeHost::Mount(tree_options.root), "mount the Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    drive_options.ram = drive_options.drive_c == nullptr;
    require(HostVolume::Mount(drive_options), "mount the writable C: drive");
    if (argv[3][0] != '-' || argv[3][1]) require(seed_manifest(argv[3]), "seed manifest");
    auto exe = load(argv[1]), font = load(argv[2]);
    bool trace_calls = false;
    for (int i = 4; i < argc; ++i) if (!strcmp(argv[i], "--trace-calls")) trace_calls = true;
    shell = new App(trace_calls ? logger : nullptr);
    shell->trace_calls = trace_calls;
    for (int i = 4; i < argc && !option_start(argv[i]); ++i)
        require(shell->AddNativeModule(argv[i]), "native module override");
    require(shell->Start(exe.data(), exe.size(), font.data(), font.size(), "EXPLORER.EXE",
                         "EXPLORER.EXE /e,C:\\WINDOWS\\HELP"), "explorer start");
    // C:\WINDOWS\HELP holds far more entries than a folder window shows, so
    // its list pane always carries a vertical bar with room to move. The ISO
    // installation shows C:\WINDOWS itself in Web view behind the "these
    // files keep your system working" page, with the list hidden; the
    // reference system installation had that view turned off. This run
    // needs the folder window only; the shell itself is covered elsewhere.
    until([&] {
        if (auto h = visible_dialog("RUNDLL")) {
            auto b = User::GetDlgItem(h, 2);
            if (!b) b = User::GetDlgItem(h, 1);
            if (b) click(b);
        }
        return find("SysTreeView32") && find("SysListView32") && shell->WaitingForInput();
    }, "Explorer folder window opens", 240000);
    quiet("folder window finishes its first view", 180000);
    pump(800);
    printf("EXPLORER SCROLL folder window up calls=%u\n", shell->ApiCalls());
    fflush(stdout);

    // The pane and the axis it can scroll on. Which bar a folder view carries
    // depends on its mode: the icon views scroll down, the list view sideways.
    unsigned list = 0, vertical = 0;
    Win98Scroll::Info info{28, 0x17, 0, 0, 0, 0, 0};
    {
        unsigned nodes[User::WindowCapacity], n = 0;
        User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
        for (unsigned i = 0; i < n && !list; ++i) {
            char c[128];
            User::GetClassName(nodes[i], c, sizeof c);
            if (strcmp(c, "SysListView32") || !User::IsVisible(nodes[i])) continue;
            for (unsigned bar = 1; bar + 1 > 0 && !list; --bar) {
                Win98Scroll::Info probe{28, 0x17, 0, 0, 0, 0, 0};
                if (!User::ScrollState(nodes[i], 0, bar, &probe, 0)) continue;
                if (probe.maximum <= probe.minimum + (int)probe.page) continue;
                list = nodes[i];
                vertical = bar;
                info = probe;
            }
        }
    }
    require(list, "folder list pane with a bar that has room");
    printf("EXPLORER SCROLL list=%u vertical=%u min=%u max=%u page=%u position=%u\n", list,
           vertical, info.minimum, info.maximum, info.page, info.position);
    fflush(stdout);

    Gdi::RECT window{}, client{};
    Gdi::POINT origin{0, 0};
    require(User::GetWindowRect(list, &window), "list window rect");
    require(User::GetClientRect(list, &client), "list client rect");
    require(User::ScreenToClient(list, &origin), "list client origin");
    const Gdi::RECT view{-origin.x, -origin.y, -origin.x + client.right, -origin.y + client.bottom};
    require(view.right - view.left > 40 && view.bottom - view.top > 60, "list pane large enough");
    // The bar sits between the client edge and the window edge, on the right
    // for a vertical bar and along the bottom for a horizontal one.
    const int bar_across = vertical ? (view.right + window.right) / 2
                                    : (view.bottom + window.bottom) / 2;
    const int bar_start = vertical ? view.top : view.left;
    const int bar_end = vertical ? view.bottom : view.right;
    require(bar_across > (vertical ? view.right : view.bottom) &&
            bar_across < (vertical ? window.right : window.bottom), "bar band");
    // The window has to be the active one before a press on its frame is a
    // scrollbar press and not just an activation. A click on empty space
    // below the entries selects nothing.
    shell->MouseButtons(view.right - 12, view.bottom - 12, 0);
    shell->MouseButtons(view.right - 12, view.bottom - 12, 1);
    pump(120);
    shell->MouseButtons(view.right - 12, view.bottom - 12, 0);
    pump(400);
    screenshot("build/explorer-scroll/before.ppm");
    // One mouse step along the bar, whichever axis it runs on.
    auto at = [&](int along, unsigned buttons) {
        if (vertical) shell->MouseButtons(bar_across, along, buttons);
        else shell->MouseButtons(along, bar_across, buttons);
    };

    // the thumb. The tracker reports the live position with
    // SB_THUMBTRACK, and the original list view reads it with SIF_TRACKPOS.
    const unsigned start = position_of(list, vertical);
    const int grab = bar_start + 24;
    unsigned during = start;
    {
        const int px = vertical ? bar_across : grab, py = vertical ? grab : bar_across;
        printf("EXPLORER SCROLL press at=%d,%d style=%08x window=%d,%d,%d,%d view=%d,%d,%d,%d "
               "hit=%u\n", px, py, User::Style(list), window.left, window.top, window.right,
               window.bottom, view.left, view.top, view.right, view.bottom,
               User::PointWindow(0, User::GetDesktopWindow(), px, py, 0, nullptr, nullptr, 0));
        printf("EXPLORER SCROLL framehit=%u default=%u\n", User::FrameHitTest(list, px, py),
               User::DefaultMessage(list, 0x84, 0, (unsigned)((py << 16) | (px & 0xffff))));
        fflush(stdout);
        fflush(stdout);
    }
    at(grab, 0);
    at(grab, 1);
    pump(600);
    printf("EXPLORER SCROLL tracking=%d capture=%u\n", (int)User::FrameTrackActive(),
           User::CaptureOperation(0, 0, 0));
    fflush(stdout);
    for (int i = 1; i <= 6; ++i) {
        at(grab + i * 12, 1);
        pump(80);
        during = position_of(list, vertical);
    }
    const int release = grab + 72;
    at(release, 1);
    pump(150);
    const unsigned tracked = position_of(list, vertical);
    at(release, 0);
    pump(250);
    printf("EXPLORER SCROLL thumb start=%u during=%u tracked=%u released=%u\n", start, during,
           tracked, position_of(list, vertical));
    fflush(stdout);
    quiet("list pane repaints after the thumb drag", 60000);
    const unsigned after = position_of(list, vertical);
    printf("EXPLORER SCROLL thumb after=%u\n", after);
    fflush(stdout);
    screenshot("build/explorer-scroll/dragged.ppm");
    // The pane has to move while the button is still held, which is the part
    // was about, and the release must not take the move back. How far the
    // last move has been dispatched when the sample is taken is a matter of
    // timing, so the drag only has to end at or past what it had reported.
    require(during > start, "the list pane follows the thumb while it is dragged");
    require(after >= during && after > start, "the thumb drag moves the list pane");
    require(tracked <= after, "the release does not undo the drag");

    // every row that became visible is completely drawn. The scrolled
    // image has to be the image of a full repaint of the same view.
    auto scrolled = capture(view);
    require(!scrolled.empty(), "composed surface");
    User::InvalidateRect(list, nullptr, true);
    pump(150);
    quiet("list pane repaints in full", 60000);
    pump(250);
    auto repainted = capture(view);
    unsigned wrong = differing(scrolled, repainted);
    printf("EXPLORER SCROLL after thumb drag differing=%u of %u\n", wrong,
           (unsigned)scrolled.size());
    fflush(stdout);
    screenshot("build/explorer-scroll/repainted.ppm");
    require(wrong == 0, "the pane after a thumb drag equals a full repaint");

    // The same again with the arrow at the far end of the bar, in small steps,
    // which is the sequence the user reported the stripes for.
    for (int step = 0; step < 8; ++step) {
        at(bar_end - 6, 0);
        at(bar_end - 6, 1);
        pump(90);
        at(bar_end - 6, 0);
        pump(90);
    }
    quiet("list pane repaints after the arrow steps", 60000);
    pump(250);
    const unsigned stepped = position_of(list, vertical);
    require(stepped > after, "the arrow moves the list pane");
    scrolled = capture(view);
    screenshot("build/explorer-scroll/stepped.ppm");
    User::InvalidateRect(list, nullptr, true);
    pump(150);
    quiet("list pane repaints in full after the arrow steps", 60000);
    pump(250);
    repainted = capture(view);
    wrong = differing(scrolled, repainted);
    printf("EXPLORER SCROLL after arrow steps position=%u differing=%u of %u\n", stepped, wrong,
           (unsigned)scrolled.size());
    fflush(stdout);
    require(wrong == 0, "the pane after arrow steps equals a full repaint");

    puts("EXPLORER SCROLL PASS");
    fflush(stdout);
    return 0;
}
