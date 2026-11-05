// SPDX-License-Identifier: GPL-2.0-or-later
// WININIT.EXE of Windows 98, the step between IO.SYS and Windows. Setup
// programs that must replace a file Windows holds open (DXSETUP replacing
// DDRAW.DLL, DSOUND.DLL and the other DirectX 6.1 files) write
// "destination=source" lines into the [Rename] section of
// C:\WINDOWS\WININIT.INI. On the next start, before Windows runs, the real-mode
// WININIT.EXE carries them out and renames the file to WININIT.BAK. NEO98 has
// no real-mode phase; the host calls Run when the machine starts, which is the
// same point in the boot: before the first Windows process exists.
// The rules below are those of WININIT.EXE 4.10.2222 (the ISO installation),
// read from its code; offsets are file offsets behind the 1856-byte MZ header.
// 0C29..0D90 one [Rename] line per round. "NUL=path" deletes a file,
// "DIRNUL=path" deletes a directory with everything below it.
// Any other line is "destination=source": a line whose source
// does not exist is skipped (0CF3, find-first with 17h), the
// destination is deleted, then the source is renamed to it
// (0220, C rename). A failed step is reported and the next
// line follows.
// 00BA deleting a file: a missing name or a directory counts as done;
// 006E otherwise unlink, and after a failure other than "not found"
// clear the attributes and unlink again.
// 00F2 DIRNUL: "*.*" with 17h, "." and ".." skipped, directories
// recursively, files as above, then rmdir.
// Afterwards the file is renamed to ".bak". The sections
// [CombineVxDs] and [SetupOptions] rebuild VMM32.VXD; NEO98 has no VMM32.VXD
// to rebuild, so they are left alone and only logged.
#pragma once
#include "files.h"
#include <stdio.h>
#include <string.h>

namespace Win98Wininit
{
using Log = void (*)(const char *);

inline bool SameText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return false;
        if (!x)
            return true;
    }
}

inline void Trim(char *text)
{
    unsigned n = strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r'))
        text[--n] = 0;
    unsigned start = 0;
    while (text[start] == ' ' || text[start] == '\t')
        ++start;
    if (start)
        memmove(text, text + start, n - start + 1);
}

struct Result
{
    unsigned lines = 0, renamed = 0, deleted = 0, failed = 0, skipped = 0;
    bool found = false;
};

class Runner
{
  public:
    Runner(Files::Store &store, Log log) : store(store), session(store), log(log) {}

    // 00BA/006E. True when the name is gone or never was a file.
    bool DeleteFile(const char *path)
    {
        unsigned node = 0;
        if (store.Lookup(path, node))
            return true;
        const auto *record = store.Get(node);
        if (!record || (record->attributes & 16))
            return true;
        if (!store.Remove(path, false))
            return true;
        store.Attributes(path, 0);
        return !store.Remove(path, false);
    }

    // 00F2. True when the name is gone or never was a directory.
    bool DeleteDirectory(const char *path, unsigned depth = 0)
    {
        unsigned node = 0;
        if (store.Lookup(path, node))
            return true;
        const auto *record = store.Get(node);
        if (!record || !(record->attributes & 16))
            return true;
        if (depth > 32)
            return false;
        bool ok = true;
        // Collect the names first: every removal changes the enumeration.
        for (;;)
        {
            char child[260] = {};
            bool directory = false;
            unsigned entry = 0;
            for (unsigned index = 0; !store.Enumerate(node, index, entry); ++index)
            {
                const auto *item = store.Get(entry);
                if (!item || item->tombstone || !strcmp(item->name, ".") || !strcmp(item->name, ".."))
                    continue;
                if (strlen(path) + strlen(item->name) + 2 > sizeof child)
                    continue;
                snprintf(child, sizeof child, "%s\\%s", path, item->name);
                directory = item->attributes & 16;
                if (!Tried(child))
                    break;
                child[0] = 0;
            }
            if (!child[0])
                break;
            Remember(child);
            if (!(directory ? DeleteDirectory(child, depth + 1) : DeleteFile(child)))
                ok = false;
        }
        if (store.Remove(path, true))
            return false;
        return ok;
    }

    bool Exists(const char *path)
    {
        unsigned node = 0;
        return !store.Lookup(path, node);
    }

