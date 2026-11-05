// SPDX-License-Identifier: GPL-2.0-or-later
// Functional check for RUNDLL32.EXE shell32.dll,Control_RunDLL <applet>.cpl:
// the applet's own property sheet must appear on the rundll32 thread, stay in
// its modal loop, accept real input packets and close through Cancel. Read-only
// host files are copied into the native RAM volume; no source file is changed.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include "registry_file.h"
#include "host_metal.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || n > 8 * 1024 * 1024) { fclose(f); return {}; }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    fclose(f);
    return b;
}
static const char *leaf(const char *s)
{
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static void hex_text(const char *text)
{
    for (const auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) printf("%02x", *p);
}
static void logger(const char *s) { puts(s); fflush(stdout); }
static bool screenshot(const char *directory, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P6\n800 600\n255\n");
    User::Render(); // compose the current window tree; no guest callback runs here
    const auto *pixels = User::Pixels();
    for (unsigned i = 0; i < 800 * 600; ++i) {
        unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        if (fwrite(rgb, 1, 3, file) != 3) { fclose(file); return false; }
    }
    if (fclose(file)) return false;
    printf("CPL_SHOT name=%s\n", name);
    return true;
}
// Read-only inspection of the live window tree. No callback is entered here.
static unsigned find_sheet(const char *caption, unsigned &page, unsigned &tab)
{
    page = tab = 0;
    unsigned nodes[192], count = 0;
    if (!User::WindowSnapshot(0, nodes, 192, count)) return 0;
    for (unsigned i = 0; i < count; ++i) {
        char klass[64], text[256];
        User::GetClassName(nodes[i], klass, sizeof klass);
        User::GetWindowText(nodes[i], text, sizeof text);
        // An empty --caption accepts any visible property sheet, allowing
        // applets with different titles to share this test.
        if (!User::IsVisible(nodes[i]) || strcmp(klass, "#32770")) continue;
        if (caption[0] && strcmp(text, caption)) continue;
        unsigned frame = nodes[i], children[192], n = 0;
        User::WindowSnapshot(frame, children, 192, n);
        for (unsigned c = 0; c < n; ++c) {
            if (User::Parent(children[c]) != frame) continue;
            char name[64];
            User::GetClassName(children[c], name, sizeof name);
            if (!page && !strcmp(name, "#32770") && User::IsVisible(children[c])) page = children[c];
            if (!tab && !strcmp(name, "SysTabControl32")) tab = children[c];
        }
        return frame;
    }
    return 0;
}
static unsigned descendant(unsigned root, const char *klass, unsigned index)
{
    unsigned nodes[192], count = 0, seen = 0;
    if (!User::WindowSnapshot(root, nodes, 192, count)) return 0;
    for (unsigned i = 0; i < count; ++i) {
        char name[64];
        User::GetClassName(nodes[i], name, sizeof name);
        if (strcasecmp(name, klass) || !User::IsVisible(nodes[i])) continue;
        if (seen++ == index) return nodes[i];
    }
    return 0;
}
static void run(X86::Application &a, unsigned slices)
{
    for (unsigned i = 0; i < slices; ++i) a.Pump();
}
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    const char *command = nullptr, *out = nullptr, *caption = "Date/Time Properties";
    int limit = 90;
    // --trace-from delays call tracing until the specified call number to
    // keep startup out of the log.
    bool trace_calls = false, sheet_only = false;
    // --control reports the rectangle of a window class so display-applet
    // tests can inspect preview pixels.
    const char *control = nullptr;
    // --tab selects another property-sheet page with PSM_SETCURSEL and
    // reports its control texts, including the display applet's Settings
    // page.
    int tab_index = -1;
    // --pick ID=N selects list item N, --click ID clicks a page button, and
    // --apply clicks Apply. The Glide-page test uses these to check registry
    // writes.
    std::vector<std::pair<unsigned, int>> edits; // (id, index); -1 means click.
    bool apply = false;
    // --wizard reports page controls, clicks Next N times and reports each
    // page, then closes through Cancel.
    int wizard_steps = -1;
    // --wizard-text fills the first edit control before Next, including
    // WM_SETTEXT and EN_CHANGE notifications.
    const char *wizard_text = nullptr;
    // --wizard-button clicks the named page control before Next.
    const char *wizard_button = nullptr;
    unsigned trace_from = 0;
    // SHELL32 runs as original x86 code since PLAN.md step 7 and pulls SHLWAPI
    // and COMCTL32 with it, so the applet needs the imported Win98 tree under
    // the seeded files.
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    if (tree_options.tree && !SystemTreeHost::Mount(tree_options.root)) {
        fprintf(stderr, "cannot mount the imported Win98 tree\n");
        return 2;
    }
    // --registry FILE: a change journal to start from and to write back, as
    // in probe_application; the Glide page test registers the driver's page
    // with it.
    RegistryFile::Attach(argc, argv, nullptr);
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--registry")) { if (++i == argc) return 2; continue; }
        // --gpu: the rasterizer the host window has, so the Glide driver and
        // its page GLIDEPAG.DLL exist.
        if (!strcmp(argv[i], "--gpu")) { X86::Application::SetGpuBackend(HostMetal::Install()); continue; }
        if (!strcmp(argv[i], "--system-tree")) {
            if (i + 1 < argc && argv[i + 1][0] != '-') ++i;
            continue;
        }
        if (!strcmp(argv[i], "--trace-calls")) { trace_calls = true; continue; }
        if (!strcmp(argv[i], "--sheet-only")) { sheet_only = true; continue; }
        if (!strcmp(argv[i], "--control")) { if (++i == argc) return 2; control = argv[i]; continue; }
        if (!strcmp(argv[i], "--tab")) { if (++i == argc) return 2; tab_index = atoi(argv[i]); continue; }
        if (!strcmp(argv[i], "--pick")) {
            if (++i == argc) return 2;
            unsigned id = 0; int index = 0;
            if (sscanf(argv[i], "%u=%d", &id, &index) != 2) return 2;
            edits.push_back({id, index});
            continue;
        }
        if (!strcmp(argv[i], "--click")) { if (++i == argc) return 2; edits.push_back({unsigned(atoi(argv[i])), -1}); continue; }
        if (!strcmp(argv[i], "--apply")) { apply = true; continue; }
        if (!strcmp(argv[i], "--wizard")) { if (++i == argc) return 2; wizard_steps = atoi(argv[i]); continue; }
        if (!strcmp(argv[i], "--wizard-text")) { if (++i == argc) return 2; wizard_text = argv[i]; continue; }
        if (!strcmp(argv[i], "--wizard-button")) { if (++i == argc) return 2; wizard_button = argv[i]; continue; }
        if (!strcmp(argv[i], "--trace-from")) {
            if (++i == argc) return 2;
            trace_from = (unsigned)strtoul(argv[i], nullptr, 10);
            trace_calls = true;
            continue;
        }
        if (!strcmp(argv[i], "--command") || !strcmp(argv[i], "--output") ||
            !strcmp(argv[i], "--caption") || !strcmp(argv[i], "--time-limit")) {
            const char *option = argv[i];
            if (++i == argc) return 2;
            if (!strcmp(option, "--command")) command = argv[i];
            else if (!strcmp(option, "--output")) out = argv[i];
            else if (!strcmp(option, "--caption")) caption = argv[i];
            else limit = atoi(argv[i]);
            continue;
        }
        auto bytes = load(argv[i]);
        char guest[260];
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\SYSTEM\\%s", leaf(argv[i]));
        if (bytes.empty() || Files::System().Seed(guest, bytes.data(), bytes.size())) return 3;
    }
    if (!out) return 2;
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *a = new X86::Application(logger);
    a->trace_calls = trace_calls;
    a->trace_from = trace_from;
    if (!a->Start(exe.data(), exe.size(), font.data(), font.size(), leaf(argv[1]), command)) return 3;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limit);
    unsigned frame = 0, page = 0, tab = 0;
    auto tick = std::chrono::steady_clock::now();
    while (a->Status() != X86::Application::Exited && a->Status() != X86::Application::Failed &&
           std::chrono::steady_clock::now() < deadline) {
        a->Pump();
        // CPL_PROGRESS=1 reports counters while the guest runs. A frozen call
        // count with state Running and block 1 means the loop that owns the wait
        // is not delivering a message the scheduler already considers ready.
        if (getenv("CPL_PROGRESS") && std::chrono::steady_clock::now() - tick >= std::chrono::seconds(2)) {
            tick = std::chrono::steady_clock::now();
            printf("CPL_PROGRESS calls=%u callbacks=%u windows=%u gdi=%u state=%u block=%u\n",
                   a->ApiCalls(), a->Callbacks(), User::LiveWindows(), Gdi::LiveObjects(),
                   a->Status(), a->threads[0].block);
            fflush(stdout);
        }
        if (a->WaitingForInput()) { frame = find_sheet(caption, page, tab); break; }
        if (a->Status() == X86::Application::Idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        // Log the discovered caption so runs without --caption identify the
        // opened property sheet.
        char found[256] = {};
        if (frame) User::GetWindowText(frame, found, sizeof found);
        printf("CPL_TITLE text=");
        hex_text(found);
        putchar('\n');
    }
    printf("CPL_SHEET frame=%u page=%u tab=%u calls=%u callbacks=%u windows=%u state=%u caption=",
           frame, page, tab, a->ApiCalls(), a->Callbacks(), User::LiveWindows(), a->Status());
    hex_text(caption);
    putchar('\n');
    if (!frame || !page || !tab) { printf("CPL_RESULT FAIL no-sheet\n"); return 4; }
    if (control) {
        const unsigned found = descendant(page, control, 0);
        Gdi::RECT area{};
        if (found) User::GetWindowRect(found, &area);
        printf("CPL_CONTROL class=%s hwnd=%u rect=%d,%d,%d,%d visible=%u\n", control, found,
               area.left, area.top, area.right, area.bottom, found ? User::IsVisible(found) : 0);
    }
    if (!screenshot(out, "sheet")) return 5;

    // --sheet-only checks opening and cancellation without the date applet's
    // month-list steps.
    Gdi::RECT bounds{};
    if (!sheet_only) {
        // First real input packet sequence: open the month ComboBox in the applet's
        // own page and pick the next entry with the mouse.
        unsigned combo = descendant(page, "COMBOBOX", 0);
        if (!combo) { printf("CPL_RESULT FAIL no-combo\n"); return 4; }
        char before_text[128] = {}, after_text[128] = {};
        User::GetWindowText(combo, before_text, sizeof before_text);
        unsigned before = a->Send(combo, 0x147, 0, 0), target = before + 1;
        Gdi::RECT bounds{};
        User::GetWindowRect(combo, &bounds);
        const int bx = bounds.right - 9, by = (bounds.top + bounds.bottom) / 2;
        a->Mouse(bx, by, true, false);
        a->Mouse(bx, by, false, true);
        run(*a, 12);
        unsigned list = 0, nodes[192], count = 0;
        User::WindowSnapshot(0, nodes, 192, count);
        for (unsigned i = 0; i < count; ++i)
            if (User::Owner(nodes[i]) == combo && User::IsVisible(nodes[i])) { list = nodes[i]; break; }
        if (!list) { printf("CPL_RESULT FAIL no-dropdown combo=%u\n", combo); return 4; }
        run(*a, 24); // let the drop-down paint before the capture
        {
            // The open list is a real top-level window owned by the ComboBox. The
            // capture below has to show it, and the sibling order has to keep it
            // in front of the sheet it belongs to.
            Gdi::RECT area{};
            User::GetWindowRect(list, &area);
            printf("CPL_DROPDOWN rect=%d,%d,%d,%d visible=%u style=%08x\n", area.left, area.top,
                   area.right, area.bottom, User::IsVisible(list), User::Style(list));
            printf("CPL_ZORDER");
            for (unsigned top = User::GetTopWindow(User::GetDesktopWindow()); top;
                 top = User::GetWindow(top, 2)) {
                char name[64];
                User::GetClassName(top, name, sizeof name);
                printf(" %u:%s:owner=%u:vis=%u", top, name, User::Owner(top), User::IsVisible(top));
            }
            putchar('\n');
        }
        if (!screenshot(out, "month-list")) return 5;
        unsigned storage = a->Allocate(sizeof bounds);
        a->Send(list, 0x198, target, storage);
        a->Read(storage, &bounds, sizeof bounds);
        a->Free(storage);
        Gdi::POINT point{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2}, origin{};
        User::ScreenToClient(list, &origin);
        point.x -= origin.x;
        point.y -= origin.y;
        a->Mouse(point.x, point.y, true, false);
        a->Mouse(point.x, point.y, false, true);
        run(*a, 12);
        unsigned after = a->Send(combo, 0x147, 0, 0);
        User::GetWindowText(combo, after_text, sizeof after_text);
        printf("CPL_MONTH combo=%u list=%u before=%u target=%u after=%u visible=%u before_text=",
               combo, list, before, target, after, User::IsVisible(list));
        hex_text(before_text);
        printf(" after_text=");
        hex_text(after_text);
        printf(" calls=%u callbacks=%u\n", a->ApiCalls(), a->Callbacks());
        if (!screenshot(out, "month")) return 5;
        if (after != target || User::IsVisible(list) || a->Status() == X86::Application::Failed) {
            printf("CPL_RESULT FAIL month-selection\n");
            return 4;
        }
    }

    for (int step = 0; step <= wizard_steps; ++step) {
        if (step == 1 && wizard_button) {
            unsigned page_now = 0, strip = 0;
            find_sheet(caption, page_now, strip);
            unsigned nodes[192], count = 0, target = 0;
            if (page_now) User::WindowSnapshot(page_now, nodes, 192, count);
            for (unsigned i = 0; i < count && !target; ++i) {
                char text[256] = {};
                User::GetWindowText(nodes[i], text, sizeof text);
                if (User::IsButton(nodes[i]) && strstr(text, wizard_button)) target = nodes[i];
            }
            Gdi::RECT box{};
            if (!target || !User::GetWindowRect(target, &box)) { printf("CPL_RESULT FAIL no-button\n"); return 4; }
            const int bx = box.left + 6, by = (box.top + box.bottom) / 2;
            a->MouseButtons(bx, by, 0);
            a->MouseButtons(bx, by, 1);
            a->MouseButtons(bx, by, 0);
            run(*a, 30);
        }
        if (step == 1 && wizard_text) {
            unsigned page_now = 0, strip = 0;
            find_sheet(caption, page_now, strip);
            const unsigned edit = page_now ? descendant(page_now, "EDIT", 0) : 0;
            if (!edit) { printf("CPL_RESULT FAIL no-edit\n"); return 4; }
            // Click the field and type through keyboard events.
            Gdi::RECT box{};
            User::GetWindowRect(edit, &box);
            const int ex = (box.left + box.right) / 2, ey = (box.top + box.bottom) / 2;
            a->MouseButtons(ex, ey, 0);
            a->MouseButtons(ex, ey, 1);
            a->MouseButtons(ex, ey, 0);
            run(*a, 20);
            // USER translates the key with the layout: the key and
            // the shift state are the ones VkKeyScan names for the character.
            for (const char *c = wizard_text; *c; ++c) {
                const unsigned ch = (unsigned char)*c;
                const unsigned scanned = a->KeyScan(ch);
                if (scanned == 0xffff) continue;
                const unsigned vk = scanned & 255, modifiers = (scanned >> 8) & 7;
                if (modifiers & 1) a->RawKey(0xa0, 0, true, 0x2a, false);
                if (modifiers & 2) a->RawKey(0xa2, 0, true, 0x1d, false);
                if (modifiers & 4) a->RawKey(0xa4, 0, true, 0x38, false);
                a->RawKey(vk, ch, true);
                a->RawKey(vk, 0, false);
                if (modifiers & 4) a->RawKey(0xa4, 0, false, 0x38, false);
                if (modifiers & 2) a->RawKey(0xa2, 0, false, 0x1d, false);
                if (modifiers & 1) a->RawKey(0xa0, 0, false, 0x2a, false);
                run(*a, 4);
            }
            run(*a, 50);
        }
        if (step) {
            const unsigned next = User::GetDlgItem(frame, 0x3024);
            Gdi::RECT area{};
            if (!next || !User::IsVisible(next) || !User::IsEnabled(next) || !User::GetWindowRect(next, &area)) {
                printf("CPL_RESULT FAIL no-next step=%d\n", step);
                return 4;
            }
            unsigned before = 0, strip = 0;
            find_sheet(caption, before, strip);
            const int nx = (area.left + area.right) / 2, ny = (area.top + area.bottom) / 2;
            a->MouseButtons(nx, ny, 0);
            a->MouseButtons(nx, ny, 1);
            a->MouseButtons(nx, ny, 0);
            const auto moving = std::chrono::steady_clock::now() + std::chrono::seconds(limit);
            while (a->Status() != X86::Application::Failed && a->Status() != X86::Application::Exited &&
                   std::chrono::steady_clock::now() < moving) {
                a->Pump();
                unsigned now = 0;
                find_sheet(caption, now, strip);
                if (now && now != before && a->WaitingForInput()) break;
                if (a->Status() == X86::Application::Idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            run(*a, 50);
            if (a->Status() == X86::Application::Failed) {
                printf("CPL_RESULT FAIL wizard step=%d error=%s\n", step, a->Error());
                return 4;
            }
        }
        if (!step) run(*a, 50);
        char title[256] = {};
        User::GetWindowText(frame, title, sizeof title);
        unsigned shown = 0, strip = 0;
        find_sheet(caption, shown, strip);
        printf("CPL_WIZARD step=%d page=%u title=", step, shown);
        hex_text(title);
        putchar('\n');
        const unsigned ids[] = {0x3023, 0x3024, 0x3025, 2, 9};
        for (unsigned id : ids) {
            const unsigned button = User::GetDlgItem(frame, id);
            char text[64] = {};
            if (button) User::GetWindowText(button, text, sizeof text);
            printf("CPL_WIZBUTTON step=%d id=%u visible=%u enabled=%u default=%u text=", step, id,
                   button ? User::IsVisible(button) : 0, button ? User::IsEnabled(button) : 0,
                   button ? (User::Style(button) & 15) == 1 : 0);
            hex_text(text);
            putchar('\n');
        }
        unsigned nodes[192], count = 0;
        if (shown) User::WindowSnapshot(shown, nodes, 192, count);
        for (unsigned i = 0; i < count; ++i) {
            char klass[64], text[256];
            User::GetClassName(nodes[i], klass, sizeof klass);
            User::GetWindowText(nodes[i], text, sizeof text);
            unsigned id = 0;
            User::WindowLong(nodes[i], -12, id, false);
            printf("CPL_WIZITEM step=%d id=%u class=%s visible=%u text=", step, id, klass, User::IsVisible(nodes[i]));
            hex_text(text);
            putchar('\n');
            // A visible list view: its item count and the label of item 0
            // (LVM_GETITEMCOUNT, LVM_GETITEMTEXTA), for the model list of the
            // modem wizard.
            if (User::IsVisible(nodes[i]) && strstr(klass, "ListView")) {
                const unsigned items = a->Send(nodes[i], 0x1004, 0, 0);
                char first[256] = {};
                const unsigned block = a->Allocate(40 + 256);
                if (block && items) {
                    unsigned item[10] = {1, 0, 0, 0, 0, block + 40, 256, 0, 0, 0};
                    a->Store(block, item, sizeof item);
                    a->Send(nodes[i], 0x102d, 0, block);
                    a->Read(block + 40, first, sizeof first - 1);
                }
                if (block) a->Free(block);
                // The vertical bar (WS_VSCROLL) and the top row after one
                // SB_PAGEDOWN (LVM_GETTOPINDEX); SB_TOP puts it back.
                const unsigned bar = (User::Style(nodes[i]) & 0x200000u) ? 1 : 0;
                a->Send(nodes[i], 0x115, 3, 0);
                const unsigned paged = a->Send(nodes[i], 0x1027, 0, 0);
                a->Send(nodes[i], 0x115, 6, 0);
                printf("CPL_WIZLIST step=%d count=%u scroll=%u paged=%u first=", step, items, bar, paged);
                hex_text(first);
                putchar('\n');
            }
        }
        char name[32];
        snprintf(name, sizeof name, "wizard-%d", step);
        if (!screenshot(out, name)) return 5;
    }

    if (tab_index >= 0) {
        // Click the tab as a user would. TCM_GETITEMRECT returns its
        // rectangle in tab-control client coordinates.
        unsigned storage = a->Allocate(16);
        Gdi::RECT item{};
        const unsigned found = a->Send(tab, 0x130a, unsigned(tab_index), storage); // TCM_GETITEMRECT
        a->Read(storage, &item, sizeof item);
        a->Free(storage);
        Gdi::POINT origin{};
        User::ScreenToClient(tab, &origin);
        const int tx = (item.left + item.right) / 2 - origin.x, ty = (item.top + item.bottom) / 2 - origin.y;
        printf("CPL_TAB_CLICK found=%u x=%d y=%d\n", found, tx, ty);
        a->MouseButtons(tx, ty, 0);
        a->MouseButtons(tx, ty, 1);
        a->MouseButtons(tx, ty, 0);
        const auto switching = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (a->Status() != X86::Application::Failed && std::chrono::steady_clock::now() < switching) {
            a->Pump();
            unsigned now = 0, strip = 0;
            find_sheet(caption, now, strip);
            if (now && now != page && a->WaitingForInput()) break;
        }
        run(*a, 50);
        unsigned shown = 0, strip = 0;
        find_sheet(caption, shown, strip);
        unsigned nodes[192], count = 0;
        if (shown) User::WindowSnapshot(shown, nodes, 192, count);
        printf("CPL_TAB index=%d page=%u children=%u\n", tab_index, shown, count);
        for (unsigned i = 0; i < count; ++i) {
            char klass[64], text[256];
            User::GetClassName(nodes[i], klass, sizeof klass);
            User::GetWindowText(nodes[i], text, sizeof text);
            unsigned items = 0;
            if (!strcasecmp(klass, "COMBOBOX")) items = a->Send(nodes[i], 0x146, 0, 0); // CB_GETCOUNT
            Gdi::RECT area{};
            User::GetWindowRect(nodes[i], &area);
            unsigned id = 0;
            User::WindowLong(nodes[i], -12, id, false); // GWL_ID
            printf("CPL_ITEM hwnd=%u id=%u style=%08x class=%s visible=%u items=%u rect=%d,%d,%d,%d text=", nodes[i],
                   id, User::Style(nodes[i]), klass, User::IsVisible(nodes[i]), items, area.left, area.top,
                   area.right, area.bottom);
            hex_text(text);
            putchar('\n');
            if (!strncasecmp(klass, "SysTreeView", 11)) {
                // The first two levels of a tree view: TVM_GETNEXTITEM (root,
                // child, next) and TVM_GETITEM with the text.
                const unsigned item = a->Allocate(40 + 128);
                for (unsigned top = a->Send(nodes[i], 0x110a, 0, 0); top && item; top = a->Send(nodes[i], 0x110a, 1, top)) {
                    for (unsigned depth = 0, node = top; node && depth < 2;) {
                        const unsigned tv[10] = {1, node, 0, 0, item + 40, 128, 0, 0, 0, 0};
                        a->Store(item, tv, sizeof tv);
                        a->Send(nodes[i], 0x110c, 0, item);
                        char label[129] = {};
                        a->Read(item + 40, label, 128);
                        printf("CPL_TREE hwnd=%u depth=%u text=", nodes[i], depth);
                        hex_text(label);
                        putchar('\n');
                        if (!depth) { node = a->Send(nodes[i], 0x110a, 4, top); depth = 1; }
                        else node = a->Send(nodes[i], 0x110a, 1, node);
                    }
                }
                if (item) a->Free(item);
            }
        }
        if (!screenshot(out, "tab")) return 5;
        // Use normal input: click the control center, then navigate list
        // boxes with Down and Enter.
        auto click = [&](unsigned hwnd) {
            Gdi::RECT r{};
            User::GetWindowRect(hwnd, &r);
            const int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
            a->MouseButtons(x, y, 0);
            a->MouseButtons(x, y, 1);
            a->MouseButtons(x, y, 0);
            run(*a, 12);
        };
        for (const auto &edit : edits) {
            const unsigned control = shown ? User::GetDlgItem(shown, edit.first) : 0;
            if (!control) { printf("CPL_RESULT FAIL no-control id=%u\n", edit.first); return 4; }
            click(control);
            if (edit.second >= 0) {
                for (int k = 0; k < edit.second; ++k) { a->Key(0x28, 0, false, false); a->KeyUp(0x28); run(*a, 6); } // VK_DOWN
                a->Key(0x0d, 13, false, false); a->KeyUp(0x0d);                                                  // VK_RETURN
                run(*a, 12);
            }
            char text[128] = {};
            User::GetWindowText(control, text, sizeof text);
            printf("CPL_EDIT id=%u index=%d text=", edit.first, edit.second);
            hex_text(text);
            putchar('\n');
        }
        if (apply) {
            const unsigned button = User::GetDlgItem(frame, 0x3021); // IDC_APPLYNOW
            const unsigned enabled = button ? User::IsEnabled(button) : 0;
            if (button && enabled) click(button);
            run(*a, 50);
            printf("CPL_APPLY button=%u enabled=%u after=%u\n", button, enabled, button ? User::IsEnabled(button) : 0);
        }
        if (!edits.empty() && !screenshot(out, "edited")) return 5;
    }

    // Second sequence: Cancel must leave the modal loop, destroy the sheet and
    // let Control_RunDLL run CPL_STOP/CPL_EXIT before the process exits.
    unsigned cancel = User::GetDlgItem(frame, 2);
    if (!cancel || !User::IsVisible(cancel) || !User::GetWindowRect(cancel, &bounds)) {
        printf("CPL_RESULT FAIL no-cancel\n");
        return 4;
    }
    const int cx = (bounds.left + bounds.right) / 2, cy = (bounds.top + bounds.bottom) / 2;
    printf("CPL_CANCEL button=%u x=%d y=%d\n", cancel, cx, cy);
    a->MouseButtons(cx, cy, 0);
    a->MouseButtons(cx, cy, 1);
    a->MouseButtons(cx, cy, 0);
    const auto closing = std::chrono::steady_clock::now() + std::chrono::seconds(limit);
    while (a->Status() != X86::Application::Exited && a->Status() != X86::Application::Failed &&
           std::chrono::steady_clock::now() < closing) {
        a->Pump();
        if (!User::IsWindow(frame) && a->WaitingForInput()) break;
        if (a->Status() == X86::Application::Idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const unsigned state = a->Status(), calls = a->ApiCalls(), callbacks = a->Callbacks();
    const bool closed = !User::IsWindow(frame);
    printf("CPL_CLOSED sheet=%u state=%u code=%u calls=%u callbacks=%u windows=%u error=%s\n",
           closed, state, a->ExitCode(), calls, callbacks, User::LiveWindows(), a->Error());
    if (!screenshot(out, "closed")) return 5;
    const bool ok = closed && state == X86::Application::Exited && !a->Error()[0];
    printf("CPL_RESULT %s calls=%u callbacks=%u\n", ok ? "PASS" : "FAIL", calls, callbacks);
    delete a;
    return ok ? 0 : 4;
}
