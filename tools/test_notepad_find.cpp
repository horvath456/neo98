// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "test_menu_input.h"
#include "fixture_native_modules.h"
#include "../src/gdi_font_catalog.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static std::vector<unsigned char> load(const char *name)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    auto size = ftell(f);
    rewind(f);
    std::vector<unsigned char> v(size);
    assert(fread(v.data(), 1, size, f) == (size_t)size);
    fclose(f);
    return v;
}
static void log(const char *message)
{
    printf("APP %s\n", message);
    fflush(stdout);
}
static void idle(X86::Application &app)
{
    for (unsigned i = 0; i < 100; ++i)
    {
        auto state = app.Pump();
        if (state == X86::Application::Idle)
            return;
        if (state == X86::Application::Failed)
        {
            fprintf(stderr, "FAILED %s\n", app.Error());
            abort();
        }
    }
    assert(false);
}
int main(int argc, char **argv)
{
    assert(argc == 4 || argc == 6);
    if (argc == 6)
    {
        assert(DibText::FontCount() == 0); // Release build has no fallback fonts.
        auto common = load(argv[4]), sans = load(argv[5]);
        Files::System().MakeDirectory("C:\\WINDOWS");
        Files::System().MakeDirectory("C:\\WINDOWS\\SYSTEM");
        assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\COMDLG32.DLL", common.data(), common.size()));
        assert(GdiFonts::InstallFontFile("SSERIFE.FON", sans.data(), sans.size()) == 6);
        assert(DibText::FontCount() == 6);
        assert(DibText::OriginalSelfTest() == 0);
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    if (argc == 6)
    {
        assert(GdiFonts::InstallFnt("FIXEDSYS", font.data(), font.size(), true));
        GdiFonts::SetRole(GdiFonts::FixedRole, "FIXEDSYS");
        Gdi::SetDefaultFont(DibText::OriginalFont(0).data, DibText::OriginalFont(0).length);
    }
    auto *app = new X86::Application(log);
    assert(FixtureModules::KeepNative(*app));
    const char *document = "Alpha alpha alphabet.\r\nAnother alpha, ALPHA.";
    Files::System().MakeDirectory("C:\\Documents"); // the file demo's folder (src/app_ui.cpp)
    assert(!Files::System().Seed("C:\\Documents\\SEARCH.TXT", document, strlen(document)));
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "NOTEPAD.EXE",
                      "NOTEPAD.EXE C:\\Documents\\SEARCH.TXT"));
    idle(*app);
    unsigned edit = User::GetFocus(), main = app->MainWindow();
    User::SetEditSelection(edit, 0, 0);
    Gdi::RECT menu_rect{};
    assert(User::MenuItemRect(main, User::GetMenu(main), 2, &menu_rect));
    TestMenuInput::Click(*app, menu_rect);
    idle(*app);
    app->RawKey('F', 'f', true);
    app->RawKey('F', 0, false);
    idle(*app);
    unsigned dialog = User::FindWindow("#32770", "Find");
    assert(dialog && User::IsEnabled(main));
    if (argc == 6)
        // Reopening ten times below must use the parsed per-process cache.
        assert(!Files::System().Remove("C:\\WINDOWS\\SYSTEM\\COMDLG32.DLL", false));
    unsigned input = User::GetDlgItem(dialog, 1152);
    assert(input && User::GetFocus() == input);
    for (const char *p = "alpha"; *p; ++p)
    {
        app->RawKey(*p - 'a' + 'A', *p, true);
        app->RawKey(*p - 'a' + 'A', 0, false);
    }
    // Batch typing and Enter before pumping, as USB can do during a slow frame.
    // The dialog starts with Find Next disabled until EN_CHANGE is handled.
    app->RawKey(13, 13, true);
    app->RawKey(13, 0, false);
    idle(*app);
    unsigned start, end;
    User::EditSelection(edit, &start, &end);
    printf("SEARCH first selection=%u,%u\n", start, end);
    assert(start == 0 && end == 5);
    auto selection = [&](unsigned a, unsigned b)
    {
        unsigned first = 0, last = 0;
        User::EditSelection(edit, &first, &last);
        printf("SEARCH selection=%u,%u expected=%u,%u\n", first, last, a, b);
        assert(first == a && last == b);
    };
    auto next = [&]()
    {
        User::PostMessage(dialog, 0x111, 1, 0);
        idle(*app);
    };
    next();
    selection(6, 11);
    next();
    selection(12, 17);
    next();
    selection(30, 35); // Native EDIT indices exclude CR in the x86 CRLF buffer.
    next();
    selection(37, 42);
    User::ControlMessage(User::GetDlgItem(dialog, 1056), 0xf1, 1, 0);
    User::ControlMessage(User::GetDlgItem(dialog, 1057), 0xf1, 0, 0);
    next();
    selection(30, 35);
    next();
    selection(12, 17);
    User::ControlMessage(User::GetDlgItem(dialog, 1041), 0xf1, 1, 0);
    next();
    selection(6, 11);
    next(); // "Alpha" has a different case; Notepad opens its original not-found prompt.
    unsigned prompt = User::FindWindow("#32770", "Notepad");
    assert(prompt && prompt != dialog && !User::IsEnabled(dialog));
    app->Key(13, 13, false, false);
    idle(*app);
    assert(!User::IsWindow(prompt) && User::IsEnabled(dialog));
    selection(6, 11);
    // An empty search disables Find Next; normal keyboard editing enables it again.
    User::SetFocus(input);
    app->Key('A', 1, false, true);
    idle(*app);
    app->Key(8, 8, false, false);
    idle(*app);
    assert(!User::IsEnabled(User::GetDlgItem(dialog, 1)));
    app->Key(13, 13, false, false);
    idle(*app);
    assert(!User::FindWindow("#32770", "Notepad"));
    selection(6, 11);
    for (const char *p = "Another"; *p; ++p)
        app->Key(0, *p, false, false);
    idle(*app);
    assert(User::IsEnabled(User::GetDlgItem(dialog, 1)));
    User::ControlMessage(User::GetDlgItem(dialog, 1056), 0xf1, 0, 0);
    User::ControlMessage(User::GetDlgItem(dialog, 1057), 0xf1, 1, 0);
    next();
    selection(22, 29);
    User::Render();
    FILE *out = fopen(argv[3], "wb");
    assert(out);
    fprintf(out, "P6\n800 600\n255\n");
    const auto *pixels = User::Pixels();
    for (unsigned i = 0; i < 800 * 600; ++i)
    {
        unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
    unsigned frame = User::FrameSerial(), before = app->ApiCalls();
    for (unsigned i = 0; i < 100; ++i)
        assert(app->Pump() == X86::Application::Idle);
    assert(User::FrameSerial() == frame && app->ApiCalls() == before);
    assert(!User::EditModified(edit));
    User::PostMessage(dialog, 0x111, 2, 0);
    idle(*app);
    assert(!User::IsWindow(dialog));
    for (unsigned i = 0; i < 10; ++i)
    {
        User::PostMessage(main, 0x111, 3, 0);
        idle(*app);
        dialog = User::FindWindow("#32770", "Find");
        char retained[64];
        User::GetWindowText(User::GetDlgItem(dialog, 1152), retained, sizeof retained);
        assert(dialog && !strcmp(retained, "Another"));
        User::PostMessage(dialog, 0x10, 0, 0);
        idle(*app);
        assert(!User::IsWindow(dialog));
    }
    User::PostMessage(main, 0x111, 28, 0);
    for (unsigned i = 0; i < 100 && app->Status() != X86::Application::Exited; ++i)
        app->Pump();
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    unsigned calls = app->ApiCalls(), callbacks = app->Callbacks();
    delete app;
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
    printf("NOTEPAD SEARCH PASS calls=%u callbacks=%u exit=0 windows=0 gdi_objects=0\n", calls, callbacks);
}
