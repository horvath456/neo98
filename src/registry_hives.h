// SPDX-License-Identifier: GPL-2.0-or-later
// The registry a machine starts with, read from its own hives.
// Win98 reads C:\WINDOWS\SYSTEM.DAT and USER.DAT when it starts. This file
// does what tools/import_registry.py does at build time for the generated
// image, at run time and from the hives of the drive the machine got: it
// decodes both hives (src/win98_creg.h), imports every subtree,
// adds the keys of the port's LAN card and display adapter
// (tools/network_setup.py, tools/display_setup.py) and hands every key and
// value to the store in the same order. tools/test_registry_hives_native.py
// compares the result with the Python import, key by key, and the decoder
// with the Python decoder over the whole hives.
// Nothing here knows a file system: the caller passes the bytes of the two
// hives and of NETAMD.INF and NETCLI.INF (C:\WINDOWS\INF). Without the INF
// files, or on a hive without the keys the network additions copy from, the
// network additions are left out as a whole.
#pragma once
#include "neo98_hardware.h"
#include "win98_creg.h"
#include <new>
#include <string.h>
namespace Registry
{
namespace HiveImport
{
using U = unsigned;
constexpr U None = ~0u;
// Memory the store keeps for as long as it lives: names and payloads.
struct Arena
{
    struct Chunk
    {
        Chunk *next;
        U used, size;
    };
    Chunk *chunks = nullptr;
    bool failed = false;
    ~Arena()
    {
        while (chunks)
        {
            Chunk *next = chunks->next;
            delete[] (unsigned char *)chunks;
            chunks = next;
        }
    }
    unsigned char *Take(U size)
    {
        if (!chunks || chunks->size - chunks->used < size)
        {
            const U room = size > 0x40000 ? size : 0x40000;
            auto *raw = new (std::nothrow) unsigned char[sizeof(Chunk) + room];
            if (!raw)
            {
                failed = true;
                return nullptr;
            }
            auto *chunk = (Chunk *)raw;
            chunk->next = chunks;
            chunk->used = 0;
            chunk->size = room;
            chunks = chunk;
        }
        unsigned char *at = (unsigned char *)(chunks + 1) + chunks->used;
        chunks->used += size;
        return at;
    }
};
struct Str
{
    const char *p = "";
    U n = 0;
};
inline Str Text(const char *z) { return {z, (U)strlen(z)}; }
inline U Fold(U c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
// Python's str.lower for text decoded as cp1252.
inline U Lower1252(U c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c >= 0xc0 && c <= 0xde && c != 0xd7)
        return c + 32;
    if (c == 0x8a || c == 0x8c || c == 0x8e)
        return c + 16;
    if (c == 0x9f)
        return 0xff;
    return c;
}
inline bool Same(Str a, Str b) { return a.n == b.n && !memcmp(a.p, b.p, a.n); }
inline bool SameFolded(Str a, Str b)
{
    if (a.n != b.n)
        return false;
    for (U i = 0; i < a.n; ++i)
        if (Fold((unsigned char)a.p[i]) != Fold((unsigned char)b.p[i]))
            return false;
    return true;
}
inline bool StartsFolded(Str text, Str prefix)
{
    return text.n >= prefix.n && SameFolded({text.p, prefix.n}, prefix);
}
// Plain growing array of trivially copyable items.
template <class T> struct Vec
{
    T *data = nullptr;
    U count = 0, capacity = 0;
    bool failed = false;
    Vec() = default;
    Vec(const Vec &) = delete;
    Vec &operator=(const Vec &) = delete;
    ~Vec() { delete[] data; }
    T &operator[](U i) { return data[i]; }
    const T &operator[](U i) const { return data[i]; }
    U Push(const T &item)
    {
        if (count == capacity)
        {
            const U wider = capacity ? capacity * 2 : 64;
            T *more = new (std::nothrow) T[wider];
            if (!more)
            {
                failed = true;
                return None;
            }
            for (U i = 0; i < count; ++i)
                more[i] = data[i];
            delete[] data;
            data = more;
            capacity = wider;
        }
        data[count] = item;
        return count++;
    }
};
struct Builder
{
    Arena &arena;
    explicit Builder(Arena &a) : arena(a) {}
    Str Copy(Str text)
    {
        auto *at = (char *)arena.Take(text.n + 1);
        if (!at)
            return {};
        memcpy(at, text.p, text.n);
        at[text.n] = 0;
        return {at, text.n};
    }
    Str Join(Str a, Str b, Str c = {})
    {
        auto *at = (char *)arena.Take(a.n + b.n + c.n + 1);
        if (!at)
            return {};
        memcpy(at, a.p, a.n);
        memcpy(at + a.n, b.p, b.n);
        memcpy(at + a.n + b.n, c.p, c.n);
        at[a.n + b.n + c.n] = 0;
        return {at, a.n + b.n + c.n};
    }
    Str Lowered(Str text)
    {
        Str copy = Copy(text);
        for (U i = 0; i < copy.n; ++i)
            ((char *)copy.p)[i] = (char)Lower1252((unsigned char)copy.p[i]);
        return copy;
    }
};

// One decoded hive: the keys in the order of their stored links, each with
// its values, like the `tree` of tools/import_registry.py.
struct Tree
{
    struct Key
    {
        U parent = None, first_child = None, last_child = None, next = None;
        Str name;
        U first_value = 0, values = 0;
        U selected = None; // index in the selection, import_registry's index_of
    };
    struct Value
    {
        Str name;
        U type = 0;
        const unsigned char *data = nullptr;
        U size = 0;
    };
    Vec<Key> keys;
    Vec<Value> values;
    U first_root = None, last_root = None;
    U Child(U parent, Str name) const
    {
        for (U i = parent == None ? first_root : keys[parent].first_child; i != None; i = keys[i].next)
            if (SameFolded(keys[i].name, name))
                return i;
        return None;
    }
    U Find(Str path) const
    {
        U at = None;
        for (U from = 0; from <= path.n;)
        {
            U to = from;
            while (to < path.n && path.p[to] != '\\')
                ++to;
            at = Child(at, {path.p + from, to - from});
            if (at == None)
                return None;
            from = to + 1;
        }
        return at;
    }
    bool Below(U key, U ancestor) const
    {
        for (U i = keys[key].parent; i != None; i = keys[i].parent)
            if (i == ancestor)
                return true;
        return false;
    }
    const Value *ValueOf(U key, Str name) const
    {
        for (U i = 0; i < keys[key].values; ++i)
            if (Same(values[keys[key].first_value + i].name, name))
                return &values[keys[key].first_value + i];
        return nullptr;
    }
    Str Path(U key, Builder &builder) const
    {
        U parts[64], count = 0, length = 0;
        for (U i = key; i != None && count < 64; i = keys[i].parent)
        {
            parts[count++] = i;
            length += keys[i].name.n + 1;
        }
        auto *at = (char *)builder.arena.Take(length + 1);
        if (!at)
            return {};
        U end = 0;
        while (count)
        {
            const Str part = keys[parts[--count]].name;
            if (end)
                at[end++] = '\\';
            memcpy(at + end, part.p, part.n);
            end += part.n;
        }
        at[end] = 0;
        return {at, end};
    }
};
struct TreeSink final : Win98Creg::Sink
{
    Tree &tree;
    explicit TreeSink(Tree &t) : tree(t) {}
    U Key(U parent, const char *name, U length) override
    {
        Tree::Key key;
        key.parent = parent;
        key.name = {name, length};
        key.first_value = tree.values.count;
        const U index = tree.keys.Push(key);
        if (index == None)
            return None;
        U &first = parent == None ? tree.first_root : tree.keys[parent].first_child;
        U &last = parent == None ? tree.last_root : tree.keys[parent].last_child;
        if (first == None)
            first = index;
        else
            tree.keys[last].next = index;
        last = index;
        return index;
    }
    bool Value(U key, const char *name, U name_length, U type, const unsigned char *data, U size) override
    {
        if (key == None)
            return true; // the hive root has no place in the selection
        // The values of a key follow it at once, so they are one run.
        if (key + 1 != tree.keys.count)
            return false;
        Tree::Value value;
        value.name = {name, name_length};
        value.type = type;
        value.data = data;
        value.size = size;
        if (tree.values.Push(value) == None)
            return false;
        ++tree.keys[key].values;
        return true;
    }
};

// SYSTEM.DAT belongs to HKLM, USER.DAT to HKU. The host imports every
// subtree; only the generated Pi image retains the size-limited selection.
constexpr U Hkcr = 0, Hkcu = 1, Hklm = 2, Hku = 3, Hkcc = 5;

// What the store receives: the keys, every parent ahead of its children.
struct Chosen_Key
{
    bool anchor = false, removed = false;
    U root = 0, parent = None;
    Str path; // of an anchor, below its root
    Str name;
    U first = None, last = None; // its values
};
struct Chosen_Value
{
    Str name;
    U type = 0;
    const unsigned char *data = nullptr;
    U size = 0;
    U next = None;
};
// A key the additions made that no hive holds.
struct Made
{
    U hive;
    Str path;
    U selected;
};
// The keys and values the setup of the port adds, grouped by key in the order
// they were first named: tools/network_setup.py `grouped`.
struct Group
{
    U hive = 0;
    Str path;
    U first = None, last = None;
    U depth = 0;
};

// INF files, as far as tools/network_setup.py reads them.
struct Inf
{
    struct Line
    {
        Str text;
        U next = None;
    };
    struct Section
    {
        Str name; // lowered
        U first = None, last = None;
    };
    struct Entry
    {
        Str key, value;
    };
    Vec<Line> lines;
    Vec<Section> sections;
    Vec<Entry> strings;
    static bool Space(U c)
    {
        return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f) || c == 0xa0;
    }
    static Str Strip(Str text)
    {
        while (text.n && Space((unsigned char)text.p[0]))
        {
            ++text.p;
            --text.n;
        }
        while (text.n && Space((unsigned char)text.p[text.n - 1]))
            --text.n;
        return text;
    }
    static Str StripQuotes(Str text)
    {
        while (text.n && text.p[0] == '"')
        {
            ++text.p;
            --text.n;
        }
        while (text.n && text.p[text.n - 1] == '"')
            --text.n;
        return text;
    }
    // text.partition(c): the part before the first c, and the rest behind it.
    static Str Before(Str text, char c, Str &rest)
    {
        for (U i = 0; i < text.n; ++i)
            if (text.p[i] == c)
            {
                rest = {text.p + i + 1, text.n - i - 1};
                return {text.p, i};
            }
        rest = {text.p + text.n, 0};
        return text;
    }
    U Find(Str lowered) const
    {
        for (U i = 0; i < sections.count; ++i)
            if (Same(sections[i].name, lowered))
                return i;
        return None;
    }
    U FindFolded(Builder &builder, Str name) const { return Find(builder.Lowered(name)); }
    const Str *String(Str lowered) const
    {
        for (U i = 0; i < strings.count; ++i)
            if (Same(strings[i].key, lowered))
                return &strings[i].value;
        return nullptr;
    }
    bool Parse(const unsigned char *data, U size, Builder &builder)
    {
        U section = None;
        for (U at = 0; at < size;)
        {
            // str.splitlines of the cp1252 text.
            U end = at;
            while (end < size && data[end] != '\n' && data[end] != '\r' && data[end] != 0x0b &&
                   data[end] != 0x0c && !(data[end] >= 0x1c && data[end] <= 0x1e))
                ++end;
            Str raw{(const char *)data + at, end - at};
            at = end < size && data[end] == '\r' && end + 1 < size && data[end + 1] == '\n' ? end + 2 : end + 1;
            Str leading = raw;
            while (leading.n && Space((unsigned char)leading.p[0]))
            {
                ++leading.p;
                --leading.n;
            }
            Str line;
            if (leading.n && leading.p[0] == '"')
                line = Strip(raw);
            else
            {
                Str rest;
                line = Strip(Before(raw, ';', rest));
            }
            if (!line.n)
                continue;
            if (line.n >= 3 && line.p[0] == '[' && line.p[line.n - 1] == ']')
            {
                const Str name = builder.Lowered(Strip({line.p + 1, line.n - 2}));
                section = Find(name);
                if (section == None)
                {
                    Section fresh;
                    fresh.name = name;
                    section = sections.Push(fresh);
                    if (section == None)
                        return false;
                }
                continue;
            }
            if (section == None)
                continue;
            Line entry;
            entry.text = line;
            const U index = lines.Push(entry);
            if (index == None)
                return false;
            if (sections[section].first == None)
                sections[section].first = index;
            else
                lines[sections[section].last].next = index;
            sections[section].last = index;
        }
        const U table = Find(Text("strings"));
        if (table != None)
            for (U i = sections[table].first; i != None; i = lines[i].next)
            {
                Str value;
                const Str key = builder.Lowered(Strip(Before(lines[i].text, '=', value)));
                value = StripQuotes(Strip(value));
                bool replaced = false;
                for (U k = 0; k < strings.count && !replaced; ++k)
                    if (Same(strings[k].key, key))
                    {
                        strings[k].value = value;
                        replaced = true;
                    }
                if (!replaced && strings.Push({key, value}) == None)
                    return false;
            }
        return !builder.arena.failed;
    }
    // re.sub(r'%([^%]+)%',...): %10% and %11% are the Windows directories,
    // any other token a line of [Strings]; an unknown token stays.
    Str Expand(Str value, Builder &builder) const
    {
        bool any = false;
        for (U i = 0; i < value.n; ++i)
            any = any || value.p[i] == '%';
        if (!any)
            return value;
        Vec<char> out;
        const auto append = [&](Str text) {
            for (U i = 0; i < text.n; ++i)
                out.Push(text.p[i]);
        };
        for (U i = 0; i < value.n;)
        {
            if (value.p[i] == '%')
            {
                U end = i + 1;
                while (end < value.n && value.p[end] != '%')
                    ++end;
                if (end < value.n && end > i + 1)
                {
                    const Str token{value.p + i + 1, end - i - 1};
                    if (Same(token, Text("10")))
                        append(Text("C:\\WINDOWS"));
                    else if (Same(token, Text("11")))
                        append(Text("C:\\WINDOWS\\SYSTEM"));
                    else if (const Str *known = String(builder.Lowered(token)))
                        append(*known);
                    else
                        append({value.p + i, end + 1 - i});
                    i = end + 1;
                    continue;
                }
            }
            out.Push(value.p[i++]);
        }
        return builder.Copy({out.data ? out.data : "", out.count});
    }
    // The comma separated fields of a line; quotes group and are dropped.
    static bool Fields(Str line, Vec<Str> &out, Builder &builder)
    {
        Vec<char> current;
        bool quoted = false;
        const auto flush = [&]() {
            const Str field = Strip({current.data ? current.data : "", current.count});
            out.Push(builder.Copy(field));
            current.count = 0;
        };
        for (U i = 0; i < line.n; ++i)
        {
            const char c = line.p[i];
            if (c == '"')
                quoted = !quoted;
            else if (c == ',' && !quoted)
                flush();
            else
                current.Push(c);
        }
        flush();
        return !out.failed && !current.failed;
    }
};
// One value an AddReg section writes; a later line for the same key and name
// replaces the payload and keeps the place (a Python dict).
struct Added
{
    Str path, name;
    U type = 0;
    Str payload;
};
inline bool Number(Str text, U base, U &value)
{
    text = Inf::Strip(text);
    value = 0;
    if (text.n > 2 && text.p[0] == '0' && (text.p[1] == 'x' || text.p[1] == 'X'))
    {
        base = 16;
        text = {text.p + 2, text.n - 2};
    }
    else if (!base)
        base = 10;
    if (!text.n)
        return false;
    for (U i = 0; i < text.n; ++i)
    {
        const U c = Fold((unsigned char)text.p[i]);
        U digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return false;
        if (digit >= base)
            return false;
        value = value * base + digit;
    }
    return true;
}

class Import
{
  public:
    struct Input
    {
        const unsigned char *system = nullptr, *user = nullptr, *netamd = nullptr, *netcli = nullptr;
        U system_size = 0, user_size = 0, netamd_size = 0, netcli_size = 0;
    };
    Arena &arena;
    Builder builder;
    Tree trees[2];
    Vec<Chosen_Key> keys;
    Vec<Chosen_Value> values;
    Vec<Made> made;
    Vec<Group> groups;
    Vec<Chosen_Value> group_values;
    U error = 0;
    bool network = false; // the network additions were made
    explicit Import(Arena &a) : arena(a), builder(a) {}

