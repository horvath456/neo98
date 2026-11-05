// SPDX-License-Identifier: GPL-2.0-or-later
// The screen saver and the power states of the display.
//
// USER keeps the three timeouts in DS:DAh (screen saver), DCh (low power)
// and DEh (power off), in seconds and negative while inactive, and the time
// of the last hardware input in DS:138h/13Ch. When the idle time
// reaches the screen saver's timeout and a saver is chosen, USER posts
// WM_SYSCOMMAND SC_SCREENSAVE to the active window, and DefWindowProc starts
// SYSTEM.INI [boot] SCRNSAVE.EXE with "/s". The power timeouts set the
// display to low power and off through the VDD (function 91h answers the
// states the display has). The display of this port has both: in either
// state the host shows a black screen until the next input turns it on.
#include "x86_app.h"
#include "user_settings.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
void Application::NoteHardwareInput()
{
    User::NoteActivity(TickCount());
}

void Application::ScreenSaverTick()
{
    const unsigned now = TickCount();
    static unsigned last = 0;
    if (now - last < 500)
        return;
    last = now;
    auto &settings = User::SystemSettings();
    const unsigned idle = now - User::IdleSince();
    const int saver = settings.saver[0], low = settings.saver[1], off = settings.saver[2];
    if (saver > 0 && !settings.saver_running && !User::SaverRequested() && idle >= unsigned(saver) * 1000u)
    {
        User::SetSaverRequested(true);
        unsigned target = User::GetForegroundWindow();
        if (!target || !User::IsWindow(target))
            target = User::GetDesktopWindow();
        User::PostMessage(target, 0x112, 0xf140, 0); // WM_SYSCOMMAND, SC_SCREENSAVE
    }
    unsigned level = 0;
    if (off > 0 && idle >= unsigned(off) * 1000u)
        level = 2;
    else if (low > 0 && idle >= unsigned(low) * 1000u)
        level = 1;
    if (level > User::DisplayPower())
        User::SetDisplayPower(level);
}

// DefWindowProc, SC_SCREENSAVE: the program SYSTEM.INI [boot] SCRNSAVE.EXE
// names, with "/s", while the screen saver is active and none runs.
unsigned Application::StartScreenSaver()
{
    auto &settings = User::SystemSettings();
    if (settings.saver[0] <= 0 || settings.saver_running)
        return 0;
    const unsigned section = String("boot"), key = String("SCRNSAVE.EXE"), empty = String(""),
                   file = String("SYSTEM.INI"), buffer = Allocate(300);
    unsigned started = 0;
    if (section && key && empty && file && buffer)
    {
        const unsigned a[] = {section, key, empty, buffer, 260, file};
        if (Api("GetPrivateProfileStringA", a))
        {
            char command[300];
            snprintf(command, sizeof command, "%s /s", ReadString(buffer));
            Store(buffer, command, unsigned(strlen(command)) + 1);
            const unsigned exec[] = {buffer, 1};
            started = Api("WinExec", exec) >= 32;
        }
    }
    Free(section);
    Free(key);
    Free(empty);
    Free(file);
    Free(buffer);
    return started;
}
} // namespace X86
