// SPDX-License-Identifier: GPL-2.0-or-later
// WinHelpA: a help button, F1 or "Details" opens the help file in WINHLP32.
//
// USER32 thunks WinHelpA to USER.EXE WinHelp (ordinal 171), whose
// body is; this follows it:
// - HELP_CONTEXT and HELP_FINDER/HELP_CONTENTS look the file up in
// HTMLHELP.INI first. A topic that moved into an HTML help
// file starts "hh.exe <file.chm::/topic.htm>" instead.
// - HELP_CONTEXTMENU and HELP_WM_HELP find the control's ID in the table of
// (ID, topic) pairs, or a WINDOWS.HLP topic for OK, Cancel, Help and none;
// HELP_CONTEXTMENU asks "What's This?" first. Both place the popup at the
// control (HELP_SETPOPUP_POS) and show the topic as HELP_CONTEXTPOPUP.
// - The command goes into a block : cbData, usCommand, ulTopic,
// ulReserved, offszHelpFile, offabData, then the file name with its window
// type ("apps.hlp>Proc4") and the string or structure of the command.
// - The block goes by WM_WINHELP to the window of class MS_WINHELP (popups
// MS_POPUPHELP, training cards MS_TCARDHELP). Without one, USER starts
// "<windir>\WINHLP32 -x" (-p, -c) and looks again.
// USER sends a 16-bit global handle; WINHLP32 (0042DFED) takes an lParam
// whose high word is not 0 as a flat pointer, so the block lies in a shared
// heap that both processes
#include "x86_app.h"
#include "user.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace X86
{
namespace
{
constexpr unsigned HeapShared = 0x04000000, HeapZero = 8;
}

unsigned Application::WinHelpApi(const unsigned *a)
{
    unsigned hwnd = a[0], command = a[2], data = a[3];
    char file[260] = "";
    if (a[1])
        snprintf(file, sizeof file, "%s", ReadString(a[1]));
    const char *window_class = "MS_WINHELP";
    char mode = 'x';
    if (command & 0x8000)
    {
        window_class = "MS_TCARDHELP";
        mode = 'c';
        command &= 0x7fff;
    }
    else if (command == 0xa || command == 8 || command == 0xd || command == 0xc)
    {
        if (!hwnd)
            return 0;
        window_class = "MS_POPUPHELP";
        mode = 'p';
    }

    auto text = [&](const char *value) { return String(value); };
    // GetPrivateProfileString(section, key, "",..., "htmlhelp.ini"); a key
    // with a path is tried again with its last name only.
    auto html_help = [&](const char *section, const char *key, char *out, unsigned size) {
        unsigned found = 0;
        const unsigned s = text(section), ini = text("htmlhelp.ini"), empty = text(""), buffer = Allocate(size);
        for (int pass = 0; pass < 2 && found <= 1 && s && ini && empty && buffer; ++pass)
        {
            const char *name = key;
            if (pass)
            {
                const char *slash = strrchr(key, '\\');
                if (!slash || !slash[1])
                    break;
                name = slash + 1;
            }
            const unsigned k = text(name);
            const unsigned args[] = {s, k, empty, buffer, size, ini};
            found = k ? Api("GetPrivateProfileStringA", args) : 0;
            Free(k);
        }
        if (found > 1)
            snprintf(out, size, "%s", ReadString(buffer));
        Free(s);
        Free(ini);
        Free(empty);
        Free(buffer);
        return found > 1;
    };
    auto exec = [&](const char *line) {
        const unsigned l = text(line);
        const unsigned args[] = {l, 5}; // SW_SHOW
        const unsigned result = l ? Api("WinExec", args) : 0;
        Free(l);
        return result > 32;
    };

    if (file[0] && (command == 1 || command == 3 || command == 0xb))
    {
        char key[300], topic[520], line[540];
        if (command == 1)
            snprintf(key, sizeof key, "%s,%u", file, data & 0xffff);
        else
            snprintf(key, sizeof key, "%s", file);
        if (html_help(command == 1 ? "HELP_CONTEXT" : "HELP_FINDER", key, topic, sizeof topic))
        {
            snprintf(line, sizeof line, "hh.exe %s", topic);
            exec(line);
            return 1;
        }
    }

    if (command == 0xa || command == 0xc)
    {
        const unsigned id = User::ControlID(hwnd) & 0xffff;
        unsigned topic = 0;
        bool listed = false;
        for (unsigned at = data; at && Read32(at); at += 8)
            if ((Read32(at) & 0xffff) == id)
            {
                topic = Read32(at + 4);
                listed = true;
                break;
            }
        if (!listed)
        {
            // WINDOWS.HLP: OK, Cancel, Help, else "No Help topic is associated
            // with this item."
            snprintf(file, sizeof file, "windows.hlp");
            topic = id == 1 ? 0x6f1b : id == 2 ? 0x6f1c : id == 9 ? 0x6f1d : 0x6f19;
        }
        if (command == 0xa)
        {
            // the "What's This?" menu at the pointer.
            const unsigned menu = User::CreateMenu(true), item = text("&What's This?"), point = Allocate(8);
            unsigned chosen = 0;
            if (menu && item && point)
            {
                const unsigned append[] = {menu, 0, 1, item};
                Api("AppendMenuA", append);
                const unsigned cursor[] = {point};
                Api("GetCursorPos", cursor);
                const unsigned track[] = {menu, 0x102, Read32(point), Read32(point + 4), 0, hwnd, 0};
                chosen = Api("TrackPopupMenu", track); // TPM_RIGHTBUTTON | TPM_RETURNCMD
            }
            if (menu)
                User::DestroyMenu(menu);
            Free(item);
            Free(point);
            if (!chosen)
                return 1;
        }
        if (listed && topic == ~0u)
            return 1;
        const unsigned rect = Allocate(16);
        if (rect)
        {
            const unsigned get[] = {hwnd, rect};
            Api("GetWindowRect", get);
            const unsigned position = (Read32(rect) & 0xffff) | Read32(rect + 4) << 16;
            Free(rect);
            const unsigned name = text(file);
            const unsigned place[] = {hwnd, name, 0xd, position};
            const unsigned placed = name ? WinHelpApi(place) : 0;
            Free(name);
            if (!placed)
                return 0;
        }
        command = 8;
        data = topic;
    }
    if (command == 8 && !file[0])
        snprintf(file, sizeof file, "windows.hlp");

    // The WinHelp request block.
    const unsigned name_bytes = file[0] ? unsigned(strlen(file)) + 1 : 0;
    const unsigned kind = command >> 8 & 0xff;
    unsigned data_bytes = 0;
    char key[256];
    if (kind == 1)
    {
        snprintf(key, sizeof key, "%s", ReadString(data));
        data_bytes = unsigned(strlen(key)) + 1;
    }
    else if (kind == 2 && data)
        data_bytes = Read32(data) & 0xffff;
    const unsigned size = 16 + name_bytes + data_bytes;
    const unsigned create[] = {HeapShared, 0x1000, 0};
    const unsigned heap = Api("HeapCreate", create);
    if (!heap)
        return 0;
    const unsigned alloc[] = {heap, HeapZero, size};
    const unsigned block = Api("HeapAlloc", alloc);
    unsigned result = 0;
    if (block)
    {
        unsigned char head[16] = {};
        head[0] = size & 0xff, head[1] = size >> 8 & 0xff;
        head[2] = command & 0xff, head[3] = command >> 8 & 0xff;
        const unsigned topic = kind ? 0 : data;
        memcpy(head + 4, &topic, 4);
        if (name_bytes)
            head[0xc] = 0x10;
        if (kind == 1 || kind == 2)
        {
            const unsigned offset = 16 + name_bytes;
            head[0xe] = offset & 0xff, head[0xf] = offset >> 8 & 0xff;
        }
        Store(block, head, 16);
        if (name_bytes)
            Store(block + 16, file, name_bytes);
        if (kind == 1)
            Store(block + 16 + name_bytes, key, data_bytes);
        else if (kind == 2)
        {
            auto *structure = static_cast<unsigned char *>(malloc(data_bytes ? data_bytes : 1));
            if (!structure)
            {
                const unsigned destroy[] = {heap};
                Api("HeapDestroy", destroy);
                return 0;
            }
            if (Read(data, structure, data_bytes))
                Store(block + 16 + name_bytes, structure, data_bytes);
            free(structure);
        }

        const unsigned name = text(window_class);
        auto find = [&] {
            const unsigned args[] = {name, 0};
            return name ? Api("FindWindowA", args) : 0;
        };
        unsigned help = find();
        if (!help && command != 2)
        {
            // "<windir>\WINHLP32 -x", then "WINHLP32 -x" on the path.
            char line[300] = "";
            const unsigned directory = Allocate(260);
            if (directory)
            {
                const unsigned args[] = {directory, 260};
                if (Api("GetWindowsDirectoryA", args))
                {
                    const char *path = ReadString(directory);
                    const size_t length = strlen(path);
                    snprintf(line, sizeof line, "%s%sWINHLP32 -%c", path,
                             length && path[length - 1] == '\\' ? "" : "\\", mode);
                }
                Free(directory);
            }
            char fallback[20];
            snprintf(fallback, sizeof fallback, "WINHLP32 -%c", mode);
            if ((line[0] && exec(line)) || exec(fallback))
                help = find();
        }
        Free(name);
        if (help)
        {
            const unsigned send[] = {help, 0x38, hwnd, block}; // WM_WINHELP
            Api("SendMessageA", send);
            result = 1;
        }
        else
            result = command == 2;
    }
    const unsigned destroy[] = {heap};
    Api("HeapDestroy", destroy);
    return result;
}
} // namespace X86