    // Decodes both complete hives. Zero, or the error of
    // src/win98_creg.h (1009 for a file that is no hive), or 8.
    U Run(const Input &input)
    {
        TreeSink system(trees[0]), user(trees[1]);
        U sink_error = 0;
        if ((error = Win98Creg::Read(input.system, input.system_size, system, sink_error)))
            return error;
        if ((error = Win98Creg::Read(input.user, input.user_size, user, sink_error)))
            return error;
        Select();
        const U before = groups.count;
        if (input.netamd && input.netcli)
            network = Network(input);
        if (!network)
        {
            groups.count = before;
            group_values.count = 0;
        }
        Display();
        Hardware();
        Merge();
        Remove(0, Text("System\\CurrentControlSet\\Services\\Class\\NetClient\\0000\\Ndi\\Default"));
        HardwareRemovals();
        if (arena.failed || keys.failed || values.failed || made.failed || groups.failed || group_values.failed ||
            trees[0].keys.failed || trees[1].keys.failed || trees[0].values.failed || trees[1].values.failed)
            error = 8;
        return error;
    }

  private:
    void AddValue(U key, Str name, U type, const unsigned char *data, U size)
    {
        Chosen_Value value;
        value.name = name;
        value.type = type;
        value.data = data;
        value.size = size;
        const U index = values.Push(value);
        if (index == None)
            return;
        if (keys[key].first == None)
            keys[key].first = index;
        else
            values[keys[key].last].next = index;
        keys[key].last = index;
    }
    void Select()
    {
        for (U hive = 0; hive < 2; ++hive)
        {
            Tree &tree = trees[hive];
            for (U from = 0; from < tree.keys.count; ++from)
            {
                const auto &source = tree.keys[from];
                Chosen_Key key;
                key.name = source.name;
                if (source.parent == None)
                {
                    key.anchor = true;
                    key.root = hive ? Hku : Hklm;
                    key.path = source.name;
                }
                else
                    key.parent = tree.keys[source.parent].selected;
                const U index = keys.Push(key);
                if (index == None)
                    return;
                tree.keys[from].selected = index;
                for (U i = 0; i < source.values; ++i)
                {
                    const auto &value = tree.values[source.first_value + i];
                    AddValue(index, value.name, value.type, value.data, value.size);
                }
            }
        }
    }

