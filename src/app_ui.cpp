// SPDX-License-Identifier: GPL-2.0-or-later
#include "kernel.h"
#include <circle/string.h>
#include <circle/util.h>
#include "../build/generated/notepad_program.h"
// Files of the application this image was built for; empty for the default set.
#include "app_extra.h"

bool Kernel::InitializeApplication()
{
    application_keymap = new CKeyMap;
    application = new X86::Application(
        [](const char *message)
        {
            CString line;
            line.Format("NEO98 APP %s", message);
            instance->Log(line);
        });
    if(!application->ConfigureKeyboardLayout(options.GetKeyMap())) {
        Log("NEO98 FAIL keyboard layout requires the DE or US input provider");return false;
    }
    const char *selected = options.GetAppOptionString("app", "notepad");
    const bool file_demo = !strcmp(selected, "notepad-file");
    if (file_demo)
    {
        const char *document = "Original file on native ARM.\r\nSecond line.";
        // The demonstration's own folder on the RAM volume; Win98 has none.
        Files::System().MakeDirectory("C:\\Documents");
        if (Files::System().Seed("C:\\Documents\\NEO98.TXT", document, strlen(document)))
            return false;
    }
    const bool taskman = !strcmp(selected, "taskman");
    const bool regedit = !strcmp(selected, "regedit");
    const bool calculator = !strcmp(selected, "calc");
    const unsigned char *image = taskman      ? OriginalTaskman
                                 : regedit    ? OriginalRegedit
                                 : calculator ? OriginalCalculator
                                              : OriginalNotepad;
    unsigned bytes = taskman      ? sizeof OriginalTaskman
                     : regedit    ? sizeof OriginalRegedit
                     : calculator ? sizeof OriginalCalculator
                                  : sizeof OriginalNotepad;
    const char *program = taskman      ? "TASKMAN.EXE"
                          : regedit    ? "REGEDIT.EXE"
                          : calculator ? "CALC.EXE"
                                       : "NOTEPAD.EXE";
#ifdef NEO98_EXTRA_APPLICATION
    if (!strcmp(selected, NEO98_EXTRA_APPLICATION))
    {
#ifdef NEO98_EXTRA_APPLICATION_DIRECTORIES
        // Folders of the installation the application needs, parents first.
        // Seed does not create a missing parent, so the directories come
        // before the files. 183 is "already exists", which the RAM volume
        // reports for C:\WINDOWS and is not a failure here.
        for (const char *directory : ExtraApplicationDirectories)
        {
            const unsigned error = Files::System().MakeDirectory(directory);
            if (error && error != 183)
            {
                CString line;
                line.Format("NEO98 FAIL application directory %s error=%u", directory, error);
                Log(line);
                return false;
            }
        }
        CString folders;
        folders.Format("NEO98 APPLICATION directories=%u",
                       (unsigned)(sizeof ExtraApplicationDirectories /
                                  sizeof ExtraApplicationDirectories[0]));
        Log(folders);
#endif
        // The original DLLs reach the program only as files in the RAM volume,
        // in the same places tools/probe_application.cpp puts them.
        for (const auto &file : ExtraApplicationFiles)
            if (Files::System().Seed(file.path, file.data, file.bytes))
            {
                CString line;
                line.Format("NEO98 FAIL application file %s", file.path);
                Log(line);
                return false;
            }
        CString line;
        line.Format("NEO98 APPLICATION files=%u program=%s", (unsigned)(sizeof ExtraApplicationFiles /
                                                                       sizeof ExtraApplicationFiles[0]),
                    ExtraApplicationProgram);
        Log(line);
        image = ExtraApplicationFiles[0].data;
        bytes = ExtraApplicationFiles[0].bytes;
        program = ExtraApplicationProgram;
    }
    else
#endif
        if (!taskman && !regedit && !calculator && !file_demo && strcmp(selected, "notepad"))
    {
        CString line;
        line.Format("NEO98 FAIL unknown application %s in this kernel image", selected);
        Log(line);
        return false;
    }
    if (!application->Start(image, bytes, OriginalFixedsys, sizeof OriginalFixedsys, program,
                            file_demo ? "NOTEPAD.EXE C:\\Documents\\NEO98.TXT" : nullptr))
        return false;
    // WordPad's MFC and OLE startup needs about twelve thousand API calls
    // before its first idle, far more than Notepad's, and Explorer never idles
    // at all: it builds the taskbar and the desktop, launches RUNDLL32 for a
    // RunOnce entry and waits on the child's modal boxes. Startup is therefore
    // a full pump loop, with input and frames, not a silent budget.
    const unsigned startup_rounds = 16384;
    for (unsigned i = 0; i < startup_rounds; ++i)
    {
        auto state = application->Pump(32);
        // Show what the application has drawn so far. Startup is a pump loop
        // like any other, and an application that stops before its first idle
        // still leaves its windows on the framebuffer.
        PresentApplicationFrame();
        // Keyboard and mouse reports are routed here too, so the scripted
        // clicks that answer a child's dialog arrive while the application is
        // still starting. Without this the shell waits for a child that
        // nobody can dismiss.
        ServiceInput();
        if (state == X86::Application::Failed || state == X86::Application::Exited)
            return false;
        // A visible top-level window is the point where the application is on
        // screen and can be operated, whether or not it ever idles. MainWindow
        // is the newest window without a parent and may well be hidden, so ask
        // the desktop for its children instead.
        if (!application_running_published && VisibleTopLevelWindow())
            PublishApplicationState("running", "window", i + 1);
        if (application->WaitingForInput())
        {
            PublishApplicationState("ready", "idle", i + 1);
            PumpApplication();
            return true;
        }
    }
    // The budget is up and the application is still running. That is not a
    // failure; it is an application that does not idle. Hand it to the normal
    // pump loop, which routes the same input this loop did.
    PublishApplicationState("running", "rounds", startup_rounds);
    return true;
}

