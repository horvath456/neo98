// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_short_path.h"

// Original KERNEL32 path family.
namespace Win98PathFamily
{
using U = unsigned;
struct State
{
    U path, current_scratch, set_scratch, temp_scratch;
    U current, oem_current, task_directory;
    U task_flags, task_drive, drive_cache, dos_dirty;
    U process_flags, thread_flags;
};
inline bool Separator(U c) { return c == '\\' || c == '/'; }
inline bool SignedGreater(U a, U b) { return (a ^ 0x80000000u) > (b ^ 0x80000000u); }
// Flags at process+20h and thread+45h.
inline bool FileApiMode(State &s, U direction)
{
    if (!(s.process_flags & 0x40) || (s.thread_flags & 6)) return false;
    s.thread_flags |= direction ? 4 : 2;
    return true;
}
// Query the same process+20h bit used by path conversion.
inline unsigned AreFileApisANSI(const State &s) { return (s.process_flags & 0x40) == 0; }
inline void ClearMode(State &s) { s.thread_flags &= ~6u; }
template <class E> U UnsupportedWide(E &e) { e.Error(120); return 0; }
template <class E> U Length(E &e, U p) { U n = 0; while (e.Read8(p + n)) ++n; return n; }
// forward byte copy, including the NUL. Aliasing is intentional.
template <class E> void Copy(E &e, U to, U from)
{
    U c; do { c = e.Read8(from++); e.Write8(to++, c); } while (c);
}
// canonicalize slash and reject control bytes or the '<>|' table.
template <class E> bool ValidCharacters(E &e, U p)
{
    for (U c; (c = e.Read8(p)); ++p)
    {
        if (c < 32 || c == '<' || c == '>' || c == '|') return false;
        else if (c == '/') e.Write8(p, '\\');
    }
    return true;
}
// pointer to the end of a UNC server/share prefix, or zero.
template <class E> U UncRoot(E &e, U p)
{
    p += 2;
    while (e.Read8(p) != '\\') { if (!e.Read8(p)) return 0; ++p; }
    ++p;
    for (;;)
    {
        U c = e.Read8(p);
        if (Separator(c)) return p;
        if (!c) return e.Read8(p - 1) == '\\' ? 0 : p;
        ++p;
    }
}
// + : bytewise last-backslash search.
template <class E> U FilePart(E &e, U p)
{
    U result = p;
    for (U c; (c = e.Read8(p)); ++p) if (c == '\\') result = p + 1;
    return result;
}
// the original in-place dot/separator normalization.
// Return value counts excess parent traversals; ~0u means invalid characters.
template <class E> U Normalize(E &e, U path)
{
    if (!ValidCharacters(e, path)) return ~0u;
    U base = path, scan, write, excess = 0;
    bool normal = false;
    while (e.Read8(base) == '\\') ++base;
    scan = write = base;
    for (;;)
    {
        U c = e.Read8(scan);
        if (!c) return excess;
        ++scan;
        if (c == '.')
        {
            U parents = 0;
            while (e.Read8(scan) == '.') { ++parents; ++scan; }
            c = e.Read8(scan);
            if (parents && (!c || c == '\\'))
            {
                while (e.Read8(scan) == '\\') ++scan;
                if (normal)
                {
                    while (parents)
                    {
                        if (base >= write) { excess += parents; normal = false; break; }
                        write -= 2;
                        while (e.Read8(write) != '\\')
                        {
                            if (base >= write) { normal = false; --write; break; }
                            --write;
                        }
                        ++write;
                        --parents;
                    }
                }
                else excess += parents;
                if (!e.Read8(scan) && base != write) --write;
                Copy(e, write, scan);
                scan = write;
                continue;
            }
            if (!parents)
            {
                if (c == '\\') { ++scan; Copy(e, write, scan); scan = write; continue; }
                if (!c)
                {
                    e.Write8(scan - (base == write ? 1 : 2), 0);
                    return excess;
                }
            }
        }
        normal = true;
        while ((c = e.Read8(scan)) && c != '\\') ++scan;
        if (!c)
        {
            while (e.Read8(scan - 1) == '.') --scan;
            e.Write8(scan, 0);
            write = scan;
        }
        else
        {
            write = scan - 1;
            if (e.Read8(write) == '.') { Copy(e, write, scan); scan = write; }
            while (e.Read8(scan) == '\\') ++scan;
            write = scan;
        }
    }
}

template <class E> bool IsDirectory(E &e, U path)
{
    U attributes = e.Attributes(path);
    if (attributes == ~0u) return false;
    if (attributes & 0x10) return true;
    e.Error(267);
    return false;
}

template <class E> U CurrentDirectory(E &e, State &s, U capacity, U output)
{
    e.Lock(0);
    bool mode = FileApiMode(s, 1);
    if (mode) e.ConvertInput(output, 1);
    U path = s.current, error = 0;
    if (!(s.task_flags & 0x10))
    {
        U drive = (s.task_drive & 0x7f) + 1;
        e.Write8(s.current_scratch, (drive - 1 + 'A') & 255);
        e.Write8(s.current_scratch + 1, ':');
        if (!(s.task_drive & 0x80))
        {
            drive = s.drive_cache;
            if (drive == 255) drive = e.DosCurrentDrive() & 255;
            e.Write8(s.current_scratch + 1, ':');
            e.Write8(s.current_scratch, (drive + 'A') & 255);
            error = e.DosCurrentDirectory((drive + 1) & 65535, s.current_scratch + 3) & 65535;
            e.Write8(s.current_scratch + 2, '\\');
        }
        else if (s.thread_flags & 4) Copy(e, s.current_scratch + 2, s.task_directory);
        else e.ConvertString(s.current_scratch + 2, s.task_directory, 0);
        path = s.current_scratch;
    }
    else if (s.thread_flags & 4)
    {
        if (!s.oem_current)
        {
            s.oem_current = e.Duplicate(s.current);
            if (!s.oem_current) { e.Unlock(0); return 0; }
            e.ConvertString(s.oem_current, s.oem_current, 1);
        }
        path = s.oem_current;
    }
    U result;
    if (error) { e.Error(error); result = 0; }
    else
    {
        result = Length(e, path);
        if (capacity <= result) { ++result; e.Error(111); }
        else Copy(e, output, path);
        if (mode)
        {
            if (capacity > result) e.FreeInput(output, 1);
            ClearMode(s);
        }
    }
    e.Unlock(0);
    return result;
}

// The shared scratch is protected by the original recursive lock.
template <class E> U FullPath(E &e, State &s, U input, U capacity, U output, U part)
{
    U result = 0, path = input, root = 0;
    bool absolute = false;
    e.Lock(0);
    bool mode = FileApiMode(s, 1);
    if (mode) { path = e.ConvertInput(input, 0); output = e.ConvertInput(output, 1); }
    auto finish = [&]() -> U {
        if (mode) { e.FreeInput(path, 0); e.FreeInput(output, 1); ClearMode(s); }
        e.Unlock(0); return result;
    };
    if (!e.Read8(path)) { e.Error(161); return finish(); }
    if (Win98ShortPath::TooLong(e, path)) return finish();
    e.Write32(s.path, 0);
    if (e.Read8(path + 1) == ':')
    {
        U drive = e.Read8(path) & 0xdf;
        if (drive < 'A' || drive > 'Z') { e.Error(161); return finish(); }
        U rest = path + 2;
        if (Separator(e.Read8(rest)))
        {
            Copy(e, s.path, path); root = s.path + 2; absolute = true;
        }
        else
        {
            result = CurrentDirectory(e, s, 260, s.path);
            if (!result) { e.Error(16); return finish(); }
            if (e.Read8(s.path + 1) != ':' || (e.Read8(s.path) & 0xdf) != drive)
            {
                e.Write8(s.path, drive); e.Write8(s.path + 1, ':');
                e.Write8(s.path + 2, '\\'); e.Write8(s.path + 3, 0);
                result = 3;
                char name[4] = {'=', static_cast<char>(drive), ':', 0};
                U found = e.Environment(name);
                if (found) { Copy(e, s.path, found); result = Length(e, s.path); }
            }
            path = rest;
            absolute = false;
            goto append_current;
        }
    }
    else if (e.Read8(path) == '\\' && e.Read8(path + 1) == '\\')
    {
        Copy(e, s.path, path); root = UncRoot(e, s.path);
        if (!root)
        {
            if (e.Read8(s.path + 2) != '.' || e.Read8(s.path + 3) != '\\')
            { e.Error(161); return finish(); }
            root = s.path + 4;
        }
        absolute = true;
    }
    if (e.Read8(s.path) == '\\' && e.Read8(s.path + 1) == '\\' &&
        e.Read8(s.path + 2) == '.' && e.Read8(s.path + 3) == '\\')
    { root = s.path + 4; absolute = true; }
    if (!absolute)
    {
        result = CurrentDirectory(e, s, 260, s.path);
append_current:
        if (e.Read8(s.path + result - 1) != '\\')
        { e.Write8(s.path + result, '\\'); e.Write8(s.path + result + 1, 0); }
        if (e.Read8(s.path) == '\\' && e.Read8(s.path + 1) == '\\')
        {
            root = UncRoot(e, s.path);
            if (!root) { result = 0; e.Error(161); return finish(); }
        }
        else root = s.path + 2;
        if (Separator(e.Read8(path))) Copy(e, root, path);
        else Copy(e, s.path + Length(e, s.path), path);
    }
    if (Normalize(e, root) == ~0u) { result = 0; e.Error(161); return finish(); }
    result = Length(e, s.path);
    if (capacity <= result) { ++result; e.Error(111); return finish(); }
    Copy(e, output, s.path);
    if (part)
    {
        U value = FilePart(e, output);
        e.Write32(part, e.Read8(value) ? value : 0);
    }
    return finish();
}

// This preserves original frees before allocation, including
// stale state pointers on allocation failure. The provider must keep this safe.
template <class E> U SetCurrentDirectory(E &e, State &s, U input)
{
    U result = 0, path = input;
    e.Lock(0);
    bool mode = FileApiMode(s, 1);
    if (mode) path = e.ConvertInput(input, 0);
    auto finish = [&]() -> U { if (mode) { e.FreeInput(path, 0); ClearMode(s); } e.Unlock(0); return result; };
    U length = FullPath(e, s, path, 260, s.set_scratch, 0);
    if (!length || !IsDirectory(e, s.set_scratch)) return finish();
    if (length > 3 && e.Read8(s.set_scratch + length - 1) == '\\')
        e.Write8(s.set_scratch + --length, 0);
    e.Free(s.current);
    if (s.oem_current) e.Free(s.oem_current);
    U current = e.Allocate(length + 1, 8);
    if (!current) return finish();
    U oem = 0;
    if (s.thread_flags & 4)
    {
        oem = e.Allocate(length + 1, 8);
        if (!oem) { e.Free(current); return finish(); }
    }
    e.Lock(1); s.dos_dirty = 0;
    if (e.Read8(s.set_scratch + 1) == ':')
    {
        s.task_drive = (e.Read8(s.set_scratch) - 1) & 31;
        if (!(s.task_flags & 0x10)) e.DosSelectDrive(s.task_drive);
        if (e.Read8(s.set_scratch + 2))
        {
            if (!(s.task_flags & 0x10)) e.DosSetDirectory(s.set_scratch + 2);
            if (s.thread_flags & 4) Copy(e, s.task_directory, s.set_scratch + 2);
            else e.ConvertString(s.task_directory, s.set_scratch + 2, 1);
            s.task_drive |= 0x80;
        }
    }
    else
    {
        if (!(s.task_flags & 0x10)) e.DosSetDirectory(s.set_scratch);
        s.task_drive &= 0x7f;
    }
    if (s.thread_flags & 4)
    {
        Copy(e, oem, s.set_scratch); s.oem_current = oem;
        e.ConvertString(current, oem, 0); s.current = current;
    }
    else
    {
        Copy(e, current, s.set_scratch); s.current = current; s.oem_current = 0;
    }
    e.Unlock(1); result = 1;
    return finish();
}

// environment path lookup, full resolution and directory check.
template <class E> U EnvironmentDirectory(E &e, State &s, const char *name, U output, U capacity)
{
    U value = e.Environment(name);
    if (value)
    {
        U n = FullPath(e, s, value, capacity, output, 0);
        if (!n || n > capacity) return n;
        if (IsDirectory(e, output))
        {
            if (n != 3)
            {
                U end = output + Length(e, output);
                if (e.Read8(end - 1) == '\\') { e.Write8(end, 0); --n; }
            }
            return n;
        }
    }
    e.Error(161); return 0;
}
// On shortage the ORIGINAL SOURCE is cleared.
template <class E> U BoundedCopy(E &e, U source, U output, U capacity, bool &carry)
{
    U n = Length(e, source) + 1;
    if (n > capacity) { e.Write8(source, 0); e.Error(111); carry = true; }
    else { for (U i = 0; i != n; ++i) e.Write8(output + i, e.Read8(source + i)); carry = false; }
    return n - 1;
}
// TMP, then TEMP, then current directory.
template <class E> U TempPath(E &e, State &s, U capacity, U output)
{
    e.Lock(0);
    bool mode = FileApiMode(s, 0), copy = false;
    U result = EnvironmentDirectory(e, s, "TMP", s.temp_scratch, 260);
    if (!result) result = EnvironmentDirectory(e, s, "TEMP", s.temp_scratch, 260);
    if (result)
    {
        if (result < capacity) copy = true;
        else { ++result; if (e.Read8(s.temp_scratch + result - 2) != '\\') ++result; }
    }
    else
    {
        result = CurrentDirectory(e, s, 260, s.temp_scratch);
        if (!result) e.Error(result & 65535);
        else
        {
            ++result;
            if (capacity >= result) { --result; copy = true; }
            else if (e.Read8(s.temp_scratch + result - 2) != '\\') ++result;
        }
    }
    if (copy)
    {
        bool carry;
        result = BoundedCopy(e, s.temp_scratch, output, capacity, carry);
        if (!carry && e.Read8(output + result - 1) != '\\')
        {
            e.Write8(output + result, '\\'); result += 2;
            if (capacity >= result) { --result; e.Write8(output + result, 0); }
        }
    }
    if (mode)
    {
        if (result && !SignedGreater(result, capacity)) e.ConvertBytes(output, output, result, 1);
        ClearMode(s);
    }
    e.Unlock(0); return result;
}
// generated hex suffix, collision retry and file lifetime.
template <class E> U TempFileName(E &e, State &s, U directory, U prefix, U unique, U output)
{
    U path = directory, stem = prefix, result = 0;
    bool mode = FileApiMode(s, 1);
    if (mode) { path = e.ConvertInput(directory, 0); stem = e.ConvertInput(prefix, 0); }
    auto finish = [&]() -> U {
        if (mode)
        {
            e.FreeInput(path, 0);
            U value = e.FreeInput(stem, 0);
            if (value) e.FreeInput(output, 1);
            ClearMode(s);
        }
        return result;
    };
    U n = FullPath(e, s, path, 260, output, 0);
    if (!n) return finish();
    bool slash = n != 3 && e.Read8(output + n - 1) == '\\';
    if (slash) e.Write8(output + n - 1, 0);
    bool is_directory = IsDirectory(e, output);
    if (slash) { e.Write8(output + n - 1, '\\'); e.Write8(output + n, 0); }
    if (!is_directory) { e.Error(3); return finish(); }
    if (e.Read8(output + n - 1) != '\\') { e.Write8(output + n++, '\\'); e.Write8(output + n, 0); }
    U suffix = output + n;
    for (U i = 0; i != 3; ++i) { U c = e.Read8(stem + i); if (!c) break; e.Write8(suffix++, c); }
    U number = unique;
    if (!number)
    {
        U cx, dx; e.DosClock(cx, dx);
        number = (((cx & 65535) << 16 | (dx & 65535)) >> 4) & 65535;
        if (!number) ++number;
    }
    bool leading = true;
    for (;;)
    {
        U write = suffix;
        for (U shift = 16; shift;)
        {
            shift -= 4; U nibble = (number >> shift) & 15;
            if (nibble) leading = false;
            if (!leading) e.Write8(write++, "0123456789ABCDEF"[nibble]);
        }
        e.Write8(write++, '.'); e.Write8(write++, 'T'); e.Write8(write++, 'M'); e.Write8(write++, 'P'); e.Write8(write, 0);
        if (unique) { result = number; break; }
        U handle = e.CreateFile(output, 0x40000000, 0, 0, 1, 0x80, 0);
        if (handle != ~0u) { if (e.CloseFile(handle)) result = number; break; }
        U error = e.LastError();
        if (error != 183 && error != 80) break;
        ++number;
    }
    return finish();
}
} // namespace Win98PathFamily
