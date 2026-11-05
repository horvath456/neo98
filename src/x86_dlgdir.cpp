// SPDX-License-Identifier: GPL-2.0-or-later
// DlgDirListA and the two messages it needs. USER does no enumeration
// of its own: it changes the current directory, writes the remaining file
// pattern back into the caller's buffer and then sends LB_DIR or CB_DIR. The
// enumeration is USER, which both messages share; the combobox handler
// only routes its own window's child listbox into it.
#include "files.h"
#include "user.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86 {
namespace {
// The DDL_ bits. Windows 98 maps the low ones one to one onto the DOS attribute
// bits and always adds DDL_SYSTEM, so DOS returns system entries too.
constexpr unsigned DdlReadOnly = 0x0001, DdlHidden = 0x0002, DdlSystem = 0x0004;
constexpr unsigned DdlDirectory = 0x0010, DdlArchive = 0x0020;
constexpr unsigned DdlPostMessages = 0x2000, DdlDrives = 0x4000, DdlExclusive = 0x8000;
// USER's own bit. Windows 98 uses it for the second pass of DlgDirList, which
// wants the directories and drives and none of the files. Windows 98 clears it
// out of whatever the application passed.
constexpr unsigned DdlInternal = 0x1000;
// The three attribute bits DOS only reports when they are asked for.
constexpr unsigned SelectiveAttributes = DdlHidden | DdlSystem | DdlDirectory;
constexpr unsigned MessageAddString = 0x180, MessageInsertString = 0x181;
constexpr unsigned MessageGetCount = 0x18b, MessageDirectory = 0x18d;
constexpr unsigned MessageListReset = 0x184, MessageComboReset = 0x14b;
constexpr unsigned MessageComboDirectory = 0x145;
} // namespace

// USER. wParam is the DDL mask, lParam the file pattern. The entries
// go in through LB_ADDSTRING, so a sorted listbox sorts them itself, exactly
// as Windows 98 leaves the decision to the listbox style. The drives follow with
// LB_INSERTSTRING at the end, unsorted, the way appends them.
unsigned Application::ListBoxDirectory(unsigned hwnd, unsigned attributes, unsigned pattern)
{
    // Windows 98 copies the pattern into a local of 81h bytes.
    char spec[132] = "*.*";
    if (pattern)
    {
        const char *text = ReadString(pattern);
        if (!text) { last_error = 87; return ~0u; }
        const unsigned length = unsigned(strlen(text));
        if (length > 0x80) return ~0u;
        if (length) memcpy(spec, text, length + 1);
    }
    const unsigned mask = (attributes | DdlSystem) & 0x4fff;
    const bool exclusive = (attributes & DdlExclusive) != 0;
    const bool directories_only = (attributes & DdlInternal) != 0;
    // the second pass with no directory bit skips the files outright.
    const bool drives_only = (attributes & DdlDrives) && exclusive && !(attributes & DdlDirectory);
    const unsigned scratch = Allocate(640);
    if (!scratch) { last_error = 8; return ~1u; }
    const unsigned find_data = scratch + 256;
    unsigned inserted = 0;
    bool space = false;
    if (!drives_only)
    {
        Store(scratch, spec, strlen(spec) + 1);
        const unsigned arguments[] = {scratch, find_data};
        unsigned search = FileApi("FindFirstFileA", arguments);
        while (search != ~0u)
        {
            unsigned entry = 0;
            char name[260] = {};
            if (!Read(find_data, &entry, 4) || !Read(find_data + 44, name, sizeof name - 1)) break;
            name[sizeof name - 1] = 0;
            const bool directory = (entry & DdlDirectory) != 0;
            // DOS reports the hidden, system and directory entries only when
            // the mask asks for them; read only and archive always come back.
            bool wanted = !(entry & SelectiveAttributes & ~mask);
            // with DDL_EXCLUSIVE the entry has to share a bit with
            // the mask, without it the test always passes.
            if (wanted && exclusive && !(entry & (attributes | DdlSystem) & 0x7fff)) wanted = false;
            // drops the entry for the directory itself, but keeps "..".
            if (wanted && directory && !strcmp(name, ".")) wanted = false;
            if (wanted && directories_only && !directory) wanted = false;
            if (wanted)
            {
                // wraps a directory in brackets and leaves a file bare.
                char label[264];
                if (directory) snprintf(label, sizeof label, "[%s]", name);
                else { memcpy(label, name, strlen(name) + 1); }
                const unsigned text = String(label);
                const unsigned result = text ? Send(hwnd, MessageAddString, 0, text) : ~1u;
                if (text) Free(text);
                // Windows 98 stops on LB_ERRSPACE and only on that.
                if (result == ~1u) { space = true; break; }
                if (result != ~0u) ++inserted;
            }
            const unsigned next[] = {search, find_data};
            if (!FileApi("FindNextFileA", next)) break;
        }
        if (search != ~0u) FileApi("FindClose", &search);
    }
    if (!space && (attributes & DdlDrives))
    {
        // Windows 98 builds "[-x-]" from the drive letter and appends it
        // at the end, so the drives never take part in the sort.
        const unsigned drives = Files::System().DriveMask();
        for (unsigned i = 0; i < 26; ++i)
        {
            if (!(drives & (1u << i))) continue;
            char label[8];
            snprintf(label, sizeof label, "[-%c-]", char('a' + i));
            const unsigned text = String(label);
            const unsigned result = text ? Send(hwnd, MessageInsertString, ~0u, text) : ~1u;
            if (text) Free(text);
            if (result == ~1u) { space = true; break; }
            if (result != ~0u) ++inserted;
        }
    }
    Free(scratch);
    if (space) return ~1u;
    // Windows 98 answers the number of items in the box minus one, not the index
    // of the last one it added, so an empty box answers LB_ERR.
    const unsigned count = Send(hwnd, MessageGetCount, 0, 0);
    return count == ~0u ? ~0u : count - 1;
}