    // --- tools/network_setup.py and tools/display_setup.py -----------------
    U GroupOf(U hive, Str path, bool create)
    {
        for (U i = 0; i < groups.count; ++i)
            if (groups[i].hive == hive && SameFolded(groups[i].path, path))
                return i;
        if (!create)
            return None;
        Group group;
        group.hive = hive;
        group.path = builder.Copy(path);
        for (U i = 0; i < path.n; ++i)
            group.depth += path.p[i] == '\\';
        return groups.Push(group);
    }
    void Put(U hive, Str path, Str name, U type, Str payload)
    {
        const U at = GroupOf(hive, path, true);
        if (at == None)
            return;
        Group &group = groups[at];
        // A name already there goes; the new one comes last.
        for (U previous = None, i = group.first; i != None;)
        {
            const U next = group_values[i].next;
            if (SameFolded(group_values[i].name, name))
            {
                if (previous == None)
                    group.first = next;
                else
                    group_values[previous].next = next;
                if (group.last == i)
                    group.last = previous;
            }
            else
                previous = i;
            i = next;
        }
        Chosen_Value value;
        value.name = builder.Copy(name);
        value.type = type;
        const Str copy = builder.Copy(payload);
        value.data = (const unsigned char *)copy.p;
        value.size = payload.n;
        const U index = group_values.Push(value);
        if (index == None)
            return;
        if (group.first == None)
            group.first = index;
        else
            group_values[group.last].next = index;
        group.last = index;
    }
    void PutText(U hive, Str path, const char *name, Str text) { Put(hive, path, Text(name), 1, text); }
    void PutLike(U hive, Str path, const Tree::Value &value)
    {
        Put(hive, path, value.name, value.type, {(const char *)value.data, value.size});
    }
    // The values one AddReg section writes.
    bool AddReg(const Inf &inf, Str section, Str hkr, Vec<Added> &out)
    {
        const U at = inf.FindFolded(builder, section);
        if (at == None)
            return true;
        for (U i = inf.sections[at].first; i != None; i = inf.lines[i].next)
        {
            Vec<Str> part;
            if (!Inf::Fields(inf.lines[i].text, part, builder))
                return false;
            const Str subkey = part.count > 1 ? inf.Expand(part[1], builder) : Str{};
            const Str name = part.count > 2 ? inf.Expand(part[2], builder) : Str{};
            Str path;
            if (SameFolded(part[0], Text("HKR")))
                path = subkey.n ? builder.Join(hkr, Text("\\"), subkey) : hkr;
            else if (SameFolded(part[0], Text("HKLM")))
                path = subkey;
            else
                continue;
            U flags = 0;
            if (part.count > 3 && part[3].n && !Number(part[3], 0, flags))
                return false;
            Added added;
            added.path = path;
            added.name = name;
            if (flags & 1)
            {
                const U count = part.count > 4 ? part.count - 4 : 0;
                auto *bytes = arena.Take(count + 1);
                if (!bytes)
                    return false;
                for (U k = 0; k < count; ++k)
                {
                    U byte = 0;
                    if (!Number(inf.Expand(part[4 + k], builder), 16, byte) || byte > 255)
                        return false;
                    bytes[k] = (unsigned char)byte;
                }
                added.type = 3;
                added.payload = {(const char *)bytes, count};
            }
            else
            {
                added.type = 1;
                added.payload = part.count > 4 ? inf.Expand(part[4], builder) : Str{};
            }
            bool replaced = false;
            for (U k = 0; k < out.count && !replaced; ++k)
                if (Same(out[k].path, added.path) && Same(out[k].name, added.name))
                {
                    out[k].type = added.type;
                    out[k].payload = added.payload;
                    replaced = true;
                }
            if (!replaced && out.Push(added) == None)
                return false;
        }
        return true;
    }
    // Every AddReg section an install section names.
    bool Install(const Inf &inf, const char *install, Str hkr, Vec<Added> &out)
    {
        const U at = inf.Find(builder.Lowered(Text(install)));
        if (at == None)
            return true;
        for (U i = inf.sections[at].first; i != None; i = inf.lines[i].next)
        {
            Str value;
            const Str key = Inf::Strip(Inf::Before(inf.lines[i].text, '=', value));
            if (!SameFolded(key, Text("addreg")))
                continue;
            // value.split(','): every piece, the empty ones too.
            for (U from = 0;;)
            {
                U to = from;
                while (to < value.n && value.p[to] != ',')
                    ++to;
                if (!AddReg(inf, Inf::Strip({value.p + from, to - from}), hkr, out))
                    return false;
                if (to >= value.n)
                    break;
                from = to + 1;
            }
        }
        return true;
    }
    // A subtree of the system hive under another name; `rename` replaces
    // the payloads of the named values.
    struct Rename
    {
        const char *name, *payload;
    };
    void Clone(const char *source, const char *target, const Rename *rename = nullptr, U renames = 0)
    {
        const Tree &tree = trees[0];
        const U top = tree.Find(Text(source));
        if (top == None)
            return;
        const U cut = (U)strlen(source);
        for (U i = top; i < tree.keys.count && (i == top || tree.Below(i, top)); ++i)
        {
            const Str full = tree.Path(i, builder);
            const Str path = builder.Join(Text(target), {full.p + cut, full.n - cut});
            GroupOf(0, path, true);
            for (U k = 0; k < tree.keys[i].values; ++k)
            {
                const auto &value = tree.values[tree.keys[i].first_value + k];
                Str payload{(const char *)value.data, value.size};
                for (U r = 0; r < renames; ++r)
                    if (Same(value.name, Text(rename[r].name)))
                        payload = Text(rename[r].payload);
                Put(0, path, value.name, value.type, payload);
            }
        }
    }
    bool Network(const Input &input)
    {
        const Tree &system = trees[0], &user = trees[1];
        static const char Class[] = "System\\CurrentControlSet\\Services\\Class";
        Inf netamd, netcli;
        if (!netamd.Parse(input.netamd, input.netamd_size, builder) ||
            !netcli.Parse(input.netcli, input.netcli_size, builder))
            return false;
        const Str *description = netamd.String(Text("*pnp828c.devicedesc"));
        const Str *microsoft = netamd.String(Text("v_ms")), *amd = netamd.String(Text("v_amd"));
        const Str *client_description = netcli.String(Text("vredir.devicedesc"));
        const Str *client_microsoft = netcli.String(Text("v_ms"));
        const U dial_up = system.Find(Text("Enum\\Root\\Net\\0000"));
        const U family_class = system.Find(Text("System\\CurrentControlSet\\Services\\Class\\NetClient\\0000"));
        const U family = system.Find(Text("Enum\\Network\\FAMILY\\0000"));
        if (!description || !microsoft || !amd || !client_description || !client_microsoft || dial_up == None ||
            family_class == None || family == None)
            return false;
        const Tree::Value *config = system.ValueOf(dial_up, Text("ConfigFlags"));
        const Tree::Value *capabilities = system.ValueOf(dial_up, Text("Capabilities"));
        const Tree::Value *driver_date = system.ValueOf(family_class, Text("DriverDate"));
        const Tree::Value *family_config = system.ValueOf(family, Text("ConfigFlags"));
        if (!config || !capabilities || !driver_date || !family_config)
            return false;

        // The card, NETAMD.INF model *pnp828c (PCIpcnet.ndi).
        const Str card = builder.Join(Text(Class), Text("\\Net\\0001"));
        {
            Vec<Added> added;
            if (!Install(netamd, "PCIpcnet.ndi", card, added))
                return false;
            // Without DeviceVxDs: the NDIS driver of the model is no file of
            // the folder and nothing loads it, the card's driver is the
            // port's.
            for (U i = 0; i < added.count; ++i)
                if (!SameFolded(added[i].name, Text("DeviceVxDs")))
                    Put(0, added[i].path, added[i].name, added[i].type, added[i].payload);
        }
        // The card is the port's own (src/neo98_hardware.h); the driver key is
        // the one of the model it is compatible with.
        PutText(0, card, "DriverDesc", Text(Neo98Hardware::NetName));
        PutText(0, card, "InfSection", Text("PCIpcnet.ndi"));
        PutText(0, card, "InfPath", Text("NETAMD.INF"));
        PutText(0, card, "ProviderName", Text(Neo98Hardware::Maker));
        PutText(0, card, "DriverDate", Text(Neo98Hardware::DriverDate));
        PutText(0, card, "MatchingDeviceId", Text(Neo98Hardware::NetCompatibleId));
        // Its enumerator key, as a root-enumerated card with the Dial-Up
        // Adapter's instance as the pattern for the flags.
        const Str enum_card = Text("Enum\\Root\\Net\\0001");
        PutText(0, enum_card, "Class", Text("Net"));
        PutText(0, enum_card, "Driver", Text("Net\\0001"));
        PutText(0, enum_card, "DeviceDesc", Text(Neo98Hardware::NetName));
        PutText(0, enum_card, "HardwareID", Text(Neo98Hardware::NetId));
        PutText(0, enum_card, "CompatibleIDs", Text(Neo98Hardware::NetCompatibleId));
        PutText(0, enum_card, "Mfg", Text(Neo98Hardware::Maker));
        PutText(0, enum_card, "ClassGUID", Text("{4d36e972-e325-11ce-bfc1-08002be10318}"));
        PutLike(0, enum_card, *config);
        PutLike(0, enum_card, *capabilities);
        PutText(0, Text("Enum\\Root\\Net\\0001\\Bindings"), "MSTCP\\0001", Str{});

        // TCP/IP on the card: a second instance written like the first.
        Clone("System\\CurrentControlSet\\Services\\Class\\NetTrans\\0000",
              "System\\CurrentControlSet\\Services\\Class\\NetTrans\\0001");
        static const Rename renamed[] = {{"Driver", "NetTrans\\0001"}, {"MasterCopy", "Enum\\Network\\MSTCP\\0001"}};
        Clone("Enum\\Network\\MSTCP\\0000", "Enum\\Network\\MSTCP\\0001", renamed, 2);

        // Client for Microsoft Networks, NETCLI.INF model VREDIR.
        const Str client = builder.Join(Text(Class), Text("\\NetClient\\0001"));
        {
            Vec<Added> added;
            if (!Install(netcli, "VREDIR.ndi", client, added))
                return false;
            for (U i = 0; i < added.count; ++i)
                Put(0, added[i].path, added[i].name, added[i].type, added[i].payload);
        }
        {
            // VREDIR.Install without the providers and WinPopup; see
            // tools/network_setup.py.
            Vec<Added> added;
            if (!Install(netcli, "VREDIR.Install", client, added))
                return false;
            for (U i = 0; i < added.count; ++i)
            {
                const Str lowered = builder.Lowered(added[i].path);
                bool skip = false;
                static const char *const left_out[] = {"print\\providers", "optionalcomponents", "networkprovider"};
                for (const char *part : left_out)
                {
                    const U n = (U)strlen(part);
                    for (U at = 0; at + n <= lowered.n && !skip; ++at)
                        skip = !memcmp(lowered.p + at, part, n);
                }
                if (!skip)
                    Put(0, added[i].path, added[i].name, added[i].type, added[i].payload);
            }
        }
        PutText(0, client, "DriverDesc", *client_description);
        PutText(0, client, "InfSection", Text("VREDIR.ndi"));
        PutText(0, client, "InfPath", Text("NETCLI.INF"));
        PutText(0, client, "ProviderName", *client_microsoft);
        Put(0, client, Text("DriverDate"), driver_date->type, {(const char *)driver_date->data, driver_date->size});
        const Str ndi = builder.Join(client, Text("\\Ndi")), interfaces = builder.Join(client, Text("\\Ndi\\Interfaces"));
        PutText(0, ndi, "InstallInf", Str{});
        PutText(0, interfaces, "Upper", Text("winnet4,vredir"));
        PutText(0, interfaces, "Lower", Text("netbios,ipxDHost"));
        // One instance of each client per TCP/IP instance, the first the
        // master copy.
        static const char *const instances[] = {"0000", "0001"};
        for (const char *index : instances)
        {
            const Str vredir = builder.Join(Text("Enum\\Network\\VREDIR\\"), Text(index));
            PutText(0, vredir, "Class", Text("NetClient"));
            PutText(0, vredir, "Driver", Text("NetClient\\0001"));
            PutText(0, vredir, "MasterCopy", Text("Enum\\Network\\VREDIR\\0000"));
            PutText(0, vredir, "DeviceDesc", *client_description);
            PutText(0, vredir, "CompatibleIDs", Text("VREDIR"));
            PutText(0, vredir, "Mfg", *client_microsoft);
            PutText(0, vredir, "ClassGUID", Text("{4d36e973-e325-11ce-bfc1-08002be10318}"));
            Put(0, vredir, Text("ConfigFlags"), family_config->type,
                {(const char *)family_config->data, family_config->size});
            GroupOf(0, builder.Join(vredir, Text("\\Bindings")), true);
        }
        const Str family_copy = Text("Enum\\Network\\FAMILY\\0001");
        for (U k = 0; k < system.keys[family].values; ++k)
        {
            const auto &value = system.values[system.keys[family].first_value + k];
            if (Same(value.name, Text("MasterCopy")))
                Put(0, family_copy, value.name, value.type, Text("Enum\\Network\\FAMILY\\0000"));
            else
                PutLike(0, family_copy, value);
        }
        GroupOf(0, Text("Enum\\Network\\FAMILY\\0001\\Bindings"), true);
        PutText(0, Text("Enum\\Network\\MSTCP\\0000\\Bindings"), "VREDIR\\0000", Str{});
        const U lan = GroupOf(0, Text("Enum\\Network\\MSTCP\\0001\\Bindings"), false);
        if (lan == None)
            return false;
        groups[lan].first = groups[lan].last = None;
        PutText(0, Text("Enum\\Network\\MSTCP\\0001\\Bindings"), "FAMILY\\0001", Str{});
        PutText(0, Text("Enum\\Network\\MSTCP\\0001\\Bindings"), "VREDIR\\0001", Str{});
        // Windows Logon as the primary network logon.
        PutText(0, Text("Network\\Logon"), "PrimaryProvider", Str{});

        // Mark the Internet Connection Wizard as complete.
        Put(1, Text(".DEFAULT\\Software\\Microsoft\\Internet Connection Wizard"), Text("Completed"), 3,
            {"\x01\x00\x00\x00", 4});
        // IE's per-user defaults, from the AdvancedOptions the machine half
        // declares; in the order of the lowered key paths.
        const U options = system.Find(Text("Software\\Microsoft\\Internet Explorer\\AdvancedOptions"));
        if (options != None)
        {
            Vec<U> order;
            Vec<Str> lowered;
            for (U i = options + 1; i < system.keys.count && system.Below(i, options); ++i)
            {
                order.Push(i);
                lowered.Push(builder.Lowered(system.Path(i, builder)));
            }
            if (order.failed || lowered.failed)
                return false;
            for (U i = 1; i < order.count; ++i)
                for (U k = i; k && Compare(lowered[k], lowered[k - 1]) < 0; --k)
                {
                    const U key = order[k];
                    order[k] = order[k - 1];
                    order[k - 1] = key;
                    const Str text = lowered[k];
                    lowered[k] = lowered[k - 1];
                    lowered[k - 1] = text;
                }
            for (U i = 0; i < order.count; ++i)
                Option(system, user, order[i]);
        }
        return true;
    }
    static int Compare(Str a, Str b)
    {
        const U n = a.n < b.n ? a.n : b.n;
        const int order = memcmp(a.p, b.p, n);
        return order ? order : a.n < b.n ? -1 : a.n > b.n ? 1 : 0;
    }
    static Str TrimNul(Str text)
    {
        while (text.n && !text.p[text.n - 1])
            --text.n;
        return text;
    }
    // One option of the Advanced page of the Internet Options, if it is one
    // of the browser's own settings the profile does not have yet.
    void Option(const Tree &system, const Tree &user, U key)
    {
        const Tree::Value *regpath = nullptr, *valuename = nullptr, *defaultvalue = nullptr, *hkeyroot = nullptr,
                          *mask = nullptr, *type = nullptr, *checked = nullptr;
        // A dict of the lowered names: the last of two names that differ only
        // in case holds.
        for (U i = 0; i < system.keys[key].values; ++i)
        {
            const Tree::Value *value = &system.values[system.keys[key].first_value + i];
            if (!value->name.n)
                continue;
            const auto is = [&](const char *name) { return SameFolded(value->name, Text(name)); };
            if (is("regpath")) regpath = value;
            else if (is("valuename")) valuename = value;
            else if (is("defaultvalue")) defaultvalue = value;
            else if (is("hkeyroot")) hkeyroot = value;
            else if (is("mask")) mask = value;
            else if (is("type")) type = value;
            else if (is("checkedvalue")) checked = value;
        }
        if (!regpath || !valuename || !defaultvalue || !hkeyroot)
            return;
        if (hkeyroot->size != 4 || memcmp(hkeyroot->data, "\x01\x00\x00\x80", 4))
            return;
        if (mask)
            return;
        if (type)
            for (U at = 0; at + 7 <= type->size; ++at)
            {
                bool found = true;
                for (U i = 0; i < 7 && found; ++i)
                    found = Fold(type->data[at + i]) == (U)(unsigned char)"bitmask"[i];
                if (found)
                    return;
            }
        const Str path_text = TrimNul({(const char *)regpath->data, regpath->size});
        if (!StartsFolded(path_text, Text("software\\microsoft\\internet explorer")))
            return;
        const Str path = builder.Join(Text(".DEFAULT\\"), path_text);
        const Str name = TrimNul({(const char *)valuename->data, valuename->size});
        const U present = user.Find(path);
        if (present != None)
            for (U i = 0; i < user.keys[present].values; ++i)
            {
                const Str have = user.values[user.keys[present].first_value + i].name;
                if (have.n && SameFolded(have, name))
                    return;
            }
        U kind = defaultvalue->type;
        Str payload{(const char *)defaultvalue->data, defaultvalue->size};
        unsigned char number[4];
        if (checked && checked->type == 4 && kind != 4)
        {
            // int of the text, as a DWORD.
            Str digits = Inf::Strip(TrimNul(payload));
            U value = 0;
            if (digits.n && digits.p[0] == '+')
            {
                ++digits.p;
                --digits.n;
            }
            for (U i = 0; i < digits.n; ++i)
                if (digits.p[i] >= '0' && digits.p[i] <= '9')
                    value = value * 10 + (U)(digits.p[i] - '0');
            Win98Creg::PutLong(number, value);
            payload = {(const char *)number, 4};
            kind = 4;
        }
        Put(1, path, name, kind, payload);
    }
    void Display()
    {
        // The mode of the port's display adapter, tools/display_setup.py.
        const Str settings = Text("Config\\0001\\Display\\Settings");
        PutText(0, settings, "Resolution", Text("800,600"));
        PutText(0, settings, "BitsPerPixel", Text("32"));
    }