unsigned Kernel::VisibleTopLevelWindow()
{
    // The desktop's own children, in z-order.
    unsigned windows[User::WindowCapacity], count = 0;
    if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count))
        return 0;
    for (unsigned i = 0; i < count; ++i)
        if (User::IsVisible(windows[i]))
            return windows[i];
    return 0;
}

void Kernel::PublishApplicationState(const char *state, const char *reason, unsigned rounds)
{
    if (!strcmp(state, "running"))
    {
        if (application_running_published)
            return;
        application_running_published = true;
    }
    // An application without a main window, or one without a caption, leaves
    // the buffer untouched. Start it empty, so the line reports no title
    // instead of stack bytes.
    char title[256] = {};
    const unsigned window = application->MainWindow() ? application->MainWindow()
                                                      : VisibleTopLevelWindow();
    if (window)
        User::GetWindowText(window, title, sizeof title);
    CString line;
    line.Format("NEO98 APPLICATION %s title=%s calls=%u callbacks=%u rounds=%u reason=%s", state,
                title, application->ApiCalls(), application->Callbacks(), rounds, reason);
    Log(line);
}
void Kernel::PresentApplicationFrame()
{
    // Rendering may call the x86 owner-draw procedure and must run on its fiber.
    if (!application || (application_frame == User::FrameSerial() &&
                         application_cursor_visible == application->CursorVisible()))
        return;
    application_frame = User::FrameSerial();
    application_cursor_visible = application->CursorVisible();
    // The Pi's surface stays 800x600; a desktop of another size goes onto it
    // nearest pixel, as the SDL host did before it followed the desktop size.
    int frame_width = 800, frame_height = 600;
    User::PixelsSize(frame_width, frame_height);
    if (frame_width == 800 && frame_height == 600)
        memcpy(surface.pixels, User::Pixels(), 800 * 600 * 4);
    else
    {
        const unsigned char *frame = User::Pixels();
        unsigned char *out = (unsigned char *)surface.pixels;
        for (int y = 0; y < 600; ++y)
            for (int x = 0; x < 800; ++x)
                memcpy(out + (y * 800 + x) * 4,
                       frame + ((y * frame_height / 600) * frame_width + x * frame_width / 800) * 4, 4);
    }
    Present({0, 0, 800, 600});
    DrawCursor();
    CString line;
    line.Format("NEO98 APPFRAME frame=%u calls=%u callbacks=%u", application_frame,
                application->ApiCalls(), application->Callbacks());
    Log(line);
}
void Kernel::PumpApplication()
{
    if (!application)
        return;
    auto state = application->Pump(32);
    if (state == X86::Application::Failed && !X86::Application::LiveChildren())
    {
        Log("NEO98 FAIL application");
        running = false;
        return;
    }
    if (state == X86::Application::Exited && !X86::Application::LiveChildren())
    {
        CString line;
        line.Format("NEO98 APPLICATION exited code=%u", application->ExitCode());
        Log(line);
        delete application;
        application = nullptr;
        running = false;
        return;
    }
    PresentApplicationFrame();
    char text[4096];
    unsigned output=User::GetDlgItem(application->MainWindow(),403);
    if(output){User::GetWindowText(output,text,sizeof text);unsigned hash=2166136261u;for(unsigned i=0;text[i];++i)hash=(hash^(unsigned char)text[i])*16777619u;
        if(hash!=application_state_hash){application_state_hash=hash;CString line;line.Format("NEO98 CALC display=%s threads=%u created=%u",text,application->LiveThreads(),application->ThreadsCreated());Log(line);}}
    unsigned focus = User::GetFocus();
    if (User::IsEdit(focus))
    {
        User::GetWindowText(focus, text, sizeof text);
        unsigned hash = 2166136261u, length = 0;
        while (text[length])
            hash = (hash ^ (unsigned char)text[length++]) * 16777619u;
        unsigned start = 0, end = 0;
        User::EditSelection(focus, &start, &end);
        unsigned selection = start | (end << 16);
        if (hash != application_state_hash || selection != application_selection ||
            focus != application_focus)
        {
            application_state_hash = hash;
            application_selection = selection;
            application_focus = focus;
            CString line;
            line.Format("NEO98 APPDOC length=%u hash=%08x hwnd=%u start=%u end=%u", length, hash, focus,
                        start, end);
            Log(line);
        }
    }
}
void Kernel::ApplicationKey(const char *text, unsigned modifiers)
{
    if (!application)
        return;
    bool shift = modifiers & 0x22, control = modifiers & 0x11;
    unsigned key = 0, character = (unsigned char)text[0];
    if (character == 27)
    {
        character = 0;
        if (!strcmp(text, "\x1b[D"))
            key = 0x25;
        else if (!strcmp(text, "\x1b[C"))
            key = 0x27;
        else if (!strcmp(text, "\x1b[A"))
            key = 0x26;
        else if (!strcmp(text, "\x1b[B"))
            key = 0x28;
        else if (!strcmp(text, "\x1b[1~"))
            key = 0x24;
        else if (!strcmp(text, "\x1b[4~"))
            key = 0x23;
        else if (!strcmp(text, "\x1b[5~"))
            key = 0x21;
        else if (!strcmp(text, "\x1b[6~"))
            key = 0x22;
        else if (!strcmp(text, "\x1b[3~"))
            key = 0x2e;
        else if (!strcmp(text, "\x1b[1;5H"))
        {
            key = 0x24;
            control = true;
        }
        else if (!strcmp(text, "\x1b[1;5F"))
        {
            key = 0x23;
            control = true;
        }
        else if (!text[1])
            key = 27;
        else
            return;
    }
    else if (character == 127 || character == 8)
    {
        key = 8;
        character = 8;
    }
    else if (character == '\r' || character == '\n')
    {
        key = 13;
        character = 13;
    }
    else if (character == '\t')
    {
        key = 9;
        character = 9;
    }
    else if (character >= 1 && character <= 26)
    {
        key = 'A' + character - 1;
        control = true;
    }
    else
        key = character >= 'a' && character <= 'z' ? character - 32 : character;
    application->Key(key, character, shift, control);
    application->KeyUp(key,shift,control);
}
void Kernel::ApplicationMouse(unsigned buttons, int dx, int dy, int wheel)
{
    const Rect old = {mouse_x, mouse_y, mouse_x + 11, mouse_y + 17};
    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0)
        mouse_x = 0;
    if (mouse_y < 0)
        mouse_y = 0;
    if (mouse_x > 799)
        mouse_x = 799;
    if (mouse_y > 599)
        mouse_y = 599;
    Present(old);
    mouse_buttons = buttons;
    if (application)
    {
        application->MouseButtons(mouse_x,mouse_y,
            ((buttons&MOUSE_BUTTON_LEFT)?1:0)|((buttons&MOUSE_BUTTON_RIGHT)?2:0)|((buttons&MOUSE_BUTTON_MIDDLE)?4:0));
        if (wheel) application->MouseWheel(wheel);
    }
    DrawCursor();
}