// USER, shared by DlgDirList and DlgDirListComboBox. The last argument
// of the worker is which of the two the caller asked for.
// USER, the worker both DlgDirSelectEx entries share. It takes the
// selected line of a list box apart: a directory comes in as `[name]` and
// leaves as `name\`, a drive comes in as `[-x-]` and leaves as `x:`, and a
// file name without a dot gets one. The answer says whether the selection was
// a directory or a drive.
//
// Windows 98 reads the line into a 142-byte frame buffer, so a longer line
// overruns in the original; NEO98 stops at the buffer instead.
bool Application::DialogDirectorySelection(unsigned list, char *out, unsigned capacity)
{
    if (!capacity) return false;
    out[0] = 0;
    // LB_GETCURSEL, then LB_GETTEXT. Nothing selected answers FALSE.
    const unsigned index = SendFromNative(list, 0x0188, 0, 0);
    if (index == ~0u) return false;
    char line[142] = {};
    const unsigned scratch = Allocate(sizeof line);
    if (!scratch) return false;
    char zero = 0;
    Store(scratch, &zero, 1);
    const unsigned length = SendFromNative(list, 0x0189, index, scratch);
    if (length != ~0u && length < sizeof line) Read(scratch, line, length + 1);
    Free(scratch);
    line[sizeof line - 1] = 0;
    const bool directory = line[0] == '[';
    // the columns behind a tab are not part of the name.
    for (char *p = line; *p; ++p) if (*p == '\t') { *p = 0; break; }
    char *text = line;
    if (directory)
    {
        unsigned n = unsigned(strlen(line));
        if (n) --n;
        line[n] = '\\';        // overwrites the closing bracket
        text = line + 1;
        if (line[1] == '-')
        {
            // the drive form [-x-] becomes x: and nothing more.
            text = line + 2;
            text[1] = ':';
            text[2] = 0;
        }
    }
    else
    {
        // a name without a dot gets a trailing one.
        char *dot = line;
        while (*dot && *dot != '.') ++dot;
        if (!*dot) { *dot = '.'; dot[1] = 0; }
    }
    unsigned copied = unsigned(strlen(text));
    if (copied >= capacity) copied = capacity - 1;
    memcpy(out, text, copied);
    out[copied] = 0;
    return directory;
}