    // --- tools/hardware_setup.py -------------------------------------------
    // The devices of the machine the folder was installed on become the
    // port's own (src/neo98_hardware.h): the display adapter, the CD-ROM
    // drives and the BIOS string of the system board. What a class installer
    // writes when a device is exchanged: a new instance under Enum, the
    // description in the driver's class key, the old instance gone.
    Vec<U> replaced; // instance keys of the system hive that go
    Vec<U> removed;  // those of them whose driver key goes as well
    const Tree::Value *Folded(U key, const char *name) const
    {
        const Tree &system = trees[0];
        for (U i = 0; i < system.keys[key].values; ++i)
            if (SameFolded(system.values[system.keys[key].first_value + i].name, Text(name)))
                return &system.values[system.keys[key].first_value + i];
        return nullptr;
    }
    bool ValueIs(U key, const char *name, const char *text) const
    {
        const Tree::Value *value = Folded(key, name);
        if (!value)
            return false;
        Str payload{(const char *)value->data, value->size};
        while (payload.n && !payload.p[payload.n - 1])
            --payload.n;
        return SameFolded(payload, Text(text));
    }
    void Hardware()
    {
        const Tree &system = trees[0];
        static const char Class[] = "System\\CurrentControlSet\\Services\\Class";
        const U enumeration = system.Find(Text("Enum"));
        if (enumeration == None)
            return;
        // The instances three levels below Enum, in stored order.
        for (U enumerator = system.keys[enumeration].first_child; enumerator != None;
             enumerator = system.keys[enumerator].next)
            for (U device = system.keys[enumerator].first_child; device != None; device = system.keys[device].next)
                for (U instance = system.keys[device].first_child; instance != None;
                     instance = system.keys[instance].next)
                {
                    if (ValueIs(instance, "Class", "Display") && ValueIs(instance, "Driver", "Display\\0000") &&
                        !SameFolded(system.keys[device].name, Text(Neo98Hardware::DisplayId)))
                    {
                        const Str node = Text(Neo98Hardware::DisplayNode);
                        PutText(0, node, "Class", Text("Display"));
                        PutText(0, node, "Driver", Text("Display\\0000"));
                        PutText(0, node, "DeviceDesc", Text(Neo98Hardware::DisplayName));
                        PutText(0, node, "HardwareID", Text(Neo98Hardware::DisplayId));
                        PutText(0, node, "Mfg", Text(Neo98Hardware::Maker));
                        static const char *const kept[] = {"ClassGUID", "ConfigFlags", "Capabilities"};
                        for (const char *name : kept)
                            if (const Tree::Value *value = Folded(instance, name))
                                PutLike(0, node, *value);
                        const Str driver = builder.Join(Text(Class), Text("\\Display\\0000"));
                        PutText(0, driver, "DriverDesc", Text(Neo98Hardware::DisplayName));
                        PutText(0, driver, "ProviderName", Text(Neo98Hardware::Maker));
                        PutText(0, driver, "MatchingDeviceId", Text(Neo98Hardware::DisplayId));
                        const Str standard = builder.Join(driver, Text("\\DEFAULT"));
                        PutText(0, standard, "Mode", Text(Neo98Hardware::DisplayMode));
                        PutText(0, standard, "drv", Text(Neo98Hardware::DisplayDriverFile));
                        PutText(0, standard, "vdd", Text(Neo98Hardware::DisplayVdd));
                        PutText(0, driver, "DriverDate", Text(Neo98Hardware::DriverDate));
                        const Str information = builder.Join(driver, Text("\\INFO"));
                        PutText(0, information, "ChipType", Text(Neo98Hardware::DisplayChip));
                        PutText(0, information, "DACType", Text(Neo98Hardware::DisplayDac));
                        PutText(0, information, "Revision", Text(Neo98Hardware::DisplayRevision));
                        replaced.Push(instance);
                    }
                    else if (ValueIs(instance, "Class", "CDROM") && SameFolded(system.keys[enumerator].name, Text("SCSI")) &&
                             !SameFolded(system.keys[device].name, Text(Neo98Hardware::CdId)))
                    {
                        const Str node = builder.Join(Text(Neo98Hardware::CdDevice), Text("\\"), system.keys[instance].name);
                        for (U k = 0; k < system.keys[instance].values; ++k)
                        {
                            const auto &value = system.values[system.keys[instance].first_value + k];
                            static const Rename renamed[] = {{"DeviceDesc", Neo98Hardware::CdName},
                                                             {"Manufacturer", Neo98Hardware::CdVendor},
                                                             {"ProductId", Neo98Hardware::CdProduct},
                                                             {"RevisionLevel", Neo98Hardware::CdRevision},
                                                             {"HardwareID", Neo98Hardware::CdHardwareId}};
                            Str payload{(const char *)value.data, value.size};
                            for (const Rename &rename : renamed)
                                if (SameFolded(value.name, Text(rename.name)))
                                    payload = Text(rename.payload);
                            Put(0, node, value.name, value.type, payload);
                        }
                        replaced.Push(instance);
                    }
                    else if (ValueIs(instance, "Class", "fdc") || SameFolded(system.keys[enumerator].name, Text("FLOP")))
                    {
                        // No floppy disk drive here: the controller and its
                        // drive go with their driver keys.
                        replaced.Push(instance);
                        removed.Push(instance);
                    }
                }
        const U board = system.Find(Text("Enum\\Root\\*PNP0C01\\0000"));
        if (board == None)
            return;
        if (Folded(board, "BIOSVersion"))
            PutText(0, Text("Enum\\Root\\*PNP0C01\\0000"), "BIOSVersion", Text(Neo98Hardware::BiosVersion));
        // BootConfig, the resources Setup's detection found a device at on
        // its machine (LPT1 at 0378-037A without an interrupt request). The
        // report "System summary" the Device Manager prints reads this value
        // before it asks the configuration manager, so a device
        // that has one gets the resources of Neo98Hardware::DeviceResources
        // in the same form: 040Ah and a priority of zero, then a resource a
        // record (size, ResType_Mem 1, _IO 2, _DMA 3, _IRQ 4, the MEM_DES,
        // IO_DES, DMA_DES or IRQ_DES with one allocated range), then zero.
        for (const auto &entry : Neo98Hardware::DeviceResources)
        {
            const U key = system.Find(builder.Join(Text("Enum\\"), Text(entry.device)));
            if (key == None || !Folded(key, "BootConfig"))
                continue;
            auto *bytes = (unsigned char *)builder.arena.Take(8 + 24 * 8 + 4);
            if (!bytes)
                continue;
            U n = 0;
            const auto word = [&](U value) { bytes[n++] = (unsigned char)value; bytes[n++] = (unsigned char)(value >> 8); };
            const auto dword = [&](U value) { word(value & 0xffff); word(value >> 16); };
            dword(0x040a);
            dword(0);
            for (const auto &resource : entry.list)
            {
                if (resource.kind == 'P')
                {
                    dword(0x14); dword(2); word(0); word(0x0c);
                    word(resource.first); word(resource.last); word(0);
                    bytes[n++] = 0;
                    bytes[n++] = resource.last < 0x100 ? 0xff : 0x03; // the address lines decoded
                }
                else if (resource.kind == 'Q')
                {
                    dword(0x10); dword(4); word(0); word(resource.first); dword(0);
                }
                else if (resource.kind == 'D')
                {
                    dword(0x0c); dword(3);
                    bytes[n++] = 0;
                    bytes[n++] = (unsigned char)resource.first;
                    word(0);
                }
                else if (resource.kind == 'M')
                {
                    dword(0x18); dword(1); word(0); word(0x14);
                    dword(resource.first); dword(resource.last); dword(0);
                }
            }
            dword(0);
            Put(0, system.Path(key, builder), Text("BootConfig"), 3, {(const char *)bytes, n});
        }
        // The devices Setup did not find on its machine: the sound card and
        // the monitor, written as their class installers write a device.
        const auto device = [&](const char *node, const char *kind, const char *guid, const char *driver,
                                const char *name, Str id) {
            const Str at = Text(node), key = builder.Join(Text(Class), Text("\\"), Text(driver));
            PutText(0, at, "Class", Text(kind));
            PutText(0, at, "ClassGUID", Text(guid));
            PutText(0, at, "Driver", Text(driver));
            PutText(0, at, "DeviceDesc", Text(name));
            PutText(0, at, "HardwareID", id);
            PutText(0, at, "Mfg", Text(Neo98Hardware::Maker));
            Put(0, at, Text("ConfigFlags"), 3, {"\0\0\0\0", 4});
            PutText(0, key, "DriverDesc", Text(name));
            PutText(0, key, "ProviderName", Text(Neo98Hardware::Maker));
            PutText(0, key, "DriverDate", Text(Neo98Hardware::DriverDate));
            PutText(0, key, "MatchingDeviceId", id);
            return key;
        };
        const Str sound = device(Neo98Hardware::SoundNode, "MEDIA", "{4d36e96c-e325-11ce-bfc1-08002be10318}",
                                 Neo98Hardware::SoundDriver, Neo98Hardware::SoundName, Text(Neo98Hardware::SoundId));
        PutText(0, sound, "DevLoader", Text(Neo98Hardware::SoundLoader));
        static const Rename parts[] = {{"wave", Neo98Hardware::WaveOutName},
                                       {"midi", Neo98Hardware::MidiOutName},
                                       {"mixer", Neo98Hardware::MixerName}};
        for (const Rename &part : parts)
        {
            const Str key = builder.Join(builder.Join(Text(Neo98Hardware::MediaResources), Text("\\"), Text(part.name)),
                                         Text("\\"), Text(Neo98Hardware::SoundResource));
            PutText(0, key, "Active", Text("1"));
            PutText(0, key, "Description", Text(part.payload));
            PutText(0, key, "FriendlyName", Text(part.payload));
            PutText(0, key, "Driver", Text(Neo98Hardware::SoundResourceDriver));
        }
        const Str monitor_id = builder.Join(Text("MONITOR\\"), Text(Neo98Hardware::MonitorId));
        const Str monitor = device(Neo98Hardware::MonitorNode, "Monitor", "{4d36e96e-e325-11ce-bfc1-08002be10318}",
                                   Neo98Hardware::MonitorDriver, Neo98Hardware::MonitorName, monitor_id);
        PutText(0, monitor, "MaxResolution", Text(Neo98Hardware::MonitorMaxResolution));
        PutText(0, monitor, "DPMS", Text("0"));
        PutText(0, builder.Join(monitor, Text("\\MODES\\"), Text(Neo98Hardware::MonitorMaxResolution)), "Mode1",
                Text("30.0-130.0,50.0-160.0"));
    }
    // After the merge: the old instances with their device keys, the values
    // of the class key that named the old driver's files and INF, and the
    // marks of the old instances under ASD\Prob.
    void HardwareRemovals()
    {
        const Tree &system = trees[0];
        bool display = false;
        for (U i = 0; i < replaced.count; ++i)
        {
            const U instance = replaced[i], device = system.keys[instance].parent;
            display = display || ValueIs(instance, "Class", "Display");
            // "ROOT\*PNP0905\0000", the name ASD keeps the instance under.
            const Str path = system.Path(instance, builder);
            const Str id{path.p + 5, path.n - 5};
            const U marks = system.Find(Text("System\\CurrentControlSet\\Control\\ASD\\Prob"));
            if (marks != None)
                for (U key = system.keys[marks].first_child; key != None; key = system.keys[key].next)
                    Forget(0, system.Path(key, builder), id);
            // The device key goes when no other instance stays under it.
            bool others = false;
            for (U sibling = system.keys[device].first_child; sibling != None; sibling = system.keys[sibling].next)
            {
                bool goes = false;
                for (U k = 0; k < replaced.count; ++k)
                    goes = goes || replaced[k] == sibling;
                others = others || !goes;
            }
            Remove(0, others ? path : system.Path(device, builder));
        }
        for (U i = 0; i < removed.count; ++i)
            if (const Tree::Value *driver = Folded(removed[i], "Driver"))
            {
                Str name{(const char *)driver->data, driver->size};
                while (name.n && !name.p[name.n - 1])
                    --name.n;
                Remove(0, builder.Join(Text("System\\CurrentControlSet\\Services\\Class\\"), name));
            }
        if (display)
        {
            static const char *const gone[] = {"InfPath", "InfSection"};
            for (const char *name : gone)
                Forget(0, Text("System\\CurrentControlSet\\Services\\Class\\Display\\0000"), Text(name));
            // The mini driver of the old adapter; drv and vdd name the files
            // of Win98 the port's driver stands in for.
            Forget(0, Text("System\\CurrentControlSet\\Services\\Class\\Display\\0000\\DEFAULT"), Text("minivdd"));
        }
    }

