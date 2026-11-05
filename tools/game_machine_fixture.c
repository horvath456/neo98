/* SPDX-License-Identifier: GPL-2.0-or-later
   The guest of tools/test_game_machine_host.py. What
   it does follows the last word of its command line:

     (none) the game: writes C:\GMTEST\SAVE.TXT and counts
               HKCU\Software\NEO98Test\GameMachine\Score up, shows a window
               for a second and a half and ends with code 7
     launcher starts itself as "child" with CreateProcess and ends at once
     shell the same with ShellExecute, which has no Explorer to ask
     child shows a window for three seconds, writes CHILD.TXT and ends
     stay shows a window for twenty seconds of the tick count
     crash writes through a null pointer
     prepare in the normal machine: makes C:\GMBASE with the files the
               next two modes work on
     prepare2 in the normal machine: the files the DOS guest, the 16-bit
               guest and "cross" work on
     layer1 in the game machine: reads, writes, deletes, renames, lists,
               maps, locks and watches files of C:\GM, which lie in the
               drive of the normal machine below the write layer
     layer2 the next start: what layer1 did is still so
     cross in the game machine: deletes C:\GMBASE\XDEL.TXT, writes
               XWIN.TXT, starts the DOS program C:\SPIELE\GMDOS.COM
               (tools/layer_dos_fixture.asm) and, when that has written its
               DOSOUT.TXT, looks at what the DOS program did to C:\GM:
               both see each other's changes while both run
     dgame a game installed on another drive: writes a save and its
               settings into its own folder, deletes a file there, renames
               one and lists the folder

   Every mode writes what it saw into a file of its own in C:\GMTEST. */
