// SPDX-License-Identifier: GPL-2.0-or-later
// GetOpenFileName and GetSaveFileName of the 16-bit COMMDLG, built natively
// the way COMMDLG.DLL of Win98 builds them. Included in src/win16_commdlg.cpp.
//
// The 16-bit COMMDLG does not show a 16-bit program the Explorer dialog of
// COMDLG32. Windows 98 loads its own template 600h (FILEOPENORD), or 601h
// (MULTIFILEOPENORD) with OFN_ALLOWMULTISELECT, or the caller's template,
// and runs it with DialogBoxIndirectParam; the dialog has the file name
// field edt1, the file list lst1, the folder list lst2 with the current
// folder stc1, the type combo cmb1, the drive combo cmb2, "Read only" chx1
// and "Help" psh15. The hook of the caller is a 16-bit procedure that sees
// every message first, WM_INITDIALOG with the far pointer of its own
// OPENFILENAME, and the registered messages commdlg_FileNameOK,
// commdlg_LBSelChangedNotify and commdlg_help; WM_NOTIFY does not exist
// there. Everything a 16-bit caller reads back is the 8.3 form:
// the lists show short names in lower case, the way the original fills them.
//
// The template and the images of the two lists (bitmap 191h, eight 16x16
// cells: open folder, current folder, closed folder, floppy, hard disk,
// CD-ROM, network drive, RAM drive; blue is transparent) are read out of
// COMMDLG.DLL of the tree. Nothing of the dialog is invented; the strings
// are those of its string table.
// The resources of COMMDLG.DLL, read once for the session.
struct CommDlgImages
{
    bool loaded = false;
    unsigned char *file = nullptr;
    unsigned length = 0;
    unsigned char strip[16][128] = {}; // palette indices of bitmap 191h, top row first
    unsigned palette[16] = {};         // 00RRGGBB
    bool strip_ok = false;
};
namespace
{
constexpr unsigned Win16FileDialogToken = 0x71000043;
// Control IDs of the template (dlgs.h).
constexpr unsigned Edt1 = 0x480, Lst1 = 0x460, Lst2 = 0x461, Stc1 = 0x440, Stc2 = 0x441, Cmb1 = 0x470,
                   Cmb2 = 0x471, Chx1 = 0x410, Psh15 = 0x40e;
// OPENFILENAME16 fields.
constexpr unsigned OfnOwner = 0x04, OfnInstance = 0x06, OfnFilter = 0x08, OfnCustomFilter = 0x0c,
                   OfnMaxCustom = 0x10, OfnFilterIndex = 0x14, OfnFile = 0x18, OfnMaxFile = 0x1c,
                   OfnFileTitle = 0x20, OfnMaxFileTitle = 0x24, OfnInitialDir = 0x28, OfnTitle = 0x2c,
                   OfnFlags = 0x30, OfnFileOffset = 0x34, OfnFileExtension = 0x36, OfnDefExt = 0x38,
                   OfnHook = 0x40, OfnTemplateName = 0x44;
constexpr unsigned OfnReadOnly = 0x1, OfnOverwritePrompt = 0x2, OfnHideReadOnly = 0x4, OfnNoChangeDir = 0x8,
                   OfnShowHelp = 0x10, OfnEnableHook = 0x20, OfnEnableTemplate = 0x40,
                   OfnEnableTemplateHandle = 0x80, OfnAllowMultiSelect = 0x200, OfnExtensionDifferent = 0x400,
                   OfnPathMustExist = 0x800, OfnFileMustExist = 0x1000, OfnCreatePrompt = 0x2000,
                   OfnNoReadOnlyReturn = 0x8000;
// A resource of an NE image in memory: type and id are the ordinal words
// without the 8000h bit. Answers the bytes, zero when missing.
const unsigned char *NeResource(const unsigned char *d, unsigned length, unsigned type, unsigned id,
                                unsigned &size)
{
    auto u16 = [&](unsigned at) { return at + 2 <= length ? unsigned(d[at] | d[at + 1] << 8) : 0u; };
    if (length < 0x40)
        return nullptr;
    const unsigned ne = u16(0x3c) | u16(0x3e) << 16;
    if (ne + 0x40 > length || d[ne] != 'N' || d[ne + 1] != 'E')
        return nullptr;
    unsigned at = ne + u16(ne + 0x24);
    const unsigned shift = u16(at);
    at += 2;
    for (unsigned guard = 0; guard < 64 && at + 8 <= length; ++guard)
    {
        const unsigned kind = u16(at), count = u16(at + 2);
        if (!kind)
            break;
        at += 8;
        for (unsigned i = 0; i < count && at + 12 <= length; ++i, at += 12)
            if (kind == (0x8000 | type) && u16(at + 6) == (0x8000 | id))
            {
                const unsigned offset = u16(at) << shift;
                size = u16(at + 2) << shift;
                if (offset >= length)
                    return nullptr;
                if (offset + size > length)
                    size = length - offset;
                return d + offset;
            }
    }
    return nullptr;
}
struct Win16OpenFrame
{
    Application *app;
    Win16OpenFrame *next;
    unsigned hwnd = 0, thread = 0;
    unsigned ofn = 0, ofn_far = 0, hook = 0, flags = 0;
    bool save = false, initializing = true, modal_entered = false;
    unsigned message_ok = 0, message_selchange = 0, message_help = 0;
    unsigned scratch = 0; // guest bytes for strings and structures, 4096
    unsigned native_template = 0, template_size = 0;
    unsigned char *template_bytes = nullptr;
    char pattern[260] = "*.*";
    char start_directory[260] = {};
    bool custom = false;
    static Win16OpenFrame *head;
    Win16OpenFrame(Application *a) : app(a), next(head), thread(a->ThreadId()) { head = this; }
    ~Win16OpenFrame()
    {
        auto **at = &head;
        while (*at && *at != this)
            at = &(*at)->next;
        if (*at)
            *at = next;
        delete[] template_bytes;
    }
};
Win16OpenFrame *Win16OpenFrame::head = nullptr;
Win16OpenFrame *Win16OpenFrameOf(Application *app, unsigned hwnd, bool initialize)
{
    for (auto *f = Win16OpenFrame::head; f; f = f->next)
        if (f->app == app && (f->hwnd == hwnd || (initialize && !f->hwnd)))
        {
            if (!f->hwnd)
                f->hwnd = hwnd;
            return f;
        }
    return nullptr;
}
void Lower(char *s)
{
    for (; *s; ++s)
        if (*s >= 'A' && *s <= 'Z')
            *s = char(*s - 'A' + 'a');
}
bool Wild(const char *s) { return strchr(s, '*') || strchr(s, '?'); }
} // namespace