    // One [Rename] line, 0C29..0D90.
    void Line(const char *key, const char *value, Result &result)
    {
        ++result.lines;
        char target[260], source[260];
        const bool nul = SameText(key, "NUL"), dirnul = SameText(key, "DIRNUL");
        if (nul || dirnul)
        {
            if (session.FullPath(value, target))
            {
                Report("WININIT cannot resolve", value, nullptr);
                ++result.failed;
                return;
            }
            tried_count = 0;
            if (dirnul ? DeleteDirectory(target) : DeleteFile(target))
            {
                ++result.deleted;
                Report(dirnul ? "WININIT DIRNUL" : "WININIT NUL", target, nullptr);
            }
            else
            {
                ++result.failed;
                Report("WININIT cannot delete", target, nullptr);
            }
            return;
        }
        if (session.FullPath(value, source) || !Exists(source))
        {
            ++result.skipped;
            Report("WININIT source missing", value, key);
            return;
        }
        if (session.FullPath(key, target))
        {
            ++result.failed;
            Report("WININIT cannot resolve", key, nullptr);
            return;
        }
        if (!DeleteFile(target))
        {
            ++result.failed;
            Report("WININIT cannot delete", target, nullptr);
            return;
        }
        if (const unsigned error = store.Rename(source, target))
        {
            ++result.failed;
            char detail[32];
            snprintf(detail, sizeof detail, "error=%u", error);
            Report("WININIT cannot rename", source, detail);
            return;
        }
        ++result.renamed;
        Report("WININIT rename", source, target);
    }

    Result Run(const char *ini)
    {
        Result result;
        unsigned handle = 0, status = 0, size = 0, done = 0;
        if (session.Open(ini, 1, 1, 3, 0, handle, status))
            return result;
        result.found = true;
        char *text = nullptr;
        if (!session.Size(handle, size) && size < 4u * 1024 * 1024)
        {
            text = new char[size + 1];
            if (session.Read(handle, text, size, done) || done != size)
            {
                delete[] text;
                text = nullptr;
            }
            else
                text[size] = 0;
        }
        session.Close(handle);
        if (!text)
        {
            Report("WININIT cannot read", ini, nullptr);
            return result;
        }
        bool rename = false;
        for (char *line = text; line && *line;)
        {
            char *end = strchr(line, '\n');
            if (end)
                *end = 0;
            char entry[600];
            snprintf(entry, sizeof entry, "%s", line);
            line = end ? end + 1 : nullptr;
            Trim(entry);
            if (!entry[0] || entry[0] == ';')
                continue;
            if (entry[0] == '[')
            {
                char *close = strchr(entry, ']');
                if (close)
                    *close = 0;
                Trim(entry + 1);
                rename = SameText(entry + 1, "Rename");
                if (!rename)
                    Report("WININIT section left alone", entry + 1, nullptr);
                continue;
            }
            if (!rename)
                continue;
            char *equal = strchr(entry, '=');
            if (!equal)
                continue;
            *equal = 0;
            char *value = equal + 1;
            Trim(entry);
            Trim(value);
            if (!entry[0] || !value[0])
                continue;
            Line(entry, value, result);
        }
        delete[] text;
        // The ".bak" of 8E9C: the file is done and does not run again.
        char backup[260];
        snprintf(backup, sizeof backup, "%s", ini);
        if (char *dot = strrchr(backup, '.'))
            strcpy(dot, ".BAK");
        DeleteFile(backup);
        if (store.Rename(ini, backup))
        {
            Report("WININIT cannot rename", ini, backup);
            DeleteFile(ini);
        }
        store.Flush();
        char summary[200];
        snprintf(summary, sizeof summary, "lines=%u renamed=%u deleted=%u skipped=%u failed=%u",
                 result.lines, result.renamed, result.deleted, result.skipped, result.failed);
        Report("WININIT done", summary, nullptr);
        return result;
    }

  private:
    Files::Store &store;
    Files::Session session;
    Log log;
    // Names DeleteDirectory already tried, so an entry that cannot go does not
    // keep the enumeration in a loop.
    char tried[64][260] = {};
    unsigned tried_count = 0;
    bool Tried(const char *path) const
    {
        for (unsigned i = 0; i < tried_count; ++i)
            if (SameText(tried[i], path))
                return true;
        return false;
    }
    void Remember(const char *path)
    {
        if (tried_count < 64)
            snprintf(tried[tried_count++], sizeof tried[0], "%s", path);
    }
    void Report(const char *what, const char *first, const char *second)
    {
        if (!log)
            return;
        char line[700];
        if (second)
            snprintf(line, sizeof line, "%s %s -> %s", what, first, second);
        else
            snprintf(line, sizeof line, "%s %s", what, first);
        log(line);
    }
};

// The start of the machine: C:\WINDOWS\WININIT.INI, if there is one.
inline Result Run(Files::Store &store, Log log)
{
    auto *runner = new Runner(store, log);
    const Result result = runner->Run("C:\\WINDOWS\\WININIT.INI");
    delete runner;
    return result;
}
} // namespace Win98Wininit