typedef unsigned U;
typedef struct { U style; void *proc; int a, b; U instance, icon, cursor, brush; const char *menu, *name; } WNDCLASSA;
typedef struct { U hwnd, message, wparam, lparam, time; int x, y; } MSG;
typedef struct { U cb; char *reserved, *desktop, *title; U x, y, xs, ys, xc, yc, fill, flags; unsigned short show, r2; void *r3; U in, out, err; } STARTUPINFOA;
typedef struct { U process, thread, pid, tid; } PROCESS_INFORMATION;
#define I(r, n, ...) __declspec(dllimport) r __stdcall n(__VA_ARGS__)
I(U, RegisterClassA, const WNDCLASSA *);
I(U, CreateWindowExA, U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
I(U, DefWindowProcA, U, U, U, U);
I(int, GetMessageA, MSG *, U, U, U);
I(U, DispatchMessageA, const MSG *);
I(int, ShowWindow, U, int);
I(U, SetTimer, U, U, U, void *);
I(void, PostQuitMessage, int);
I(U, FindWindowA, const char *, const char *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
I(U, GetStockObject, int);
I(U, GetModuleHandleA, const char *);
I(U, GetModuleFileNameA, U, char *, U);
I(char *, GetCommandLineA, void);
I(U, GetCurrentDirectoryA, U, char *);
I(int, CreateDirectoryA, const char *, void *);
I(U, CreateFileA, const char *, U, U, void *, U, U, U);
I(int, WriteFile, U, const void *, U, U *, void *);
I(int, CloseHandle, U);
I(U, GetFileAttributesA, const char *);
I(int, CreateProcessA, const char *, char *, void *, void *, int, U, void *, const char *, STARTUPINFOA *, PROCESS_INFORMATION *);
I(void, ExitProcess, U);
I(U, GetTickCount, void);
I(int, RegCreateKeyExA, U, const char *, U, char *, U, U, void *, U *, U *);
I(int, RegQueryValueExA, U, const char *, U *, U *, void *, U *);
I(int, RegSetValueExA, U, const char *, U, U, const void *, U);
I(int, RegCloseKey, U);
I(U, ShellExecuteA, U, const char *, const char *, const char *, const char *, int);
I(int, DeleteFileA, const char *);
I(int, MoveFileA, const char *, const char *);
I(int, RemoveDirectoryA, const char *);
I(int, SetFileAttributesA, const char *, U);
I(U, GetShortPathNameA, const char *, char *, U);
I(U, FindFirstFileA, const char *, void *);
I(int, FindNextFileA, U, void *);
I(int, FindClose, U);
I(int, ReadFile, U, void *, U, U *, void *);
I(U, SetFilePointer, U, int, int *, U);
I(void, Sleep, U);
I(int, GetVolumeInformationA, const char *, char *, U, U *, U *, U *, char *, U);
I(int, GetDiskFreeSpaceA, const char *, U *, U *, U *, U *);
I(U, CreateFileMappingA, U, void *, U, U, U, const char *);
I(char *, MapViewOfFile, U, U, U, U, U);
I(int, UnmapViewOfFile, const void *);
I(int, LockFile, U, U, U, U, U);
I(int, UnlockFile, U, U, U, U, U);
I(U, FindFirstChangeNotificationA, const char *, int, U);
I(int, FindCloseChangeNotification, U);
I(U, WaitForSingleObject, U, U);
I(int, WritePrivateProfileStringA, const char *, const char *, const char *, const char *);
I(U, GetPrivateProfileStringA, const char *, const char *, const char *, char *, U, const char *);
I(int, WriteProfileStringA, const char *, const char *, const char *);
I(U, GetLastError, void);

static char self[260], text[1200];
static U exit_code;

static int Same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void Record(const char *name, U length)
{
    char path[64];
    U file, written;
    CreateDirectoryA("C:\\GMTEST", 0);
    wsprintfA(path, "C:\\GMTEST\\%s", name);
    file = CreateFileA(path, 0x40000000, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, length, &written, 0);
    CloseHandle(file);
}
static U __stdcall Proc(U hwnd, U message, U wparam, U lparam)
{
    if (message == 0x113) /* WM_TIMER */
    {
        if (wparam == 2)
        {
            Record("CHILD.TXT", wsprintfA(text, "mode=child\r\ntick=%u\r\n", GetTickCount()));
        }
        if (wparam == 3)
            Record("END.TXT", wsprintfA(text, "mode=stay\r\ntick=%u\r\n", GetTickCount()));
        PostQuitMessage(0);
        return 0;
    }
    if (message == 0x10 || message == 0x100) /* WM_CLOSE, WM_KEYDOWN */
    {
        Record("END.TXT", wsprintfA(text, "mode=stay\r\nclosed=%u\r\n", message));
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}
static U Window(const char *title)
{
    WNDCLASSA c = {0};
    U window;
    c.proc = Proc;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(0);
    c.name = "NEO98GameMachineFixture";
    RegisterClassA(&c);
    window = CreateWindowExA(0, c.name, title, 0x10cf0000, 60, 60, 320, 200, 0, 0, c.instance, 0);
    ShowWindow(window, 1);
    return window;
}
/* --- The write layer ------------------------------------------------------ */
#define B "C:\\GMBASE\\"
#define BIG (5u * 1024 * 1024)
static char out[6000], names[3000], block[65536];
static U used;
static unsigned char Pattern(U offset) { return (unsigned char)(offset * 31 + (offset >> 12)); }
static U Length(const char *a) { U n = 0; while (a[n]) ++n; return n; }
static void Put(const char *path, const char *content)
{
    U file = CreateFileA(path, 0x40000000, 0, 0, 2, 0x80, 0), written;
    WriteFile(file, content, Length(content), &written, 0);
    CloseHandle(file);
}
/* The content of a file as a string in `block`; "(none)" when it is not there. */
static const char *Get(const char *path)
{
    U file = CreateFileA(path, 0x80000000, 3, 0, 3, 0, 0), got = 0;
    if (file == 0xffffffffu) return "(none)";
    ReadFile(file, block, 200, &got, 0);
    CloseHandle(file);
    block[got] = 0;
    return block;
}
static int There(const char *path) { return GetFileAttributesA(path) != 0xffffffffu; }
static void Big(const char *path)
{
    U file = CreateFileA(path, 0x40000000, 0, 0, 2, 0x80, 0), written, at, i;
    for (at = 0; at < BIG; at += sizeof block)
    {
        for (i = 0; i < sizeof block; ++i) block[i] = (char)Pattern(at + i);
        WriteFile(file, block, sizeof block, &written, 0);
    }
    CloseHandle(file);
}
/* Whether `count` bytes at `offset` of an open file are the pattern, or `with` in place of it. */
static int BigIs(U file, U offset, U count, const char *with)
{
    U got = 0, i;
    SetFilePointer(file, (int)offset, 0, 0);
    if (!ReadFile(file, block, count, &got, 0) || got != count) return 0;
    for (i = 0; i < count; ++i)
        if ((unsigned char)block[i] != (with ? (unsigned char)with[i] : Pattern(offset + i))) return 0;
    return 1;
}
/* The entries a pattern finds as "NAME/ALIAS;" in `names`, and how many
   names or aliases come twice. */
static U List(const char *pattern, U *count)
{
    static struct { U attributes, times[6], high, low, r0, r1; char name[260], alias[14]; } found, other;
    U search = FindFirstFileA(pattern, &found), twice = 0, n = 0, inner, unused;
    if (!count) count = &unused;
    *count = 0;
    names[0] = 0;
    if (search == 0xffffffffu) return 0;
    do
    {
        U seen = 0;
        if (found.name[0] == '.') continue;
        ++*count;
        n += wsprintfA(names + n, "%s/%s;", found.name, found.alias);
        /* Every entry again: this name or alias must answer to one only. */
        inner = FindFirstFileA(pattern, &other);
        do
        {
            const char *mine = found.alias[0] ? found.alias : found.name, *theirs = other.alias[0] ? other.alias : other.name;
            if (Same(found.name, other.name) || Same(mine, theirs)) ++seen;
        } while (FindNextFileA(inner, &other));
        FindClose(inner);
        if (seen != 1) ++twice;
    } while (FindNextFileA(search, &found) && n < sizeof names - 300);
    FindClose(search);
    return twice;
}
static void Add(const char *key, U value) { used += wsprintfA(out + used, "%s=%u\r\n", key, value); }
static void AddText(const char *key, const char *value) { used += wsprintfA(out + used, "%s=%s\r\n", key, value); }
static U Serial(void)
{
    U serial = 0, length = 0, flags = 0;
    GetVolumeInformationA("C:\\", 0, 0, &serial, &length, &flags, 0, 0);
    return serial;
}
static void Finish(const char *name)
{
    U file, written;
    CreateDirectoryA("C:\\GMTEST", 0);
    file = CreateFileA(name, 0x40000000, 0, 0, 2, 0x80, 0);
    WriteFile(file, out, used, &written, 0);
    CloseHandle(file);
}
static void Prepare(void)
{
    CreateDirectoryA("C:\\GMBASE", 0);
    Put(B "READ.TXT", "base read");
    Put(B "WRITE.TXT", "base write 0123456789");
    Put(B "DELETE.TXT", "base delete");
    Put(B "RENAME.TXT", "base rename");
    Put(B "ATTR.TXT", "base attributes");
    Put(B "Long Name One.txt", "one");
    Put(B "OPEN.TXT", "base open, read while it goes away");
    Put(B "CHANGE.TXT", "old");
    Put(B "SHADOW.TXT", "base shadow");
    Put(B "BASE.INI", "[a]\r\nk=v\r\n");
    CreateDirectoryA(B "DELDIR", 0);
    Put(B "DELDIR\\INNER.TXT", "inner");
    CreateDirectoryA(B "MOVEDIR", 0);
    Put(B "MOVEDIR\\A.TXT", "moved a");
    CreateDirectoryA(B "MOVEDIR\\SUB", 0);
    Put(B "MOVEDIR\\SUB\\B.TXT", "moved b");
    Big(B "BIG.BIN");
    Big(B "BIG2.BIN");
    Add("serial", Serial());
    Finish("C:\\GMTEST\\PREPARE.TXT");
    ExitProcess(0);
}
/* In the normal machine, for the DOS and the 16-bit guest
   (tools/layer_dos_fixture.asm, tools/layer_win16_fixture.asm) and for
   "cross" (tools/test_game_machine_files_host.py). */
static void Prepare2(void)
{
    CreateDirectoryA("C:\\GMBASE", 0);
    Put(B "DOSBASE.TXT", "dos base");
    Put(B "DOSDEL.TXT", "dos delete");
    Put(B "DOSREN.TXT", "dos rename");
    CreateDirectoryA(B "DOSRD", 0);
    Put(B "DOSRD\\RD.TXT", "rd");
    Put(B "W16BASE.TXT", "w16 base");
    Put(B "W16DEL.TXT", "w16 delete");
    Put(B "W16REN.TXT", "ren16");
    Put(B "XDEL.TXT", "x delete");
    Put(B "Long Name One.txt", "one");
    Add("prepared", 1);
    Finish("C:\\GMTEST\\PREPARE2.TXT");
    ExitProcess(0);
}
static void Layer1(void)
{
    static char text[300];
    U file, second, got = 0, count = 0, mapping, change, a = 0, b = 0, c = 0, d = 0, held, big;
    char *view;
    /* What the normal drive holds, read through the layer. */
    AddText("read", Get(B "READ.TXT"));
    AddText("added", Get(B "ADDED.TXT"));
    AddText("changed", Get(B "CHANGE.TXT"));
    file = CreateFileA(B "BIG.BIN", 0x80000000, 3, 0, 3, 0, 0);
    Add("big_read", BigIs(file, 0, 4096, 0) && BigIs(file, BIG - 5000, 5000, 0) && BigIs(file, 3u * 1024 * 1024 + 7, 60000, 0));
    CloseHandle(file);
    /* A mapping of a file of the normal drive. */
    file = CreateFileA(B "READ.TXT", 0x80000000, 3, 0, 3, 0, 0);
    mapping = CreateFileMappingA(file, 0, 2, 0, 0, 0);
    view = mapping ? MapViewOfFile(mapping, 4, 0, 0, 0) : 0;
    Add("map_read", view && view[0] == 'b' && view[5] == 'r' && view[8] == 'd');
    if (view) UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(file);
    /* Writing to files of the normal drive. */
    file = CreateFileA(B "WRITE.TXT", 0xc0000000, 3, 0, 3, 0, 0);
    Add("write_open", file != 0xffffffffu);
    WriteFile(file, "GAME", 4, &got, 0);
    SetFilePointer(file, 0, 0, 2);
    WriteFile(file, " and more", 9, &got, 0);
    /* A lock held through this handle keeps another one from reading there. */
    Add("lock", LockFile(file, 0, 0, 4, 0));
    second = CreateFileA(B "WRITE.TXT", 0x80000000, 3, 0, 3, 0, 0);
    Add("lock_read", ReadFile(second, text, 4, &got, 0) ? 0u : GetLastError());
    Add("unlock", UnlockFile(file, 0, 0, 4, 0));
    CloseHandle(second);
    CloseHandle(file);
    AddText("write", Get(B "WRITE.TXT"));
    /* A writable mapping of the file that is in the layer now. */
    file = CreateFileA(B "WRITE.TXT", 0xc0000000, 3, 0, 3, 0, 0);
    mapping = CreateFileMappingA(file, 0, 4, 0, 0, 0);
    view = mapping ? MapViewOfFile(mapping, 2, 0, 0, 0) : 0;
    if (view) { view[5] = 'W'; UnmapViewOfFile(view); }
    CloseHandle(mapping);
    CloseHandle(file);
    AddText("map_write", Get(B "WRITE.TXT"));
    big = CreateFileA(B "BIG.BIN", 0xc0000000, 3, 0, 3, 0, 0);
    SetFilePointer(big, (int)(BIG - 1000), 0, 0);
    WriteFile(big, "LAYER", 5, &got, 0);
    Add("big_write", got == 5 && BigIs(big, BIG - 1000, 5, "LAYER") && BigIs(big, 0, 4096, 0) && BigIs(big, BIG - 995, 995, 0));
    CloseHandle(big);
    Put(B "SHADOW.TXT", "game shadow");
    /* Deleting and renaming what the normal drive holds. */
    Add("delete", DeleteFileA(B "DELETE.TXT"));
    Add("delete_gone", !There(B "DELETE.TXT"));
    Add("rename", MoveFileA(B "RENAME.TXT", B "RENAMED.TXT"));
    AddText("renamed", Get(B "RENAMED.TXT"));
    Add("rename_gone", !There(B "RENAME.TXT"));
    Add("deldir_full", RemoveDirectoryA(B "DELDIR") ? 0u : GetLastError());
    Add("deldir", DeleteFileA(B "DELDIR\\INNER.TXT") && RemoveDirectoryA(B "DELDIR"));
    Add("deldir_gone", !There(B "DELDIR"));
    Add("deldir_again", CreateDirectoryA(B "DELDIR", 0) && !There(B "DELDIR\\INNER.TXT"));
    Put(B "DELDIR\\NEW.TXT", "new");
    Add("movedir", MoveFileA(B "MOVEDIR", B "MOVED") ? 1u : 1000u + GetLastError());
    AddText("movedir_a", Get(B "MOVED\\A.TXT"));
    AddText("movedir_b", Get(B "MOVED\\SUB\\B.TXT"));
    Add("movedir_gone", !There(B "MOVEDIR") && !There(B "MOVEDIR\\A.TXT"));
    Add("attributes", SetFileAttributesA(B "ATTR.TXT", 3) && GetFileAttributesA(B "ATTR.TXT") == 3);
    /* A new long name beside one of the normal drive: the aliases differ. */
    Put(B "Long Name Two.txt", "two");
    GetShortPathNameA(B "Long Name One.txt", text, sizeof text);
    AddText("alias_one", text);
    GetShortPathNameA(B "Long Name Two.txt", text, sizeof text);
    AddText("alias_two", text);
    AddText("alias_one_reads", Get(B "LONGNA~1.TXT"));
    /* A change notification on a directory of the normal drive. */
    change = FindFirstChangeNotificationA("C:\\GMBASE", 0, 1);
    Put(B "NOTIFY.TXT", "notify");
    Add("notify", change != 0xffffffffu && WaitForSingleObject(change, 2000) == 0);
    FindCloseChangeNotification(change);
    /* Profile files: one of the normal drive, and WIN.INI. */
    Add("ini_write", WritePrivateProfileStringA("b", "game", "yes", B "BASE.INI"));
    GetPrivateProfileStringA("a", "k", "?", text, sizeof text, B "BASE.INI");
    AddText("ini_old", text);
    GetPrivateProfileStringA("b", "game", "?", text, sizeof text, B "BASE.INI");
    AddText("ini_new", text);
    Add("winini", WriteProfileStringA("NEO98GameMachine", "layer", "1"));
    Add("twice", List(B "*.*", &count));
    Add("count", count);
    AddText("names", names);
    Add("serial", Serial());
    Add("space", GetDiskFreeSpaceA("C:\\", &a, &b, &c, &d) && a && b && c && d);
    /* Two files of the normal drive stay open while the test deletes them
       there: a small one and one that is read where it lies. */
    held = CreateFileA(B "OPEN.TXT", 0x80000000, 3, 0, 3, 0, 0);
    ReadFile(held, text, 4, &got, 0);
    big = CreateFileA(B "BIG2.BIN", 0x80000000, 3, 0, 3, 0, 0);
    Add("held_before", got == 4 && BigIs(big, 100, 1000, 0));
    Put("C:\\GMTEST\\HOLD.TXT", "hold");
    Sleep(8000);
    got = 0;
    ReadFile(held, text, 100, &got, 0);
    text[got] = 0;
    AddText("held_after", text);
    Add("held_big_after", BigIs(big, BIG - 70000, 60000, 0));
    CloseHandle(held);
    CloseHandle(big);
    Finish("C:\\GMTEST\\LAYER1.TXT");
    ExitProcess(0);
}
static void Layer2(void)
{
    static char text[300];
    U count = 0, big;
    AddText("read", Get(B "READ.TXT"));
    AddText("added2", Get(B "ADDED2.TXT"));
    AddText("write", Get(B "WRITE.TXT"));
    AddText("shadow", Get(B "SHADOW.TXT"));
    Add("delete_gone", !There(B "DELETE.TXT"));
    Add("rename_gone", !There(B "RENAME.TXT"));
    AddText("renamed", Get(B "RENAMED.TXT"));
    Add("deldir", There(B "DELDIR") && !There(B "DELDIR\\INNER.TXT"));
    AddText("deldir_new", Get(B "DELDIR\\NEW.TXT"));
    Add("movedir_gone", !There(B "MOVEDIR") && !There(B "MOVEDIR\\A.TXT") && !There(B "MOVEDIR\\SUB\\B.TXT"));
    AddText("movedir_a", Get(B "MOVED\\A.TXT"));
    AddText("movedir_b", Get(B "MOVED\\SUB\\B.TXT"));
    Add("attributes", GetFileAttributesA(B "ATTR.TXT"));
    GetShortPathNameA(B "Long Name One.txt", text, sizeof text);
    AddText("alias_one", text);
    GetShortPathNameA(B "Long Name Two.txt", text, sizeof text);
    AddText("alias_two", text);
    GetPrivateProfileStringA("b", "game", "?", text, sizeof text, B "BASE.INI");
    AddText("ini_new", text);
    big = CreateFileA(B "BIG.BIN", 0x80000000, 3, 0, 3, 0, 0);
    Add("big", BigIs(big, BIG - 1000, 5, "LAYER") && BigIs(big, 0, 4096, 0) && BigIs(big, BIG - 995, 995, 0));
    CloseHandle(big);
    Add("held_gone", !There(B "OPEN.TXT") && !There(B "BIG2.BIN"));
    Add("twice", List(B "*.*", &count));
    Add("count", count);
    AddText("names", names);
    Add("serial", Serial());
    Finish("C:\\GMTEST\\LAYER2.TXT");
    ExitProcess(0);
}
static void Game(const char *line)
{
    static char directory[260];
    U key = 0, type = 0, size = 4, score = 0, created = 0, set = 99, machine = 99;
    GetCurrentDirectoryA(sizeof directory, directory);
    if (!RegCreateKeyExA(0x80000001u, "Software\\NEO98Test\\GameMachine", 0, 0, 0, 0xf003f, 0, &key, &created))
    {
        RegQueryValueExA(key, "Score", 0, &type, &score, &size);
        ++score;
        set = RegSetValueExA(key, "Score", 0, 4, &score, 4);
        RegCloseKey(key);
    }
    if (!RegCreateKeyExA(0x80000002u, "Software\\NEO98Test\\GameMachine", 0, 0, 0, 0xf003f, 0, &key, &created))
    {
        machine = RegSetValueExA(key, "Installed", 0, 1, "yes", 4);
        RegCloseKey(key);
    }
    Record("SAVE.TXT", wsprintfA(text,
        "mode=game\r\nline=%s\r\nself=%s\r\ndirectory=%s\r\nscore=%u\r\nregistry=%u,%u\r\nsource_marker=%u\r\ntray=%u\r\n",
        line, self, directory, score, set, machine, GetFileAttributesA("C:\\GMTEST\\SOURCE.TXT") != 0xffffffffu,
        FindWindowA("Shell_TrayWnd", 0) != 0));
    SetTimer(Window("Game machine fixture"), 1, 1500, 0);
    exit_code = 7;
}
/* A Windows and a DOS program of the game machine at the same time. */
static void Cross(void)
{
    static STARTUPINFOA startup;
    static PROCESS_INFORMATION process;
    static char line[] = "C:\\SPIELE\\GMDOS.COM";
    U waited = 0, started;
    Add("delete", DeleteFileA(B "XDEL.TXT") != 0);
    Put(B "XWIN.TXT", "win wrote");
    Add("dos_file_before", There(B "DOSNEW.TXT"));
    startup.cb = sizeof startup;
    started = (U)CreateProcessA(0, line, 0, 0, 0, 0, 0, 0, &startup, &process);
    Add("started", started);
    /* The DOS program writes DOSOUT.TXT last. */
    while (!There(B "DOSOUT.TXT") && waited < 90000) { Sleep(100); waited += 100; }
    Sleep(300);
    Add("waited", waited < 90000);
    AddText("dos_out", Get(B "DOSOUT.TXT"));
    /* What the DOS program did is so for this program at once. */
    Add("dos_new", Same(Get(B "DOSNEW.TXT"), "dos new"));
    AddText("dos_base", Get(B "DOSBASE.TXT"));
    Add("dos_deleted_gone", !There(B "DOSDEL.TXT"));
    Add("dos_renamed_gone", !There(B "DOSREN.TXT"));
    AddText("dos_moved", Get(B "DOSMOVED.TXT"));
    Add("dos_directory", There(B "DOSDIR\\IN.TXT"));
    Add("dos_removed_directory_empty", There(B "DOSRD") && !There(B "DOSRD\\RD.TXT"));
    Add("listed_twice", List(B "*", 0));
    AddText("names", names);
    Finish("C:\\GMTEST\\CROSS.TXT");
    ExitProcess(0);
}
/* A game that lies on another drive than C: and keeps its files beside its
   program: the path of a file in the folder of this program. */
static const char *Beside(const char *name)
{
    static char paths[8][300];
    static U next;
    char *path = paths[next++ & 7];
    U end = Length(self);
    while (end && self[end - 1] != '\\') --end;
    wsprintfA(path, "%s", self);
    wsprintfA(path + end, "%s", name);
    return path;
}
static void DriveGame(void)
{
    U run = There(Beside("SAVE.DAT"));
    AddText("self", self);
    Add("run", run + 1);
    AddText("data", Get(Beside("DATA.BIN")));
    AddText("settings_before", Get(Beside("Game Settings.ini")));
    Put(Beside("Game Settings.ini"), run ? "volume=11" : "volume=9");
    AddText("settings_after", Get(Beside("Game Settings.ini")));
    AddText("save_before", Get(Beside("SAVE.DAT")));
    Put(Beside("SAVE.DAT"), run ? "level 2" : "level 1");
    AddText("save_after", Get(Beside("SAVE.DAT")));
    Add("old_there", There(Beside("OLD.DAT")));
    Add("delete", DeleteFileA(Beside("OLD.DAT")) != 0);
    Add("old_gone", !There(Beside("OLD.DAT")));
    Add("rename", MoveFileA(Beside("INTRO.AVI"), Beside("INTRO.OFF")) != 0);
    Add("renamed", There(Beside("INTRO.OFF")) && !There(Beside("INTRO.AVI")));
    Add("mkdir", CreateDirectoryA(Beside("SAVES"), 0) != 0);
    Put(Beside("SAVES\\SLOT1.SAV"), "slot");
    Add("slot", Same(Get(Beside("SAVES\\SLOT1.SAV")), "slot"));
    Add("listed_twice", List(Beside("*"), 0));
    AddText("names", names);
    /* The record lies beside the program as well, per run. */
    Finish(Beside(run ? "RUN2.TXT" : "RUN1.TXT"));
    ExitProcess(0);
}
static void Launch(int shell)
{
    static char line[400];
    U result;
    if (shell)
        result = ShellExecuteA(0, "open", self, "child", 0, 1);
    else
    {
        static STARTUPINFOA startup;
        static PROCESS_INFORMATION process;
        startup.cb = sizeof startup;
        wsprintfA(line, "\"%s\" child", self);
        result = (U)CreateProcessA(0, line, 0, 0, 0, 0, 0, 0, &startup, &process);
    }
    Record(shell ? "SHELL.TXT" : "LAUNCH.TXT", wsprintfA(text, "mode=%s\r\nresult=%u\r\ntick=%u\r\n", shell ? "shell" : "launcher",
                                                         result, GetTickCount()));
    ExitProcess(3);
}

void mainCRTStartup(void)
{
    MSG message;
    const char *line = GetCommandLineA(), *mode = line, *at;
    GetModuleFileNameA(0, self, sizeof self);
    /* The last word of the command line, after the program's own name. */
    for (at = line; *at; ++at)
        if (*at == ' ' && at[1] && at[1] != ' ') mode = at + 1;
    if (Same(mode, "launcher")) Launch(0);
    else if (Same(mode, "shell")) Launch(1);
    else if (Same(mode, "child")) SetTimer(Window("Spielmaschine Kind"), 2, 3000, 0);
    else if (Same(mode, "stay"))
    {
        Record("STAY.TXT", wsprintfA(text, "mode=stay\r\ntick=%u\r\n", GetTickCount()));
        SetTimer(Window("Game machine stays open"), 3, 20000, 0);
    }
    else if (Same(mode, "prepare")) Prepare();
    else if (Same(mode, "prepare2")) Prepare2();
    else if (Same(mode, "layer1")) Layer1();
    else if (Same(mode, "layer2")) Layer2();
    else if (Same(mode, "cross")) Cross();
    else if (Same(mode, "dgame")) DriveGame();
    else if (Same(mode, "crash"))
    {
        Record("CRASH.TXT", wsprintfA(text, "mode=crash\r\n"));
        Window("Game machine crash");
        *(volatile int *)0 = 1;
    }
    else Game(line);
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(exit_code);
}
