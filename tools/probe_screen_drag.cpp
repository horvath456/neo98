// SPDX-License-Identifier: GPL-2.0-or-later
// Measure Explorer window dragging. With full-window dragging disabled,
// Windows 98 moves an outline and repaints exposed areas on release. Record
// wall-clock latency for each input step and final release, plus
// published-frame and Render counts. Write SCREEN DRAG records to stdout.
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *shell;
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "SCREEN DRAG FAILED: %s\n", why);
    snapshot_windows();
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
static bool settled()
{
    return shell->WaitingForInput() && !User::Rendering() && !User::PendingPaintWindows(nullptr) && !User::Dirty();
}
template <class F> static void until(F condition, const char *why, unsigned limit = 8000)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit);
    while (!condition() && std::chrono::steady_clock::now() < end) pump(5);
    require(condition(), why);
}
static unsigned find(const char *name)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char c[128];
        User::GetClassName(nodes[i], c, sizeof c);
        if (User::IsVisible(nodes[i]) && !strcmp(c, name)) return nodes[i];
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
static double since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
// After input, pump until a new frame is published and work is idle, allowing
// up to two seconds for the frame.
static void step_settle()
{
    const unsigned serial = User::FrameSerial();
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    do {
        shell->Pump(32);
        require(shell->Status() != App::Failed, shell->Error());
    } while ((User::FrameSerial() == serial && std::chrono::steady_clock::now() < limit) || !settled());
}
// Pump until idle and measure elapsed milliseconds.
static double settle(const char *why)
{
    const auto start = std::chrono::steady_clock::now();
    until(settled, why, 30000);
    return since(start);
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
    unsigned rounds = 4, steps = 24;
    int depth = 0, start_depth = 0;
    const char *shots = nullptr;
    for (int i = 4; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "--rounds")) rounds = unsigned(atoi(argv[i + 1]));
        if (!strcmp(argv[i], "--steps")) steps = unsigned(atoi(argv[i + 1]));
        if (!strcmp(argv[i], "--shots")) shots = argv[i + 1];
        if (!strcmp(argv[i], "--depth")) depth = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--start-depth")) start_depth = atoi(argv[i + 1]);
    }

    const bool trace_calls = getenv("SCREEN_DRAG_TRACE") != nullptr;
    if (trace_calls) setvbuf(stdout, nullptr, _IONBF, 0);
    shell = new App(trace_calls ? logger : nullptr);
    shell->trace_calls = trace_calls;
    require(shell->Start(exe.data(), exe.size(), font.data(), font.size(), "EXPLORER.EXE",
                         "EXPLORER.EXE /e,C:\\WINDOWS\\HELP"), "explorer start");
    // --start-depth selects the desktop depth before the shell creates its
    // first window, as after booting with that registry setting.
    if (start_depth)
        require(User::SetDesktopMode(800, 600, start_depth) && User::SetScreenMode(800, 600, start_depth), "start depth");
    until([&] {
        if (auto h = visible_dialog("RUNDLL")) {
            auto b = User::GetDlgItem(h, 2);
            if (!b) b = User::GetDlgItem(h, 1);
            if (b) click(b);
        }
        return find("SysTreeView32") && find("SysListView32") && shell->WaitingForInput();
    }, "Explorer folder window opens", 240000);
    until(settled, "folder window settles", 180000);
    pump(800);
    until(settled, "folder window settles again", 30000);
    if (shots && start_depth) { char path[512]; snprintf(path, sizeof path, "%s/start-%d.ppm", shots, start_depth); screenshot(path); }
    // --depth switches the desktop to 800x600 at the requested depth,
    // requiring windows to repaint in that format.
    if (depth) {
        require(User::SetDesktopMode(800, 600, depth) && User::SetScreenMode(800, 600, depth), "desktop depth");
        pump(500);
        until(settled, "folder window settles in the new depth", 60000);
        pump(500);
        until(settled, "folder window settles in the new depth again", 60000);
        if (shots) { char path[512]; snprintf(path, sizeof path, "%s/depth-%d.ppm", shots, depth); screenshot(path); }
    }
    // The folder window is the list view's topmost ancestor.
    unsigned frame = find("SysListView32");
    while (User::Parent(frame) && User::Parent(frame) != User::GetDesktopWindow()) frame = User::Parent(frame);
    require(frame, "folder frame");
    // Shrink the window to leave room for dragging.
    Gdi::RECT r{};
    require(User::GetWindowRect(frame, &r), "frame rect");
    printf("SCREEN DRAG frame=%u rect=%d,%d,%d,%d\n", frame, r.left, r.top, r.right, r.bottom);
    if (shots) { char path[512]; snprintf(path, sizeof path, "%s/before.ppm", shots); screenshot(path); }
    if (getenv("SCREEN_DRAG_WINDOWS")) {
        auto sample = [](const char *what) {
            const unsigned char *s = User::ScreenBits(), *pub = User::Pixels();
            const int stride = User::ScreenStride();
            const int points[][2] = {{100, 62}, {95, 89}, {300, 120}, {160, 155}, {70, 300}};

            for (auto &pt : points) {
                const unsigned char *a = s + pt[1] * stride + pt[0] * 4, *b = pub + (pt[1] * 800 + pt[0]) * 4;
                printf("SAMPLE %s at=%d,%d screen=%02x%02x%02x published=%02x%02x%02x\n", what, pt[0], pt[1], a[2], a[1], a[0], b[2], b[1], b[0]);
            }
        };
        sample("settled");
        printf("RENDER dirty=%d result=%d\n", User::Dirty() ? 1 : 0, User::Render() ? 1 : 0);
        sample("rendered");
        static unsigned nodes[User::WindowCapacity]; unsigned n = 0;
        User::WindowSnapshot(0, nodes, User::WindowCapacity, n);
        for (unsigned k = 0; k < n && n < User::WindowCapacity; ++k) {
            unsigned more[256], m = 0;
            if (User::WindowSnapshot(nodes[k], more, 256, m))
                for (unsigned j = 0; j < m && n < User::WindowCapacity; ++j) nodes[n++] = more[j];
        }
        for (unsigned i = 0; i < n; ++i) {
            char c[128]; Gdi::RECT wr{}; Gdi::RECT u[4]; bool has = false, drawn = false;
            User::GetClassName(nodes[i], c, sizeof c); User::GetWindowRect(nodes[i], &wr);
            const unsigned updates = User::UpdateRectangles(nodes[i], u, 4);
            User::CanvasInfo(nodes[i], has, drawn);
            if (auto dc = User::GetDC(nodes[i])) {
                Gdi::RECT box{}; const unsigned kind = Gdi::GetClipBox(dc, &box);
                printf("CLIP %u kind=%u box=%d,%d,%d,%d\n", nodes[i], kind, box.left, box.top, box.right, box.bottom);
                User::ReleaseDC(nodes[i], dc);
            }
            printf("WINDOW %u parent=%u class=%s visible=%u style=%08x rect=%d,%d,%d,%d update=%u drawn=%u thread=%u\n", nodes[i],
                   User::Parent(nodes[i]), c, User::IsVisible(nodes[i]) ? 1u : 0u, User::Style(nodes[i]), wr.left, wr.top, wr.right, wr.bottom,
                   updates, drawn ? 1u : 0u, User::WindowThread(nodes[i]));
        }
    }
    double move_total = 0, release_total = 0, worst_move = 0, worst_release = 0;
    unsigned moves = 0;
    const unsigned frames_before = User::FrameSerial();
    const auto all = std::chrono::steady_clock::now();
    for (unsigned round = 0; round < rounds; ++round) {
        require(User::GetWindowRect(frame, &r), "frame rect");
        // The title bar, left of its buttons.
        const int grab_x = r.left + 60, grab_y = r.top + 10;
        const int dx = (round & 1) ? -8 : 8, dy = (round & 1) ? -4 : 4;
        shell->MouseButtons(grab_x, grab_y, 0);
        pump(20);
        shell->MouseButtons(grab_x, grab_y, 1);
        settle("press settles");
        for (unsigned step = 1; step <= steps; ++step) {
            const auto start = std::chrono::steady_clock::now();
            shell->MouseButtons(grab_x + dx * int(step), grab_y + dy * int(step), 1);
            step_settle();
            const double ms = since(start);
            move_total += ms; ++moves;
            if (ms > worst_move) worst_move = ms;
        }
        // Before release, the drag outline is inverted in screen memory.
        if (shots) { char path[512]; snprintf(path, sizeof path, "%s/round-%u-dragging.ppm", shots, round); screenshot(path); }
        const auto start = std::chrono::steady_clock::now();
        shell->MouseButtons(grab_x + dx * int(steps), grab_y + dy * int(steps), 0);
        step_settle();
        const double ms = since(start);
        release_total += ms;
        if (ms > worst_release) worst_release = ms;
        Gdi::RECT now{};
        User::GetWindowRect(frame, &now);
        printf("SCREEN DRAG round=%u release_ms=%.1f moved_to=%d,%d\n", round, ms, now.left, now.top);
        if (shots) { char path[512]; snprintf(path, sizeof path, "%s/round-%u.ppm", shots, round); screenshot(path); }
    }
    const double total = since(all);
    // Measure repaint cost by invalidating the entire folder window and
    // children with RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN.
    double repaint_total = 0;
    const unsigned repaints = 5;
    for (unsigned i = 0; i < repaints; ++i) {
        const auto start = std::chrono::steady_clock::now();
        User::RedrawWindow(frame, nullptr, 0, 0x485);
        step_settle();
        repaint_total += since(start);
    }
    printf("SCREEN DRAG repaint_avg_ms=%.1f\n", repaint_total / repaints);
    printf("SCREEN DRAG moves=%u move_avg_ms=%.2f move_worst_ms=%.1f release_avg_ms=%.1f release_worst_ms=%.1f "
           "total_ms=%.0f frames=%u\n", moves, moves ? move_total / moves : 0.0, worst_move,
           rounds ? release_total / rounds : 0.0, worst_release, total, User::FrameSerial() - frames_before);
    fflush(stdout);
    return 0;
}