struct Application::Win16FileWork
{
    Application *app; Win16FileWork *next; Win16OpenFrame *frame;
    unsigned thread, index, kind, phase = 0, value[8]{};
    char text[4][260]{};
    bool kept = false;
    static Win16FileWork *head;
    Win16FileWork(Application &a, Win16OpenFrame &f, unsigned k):app(&a),next(head),frame(&f),
        thread(a.current_thread),index(a.threads[thread].host_frame_count),kind(k) { head=this; }
    ~Win16FileWork() { auto **p=&head;while(*p&&*p!=this)p=&(*p)->next;if(*p)*p=next; }
    static Win16FileWork *Find(const Application &a,unsigned at,unsigned k,unsigned thread) {
        for(auto *w=head;w;w=w->next)if(w->app==&a&&w->thread==thread&&w->index==at&&w->kind==k)return w;return nullptr;
    }
};
Application::Win16FileWork *Application::Win16FileWork::head=nullptr;

struct Application::Win16OpenDialog
{
    Application &app;
    Win16OpenFrame &f;
    bool recorded = false;
    struct WorkScope { Win16OpenDialog &e; Win16FileWork *w; ~WorkScope() {
        e.app.GiveTail(Application::TailNone);e.app.Free(w->value[6]);e.app.Free(w->value[7]);
        if(w->kept)e.app.PopHostFrame();delete w;
    }};
    Win16FileWork *Begin(unsigned kind) {
        app.GiveTail(Application::TailNone);auto *w=new Win16FileWork(app,f,kind);
        w->kept=recorded&&app.PushHostFrame(kind,f.hwnd);return w;
    }
    void Phase(Win16FileWork *w,unsigned phase) {
        w->phase=phase;if(w->kept)app.threads[app.current_thread].host_frames[w->index].d=phase;
        app.GiveTail(w->kept?Application::TailRecord:Application::TailNone);
    }
    unsigned Wait(Win16FileWork *w,unsigned phase,unsigned style,const char *format,const char *name) {
        char text[400],caption[128]{};snprintf(text,sizeof text,format,name);User::GetWindowText(f.hwnd,caption,sizeof caption);
        app.Free(w->value[6]);app.Free(w->value[7]);w->value[6]=app.String(text);w->value[7]=app.String(caption);
        Phase(w,phase);app.GiveTail(w->kept?Application::TailFresh:Application::TailNone);
        const unsigned args[]={f.hwnd,w->value[6],w->value[7],style};return app.Api("MessageBoxA",args);
    }
    unsigned ContinueMessage(Win16FileWork *,bool);
    unsigned Item(unsigned id) { return User::GetDlgItem(f.hwnd, id); }
    unsigned SendItem(unsigned id, unsigned message, unsigned wp = 0, unsigned lp = 0)
    {
        return app.Send(Item(id), message, wp, lp);
    }
    // A string into the scratch at `slot` (256 bytes each), answered as its
    // flat address.
    unsigned Put(unsigned slot, const char *text)
    {
        const unsigned at = f.scratch + slot * 256;
        char copy[256];
        snprintf(copy, sizeof copy, "%s", text);
        app.Store(at, copy, strlen(copy) + 1);
        return at;
    }
    unsigned Add(unsigned id, const char *text, bool combo, unsigned data)
    {
        const unsigned index = SendItem(id, combo ? 0x143 : 0x180, 0, Put(0, text));
        if (index < 0x8000)
            SendItem(id, combo ? 0x151 : 0x19a, index, data);
        return index;
    }
    unsigned Field(unsigned offset) { return app.Win16Word(f.ofn + offset); }
    unsigned Far(unsigned offset) { return app.Win16FarPointer(f.ofn + offset); }
    // The hook of the caller. Every message goes to it first and its BOOL
    // (AX) says whether it handled it.
    unsigned Hook(unsigned message, unsigned wp, unsigned lp)
    {
        if (!f.hook || !User::IsWindow(f.hwnd))
            return 0;
        const unsigned arguments[] = {f.hwnd, message, wp, lp};
        return app.Call(f.hook, arguments, 4) & 0xffff;
    }
    void CurrentDirectory(char (&out)[260])
    {
        snprintf(out, sizeof out, "%s", app.files.current);
        // The 16-bit side sees the short form of the path.
        const unsigned in = Put(1, out), buffer = Put(2, "");
        const unsigned a[] = {in, buffer, 256};
        const unsigned n = app.Api("GetShortPathNameA", a);
        if (n && n < 256)
            snprintf(out, sizeof out, "%s", app.ReadString(buffer));
    }
    bool ChangeDirectory(const char *path)
    {
        char full[260] = {};
        if (app.files.FullPath(path, full))
            return false;
        unsigned node = 0;
        if (Files::System().Lookup(full, node))
            return false;
        const auto *n = Files::System().Get(node);
        if (!n || !(n->attributes & 0x10))
            return false;
        app.files.SetDirectory(full);
        return true;
    }
    // FindFirstFile over `spec`; `visit(name, attributes)` sees the short name.
    template <class Visit> void Enumerate(const char *spec, Visit visit)
    {
        const unsigned data = f.scratch + 2048;
        const unsigned a[] = {Put(3, spec), data};
        const unsigned find = app.FileApi("FindFirstFileA", a);
        if (find == ~0u)
            return;
        for (unsigned guard = 0; guard < 4096; ++guard)
        {
            unsigned attributes = 0;
            char name[260] = {}, alternate[14] = {};
            app.Read(data, &attributes, 4);
            app.Read(data + 44, name, 259);
            app.Read(data + 44 + 260, alternate, 13);
            visit(alternate[0] ? alternate : name, attributes);
            const unsigned next[] = {find, data};
            if (!app.FileApi("FindNextFileA", next))
                break;
        }
        app.FileApi("FindClose", &find);
    }
    void Scan()
    {
        char directory[260];
        CurrentDirectory(directory);
        char shown[260];
        snprintf(shown, sizeof shown, "%s", directory);
        Lower(shown);
        User::SetWindowText(Item(Stc1), shown);
        // The files of the pattern, each alternative of "a;b" once.
        SendItem(Lst1, 0x184);
        const char *part = f.pattern;
        while (*part)
        {
            const char *end = strchr(part, ';');
            const unsigned n = end ? unsigned(end - part) : unsigned(strlen(part));
            char spec[300];
            snprintf(spec, sizeof spec, "%s%s%.*s", directory,
                     directory[0] && directory[strlen(directory) - 1] == '\\' ? "" : "\\", int(n), part);
            Enumerate(spec, [&](const char *name, unsigned attributes) {
                if (attributes & (0x10 | 0x02 | 0x04))
                    return; // DlgDirList without DDL_HIDDEN, DDL_SYSTEM, DDL_DIRECTORY
                char lower[260];
                snprintf(lower, sizeof lower, "%s", name);
                Lower(lower);
                if (SendItem(Lst1, 0x1a2, ~0u, Put(0, lower)) == ~0u) // LB_FINDSTRINGEXACT
                    Add(Lst1, lower, false, 0);
            });
            if (!end)
                break;
            part = end + 1;
        }
        // The folders: the chain from the root to the current folder, open,
        // and its subfolders, closed. The strings are full paths, so the
        // sorted list keeps the chain in front; the drawing shows the last
        // component, indented by its depth.
        SendItem(Lst2, 0x184);
        const unsigned length = unsigned(strlen(shown));
        auto folder = [&](const char *path, unsigned depth, unsigned kind) {
            if (SendItem(Lst2, 0x1a2, ~0u, Put(0, path)) == ~0u)
                Add(Lst2, path, false, depth | kind << 8);
        };
        // kind 0 an open folder of the chain, 1 the current folder, 2 a
        // closed subfolder.
        char root[4] = {shown[0], shown[1], '\\', 0};
        folder(root, 0, length <= 3 ? 1u : 0u);
        unsigned depth = 1;
        for (unsigned i = 3; i <= length && length > 3; ++i)
            if (shown[i] == '\\' || !shown[i])
            {
                char prefix[260];
                snprintf(prefix, sizeof prefix, "%.*s", int(i), shown);
                folder(prefix, depth++, shown[i] ? 0u : 1u);
            }
        const unsigned current = depth ? depth - 1 : 0;
        char spec[300];
        snprintf(spec, sizeof spec, "%s%s*.*", directory,
                 directory[0] && directory[strlen(directory) - 1] == '\\' ? "" : "\\");
        Enumerate(spec, [&](const char *name, unsigned attributes) {
            if (!(attributes & 0x10) || (attributes & (0x02 | 0x04)) || !strcmp(name, ".") || !strcmp(name, ".."))
                return;
            char path[300];
            snprintf(path, sizeof path, "%s%s%s", shown, shown[strlen(shown) - 1] == '\\' ? "" : "\\", name);
            Lower(path);
            Add(Lst2, path, false, (current + 1) | 2u << 8);
        });
        const unsigned selected = SendItem(Lst2, 0x1a2, ~0u, Put(0, shown));
        if (selected != ~0u)
            SendItem(Lst2, 0x186, selected);
        User::InvalidateRect(Item(Lst1), nullptr, true);
        User::InvalidateRect(Item(Lst2), nullptr, true);
    }
    void Drives()
    {
        SendItem(Cmb2, 0x14b);
        char current = app.files.current[0];
        if (current >= 'a' && current <= 'z')
            current = char(current - 'a' + 'A');
        for (char letter = 'A'; letter <= 'Z'; ++letter)
        {
            char root[4] = {letter, ':', '\\', 0};
            const unsigned a[] = {Put(1, root)};
            const unsigned type = app.Api("GetDriveTypeA", a);
            if (type < 2)
                continue;
            // 3 floppy, 4 hard disk, 5 CD-ROM, 6 network, 7 RAM drive.
            const unsigned image = type == 2 ? 3 : type == 4 ? 6 : type == 5 ? 5 : type == 6 ? 7 : 4;
            char label[64] = {};
            if (type == 3 || type == 6)
            {
                const unsigned volume = f.scratch + 3 * 256;
                const unsigned v[] = {Put(1, root), volume, 64, 0, 0, 0, 0, 0};
                if (app.Api("GetVolumeInformationA", v))
                    snprintf(label, sizeof label, "%s", app.ReadString(volume));
            }
            char text[80];
            snprintf(text, sizeof text, "%c:%s%s", letter, label[0] ? " " : "", label);
            Lower(text);
            const unsigned index = Add(Cmb2, text, true, image);
            if (letter == current)
                SendItem(Cmb2, 0x14e, index);
        }
        // The combo is sorted: find the current drive again after the fill.
        char prefix[3] = {char(current - 'A' + 'a'), ':', 0};
        const unsigned index = SendItem(Cmb2, 0x14c, ~0u, Put(0, prefix));
        if (index != ~0u)
            SendItem(Cmb2, 0x14e, index);
    }
    void SelectFilter(unsigned index)
    {
        const unsigned pattern = SendItem(Cmb1, 0x150, index);
        if (!pattern || pattern == ~0u)
            return;
        snprintf(f.pattern, sizeof f.pattern, "%s", app.ReadString(pattern));
    }
    bool Initialize()
    {
        snprintf(f.start_directory, sizeof f.start_directory, "%s", app.files.current);
        if (const unsigned title = Far(OfnTitle))
            User::SetWindowText(f.hwnd, app.ReadString(title));
        else if (f.save)
            User::SetWindowText(f.hwnd, "Save As"); // string 181h
        if (f.save)
            User::SetWindowText(Item(Stc2), "Save file as &type:"); // string 182h
        if (f.flags & OfnHideReadOnly)
            User::ShowWindow(Item(Chx1), 0);
        SendItem(Chx1, 0xf1, f.flags & OfnReadOnly ? 1 : 0);
        if (!(f.flags & OfnShowHelp))
            User::ShowWindow(Item(Psh15), 0);
        // The types: the custom filter first, then the pairs of lpstrFilter.
        // The item data is the flat address of the pattern.
        unsigned count = 0;
        const unsigned custom = Far(OfnCustomFilter);
        if (custom && app.ReadString(custom)[0])
        {
            const unsigned pattern = custom + unsigned(strlen(app.ReadString(custom))) + 1;
            Add(Cmb1, app.ReadString(custom), true, pattern);
            f.custom = true;
            ++count;
        }
        for (unsigned filter = Far(OfnFilter), n = 0; filter && app.ReadString(filter)[0] && n < 64; ++n)
        {
            char text[256];
            snprintf(text, sizeof text, "%s", app.ReadString(filter));
            const unsigned pattern = filter + unsigned(strlen(text)) + 1;
            if (!app.ReadString(pattern)[0])
                break;
            Add(Cmb1, text, true, pattern);
            ++count;
            filter = pattern + unsigned(strlen(app.ReadString(pattern))) + 1;
        }
        unsigned index = app.Read32(f.ofn + OfnFilterIndex);
        if (!f.custom && index)
            --index;
        if (index >= count)
            index = 0;
        if (count)
        {
            SendItem(Cmb1, 0x14e, index);
            SelectFilter(index);
        }
        if (const unsigned initial = Far(OfnInitialDir))
        {
            char path[260];
            snprintf(path, sizeof path, "%s", app.ReadString(initial));
            if (path[0])
                ChangeDirectory(path);
        }
        // The name field: the caller's file, or the pattern of the type.
        const unsigned file = Far(OfnFile);
        char name[260] = {};
        if (file)
            snprintf(name, sizeof name, "%s", app.ReadString(file));
        User::SetWindowText(Item(Edt1), name[0] ? name : f.pattern);
        SendItem(Edt1, 0xc5, 259); // EM_LIMITTEXT
        Drives();
        Scan();
        f.initializing = false;
        return true;
    }
    bool Exists(const char *path, unsigned &attributes)
    {
        unsigned node = 0;
        if (Files::System().Lookup(path, node))
            return false;
        const auto *n = Files::System().Get(node);
        attributes = n ? n->attributes : 0;
        return n != nullptr;
    }
    // Ends the dialog with the caller's fields filled, or answers false when
    // the name is not acceptable and the dialog stays.
    bool Accept() { return ContinueAccept(Begin(Application::HostWin16FileAccept),false); }
    bool ContinueAccept(Win16FileWork *w,bool resumed)
    {
        WorkScope scope{*this,w};
        char (&input)[260]=w->text[0],(&full)[260]=w->text[1],(&folder)[260]=w->text[2],(&shown)[260]=w->text[3];
        unsigned n=0,begin=0,def=0,attributes=w->value[0];bool exists=w->value[1];char *leaf=nullptr;
        if(resumed) { const unsigned answer=app.ResumeInner();
            if(w->phase==4)return false;
            if(w->phase==3)return answer!=0;
            if(answer!=6)return false;
            if(w->phase==1)goto created;goto overwritten;
        }
        User::GetWindowText(Item(Edt1), input, sizeof input);
        n = unsigned(strlen(input));
        while (n && input[n - 1] == ' ')
            input[--n] = 0;
        begin = 0;
        while (input[begin] == ' ')
            ++begin;
        memmove(input, input + begin, strlen(input + begin) + 1);
        if (!input[0])
            return false;
        // A pattern: the folder part is changed to, the rest becomes the type.
        if (Wild(input))
        {
            char *slash = strrchr(input, '\\');
            if (!slash && input[1] == ':')
                slash = input + 1;
            if (slash)
            {
                char folder[260];
                snprintf(folder, sizeof folder, "%.*s", int(slash - input + 1), input);
                if (!ChangeDirectory(folder))
                {
                    Wait(w,4,0x30, "%s\nThis path does not exist.\nMake sure that you have specified the correct path.",
                            input);
                    return false;
                }
                memmove(input, slash + 1, strlen(slash + 1) + 1);
            }
            snprintf(f.pattern, sizeof f.pattern, "%s", input);
            User::SetWindowText(Item(Edt1), f.pattern);
            Scan();
            return false;
        }
        // Several names from a multiple selection: "a.txt b.txt".
        if ((f.flags & OfnAllowMultiSelect) && strchr(input, ' '))
            return AcceptMany(input);
        // A folder or a drive is changed to.
        if (ChangeDirectory(input))
        {
            User::SetWindowText(Item(Edt1), f.pattern);
            Drives();
            Scan();
            return false;
        }

        if (strpbrk(input, "<>|\"/") || app.files.FullPath(input, full))
        {
            Wait(w,4,0x30, "%s\nThis filename is not valid.", input); // string 189h
            return false;
        }
        // The default extension goes to a name without one.
        leaf = strrchr(full, '\\');
        leaf = leaf ? leaf + 1 : full;
        def = Far(OfnDefExt);
        if (!strchr(leaf, '.') && def && app.ReadString(def)[0] && strlen(full) + 5 < sizeof full)
        {
            char extension[4] = {};
            snprintf(extension, sizeof extension, "%s", app.ReadString(def));
            strcat(full, ".");
            strcat(full, extension);
        }
        snprintf(folder, sizeof folder, "%s", full);
        *strrchr(folder, '\\') = 0;
        if (strlen(folder) == 2)
            strcat(folder, "\\");
        attributes = 0;exists = Exists(full, attributes);
        snprintf(shown, sizeof shown, "%s", input);
        if ((f.flags & (OfnPathMustExist | OfnFileMustExist)) && !Exists(folder, attributes))
        {
            Wait(w,4,0x30, "%s\nThis path does not exist.\nMake sure that you have specified the correct path.",
                    shown); // string 188h
            return false;
        }
        Exists(full, attributes);
        w->value[0]=attributes;w->value[1]=exists;
        if (!exists && (f.flags & OfnCreatePrompt))
        {
            if (Wait(w,1,0x34,"%s\nThis file does not exist.\nDo you want to create it?", shown) != 6) // string 192h
                return false;
        }
        else if (!exists && (f.flags & OfnFileMustExist))
        {
            Wait(w,4,0x30, "%s\nThis file cannot be found.\nMake sure that you have specified the correct path and filename.",
                    shown); // string 187h
            return false;
        }
        created:
        if (exists && f.save && (f.flags & OfnOverwritePrompt) &&
            Wait(w,2,0x34,"%s\nThis file already exists.\nDo you want to replace it?", shown) != 6) // string 101h
            return false;
        overwritten:
        if (exists && (attributes & 1) && (f.flags & OfnNoReadOnlyReturn))
        {
            Wait(w,4,0x30, "%s\nThis file is read-only.\nTo save your changes, use a different filename.", shown);
            return false;
        }
        // The answer in the short form, lower case, as the lists show it.
        {
            const unsigned in = Put(1, full), buffer = Put(2, "");
            const unsigned a[] = {in, buffer, 256};
            const unsigned got = app.Api("GetShortPathNameA", a);
            if (got && got < 256)
                snprintf(full, sizeof full, "%s", app.ReadString(buffer));
            else
            {
                char short_folder[260];
                snprintf(short_folder, sizeof short_folder, "%s", folder);
                const unsigned b[] = {Put(1, folder), buffer, 256};
                const unsigned folder_got = app.Api("GetShortPathNameA", b);
                if (folder_got && folder_got < 256)
                    snprintf(short_folder, sizeof short_folder, "%s", app.ReadString(buffer));
                char *name = strrchr(full, '\\');
                char rebuilt[260];
                snprintf(rebuilt, sizeof rebuilt, "%s%s%s", short_folder,
                         short_folder[strlen(short_folder) - 1] == '\\' ? "" : "\\", name ? name + 1 : full);
                snprintf(full, sizeof full, "%s", rebuilt);
            }
        }
        Lower(full);
        Phase(w,3);return Output(full, 0);
    }
    bool AcceptMany(const char *names)
    {
        char directory[260];
        CurrentDirectory(directory);
        Lower(directory);
        char text[600];
        snprintf(text, sizeof text, "%s %s", directory, names);
        Lower(text);
        return Output(text, unsigned(strlen(directory)) + 1);
    }
    // The fields of the caller, commdlg_FileNameOK, and the end.
    bool Output(const char *path, unsigned multiple_offset)
    {
        const unsigned file = Far(OfnFile), capacity = app.Read32(f.ofn + OfnMaxFile) & 0xffff;
        const unsigned needed = unsigned(strlen(path)) + 1;
        if (!file || capacity < needed)
        {
            // FNERR_BUFFERTOOSMALL; the first word of the buffer says how
            // many bytes the name needs.
            if (file && capacity >= 2)
                app.Win16Word(file, needed);
            app.Win16CommonDialogError(0x3003);
            app.EndDialog(f.hwnd, 0);
            return true;
        }
        app.Store(file, path, needed);
        const char *leaf = multiple_offset ? path + multiple_offset : strrchr(path, '\\');
        leaf = multiple_offset ? leaf : leaf ? leaf + 1 : path;
        const unsigned offset = unsigned(leaf - path);
        const char *dot = multiple_offset ? nullptr : strrchr(leaf, '.');
        app.Win16Word(f.ofn + OfnFileOffset, offset);
        app.Win16Word(f.ofn + OfnFileExtension, dot && dot[1] ? unsigned(dot + 1 - path) : 0);
        if (const unsigned title = Far(OfnFileTitle))
        {
            const unsigned room = app.Read32(f.ofn + OfnMaxFileTitle) & 0xffff;
            if (room)
            {
                char copy[260];
                snprintf(copy, sizeof copy, "%.*s", int(room - 1 < 259 ? room - 1 : 259), leaf);
                app.Store(title, copy, strlen(copy) + 1);
            }
        }
        unsigned flags = app.Read32(f.ofn + OfnFlags) & ~(OfnReadOnly | OfnExtensionDifferent);
        if (!(f.flags & OfnHideReadOnly) && SendItem(Chx1, 0xf0) == 1)
            flags |= OfnReadOnly;
        const unsigned def = Far(OfnDefExt);
        if (def && dot && SameText(dot + 1, app.ReadString(def)))
            flags |= OfnExtensionDifferent;
        app.Write32(f.ofn + OfnFlags, flags);
        const unsigned index = SendItem(Cmb1, 0x147);
        app.Write32(f.ofn + OfnFilterIndex, index == ~0u ? 0 : f.custom ? index : index + 1);
        // The pattern in use goes back into the custom filter.
        if (const unsigned custom = Far(OfnCustomFilter))
        {
            const unsigned room = app.Read32(f.ofn + OfnMaxCustom) & 0xffff;
            const unsigned first = unsigned(strlen(app.ReadString(custom))) + 1;
            if (room > first + strlen(f.pattern) + 1)
            {
                app.Store(custom + first, f.pattern, strlen(f.pattern) + 1);
                app.Store(custom + first + unsigned(strlen(f.pattern)) + 1, "", 1);
            }
        }
        return ContinueOutput(Begin(Application::HostWin16FileOutput),false);
    }
    bool ContinueOutput(Win16FileWork *w,bool resumed) {
        WorkScope scope{*this,w};unsigned answer=0;
        if(resumed)answer=app.ResumeInner()&65535;
        else { Phase(w,1);answer=Hook(f.message_ok,0,f.ofn_far); }
        app.GiveTail(Application::TailNone);
        if(answer)return false;
        if(!User::IsWindow(f.hwnd))return true;
        app.Win16CommonDialogError(0);app.EndDialog(f.hwnd,1);return true;
    }
    static int SameText(const char *a, const char *b)
    {
        for (;; ++a, ++b)
        {
            char x = *a >= 'A' && *a <= 'Z' ? char(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? char(*b + 32) : *b;
            if (x != y || !x)
                return x - y;
        }
    }
    // The file list changed its selection: the name field shows it, and the
    // hook hears commdlg_LBSelChangedNotify (wParam lst1, lParam index and
    // CD_LBSELCHANGE or, with several, CD_LBSELNOITEMS/CD_LBSELADD).
    void FileSelected()
    {
        auto *w=Begin(Application::HostWin16FileSelect);WorkScope scope{*this,w};
        if (f.flags & OfnAllowMultiSelect)
        {
            const unsigned count = SendItem(Lst1, 0x190);
            if (count != ~0u && count > 0)
            {
                const unsigned items = f.scratch + 3 * 256;
                const unsigned got = SendItem(Lst1, 0x191, count < 64 ? count : 64, items);
                char text[256] = {};
                for (unsigned i = 0; i < got && i < 64; ++i)
                {
                    unsigned index = 0;
                    app.Read(items + 4 * i, &index, 4);
                    SendItem(Lst1, 0x189, index, f.scratch + 2 * 256);
                    const size_t used = strlen(text);
                    snprintf(text + used, sizeof text - used, "%s%s", i ? " " : "", app.ReadString(f.scratch + 2 * 256));
                }
                User::SetWindowText(Item(Edt1), text);
                Phase(w,1);Hook(f.message_selchange, Lst1, 0xffff | 2u << 16);
                return;
            }
        }
        const unsigned index = SendItem(Lst1, 0x188);
        if (index == ~0u)
            return;
        SendItem(Lst1, 0x189, index, f.scratch + 2 * 256);
        User::SetWindowText(Item(Edt1), app.ReadString(f.scratch + 2 * 256));
        Phase(w,1);Hook(f.message_selchange, Lst1, (index & 0xffff) | 0u << 16);
    }
    void FolderChosen()
    {
        const unsigned index = SendItem(Lst2, 0x188);
        if (index == ~0u)
            return;
        SendItem(Lst2, 0x189, index, f.scratch + 2 * 256);
        char path[260];
        snprintf(path, sizeof path, "%s", app.ReadString(f.scratch + 2 * 256));
        if (ChangeDirectory(path))
        {
            User::SetWindowText(Item(Edt1), f.pattern);
            Scan();
        }
    }
    void DriveChosen()
    {
        const unsigned index = SendItem(Cmb2, 0x147);
        if (index == ~0u)
            return;
        SendItem(Cmb2, 0x148, index, f.scratch + 2 * 256);
        char root[4] = {app.ReadString(f.scratch + 2 * 256)[0], ':', '\\', 0};
        if (!ChangeDirectory(root))
        {
            char text[160];
            snprintf(text, sizeof text, "Cannot read drive %c:.\nMake sure that the drive door is closed and that "
                                        "the disk is formatted and free of errors.", root[0]);
            char caption[128] = {};
            User::GetWindowText(f.hwnd, caption, sizeof caption);
            auto *w=Begin(Application::HostWin16FileDrive);WorkScope scope{*this,w};
            Wait(w,1,0x30,"%s",text); // string 184h
            app.GiveTail(Application::TailNone);Drives();
            return;
        }
        Scan();
    }
    // WM_DRAWITEM of the two lists and the drive combo.
    void Draw(unsigned structure)
    {
        unsigned d[12] = {};
        app.Read(structure, d, sizeof d);
        const unsigned id = d[1], item = d[2], state = d[4], dc = d[6];
        const int left = int(d[7]), top = int(d[8]), right = int(d[9]), bottom = int(d[10]);
        const unsigned data = d[11];
        if (item == ~0u)
            return;
        const bool selected = state & 1;
        Gdi::RECT rect{left, top, right, bottom};
        const unsigned background_index[] = {selected ? 13u : 5u};
        Gdi::FillRect(dc, &rect, app.Api("GetSysColorBrush", background_index));
        char text[260] = {};
        app.Send(d[5], id == Cmb2 ? 0x148 : 0x189, item, f.scratch + 2 * 256);
        snprintf(text, sizeof text, "%s", app.ReadString(f.scratch + 2 * 256));
        int x = left + 2;
        if (id == Lst2 || id == Cmb2)
        {
            unsigned image = 0, depth = 0;
            if (id == Lst2)
            {
                depth = data & 0xff;
                image = (data >> 8) & 3; // 0 open, 1 current, 2 closed
                image = image == 1 ? 0 : image;
                // Only the last component of the full path is shown.
                const size_t n = strlen(text);
                if (n > 3)
                {
                    const char *slash = strrchr(text, '\\');
                    if (slash)
                        memmove(text, slash + 1, strlen(slash + 1) + 1);
                }
            }
            else
                image = data < 8 ? data : 4;
            x += int(depth) * 8;
            const unsigned colour = app.Api("GetSysColor", background_index);
            Picture(dc, x, top + (bottom - top - 16) / 2, image, colour);
            x += 20;
        }
        const unsigned text_index[] = {selected ? 14u : 8u};
        Gdi::SetTextColor(dc, app.Api("GetSysColor", text_index));
        Gdi::SetBkMode(dc, 1);
        Gdi::TEXTMETRICA metrics{};
        Gdi::GetTextMetricsA(dc, &metrics);
        Gdi::ExtTextOutA(dc, x, top + (bottom - top - int(metrics.tmHeight)) / 2, 0, nullptr, text,
                         unsigned(strlen(text)));
        if (state & 0x10)
        {
            const unsigned at = f.scratch + 3 * 256;
            app.Store(at, &rect, sizeof rect);
            const unsigned a[] = {dc, at};
            app.Api("DrawFocusRect", a);
        }
    }
    // One cell of bitmap 191h, blue (index 12) in the colour of the row.
    void Picture(unsigned dc, int x, int y, unsigned cell, unsigned background)
    {
        auto &images = app.CommDlgResources();
        if (!images.strip_ok)
            return;
        const unsigned bits = f.scratch + 1024;
        unsigned pixels[16 * 16];
        for (unsigned row = 0; row < 16; ++row)
            for (unsigned column = 0; column < 16; ++column)
            {
                const unsigned index = images.strip[row][cell * 16 + column];
                const unsigned rgb = index == 12 ? ((background & 0xff) << 16 | (background & 0xff00) |
                                                    ((background >> 16) & 0xff))
                                                 : images.palette[index];
                pixels[row * 16 + column] = rgb;
            }
        app.Store(bits, pixels, sizeof pixels);
        const unsigned info = f.scratch + 3 * 256;
        const unsigned header[10] = {40, 16, unsigned(-16), 1 | 32u << 16, 0, 0, 0, 0, 0, 0};
        app.Store(info, header, sizeof header);
        const unsigned a[] = {dc, unsigned(x), unsigned(y), 16, 16, 0, 0, 0, 16, bits, info, 0};
        app.Api("SetDIBitsToDevice", a);
    }
};

// The images and the templates of COMMDLG.DLL of the tree.
CommDlgImages &Application::CommDlgResources()
{
    static CommDlgImages images;
    if (images.loaded)
        return images;
    images.loaded = true;
    char full[260];
    if (files.FullPath("C:\\WINDOWS\\SYSTEM\\COMMDLG.DLL", full) || Win16ReadFile(full, images.file, images.length))
        return images;
    unsigned size = 0;
    const unsigned char *b = NeResource(images.file, images.length, 2, 0x191, size);
    if (b && size >= 40 + 64 + 64 * 16)
    {
        const unsigned width = unsigned(b[4] | b[5] << 8), height = unsigned(b[8] | b[9] << 8);
        if (width == 128 && height == 16 && b[14] == 4)
        {
            for (unsigned i = 0; i < 16; ++i)
                images.palette[i] = unsigned(b[40 + 4 * i + 2]) << 16 | unsigned(b[40 + 4 * i + 1]) << 8 |
                                    b[40 + 4 * i];
            const unsigned char *bits = b + 40 + 64;
            for (unsigned row = 0; row < 16; ++row)
                for (unsigned column = 0; column < 128; ++column)
                {
                    const unsigned char byte = bits[(15 - row) * 64 + column / 2];
                    images.strip[row][column] = column & 1 ? byte & 15 : byte >> 4;
                }
            images.strip_ok = true;
        }
    }
    return images;
}

// GetOpenFileName (COMMDLG.1) and GetSaveFileName (COMMDLG.2).
bool Application::Win16FileDialog(const Win16Frame &frame, bool save, unsigned &result)
{
    const bool gate = TakeFresh();
    result = 0;
    const unsigned ofn = frame.Pointer(0);
    if (!ofn || Read32(ofn) != OpenFileName16)
    {
        Win16CommonDialogError(ErrorStructSize);
        return true;
    }
    if (log && trace_calls)
    {
        char trace[400];
        auto text = [&](unsigned field) {
            const unsigned at = Win16FarPointer(ofn + field);
            return at ? ReadString(at) : "";
        };
        char initial[128], file[128];
        snprintf(initial, sizeof initial, "%s", text(OfnInitialDir));
        snprintf(file, sizeof file, "%s", text(OfnFile));
        snprintf(trace, sizeof trace, "WIN16 COMMDLG file dialog save=%u flags=%08x initial=[%s] file=[%s] current=[%s]",
                 save ? 1u : 0u, Read32(ofn + OfnFlags), initial, file, files.current);
        log(trace);
    }
    auto *held = new Win16OpenFrame(this);
    auto &f = *held;
    struct Release { Win16OpenFrame *frame; ~Release() { delete frame; } } release{held};
    f.ofn = ofn;
    f.ofn_far = frame.Word(1) << 16 | frame.Word(0);
    f.save = save;
    f.flags = Read32(ofn + OfnFlags);
    if (f.flags & OfnEnableHook)
    {
        const unsigned hook = Win16Word(ofn + OfnHook) | Win16Word(ofn + OfnHook + 2) << 16;
        f.hook = hook ? Win16Mark(hook) : 0;
    }
    // The template: the caller's by handle or by name, or COMMDLG's own.
    unsigned native = 0;
    unsigned char *converted = nullptr;
    unsigned length = 0;
    const unsigned instance = Win16Word(ofn + OfnInstance);
    if (f.flags & (OfnEnableTemplate | OfnEnableTemplateHandle))
    {
        native = Win16NativeTemplate(instance, Read32(ofn + OfnTemplateName),
                                     f.flags & OfnEnableTemplateHandle ? instance : 0);
        if (!native)
        {
            Win16CommonDialogError(f.flags & OfnEnableTemplateHandle ? 7 : 6); // LOADRES, FINDRES
            return true;
        }
    }
    else
    {
        auto &images = CommDlgResources();
        unsigned size = 0;
        const unsigned char *bytes =
            images.file ? NeResource(images.file, images.length, 5,
                                     f.flags & OfnAllowMultiSelect ? 0x601 : 0x600, size)
                        : nullptr;
        converted = bytes ? new unsigned char[4 * size + 64] : nullptr;
        if (!converted || !ConvertDialogTemplate(bytes, size, converted, 4 * size + 64, length))
        {
            delete[] converted;
            Win16CommonDialogError(6); // CDERR_FINDRESFAILURE
            return true;
        }
    }
    f.scratch = Allocate(4096);
    const unsigned name_ok = String("commdlg_FileNameOK"), name_sel = String("commdlg_LBSelChangedNotify"),
                   name_help = String("commdlg_help");
    f.message_ok = Api("RegisterWindowMessageA", &name_ok);
    f.message_selchange = Api("RegisterWindowMessageA", &name_sel);
    f.message_help = Api("RegisterWindowMessageA", &name_help);
    Free(name_ok);
    Free(name_sel);
    Free(name_help);
    const unsigned owner = Win16Word(ofn + OfnOwner);
    const unsigned module = win16.state && win16.state->module_count ? win16.state->modules[0].handle : 0;
    unsigned hwnd = 0;
    Win16CommonDialogError(0);
    f.native_template=native;
    const bool kept = gate && PushHostFrame(HostFileDialog, 0, 1, 0, 1);
    if (native)
    {
        const unsigned locked = Api("GlobalLock", &native);
        const unsigned size = Api("GlobalSize", &native);
        auto *copy = locked && size ? new unsigned char[size] : nullptr;
        if (copy && Read(locked, copy, size))
        {
            f.template_bytes=copy;f.template_size=size;copy=nullptr;
            if (kept) GiveTail(TailRecord);
            hwnd = CreateDialogTemplate(f.template_bytes, size, owner, Win16FileDialogToken, ofn, true, instance ? instance : module);
        }
        delete[] copy;

    }
    else
    {
        // COMMDLG's own template goes out with COMMDLG's instance, so the
        // dialog is a window of a 4.0 module.
        const unsigned instance = Win16SystemModule("COMMDLG");
        f.template_bytes=converted;f.template_size=length;converted=nullptr;
        if (kept) GiveTail(TailRecord);
        hwnd = CreateDialogTemplate(f.template_bytes, length, owner, Win16FileDialogToken, ofn, true, instance);
    }
    f.hwnd = hwnd;
    FinishWin16FilePreparation(hwnd);
    if (kept) { auto &root=threads[current_thread].host_frames[threads[current_thread].host_frame_count-1];root.a=hwnd;root.d=0; }
    unsigned answer = 0;
    if (hwnd && state != Failed)
    {
        f.modal_entered = true;
        answer = RunModal(hwnd, kept);
    }
    else if (state != Failed)
        Win16CommonDialogError(0xffff); // CDERR_DIALOGFAILURE
    if (kept) PopHostFrame();
    release.frame = nullptr;
    result = FinishWin16FileDialog(hwnd, answer, f.modal_entered);
    return true;
}
void Application::FinishWin16FilePreparation(unsigned hwnd)
{
    auto *f=Win16OpenFrameOf(this,hwnd,false);if(!f)return;
    f->modal_entered=hwnd&&state!=Failed&&state!=Exited;
    delete[]f->template_bytes;f->template_bytes=nullptr;f->template_size=0;
    if(f->native_template){Api("GlobalUnlock",&f->native_template);Api("GlobalFree",&f->native_template);f->native_template=0;}
}
unsigned Application::FinishWin16FileDialog(unsigned hwnd, unsigned result, bool entered)
{
    auto *frame = Win16OpenFrameOf(this, hwnd, false);
    if (!frame) return 0;
    FinishWin16FilePreparation(hwnd);
    // OFN_NOCHANGEDIR: the folder of the task goes back to where it was.
    if (entered && frame->start_directory[0] && (frame->flags & OfnNoChangeDir))
        files.SetDirectory(frame->start_directory);
    if (frame->scratch) Free(frame->scratch);
    delete frame;
    return result == 1;
}

void Application::DiscardSavedWin16FileDialogs()
{
    for(auto *w=Win16FileWork::head,*next=w;w;w=next){next=w->next;if(w->app==this){Free(w->value[6]);Free(w->value[7]);delete w;}}
    for (auto *f = Win16OpenFrame::head, *next = f; f; f = next)
    {
        next = f->next;
        if (f->app != this) continue;
        bool live = false;
        for (unsigned i = 0; i < 16; ++i)
        {
            const auto &t = threads[i];
            if (i != current_thread && t.id == f->thread && t.fiber && t.fiber->Started() && !t.fiber->Finished())
            { live = true; break; }
        }
        if (live) continue;
        FinishWin16FilePreparation(f->hwnd);
        if (f->scratch) Free(f->scratch);
        delete f;
    }
}
bool Application::Win16FileDispatchResumable(unsigned hwnd,unsigned message) const
{
    if(!Win16Process())return false;
    const auto &t=threads[current_thread];
    for(auto *f=Win16OpenFrame::head;f;f=f->next)if(f->app==this&&f->thread==t.id&&
        (f->hwnd==hwnd||(message==0x110&&!f->hwnd))) {
        for(unsigned i=0;i<t.host_frame_count;++i)if(t.host_frames[i].kind==HostFileDialog&&t.host_frames[i].b==1&&
            (message==0x110?t.host_frames[i].d==1:t.host_frames[i].a==hwnd&&f->modal_entered&&!f->initializing))return true;
    }
    return false;
}
bool Application::Win16FileWorkRecorded(unsigned thread,unsigned index,unsigned kind,unsigned phase) const
{
    auto *w=Win16FileWork::Find(*this,index,kind,thread);
    if(!Win16Process()||!w||!w->kept||w->phase!=phase||!w->frame||w->frame->app!=this||
       w->frame->thread!=threads[thread].id||w->frame->hwnd!=threads[thread].host_frames[index].a)return false;
    if(kind==HostWin16FileMessage)return phase>=1&&phase<=4&&
        (phase!=1||(w->value[0]==0x110&&!w->frame->initializing))&&
        (phase!=4||(w->value[0]==0x111&&(w->value[1]&65535)==Lst1&&(w->value[1]>>16)==2));
    if(kind==HostWin16FileAccept)return phase>=1&&phase<=4;
    return phase==1&&(kind==HostWin16FileOutput||kind==HostWin16FileSelect||kind==HostWin16FileDrive);
}
unsigned Application::ResumeWin16FileWork(unsigned kind,unsigned index)
{
    auto *w=Win16FileWork::Find(*this,index,kind,current_thread);
    if(!w){Fail("A saved Win16 file dialog has no matching operation");return 0;}
    ++threads[current_thread].host_resume_at;Win16OpenDialog e{*this,*w->frame,w->kept};
    if(kind==HostWin16FileMessage)return e.ContinueMessage(w,true);
    if(kind==HostWin16FileAccept)return e.ContinueAccept(w,true);
    if(kind==HostWin16FileOutput)return e.ContinueOutput(w,true);
    Win16OpenDialog::WorkScope scope{e,w};ResumeInner();
    GiveTail(TailNone);if(kind==HostWin16FileDrive)e.Drives();return 0;
}
unsigned Application::Win16FileDialogMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp)
{
    const bool recorded=TakeTail()==TailRecord&&Win16FileDispatchResumable(hwnd,message);
    auto *frame=Win16OpenFrameOf(this,hwnd,message==0x110);if(!frame)return 0;
    Win16OpenDialog e{*this,*frame,recorded};auto *w=e.Begin(HostWin16FileMessage);
    w->value[0]=message;w->value[1]=wp;w->value[2]=lp;return e.ContinueMessage(w,false);
}
unsigned Application::Win16OpenDialog::ContinueMessage(Win16FileWork *w,bool resumed)
{
    WorkScope scope{*this,w};const unsigned hwnd=f.hwnd,message=w->value[0],wp=w->value[1],lp=w->value[2];
    unsigned answer=resumed?app.ResumeInner():0;
    if(resumed&&w->phase==1)return answer&65535;
    if(resumed&&w->phase==3)return w->value[3];
    if(resumed&&w->phase==4){w->value[3]=1;Phase(w,3);Accept();return 1;}
    if(!resumed) {
        if(message==0x110){Initialize();Phase(w,1);return f.hook?Hook(message,wp,f.ofn_far):1;}
        Phase(w,2);answer=Hook(message,wp,lp);
    }
    if(resumed&&w->phase==2)answer&=65535;
    if(answer)return answer;
    app.GiveTail(TailNone);
    switch(message) {
    case 0x10:app.EndDialog(hwnd,0);return 1;
    case 0x2c:app.Write32(lp+16,16);return 1;
    case 0x2b:Draw(lp);return 1;
    case 0x111: {
        const unsigned id=wp&65535,code=wp>>16;
        w->value[3]=id==1||id==Psh15||id==Lst1||id==Lst2||id==Cmb1||id==Cmb2;
        if(id==1){Phase(w,3);Accept();return 1;}
        if(id==2){app.Win16CommonDialogError(0);app.EndDialog(hwnd,0);return 1;}
        if(id==Psh15){const unsigned owner=Field(OfnOwner);if(owner){Phase(w,3);app.Send(owner,f.message_help,hwnd,f.ofn_far);}return 1;}
        if(id==Lst1&&code==1){Phase(w,3);FileSelected();}
        else if(id==Lst1&&code==2){Phase(w,4);FileSelected();w->value[3]=1;Phase(w,3);Accept();}
        else if(id==Lst2&&code==2)FolderChosen();
        else if(id==Cmb1&&code==1&&!f.initializing){SelectFilter(app.Send(lp,0x147));User::SetWindowText(Item(Edt1),f.pattern);Scan();}
        else if(id==Cmb2&&code==1&&!f.initializing){Phase(w,3);DriveChosen();}
        return w->value[3];
    }
    default:return 0;
    }
}