    // --- the additions into the selection ----------------------------------
    U Known(U hive, Str path) const
    {
        const U in_tree = trees[hive].Find(path);
        if (in_tree != None && trees[hive].keys[in_tree].selected != None)
            return trees[hive].keys[in_tree].selected;
        for (U i = made.count; i--;)
            if (made[i].hive == hive && SameFolded(made[i].path, path))
                return made[i].selected;
        return None;
    }
    static Str Above(Str path, Str &name)
    {
        for (U i = path.n; i--;)
            if (path.p[i] == '\\')
            {
                name = {path.p + i + 1, path.n - i - 1};
                return {path.p, i};
            }
        name = path;
        return {path.p, 0};
    }
    bool ImportedAbove(U hive, Str path) const
    {
        while (path.n)
        {
            if (Known(hive, path) != None)
                return true;
            Str name;
            path = Above(path, name);
        }
        return false;
    }
    U KeyFor(U hive, Str path)
    {
        U at = Known(hive, path);
        if (at != None)
            return at;
        Str name;
        const Str parent = Above(path, name);
        Chosen_Key key;
        key.name = name;
        if (ImportedAbove(hive, parent))
            key.parent = KeyFor(hive, parent);
        else
        {
            // No imported key above it: a subtree of its own.
            key.anchor = true;
            key.root = Hklm;
            key.path = builder.Copy(path);
        }
        at = keys.Push(key);
        const U in_tree = trees[hive].Find(path);
        if (in_tree != None)
            trees[hive].keys[in_tree].selected = at;
        else
            made.Push({hive, builder.Copy(path), at});
        return at;
    }
    void Merge()
    {
        // sorted(extra, key=depth), stable.
        Vec<U> order;
        for (U i = 0; i < groups.count; ++i)
            order.Push(i);
        for (U i = 1; i < order.count; ++i)
            for (U k = i; k && groups[order[k]].depth < groups[order[k - 1]].depth; --k)
            {
                const U swap = order[k];
                order[k] = order[k - 1];
                order[k - 1] = swap;
            }
        for (U o = 0; o < order.count; ++o)
        {
            const Group &group = groups[order[o]];
            const U at = KeyFor(group.hive, group.path);
            if (at == None)
                return;
            // The values of the key that the additions do not name stay in
            // front, the additions follow.
            for (U previous = None, i = keys[at].first; i != None;)
            {
                const U next = values[i].next;
                bool named = false;
                for (U g = group.first; g != None && !named; g = group_values[g].next)
                    named = SameFolded(group_values[g].name, values[i].name);
                if (named)
                {
                    if (previous == None)
                        keys[at].first = next;
                    else
                        values[previous].next = next;
                    if (keys[at].last == i)
                        keys[at].last = previous;
                }
                else
                    previous = i;
                i = next;
            }
            for (U g = group.first; g != None; g = group_values[g].next)
                AddValue(at, group_values[g].name, group_values[g].type, group_values[g].data, group_values[g].size);
        }
    }
    // A value the setup takes away.
    void Forget(U hive, Str path, Str name)
    {
        const U at = Known(hive, path);
        if (at == None)
            return;
        for (U previous = None, i = keys[at].first; i != None;)
        {
            const U next = values[i].next;
            if (SameFolded(values[i].name, name))
            {
                if (previous == None)
                    keys[at].first = next;
                else
                    values[previous].next = next;
                if (keys[at].last == i)
                    keys[at].last = previous;
            }
            else
                previous = i;
            i = next;
        }
    }
    // A key the setup takes away, with everything below it.
    void Remove(U hive, Str path)
    {
        const U at = Known(hive, path);
        if (at == None)
            return;
        keys[at].removed = true;
        for (U i = 0; i < keys.count; ++i)
            if (!keys[i].anchor && keys[i].parent != None && keys[keys[i].parent].removed)
                keys[i].removed = true;
    }
};
} // namespace HiveImport
} // namespace Registry