void Kernel::ApplicationRawKey(unsigned usage, bool down)
{
    if (!application || !application_keymap || usage>=128) return;
    auto &held=application_held_keys[usage];
    if (!down)
    {
        if(held.vk)application->RawKey(held.vk,0,false,held.scan,held.extended);
        held={};return;
    }
    const bool lock=usage==0x39 || usage==0x47 || usage==0x53;
    const unsigned modifiers=lock ? 0 : application_modifiers;
    const unsigned logical=application_keymap->Translate(usage,modifiers);
    if (!held.vk)held=UsbKeys::Map(usage,!strcmp(options.GetKeyMap(),"DE"),application_keymap->GetLEDStatus()&1);
    unsigned character=0;char buffer[2];
    const char *text=application_keymap->GetString(logical,modifiers,buffer);
    if(text && text[0] && !text[1])character=(unsigned char)text[0];
    if(held.vk==8)character=8;
    if(held.vk==13)character=13;
    if(held.vk==9)character=9;
    if(held.vk==27)character=0;
    if(held.vk)application->RawKey(held.vk,character,true,held.scan,held.extended);
}
void Kernel::ApplicationRawKeys(unsigned modifiers, const unsigned char keys[6])
{
    if(!application)return;
    // ErrorRollOver/POSTFail/ErrorUndefined reports do not mean all keys were released.
    for(unsigned i=0;i<6;++i)if(keys[i] && keys[i]<4)return;
    application_modifiers=modifiers;
    application->KeyboardModifiers(modifiers);
    auto contains=[](const unsigned char *list,unsigned key){for(unsigned i=0;i<6;++i)if(list[i]==key)return true;return false;};
    for(auto old:application_raw_keys)if(old && !contains(keys,old))
    {
        ApplicationRawKey(old,false);
        if(repeat_usage==old)repeat_usage=0;
    }
    for(unsigned i=0;i<6;++i)if(keys[i] && !contains(application_raw_keys,keys[i]))
    {
        bool duplicate=false;for(unsigned j=0;j<i;++j)duplicate|=keys[j]==keys[i];
        if(duplicate)continue;
        ApplicationRawKey(keys[i],true);
        if(keys[i]!=0x39 && keys[i]!=0x47 && keys[i]!=0x53)
        {repeat_usage=keys[i];repeat_time=CTimer::GetClockTicks()+500000;}
    }
    for(unsigned i=0;i<6;++i)application_raw_keys[i]=keys[i];
}
void Kernel::ApplicationKeyRepeat()
{
    if(!application || !application_keymap)return;
    if(repeat_usage && int(CTimer::GetClockTicks()-repeat_time)>=0)
    {ApplicationRawKey(repeat_usage,true);repeat_time=CTimer::GetClockTicks()+50000;}
    const unsigned leds=application_keymap->GetLEDStatus();
    if(keyboard && leds!=application_leds && keyboard->SetLEDs(leds))application_leds=leds;
}
