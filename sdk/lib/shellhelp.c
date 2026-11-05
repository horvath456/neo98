/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* shellhelp: start programs and documents, icons out of files, shortcuts.
   Own code; see shellhelp.h. docs/ARM64-APPS.md. */
#include "shellhelp.h"

/* --- small things ---------------------------------------------------------- */
static void *Allocate(SIZE_T size) { return HeapAlloc(GetProcessHeap(), 0, size); }
static void Release(void *memory) { if (memory) HeapFree(GetProcessHeap(), 0, memory); }
static unsigned Word(const BYTE *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned Long(const BYTE *p) { return Word(p) | Word(p + 2) << 16; }
static void Copy(char *to, const char *from, int room)
{
    lstrcpynA(to, from ? from : "", room);
}
static const char *Extension(const char *path)
{
    const char *dot = NULL;
    for (; *path; ++path)
    {
        if (*path == '.') dot = path;
        else if (*path == '\\' || *path == '/' || *path == ':') dot = NULL;
    }
    return dot ? dot : path;
}
/* The whole file in memory; the caller releases it. */
static BYTE *ReadWhole(LPCSTR path, DWORD *size)
{
    const HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE *bytes;
    DWORD got = 0;
    if (file == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(file, NULL);
    bytes = *size && *size != 0xffffffffu ? (BYTE *)Allocate(*size) : NULL;
    if (bytes && (!ReadFile(file, bytes, *size, &got, NULL) || got != *size))
    {
        Release(bytes);
        bytes = NULL;
    }
    CloseHandle(file);
    return bytes;
}

/* --- shortcuts ---------------------------------------------------------------- */
/* One string of the string data: a count of characters, then ANSI or UTF-16. */
static const BYTE *LinkString(const BYTE *at, const BYTE *end, int unicode, char *out, int room)
{
    unsigned count;
    if (at + 2 > end) return NULL;
    count = Word(at);
    at += 2;
    if (at + (unicode ? 2 * count : count) > end) return NULL;
    if (out)
    {
        if (unicode)
        {
            const int n = WideCharToMultiByte(0, 0, (LPCWSTR)at, (int)count, out, room - 1, NULL, NULL);
            out[n > 0 ? n : 0] = 0;
        }
        else
        {
            unsigned n = count < (unsigned)room - 1 ? count : (unsigned)room - 1;
            memcpy(out, at, n);
            out[n] = 0;
        }
    }
    return at + (unicode ? 2 * count : count);
}
BOOL ShellHelpReadLink(LPCSTR path, ShellHelpLink *link)
{
    static const BYTE class_id[16] = {0x01, 0x14, 0x02, 0x00, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};
    DWORD size = 0;
    BYTE *file = ReadWhole(path, &size);
    const BYTE *at, *end;
    unsigned flags;
    char relative[MAX_PATH] = "";
    BOOL ok = FALSE;
    memset(link, 0, sizeof *link);
    link->show = SW_SHOWNORMAL;
    if (!file) return FALSE;
    end = file + size;
    if (size < 0x4c || Long(file) != 0x4c || memcmp(file + 4, class_id, 16)) goto done;
    flags = Long(file + 0x14);
    link->icon_index = (int)Long(file + 0x38);
    link->show = (int)Long(file + 0x3c);
    at = file + 0x4c;
    if (flags & 1) /* the target as a list of shell items: skipped */
    {
        if (at + 2 > end) goto done;
        at += 2 + Word(at);
    }
    if (flags & 2) /* where the target is: a path on a local volume */
    {
        unsigned info_size, info_flags, base, suffix;
        if (at + 0x1c > end) goto done;
        info_size = Long(at);
        if (info_size < 0x1c || at + info_size > end) goto done;
        info_flags = Long(at + 8);
        base = Long(at + 0x10);
        suffix = Long(at + 0x18);
        if ((info_flags & 1) && base && base < info_size)
        {
            Copy(link->target, (const char *)at + base, MAX_PATH);
            if (suffix && suffix < info_size && lstrlenA(link->target) + lstrlenA((const char *)at + suffix) < MAX_PATH)
                lstrcatA(link->target, (const char *)at + suffix);
        }
        at += info_size;
    }
    if (flags & 4 && !(at = LinkString(at, end, flags & 0x80, link->description, MAX_PATH))) goto done;
    if (flags & 8 && !(at = LinkString(at, end, flags & 0x80, relative, MAX_PATH))) goto done;
    if (flags & 0x10 && !(at = LinkString(at, end, flags & 0x80, link->directory, MAX_PATH))) goto done;
    if (flags & 0x20 && !(at = LinkString(at, end, flags & 0x80, link->arguments, MAX_PATH))) goto done;
    if (flags & 0x40 && !(at = LinkString(at, end, flags & 0x80, link->icon, MAX_PATH))) goto done;
    /* Without a local path the relative one counts from the folder of the shortcut. */
    if (!link->target[0] && relative[0])
    {
        char folder[MAX_PATH], *name = NULL;
        if (GetFullPathNameA(path, MAX_PATH, folder, &name) && name)
        {
            *name = 0;
            if (lstrlenA(folder) + lstrlenA(relative) < MAX_PATH)
            {
                lstrcatA(folder, relative);
                GetFullPathNameA(folder, MAX_PATH, link->target, &name);
            }
        }
    }
    ok = link->target[0] != 0;
done:
    Release(file);
    return ok;
}

/* --- starting ------------------------------------------------------------------ */
static UINT_PTR Run(char *command_line, LPCSTR directory, int show, HANDLE *process)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION started;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = (WORD)show;
    if (!CreateProcessA(NULL, command_line, NULL, NULL, FALSE, 0, NULL, directory && directory[0] ? directory : NULL, &startup,
                        &started))
    {
        const DWORD error = GetLastError();
        return error && error < 32 ? error : 2;
    }
    CloseHandle(started.hThread);
    if (process) *process = started.hProcess;
    else CloseHandle(started.hProcess);
    return 33;
}
/* The default value of a key of HKEY_CLASSES_ROOT. */
static BOOL ClassValue(LPCSTR key, char *value, DWORD room)
{
    HKEY handle;
    DWORD type = 0, size = room;
    BOOL ok;
    if (RegOpenKeyExA(HKEY_CLASSES_ROOT, key, 0, KEY_QUERY_VALUE, &handle) != ERROR_SUCCESS) return FALSE;
    ok = RegQueryValueExA(handle, NULL, NULL, &type, (LPBYTE)value, &size) == ERROR_SUCCESS &&
         (type == REG_SZ || type == REG_EXPAND_SZ) && value[0];
    RegCloseKey(handle);
    return ok;
}
/* The command registered for opening things of this class: under the class
   itself, or under the class its default value names. */
static BOOL OpenCommand(LPCSTR class_name, char *command, DWORD room)
{
    char key[MAX_PATH + 32], type[MAX_PATH];
    wsprintfA(key, "%.200s\\shell\\open\\command", class_name);
    if (ClassValue(key, command, room)) return TRUE;
    if (!ClassValue(class_name, type, sizeof type)) return FALSE;
    wsprintfA(key, "%.200s\\shell\\open\\command", type);
    return ClassValue(key, command, room);
}
/* Fills in the command: %1 and %L are the file, %* the parameters; a command
   without %1 gets the file at its end. */
static void Fill(char *out, int room, const char *command, const char *file, const char *parameters)
{
    int n = 0, named = 0;
    for (const char *p = command; *p && n < room - 1; ++p)
    {
        const char *insert = NULL;
        if (*p == '%' && (p[1] == '1' || p[1] == 'L' || p[1] == 'l')) { insert = file; named = 1; }
        else if (*p == '%' && p[1] == '*') insert = parameters ? parameters : "";
        else if (*p == '%' && p[1] >= '2' && p[1] <= '9') insert = "";
        if (!insert)
        {
            out[n++] = *p;
            continue;
        }
        ++p;
        while (*insert && n < room - 1) out[n++] = *insert++;
    }
    out[n] = 0;
    if (!named && n + lstrlenA(file) + 4 < room) wsprintfA(out + n, " \"%s\"", file);
}
UINT_PTR ShellHelpOpen(LPCSTR file, LPCSTR parameters, LPCSTR directory, int show, HANDLE *process)
{
    char command[1024], line[2048], full[MAX_PATH], *name = NULL;
    const char *extension, *colon;
    DWORD attributes;
    if (process) *process = NULL;
    if (!file || !file[0]) return 2;
    /* An address: the part before the colon is the class ("http"). */
    for (colon = file; *colon && *colon != ':' && *colon != '\\' && *colon != '/' && *colon != '.'; ++colon) {}
    if (*colon == ':' && colon - file > 1 && colon - file < 32)
    {
        char scheme[32];
        lstrcpynA(scheme, file, (int)(colon - file) + 1);
        if (!OpenCommand(scheme, command, sizeof command)) return 31;
        Fill(line, sizeof line, command, file, parameters);
        return Run(line, directory, show, process);
    }
    /* A program named without a folder is looked for as CreateProcess does. */
    Copy(full, file, MAX_PATH);
    attributes = GetFileAttributesA(full);
    if (attributes == 0xffffffffu)
    {
        if (!SearchPathA(NULL, file, ".exe", MAX_PATH, full, &name)) return 2;
        attributes = GetFileAttributesA(full);
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        wsprintfA(line, "explorer.exe \"%.1000s\"", full);
        return Run(line, directory, show, process);
    }
    extension = Extension(full);
    if (!lstrcmpiA(extension, ".lnk"))
    {
        ShellHelpLink link;
        if (!ShellHelpReadLink(full, &link)) return 2;
        if (link.show == SW_SHOWNORMAL) link.show = show;
        return ShellHelpOpen(link.target, link.arguments[0] ? link.arguments : parameters,
                             link.directory[0] ? link.directory : directory, link.show, process);
    }
    if (!lstrcmpiA(extension, ".exe") || !lstrcmpiA(extension, ".com") || !lstrcmpiA(extension, ".bat") ||
        !lstrcmpiA(extension, ".pif") || !lstrcmpiA(extension, ".scr"))
    {
        if (lstrlenA(full) + (parameters ? lstrlenA(parameters) : 0) + 8 > (int)sizeof line) return 8;
        wsprintfA(line, parameters && parameters[0] ? "\"%s\" %s" : "\"%s\"", full, parameters);
        return Run(line, directory, show, process);
    }
    if (!extension[0] || !OpenCommand(extension, command, sizeof command)) return 31;
    Fill(line, sizeof line, command, full, parameters);
    return Run(line, directory, show, process);
}

/* --- icons ---------------------------------------------------------------------- */
/* The resource data of an icon group and of one icon picture, wherever the
   file keeps them. */
typedef struct
{
    const BYTE *file;
    DWORD size;
    int kind; /* 1 PE, 2 NE, 3 ICO */
    /* PE */
    const BYTE *resources, *sections;
    DWORD resource_rva;
    unsigned section_count;
    /* NE */
    const BYTE *table;
    unsigned shift;
} IconFile;

static const BYTE *PeAt(const IconFile *f, DWORD rva, DWORD bytes)
{
    for (unsigned i = 0; i < f->section_count; ++i)
    {
        const BYTE *s = f->sections + 40 * i;
        const DWORD virtual_size = Long(s + 8), address = Long(s + 12), raw_size = Long(s + 16), raw = Long(s + 20);
        const DWORD extent = virtual_size > raw_size ? virtual_size : raw_size;
        if (rva >= address && rva - address < extent)
        {
            const DWORD offset = raw + (rva - address);
            return offset <= f->size && bytes <= f->size - offset && rva - address + bytes <= raw_size ? f->file + offset : NULL;
        }
    }
    return NULL;
}
/* An entry of a resource directory by position (index >= 0) or by id. */
static const BYTE *PeEntry(const IconFile *f, const BYTE *directory, int index, int id)
{
    const BYTE *end = f->file + f->size;
    unsigned count;
    if (!directory || directory + 16 > end) return NULL;
    count = Word(directory + 12) + Word(directory + 14);
    if (directory + 16 + 8 * count > end) return NULL;
    for (unsigned i = 0; i < count; ++i)
    {
        const BYTE *entry = directory + 16 + 8 * i;
        if (index >= 0 ? (unsigned)index == i : Long(entry) == (DWORD)id) return entry;
    }
    return NULL;
}
static const BYTE *PeDirectory(const IconFile *f, const BYTE *entry)
{
    const DWORD offset = entry ? Long(entry + 4) : 0;
    if (!(offset & 0x80000000u)) return NULL;
    return f->resources + (offset & 0x7fffffffu) < f->file + f->size ? f->resources + (offset & 0x7fffffffu) : NULL;
}
/* The data of resource (type, position or id): first language. */
static const BYTE *PeResource(const IconFile *f, int type, int index, int id, DWORD *bytes)
{
    const BYTE *names = PeDirectory(f, PeEntry(f, f->resources, -1, type));
    const BYTE *languages = PeDirectory(f, PeEntry(f, names, index, id));
    const BYTE *leaf = PeEntry(f, languages, 0, 0), *data;
    if (!leaf || (Long(leaf + 4) & 0x80000000u)) return NULL;
    data = f->resources + Long(leaf + 4);
    if (data + 16 > f->file + f->size) return NULL;
    *bytes = Long(data + 4);
    return PeAt(f, Long(data), *bytes);
}
static int PeCount(const IconFile *f, int type)
{
    const BYTE *names = PeDirectory(f, PeEntry(f, f->resources, -1, type));
    return names && names + 16 <= f->file + f->size ? (int)(Word(names + 12) + Word(names + 14)) : 0;
}
/* The NE resource table: per type a block of entries. */
static const BYTE *NeResource(const IconFile *f, unsigned type, int index, int id, DWORD *bytes, int *count)
{
    const BYTE *end = f->file + f->size, *at = f->table + 2;
    if (count) *count = 0;
    while (at + 8 <= end && Word(at))
    {
        const unsigned kind = Word(at), entries = Word(at + 2);
        at += 8;
        if (at + 12 * entries > end) return NULL;
        if (kind == (0x8000u | type))
        {
            if (count) *count = (int)entries;
            for (unsigned i = 0; i < entries; ++i)
            {
                const BYTE *entry = at + 12 * i;
                if (index >= 0 ? (unsigned)index == i : Word(entry + 6) == (0x8000u | (unsigned)id))
                {
                    const DWORD offset = (DWORD)Word(entry) << f->shift;
                    *bytes = (DWORD)Word(entry + 2) << f->shift;
                    return offset <= f->size && *bytes <= f->size - offset ? f->file + offset : NULL;
                }
            }
            return NULL;
        }
        at += 12 * entries;
    }
    return NULL;
}
static BOOL OpenIconFile(IconFile *f, LPCSTR path)
{
    memset(f, 0, sizeof *f);
    f->file = ReadWhole(path, &f->size);
    if (!f->file) return FALSE;
    if (f->size >= 6 && Word(f->file) == 0 && Word(f->file + 2) == 1 && Word(f->file + 4) &&
        6 + 16 * Word(f->file + 4) <= f->size)
    {
        f->kind = 3;
        return TRUE;
    }
    if (f->size >= 0x40 && Word(f->file) == 0x5a4d)
    {
        const DWORD header = Long(f->file + 0x3c);
        if (header < f->size && f->size - header >= 0x40 && Long(f->file + header) == 0x4550)
        {
            const BYTE *coff = f->file + header + 4, *optional = coff + 20;
            const unsigned magic = Word(optional), directories = magic == 0x20b ? 112 : 96;
            if ((magic == 0x10b || magic == 0x20b) && Word(coff + 16) >= directories + 24 &&
                optional + Word(coff + 16) + 40 * Word(coff + 2) <= f->file + f->size)
            {
                f->sections = optional + Word(coff + 16);
                f->section_count = Word(coff + 2);
                f->resource_rva = Long(optional + directories + 16);
                f->resources = f->resource_rva ? PeAt(f, f->resource_rva, 16) : NULL;
                if (f->resources)
                {
                    f->kind = 1;
                    return TRUE;
                }
            }
        }
        else if (header < f->size && f->size - header >= 0x40 && Word(f->file + header) == 0x454e)
        {
            const unsigned table = Word(f->file + header + 0x24), resident = Word(f->file + header + 0x26);
            if (table && table != resident && header + table + 2 <= f->size)
            {
                f->table = f->file + header + table;
                f->shift = Word(f->table);
                if (f->shift < 16)
                {
                    f->kind = 2;
                    return TRUE;
                }
            }
        }
    }
    Release((void *)f->file);
    f->file = NULL;
    return FALSE;
}
int ShellHelpIconCount(LPCSTR path)
{
    IconFile f;
    int count = 0;
    DWORD bytes;
    if (!OpenIconFile(&f, path)) return 0;
    if (f.kind == 3) count = 1;
    else if (f.kind == 1) count = PeCount(&f, 14);
    else NeResource(&f, 14, 0x7fffffff, 0, &bytes, &count);
    Release((void *)f.file);
    return count;
}
HICON ShellHelpIcon(LPCSTR path, int index, int width, int height)
{
    IconFile f;
    HICON icon = NULL;
    DWORD bytes = 0;
    const BYTE *group = NULL, *picture = NULL;
    if (!OpenIconFile(&f, path)) return NULL;
    if (f.kind == 3)
    {
        /* An ICO file: the directory has the place of each picture instead
           of a resource id. The best one: the smallest that is large enough,
           else the largest, and of those the one with the most colours. */
        const unsigned count = Word(f.file + 4);
        int best = -1, best_width = 0, best_bits = 0;
        for (unsigned i = 0; i < count && index <= 0; ++i)
        {
            const BYTE *entry = f.file + 6 + 16 * i;
            const int w = entry[0] ? entry[0] : 256, bits = (int)Word(entry + 6);
            const int better = best < 0 || (best_width < width ? w > best_width : (w >= width && w < best_width)) ||
                               (w == best_width && bits > best_bits);
            if (better) { best = (int)i; best_width = w; best_bits = bits; }
        }
        if (best >= 0)
        {
            const BYTE *entry = f.file + 6 + 16 * best;
            const DWORD offset = Long(entry + 12);
            bytes = Long(entry + 8);
            if (offset <= f.size && bytes <= f.size - offset) picture = f.file + offset;
        }
    }
    else
    {
        DWORD group_bytes = 0;
        group = f.kind == 1 ? PeResource(&f, 14, index >= 0 ? index : -1, -index, &group_bytes)
                            : NeResource(&f, 14, index >= 0 ? index : -1, -index, &group_bytes, NULL);
        if (group && group_bytes >= 6 + 14)
        {
            const int id = LookupIconIdFromDirectoryEx((LPBYTE)group, TRUE, width, height, LR_DEFAULTCOLOR);
            if (id > 0)
                picture = f.kind == 1 ? PeResource(&f, 3, -1, id, &bytes) : NeResource(&f, 3, -1, id, &bytes, NULL);
        }
    }
    if (picture && bytes) icon = CreateIconFromResourceEx((LPBYTE)picture, bytes, TRUE, 0x00030000, width, height, LR_DEFAULTCOLOR);
    Release((void *)f.file);
    return icon;
}
