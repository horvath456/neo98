// SPDX-License-Identifier: GPL-2.0-or-later
// Drives the hot-key and GetClipBox guest fixture and injects real key packets
// through the native input queue once the fixture's hot keys are live.
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> Load(const char *name)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> v(n);
    assert(fread(v.data(), 1, n, f) == size_t(n));
    fclose(f);
    return v;
}
namespace {
// The scan codes the keyboard would carry; the hot-key path ignores them, the
// message parameter does not.
void Key(unsigned vk, unsigned scan, bool down, bool extended = false)
{
    User::KeyboardRaw(vk, 0, down, scan, extended);
}
void Combination(const unsigned *modifiers, const unsigned *scans, unsigned count,
                 unsigned vk, unsigned scan)
{
    for (unsigned i = 0; i < count; ++i) Key(modifiers[i], scans[i], true, modifiers[i] == 0x5b);
    Key(vk, scan, true);
    Key(vk, scan, false);
    for (unsigned i = count; i--;) Key(modifiers[i], scans[i], false, modifiers[i] == 0x5b);
}
} // namespace
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = Load(argv[1]), font = Load(argv[2]);
    const unsigned before = Gdi::LiveObjects();
    unsigned injected = 0;
    {
        X86::Application app(nullptr);
        assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "HOTKEY.EXE"));
        for (unsigned i = 0; i < 20000 && app.Status() != X86::Application::Exited &&
                             app.Status() != X86::Application::Failed; ++i)
        {
            app.Pump(32);
            // The fixture keeps three records: Alt+Q, Win+Shift+R and the
            // Ctrl+Alt+T thread hot key.
            if (injected == 0 && User::HotKeyCount() == 3)
            {
                const unsigned alt[] = {0xa4}, alt_scan[] = {0x38};
                Combination(alt, alt_scan, 1, 0x51, 0x10);           // Alt+Q -> id 100
                const unsigned win[] = {0x5b, 0xa0}, win_scan[] = {0x5b, 0x2a};
                Combination(win, win_scan, 2, 0x52, 0x13);           // Win+Shift+R -> id 101
                const unsigned both[] = {0xa2, 0xa4}, both_scan[] = {0x1d, 0x38};
                Combination(both, both_scan, 2, 0x54, 0x14);         // Ctrl+Alt+T -> id 105
                injected = 1;
            }
            // After the fixture drops Win+Shift+R the same keystroke must do
            // nothing at all.
            else if (injected == 1 && User::HotKeyCount() == 2)
            {
                const unsigned win[] = {0x5b, 0xa0}, win_scan[] = {0x5b, 0x2a};
                Combination(win, win_scan, 2, 0x52, 0x13);
                injected = 2;
            }
        }
        if (app.Status() != X86::Application::Exited || app.ExitCode())
            fprintf(stderr, "hotkey/clipbox guest: status=%u code=%u injected=%u error=%s\n",
                    app.Status(), app.ExitCode(), injected, app.Error());
        assert(injected == 2);
        assert(app.Status() == X86::Application::Exited && !app.ExitCode());
        assert(User::HotKeyCount() == 0);
        printf("HOTKEY CLIPBOX GUEST PASS calls=%u callbacks=%u\n", app.ApiCalls(), app.Callbacks());
    }
    assert(Gdi::LiveObjects() == before);
    return 0;
}