unsigned Application::DlgDirApi(ApiName name, const unsigned *a)
{
    const bool combo = API_IS(name, "DlgDirListComboBoxA");
    const unsigned dialog = a[0], path_spec = a[1];
    const unsigned list_id = a[2], static_id = a[3];
    // Windows 98 takes USER's own bit out of whatever the application passed.
    unsigned file_type = a[4] & ~DdlInternal;
    if ((file_type & DdlDrives) && !(file_type & DdlDirectory)) file_type |= DdlExclusive;
    // without a dialog there is nothing either id could name.
    if (!dialog && (static_id || list_id)) return 0;
    char saved[260];
    memcpy(saved, files.current, sizeof saved);
    char pattern[132] = "*.*";
    bool restore = false;
    if (path_spec)
    {
        const char *text = ReadString(path_spec);
        if (!text) { last_error = 87; return 0; }
        const unsigned length = unsigned(strlen(text));
        // a path longer than 80h characters is refused.
        if (length > 0x80) return 0;
        if (length)
        {
            char work[132];
            memcpy(work, text, length + 1);
            restore = true;
            char *rest = work;
            if (work[0] && work[1] == ':')
            {
                // changes to the drive first and pins its directory.
                char drive[3] = {work[0], ':', 0};
                if (files.SetDirectory(drive)) return 0;
                rest = work + 2;
            }
            if (!*rest) memcpy(pattern, "*.*", 4);
            else if (!strchr(rest, '*') && !strchr(rest, '?'))
            {
                // no wildcard at all means the whole rest is a
                // directory; it has to exist.
                if (files.SetDirectory(rest))
                {
                    memcpy(files.current, saved, sizeof saved);
                    return 0;
                }
                memcpy(pattern, "*.*", 4);
            }
            else
            {
                // scans back for the last separator and changes to
                // the directory in front of it.
                char *cut = nullptr;
                for (char *p = rest; *p; ++p)
                    if (*p == '\\' || *p == '/' || *p == ':') cut = p;
                if (cut)
                {
                    const char keep = *cut;
                    *cut = 0;
                    const unsigned failed = files.SetDirectory(rest == cut ? "\\" : rest);
                    *cut = keep;
                    if (failed)
                    {
                        memcpy(files.current, saved, sizeof saved);
                        return 0;
                    }
                    rest = cut + 1;
                }
                const unsigned n = unsigned(strlen(rest));
                memcpy(pattern, rest, n < sizeof pattern ? n + 1 : sizeof pattern);
                pattern[sizeof pattern - 1] = 0;
            }
            // Windows 98 writes the remaining file pattern back over the
            // caller's buffer. That is the documented side effect.
            for (char *p = pattern; *p; ++p) if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
            PutString(path_spec, pattern);
        }
    }
    (void)restore;
    // puts the current directory, lower cased, into the static.
    if (static_id && dialog)
    {
        char shown[260];
        memcpy(shown, files.current, sizeof shown);
        for (char *p = shown; *p; ++p)
            if (*p >= 'A' && *p <= 'Z') *p = char(*p - 'A' + 'a');
        const unsigned text = String(shown);
        if (text)
        {
            const unsigned arguments[] = {dialog, static_id, text};
            Api("SetDlgItemTextA", arguments);
            Free(text);
        }
    }
    if (!list_id) return 1;
    const unsigned control = dialog ? User::GetDlgItem(dialog, list_id) : 0;
    // a control that is not there is not an error.
    if (!control) return 1;
    Send(control, combo ? MessageComboReset : MessageListReset, 0, 0);
    const unsigned message = combo ? MessageComboDirectory : MessageDirectory;
    const unsigned text = String(pattern);
    if (!text) { last_error = 8; return 0; }
    // is the first pass: the files, with the drive bit and USER's own
    // bit taken out.
    Send(control, message, file_type & 0xbfef, text);
    Free(text);
    // is the second pass: the directories and the drives, with
    // DDL_EXCLUSIVE and USER's own bit added so no file comes back twice.
    if (file_type & (DdlDrives | DdlDirectory))
    {
        const unsigned all = String("*.*");
        if (all)
        {
            Send(control, message, (file_type & 0x4037) | DdlInternal | DdlExclusive, all);
            Free(all);
        }
    }
    User::InvalidateRect(control, nullptr, true);
    (void)DdlReadOnly;
    (void)DdlArchive;
    (void)DdlPostMessages;
    return 1;
}
} // namespace X86
