// SPDX-License-Identifier: GPL-2.0-or-later
// The state of the port written and read field by field, by the table of
// tools/state_layout.py. See src/x86_state_fields.h for what and why.
#include "x86_state_fields.h"
#include "dos_vm.h"
#include "x86_state_dos_ui.h"
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__) || defined(__linux__)
#include <cxxabi.h>
#include <dlfcn.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <malloc/malloc.h>
#else
#include "../tools/linux_state_platform.h"
#define malloc_size LinuxStatePlatform::AllocationSize
#define malloc_good_size LinuxStatePlatform::GoodSize
#endif
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace MachineState
{
bool BuildIdentity(unsigned char out[16]);
namespace
{
// ---- The table of this program (tools/state_layout.py, check_and_write) -----------
struct Header
{
    char magic[8];
    uint32_t types, fields, variables, symbols, strings;
    unsigned char uuid[16];
    uint32_t pad;
};
struct TypeRecord
{
    uint32_t name, kind, size, a, b, field_begin, field_count, rule;
};
struct FieldRecord
{
    uint32_t name, type, offset;
    uint16_t bit_offset, bit_size;
    uint32_t rule, argument;
};
struct VariableRecord
{
    uint32_t name, source, type, rule;
    uint64_t address;
    uint32_t argument, symbol;
    uint64_t guard;
};
struct SymbolRecord
{
    uint64_t address;
    uint32_t name, object, size, pad;
};
enum Kind : uint32_t { Scalar = 0, Pointer = 2, Function = 3, Array = 4, Struct = 5, Union = 6, Opaque = 7, Void = 8, Vtable = 9, Address = 10,
                       // A container of the standard library: what it holds, not how the library keeps it.
                       Vector = 11, Unique = 12, Shared = 13, Map = 14, String = 15 };
enum Rule : uint32_t { None = 0, OfHost = 1, Skip = 2, Auto = 3, Count = 4, Bytes = 5, CString = 6, Tail = 7, Custom = 8, As = 9, Headed = 10, Bind = 12, End = 13 };

struct Layout
{
    std::vector<unsigned char> bytes;
    const Header *header = nullptr;
    const TypeRecord *types = nullptr;
    const FieldRecord *fields = nullptr;
    const VariableRecord *variables = nullptr;
    const SymbolRecord *symbols = nullptr;
    const char *strings = nullptr;
    uint64_t slide = 0, image_begin = 0, image_end = 0, writable_begin = 0;
    std::vector<unsigned char> flat;       // 0 not known, 1 flat, 2 not
    std::vector<uint32_t> by_address;      // variables sorted by address
    uint32_t byte_type = 0;                // a type of one byte, for blocks of bytes
    uint32_t void_type = 0;
    std::unordered_map<std::string, uint32_t> type_by_name, variable_by_name;
    std::unordered_map<std::string, uint64_t> symbol_by_name;
    const char *Text(uint32_t at) const { return strings + at; }
    const char *TypeName(uint32_t type) const { return Text(types[type].name); }
    bool Flat(uint32_t type)
    {
        if (flat[type])
            return flat[type] == 1;
        const auto &t = types[type];
        bool answer = true;
        flat[type] = 1; // a type that holds itself only does so through a pointer
        if (t.kind == Pointer || t.kind == Function || t.kind == Vtable || t.kind == Address || t.kind >= Vector)
            answer = false;
        else if (t.kind == Array)
            answer = Flat(t.a);
        else if (t.kind == Struct || t.kind == Union)
            for (uint32_t i = 0; answer && i < t.field_count; ++i)
                answer = fields[t.field_begin + i].rule == None && Flat(fields[t.field_begin + i].type);
        flat[type] = answer ? 1 : 2;
        return answer;
    }
    // The symbol at or before an address of the program.
    const SymbolRecord *SymbolAt(uint64_t address) const
    {
        const uint64_t unslid = address - slide;
        uint32_t low = 0, high = header->symbols;
        while (low < high)
        {
            const uint32_t middle = (low + high) / 2;
            if (symbols[middle].address <= unslid)
                low = middle + 1;
            else
                high = middle;
        }
        return low ? &symbols[low - 1] : nullptr;
    }
    const VariableRecord *VariableAt(uint64_t address, uint32_t &index) const
    {
        const uint64_t unslid = address - slide;
        uint32_t low = 0, high = (uint32_t)by_address.size();
        while (low < high)
        {
            const uint32_t middle = (low + high) / 2;
            if (variables[by_address[middle]].address <= unslid)
                low = middle + 1;
            else
                high = middle;
        }
        if (!low)
            return nullptr;
        index = by_address[low - 1];
        const auto &v = variables[index];
        return unslid - v.address < (types[v.type].size ? types[v.type].size : 1) ? &v : nullptr;
    }
};
Layout *layout = nullptr;

struct RegionRecord
{
    std::string name;
    uint64_t base, bytes;
    bool imported = false;
};
std::vector<RegionRecord> &Regions()
{
    static std::vector<RegionRecord> regions;
    return regions;
}
RegionRecord *RegionAt(uint64_t address)
{
    auto &regions = Regions();
    auto after = std::upper_bound(regions.begin(), regions.end(), address,
                                 [](uint64_t value, const RegionRecord &r) { return value < r.base; });
    if (after == regions.begin()) return nullptr;
    auto &region = *--after;
    return address - region.base < region.bytes ? &region : nullptr;
}
bool ImportedAddress(uint64_t address)
{
    const auto *region = RegionAt(address);
    return region && region->imported;
}
// The strings of the program a load could not point at (another build has
// them elsewhere): copies, remembered so the next save knows them as what
// they were.
std::unordered_set<uint64_t> &Interned()
{
    static std::unordered_set<uint64_t> strings;
    return strings;
}
FieldNumbers last_numbers;
unsigned last_end_pointers = 0;

void Say(char *error, size_t bytes, const std::string &text)
{
    if (error && bytes)
        snprintf(error, bytes, "%s", text.c_str());
}
std::string Hex(uint64_t value)
{
    char text[32];
    snprintf(text, sizeof text, "%llx", (unsigned long long)value);
    return text;
}

// ---- A buffer of bytes ----------------------------------------------------------------
struct Out
{
    std::vector<unsigned char> data;
    void Bytes(const void *p, size_t n)
    {
        const auto *b = static_cast<const unsigned char *>(p);
        data.insert(data.end(), b, b + n);
    }
    void U8(unsigned v) { data.push_back((unsigned char)v); }
    void U32(uint32_t v) { Bytes(&v, 4); }
    void U64(uint64_t v) { Bytes(&v, 8); }
    void Str(const std::string &s)
    {
        U32((uint32_t)s.size());
        Bytes(s.data(), s.size());
    }
};
struct In
{
    const unsigned char *data = nullptr;
    size_t size = 0, at = 0;
    bool bad = false;
    bool Need(size_t n)
    {
        if (bad || n > size - at)
            bad = true;
        return !bad;
    }
    const unsigned char *Take(size_t n)
    {
        if (!Need(n))
            return nullptr;
        at += n;
        return data + at - n;
    }
    unsigned U8() { const auto *p = Take(1); return p ? *p : 0; }
    uint32_t U32() { uint32_t v = 0; if (const auto *p = Take(4)) memcpy(&v, p, 4); return v; }
    uint64_t U64() { uint64_t v = 0; if (const auto *p = Take(8)) memcpy(&v, p, 8); return v; }
    std::string Str()
    {
        const uint32_t n = U32();
        const auto *p = Take(n);
        return p ? std::string(reinterpret_cast<const char *>(p), n) : std::string();
    }
};

// ---- The containers of the standard library -------------------------------------------
// A map is a red-black tree; the table (tools/state_layout.py, container)
// has checked where its links lie in this build.
uint64_t &Word(uint64_t address) { return *reinterpret_cast<uint64_t *>(address); }
namespace Tree
{
#ifdef __linux__
constexpr uint64_t Left = 16, Right = 24, Parent = 8, Black = 0;
constexpr uint64_t Begin = 24, End = 8, Size = 40;
uint64_t Next(uint64_t node)
{
    if (Word(node+Right)) { node = Word(node+Right); while (Word(node+Left)) node = Word(node+Left); return node; }
    uint64_t parent = Word(node+Parent);
    while (node == Word(parent+Right)) { node = parent; parent = Word(parent+Parent); }
    return Word(node+Right) != parent ? parent : node;
}
#else
constexpr uint64_t Left = 0, Right = 8, Parent = 16, Black = 24; // of a node
constexpr uint64_t Begin = 0, End = 8, Size = 16;                // of the map; the end node is its left link alone
uint64_t Next(uint64_t node)
{
    if (Word(node + Right))
    {
        node = Word(node + Right);
        while (Word(node + Left))
            node = Word(node + Left);
        return node;
    }
    while (node != Word(Word(node + Parent) + Left))
        node = Word(node + Parent);
    return Word(node + Parent);
}
#endif
// A balanced tree over nodes in their order. Every level but the last is
// full, so the nodes of the last level are red and all others black, unless
// the tree is full to the last node.
uint64_t Build(const std::vector<uint64_t> &nodes, size_t low, size_t high, uint64_t parent, unsigned depth, unsigned red_depth)
{
    if (low >= high)
        return 0;
    const size_t middle = low + (high - low) / 2;
    const uint64_t node = nodes[middle];
    Word(node + Parent) = parent;
    *reinterpret_cast<unsigned char *>(node + Black) = depth != red_depth;
    Word(node + Left) = Build(nodes, low, middle, node, depth + 1, red_depth);
    Word(node + Right) = Build(nodes, middle + 1, high, node, depth + 1, red_depth);
    return node;
}
void Make(uint64_t map, const std::vector<uint64_t> &nodes)
{
    unsigned levels = 0;
    while (((size_t)1 << levels) - 1 < nodes.size())
        ++levels;
    const bool full = (((size_t)1 << levels) - 1) == nodes.size();
    const uint64_t root = Build(nodes, 0, nodes.size(), map + End, 1, full ? 0 : levels);
#ifdef __linux__
    *reinterpret_cast<unsigned *>(map + End) = 0;
    Word(map + 16) = root;
    Word(map + 32) = nodes.empty() ? map + End : nodes.back();
#else
    Word(map + End) = root;
#endif
    Word(map + Begin) = nodes.empty() ? map + End : nodes.front();
    Word(map + Size) = nodes.size();
}
} // namespace Tree
// The count of a shared_ptr an object of a loaded state hangs on. The one
// the saving process had belonged to its library; this one keeps the object
// for good, so an object that was shared is never destroyed after a load.
#ifdef __linux__
using SharedCount = std::_Sp_counted_base<__gnu_cxx::_S_atomic>;
struct Kept final : SharedCount
{
    void _M_dispose() noexcept override {}
    void _M_destroy() noexcept override {}
    void *_M_get_deleter(const std::type_info &) noexcept override { return nullptr; }
};
#else
using SharedCount = std::__shared_weak_count;
struct Kept final : std::__shared_weak_count
{
    void __on_zero_shared() noexcept override {}
    void __on_zero_shared_weak() noexcept override {}
};

#endif
// ---- Paths --------------------------------------------------------------------------
// Where inside a value of a type an offset lies, in names: ".threads[3].id".
// A pointer to the start of something is the empty path of that something.
bool PathOf(const Layout &l, uint32_t type, uint64_t count, uint64_t offset, std::string &path)
{
    // An object is `count` elements of its type.
    const uint64_t size = l.types[type].size;
    if (count != 1 || (size && offset >= size))
    {
        if (!size)
            return offset == 0;
        const uint64_t index = offset / size;
        if (index > count || (index == count && offset != index * size))
            return false;
        path += "[" + std::to_string(index) + "]";
        offset -= index * size;
        if (index == count)
            return true;
    }
    for (;;)
    {
        if (!offset)
            return true;
        const auto &t = l.types[type];
        if (t.kind == Array)
        {
            const uint64_t element = l.types[t.a].size;
            if (!element)
                return false;
            const uint64_t index = offset / element;
            if (index > t.b || (index == t.b && offset != index * element))
                return false;
            path += "[" + std::to_string(index) + "]";
            offset -= index * element;
            if (index == t.b)
                return true;
            type = t.a;
            continue;
        }
        if (t.kind == Struct || t.kind == Union)
        {
            const FieldRecord *found = nullptr;
            for (uint32_t i = 0; i < t.field_count; ++i)
            {
                const auto &f = l.fields[t.field_begin + i];
                if (!f.bit_size && f.offset <= offset && offset - f.offset < l.types[f.type].size)
                    found = &f;
            }
            if (found)
            {
                path += std::string(".") + l.Text(found->name);
                offset -= found->offset;
                type = found->type;
                continue;
            }
        }
        path += "+" + std::to_string(offset);
        return true;
    }
}
// The offset of a path in a value of a type of this program. False when a
// name of the path is gone.
bool Resolve(const Layout &l, uint32_t type, const std::string &path, uint64_t &offset, bool object_elements)
{
    size_t at = 0;
    offset = 0;
    bool first = true;
    while (at < path.size())
    {
        const auto &t = l.types[type];
        if (path[at] == '[')
        {
            const size_t end = path.find(']', at);
            if (end == std::string::npos)
                return false;
            const uint64_t index = strtoull(path.c_str() + at + 1, nullptr, 10);
            if (first && object_elements && (t.kind != Array))
                offset += index * t.size;
            else if (t.kind == Array)
            {
                offset += index * l.types[t.a].size;
                type = t.a;
            }
            else
                offset += index * t.size;
            at = end + 1;
        }
        else if (path[at] == '.')
        {
            size_t end = at + 1;
            while (end < path.size() && path[end] != '.' && path[end] != '[' && path[end] != '+')
                ++end;
            // A name may itself hold a dot or a bracket (a base, an unnamed
            // member): try the longest field name that matches here.
            const FieldRecord *found = nullptr;
            size_t length = 0;
            if (t.kind == Struct || t.kind == Union)
                for (uint32_t i = 0; i < t.field_count; ++i)
                {
                    const auto &f = l.fields[t.field_begin + i];
                    const size_t n = strlen(l.Text(f.name));
                    if (n > length && !path.compare(at + 1, n, l.Text(f.name)) &&
                        (at + 1 + n == path.size() || strchr(".[+", path[at + 1 + n])))
                    {
                        found = &f;
                        length = n;
                    }
                }
            if (!found)
                return false;
            offset += found->offset;
            type = found->type;
            at += 1 + length;
            (void)end;
        }
        else if (path[at] == '+')
        {
            offset += strtoull(path.c_str() + at + 1, nullptr, 10);
            return true;
        }
        else
            return false;
        first = false;
    }
    return true;
}

// ---- Pointers in the stream -------------------------------------------------------------
enum Tag : unsigned { Null = 0, ToObject = 1, ToVariable = 2, ToSymbol = 3, ToGuest = 4, ToRegion = 5, ToString = 6, ToLibrary = 7, ToNumber = 8 };

struct Object
{
    uint64_t address = 0, bytes = 0;
    uint32_t type = 0;
    uint64_t count = 0; // elements of the type
    uint64_t tail = 0;  // bytes of the block behind them, as they are
    uint64_t live = ~0ull; // how many of those bytes are in use; the rest goes out as zeros
    std::string origin; // the first place that pointed at it, for a failure
};

// ---- The writer -----------------------------------------------------------------------
struct Writer
{
    Layout &l;
    const Outside &outside;
    std::vector<Object> objects;
    std::map<uint64_t, uint32_t> object_at; // start address -> index
    std::vector<uint32_t> queue;
    std::string failure;
    FieldNumbers numbers;
    // Where the walk stands, for a failure that names its place.
    // The variable or object first, then a step for every field and index
    // down from it. Put together only when something has to be said: the
    // walk takes millions of steps.
    struct Step
    {
        const char *name; // a field; null for an index; "" for a pair of a map
        long long index;
    };
    std::string root;
    std::vector<Step> where;

    Writer(Layout &layout_, const Outside &o) : l(layout_), outside(o) {}

    void SetWhere(const std::string &text)
    {
        root = text;
        where.clear();
    }
    std::string Where() const
    {
        std::string text = root;
        for (const auto &step : where)
            text += !step.name ? "[" + std::to_string(step.index) + "]"
                    : !*step.name ? "{" + std::to_string(step.index) + "}" : std::string(".") + step.name;
        return text;
    }
    // A check wants every refusal and not the first one: the walk goes on.
    bool collect = false;
    std::vector<std::string> failures;
    std::map<std::string, unsigned> refusal_counts;
    std::map<std::string, std::string> samples;
    bool Fail(const std::string &text)
    {
        if (collect)
        {
            // One line per place, whatever its index and the address.
            std::string place = Where(), shape, told = text;
            std::string sample;
            if (const size_t colon = told.find(": "); colon != std::string::npos && told.find("neither") != std::string::npos)
            {
                sample = told.substr(colon);
                told.resize(colon);
            }
            for (size_t i = 0; i < place.size(); ++i)
            {
                shape += place[i];
                if (place[i] == '[')
                    while (i + 1 < place.size() && place[i + 1] != ']')
                        ++i;
            }
            const std::string line = told + " (" + shape + ")";
            if (++refusal_counts[line] == 1 && failures.size() < 200)
            {
                failures.push_back(line);
                samples[line] = sample;
            }
            return true;
        }
        if (failure.empty())
            failure = text + " (" + Where() + ")";
        return false;
    }
    // The object an address lies in. One past the end belongs to an object
    // as well (the end of a buffer), but the allocator may have put the next
    // block right there, and then the address is the start of that block:
    // nothing tells the two apart, and a pointer to the start of a block is
    // what nearly every pointer is. A pointer that really means the end of
    // the block before is counted when the state is loaded (EndPointers),
    // so that it is found and gets a rule.
    int FindObject(uint64_t address) const
    {
        auto it = object_at.upper_bound(address);
        if (it == object_at.begin())
            return -1;
        --it;
        const auto &o = objects[it->second];
        return address - o.address <= o.bytes ? (int)it->second : -1;
    }
    // The type an object really has: the class its vtable names.
    uint32_t DynamicType(uint32_t type, uint64_t address)
    {
        if (!Polymorphic(type))
            return type;
        const uint64_t vptr = *reinterpret_cast<const uint64_t *>(address);
        const auto *symbol = l.SymbolAt(vptr);
        if (!symbol || vptr < l.image_begin || vptr >= l.image_end)
            return ~0u;
        int status = 0;
        const char *mangled = l.Text(symbol->name);
        char *text = abi::__cxa_demangle(mangled[0] == '_' && mangled[1] == '_' ? mangled + 1 : mangled, nullptr, nullptr, &status);
        if (!text || strncmp(text, "vtable for ", 11))
        {
            free(text);
            return ~0u;
        }
        std::string name = text + 11;
        free(text);
        auto found = l.type_by_name.find(name);
        if (found == l.type_by_name.end())
            found = l.type_by_name.find(name + "@" + l.Text(symbol->object));
        return found == l.type_by_name.end() ? ~0u : found->second;
    }
    // A pointer of the state: what it means. Registers the block it owns.
    // `pointee` is the type the field says it points at.
    bool Meet(uint64_t value, uint32_t pointee, uint32_t rule, uint64_t rule_count, bool have_count)
    {
        if (!value)
            return true;
        ++numbers.pointers;
        // An end field borrows a block found through its owner. If that
        // owner has not been met yet, the end may look like the start of
        // another allocation; do not discover or type that neighbour here.
        if (field_end)
            return true;
        uint32_t index = 0;
        if (l.VariableAt(value, index))
            return true;
        const uint32_t kind = l.types[pointee].kind;
        int known = FindObject(value);
        if (known >= 0 && value == objects[known].address + objects[known].bytes && objects[known].bytes &&
            malloc_size(reinterpret_cast<const void *>(value)))
            known = -1;
        if (known >= 0)
        {
            // A block met first through a pointer that says less about it (its
            // bytes, its first member) is what the pointer that says more says.
            Object &o = objects[known];
            // Met first through a pointer that did not know how many of its
            // elements are in use: now it is known.
            if (o.address == value && have_count && (rule == Count || rule == Bytes) && o.live == ~0ull &&
                rule_count * l.types[o.type].size <= o.bytes)
            {
                o.count = rule_count;
                o.tail = o.bytes - o.count * l.types[o.type].size;
                o.live = 0;
            }
            if (o.address != value || kind != Struct || l.types[pointee].size <= l.types[o.type].size ||
                l.types[pointee].size > o.bytes || (l.types[o.type].kind != Scalar && o.count != 1))
                return true;
            const uint32_t type = DynamicType(pointee, value);
            if (type == ~0u || l.types[type].rule == OfHost || l.types[type].rule == Skip)
                return true;
            o.type = type;
            o.count = Polymorphic(type) || l.types[type].rule == Tail ? 1 : o.bytes / l.types[type].size;
            o.tail = o.bytes - o.count * l.types[type].size;
            queue.push_back((uint32_t)known);
            return true;
        }
        if (RegionAt(value)) return true;
        unsigned processor = 0, guest = 0;
        if (outside.guest && outside.guest(outside.context, reinterpret_cast<const void *>(value), processor, guest))
            return true;
        if (value >= l.image_begin && value < l.image_end)
            return true;
        if (Interned().count(value))
            return true;
        const size_t block = malloc_size(reinterpret_cast<const void *>(value));
        if (!block)
            return true; // decided when it is written: by then every block is known
        Object object;
        object.address = value;
        object.bytes = block;
        uint32_t type = pointee;
        if (rule == Bytes || rule == CString || ((kind == Void || kind == Opaque) && rule == Tail))
            type = l.byte_type;
        else if (kind == Void || kind == Opaque)
            return true; // someone who knows its type owns it, or the write refuses it
        type = DynamicType(type, value);
        if (type == ~0u)
            return Fail("a pointer to an object of a class the table does not know (" + std::string(l.TypeName(pointee)) + ")");
        if (l.types[type].rule == OfHost || l.types[type].rule == Skip)
            return Fail(std::string("a pointer to an object of the host (") + l.TypeName(type) + ") without a rule for the field");
        object.type = type;
        const uint64_t size = l.types[type].size;
        if (!size)
            return Fail(std::string("a pointer to a type without a size: ") + l.TypeName(type));
        if (rule == Tail || l.types[type].rule == Tail || Polymorphic(type))
            object.count = 1; // one object, whatever the allocator rounded its block to
        else if (have_count && (rule == Count || rule == Bytes))
            object.count = rule_count;
        else
            object.count = block / size;
        if (object.count * size > block)
            return Fail(std::string("a block smaller than what points at it says: ") + l.TypeName(type));
        // A block keeps its size, so a capacity kept beside it stays true.
        // What lies behind the last element may be a part nobody typed and
        // is written as it is; behind a length a rule names it is unused,
        // holds whatever was there before, and is written as zeros.
        object.tail = object.bytes - object.count * size;
        if (have_count && (rule == Count || rule == Bytes))
            object.live = 0;
        else if (have_count && rule == Tail)
            object.live = rule_count < object.tail ? rule_count : object.tail;
        object.origin = Where();
        object_at[value] = (uint32_t)objects.size();
        objects.push_back(object);
        queue.push_back((uint32_t)objects.size() - 1);
        numbers.object_bytes += object.bytes;
        return true;
    }
    bool Polymorphic(uint32_t type) const
    {
        const auto *t = &l.types[type];
        while (t->kind == Struct && t->field_count)
        {
            const auto &f = l.fields[t->field_begin];
            if (l.types[f.type].kind == Vtable)
                return true;
            if (f.offset || strncmp(l.Text(f.name), "(base ", 6))
                return false;
            t = &l.types[f.type];
        }
        return false;
    }
    uint64_t Sibling(uint32_t struct_type, uint64_t base, const char *name, bool &found)
    {
        const auto &t = l.types[struct_type];
        for (uint32_t i = 0; i < t.field_count; ++i)
        {
            const auto &f = l.fields[t.field_begin + i];
            if (!strcmp(l.Text(f.name), name) && l.types[f.type].kind == Scalar && l.types[f.type].size <= 8)
            {
                uint64_t value = 0;
                memcpy(&value, reinterpret_cast<const void *>(base + f.offset), l.types[f.type].size);
                found = true;
                return value;
            }
        }
        found = false;
        return 0;
    }
    // Follows every pointer in a value.
    bool Walk(uint32_t type, uint64_t address)
    {
        if (!failure.empty())
            return false;
        if (l.Flat(type))
            return true;
        const auto &t = l.types[type];
        if (t.kind == Array)
        {
            const uint64_t size = l.types[t.a].size;
            if (l.Flat(t.a))
                return true;
            for (uint32_t i = 0; i < t.b; ++i)
            {
                where.push_back({nullptr, (long long)i});
                const bool okay = Walk(t.a, address + i * size);
                where.pop_back();
                if (!okay)
                    return false;
            }
            return true;
        }
        if (t.kind == Pointer)
            return Meet(*reinterpret_cast<const uint64_t *>(address), t.a, None, 0, false);
        if (t.kind == Struct)
        {
            InStruct in_struct(*this);
            for (uint32_t i = 0; i < t.field_count; ++i)
            {
                const auto &f = l.fields[t.field_begin + i];
                if (f.rule == OfHost || f.rule == Skip || f.rule == Custom || f.rule == Bind)
                    continue;
                field_end = f.rule == End;
                where.push_back({l.Text(f.name), -1});
                bool okay = true;
                const auto &ft = l.types[f.type];
                // Asked once: the environment is a list that is searched.
                static const char *const trace = getenv("NEO98_STATE_TRACE");
                if (trace && strstr(Where().c_str(), trace))
                    fprintf(stderr, "NEO98 STATE trace %s: type %s kind %u rule %u value %llx block %zu\n", Where().c_str(),
                            l.TypeName(f.type), ft.kind, f.rule, ft.size == 8 ? *reinterpret_cast<const unsigned long long *>(address + f.offset) : 0ull,
                            ft.size == 8 ? malloc_size(*reinterpret_cast<void *const *>(address + f.offset)) : 0);
                if ((ft.kind == Pointer || ft.kind == Address) && f.rule == Headed)
                {
                    // The block starts a head before what the field points at.
                    const uint64_t value = *reinterpret_cast<const uint64_t *>(address + f.offset);
                    const uint64_t head = strtoull(l.Text(f.argument), nullptr, 10);
                    if (value && malloc_size(reinterpret_cast<const void *>(value - head)))
                        okay = Meet(value - head, l.byte_type, Bytes, 0, false);
                }
                else if (ft.kind == Array && f.rule == Count)
                {
                    // An array inside the structure with its live length in
                    // another field: what lies behind it is stale.
                    bool found = false;
                    uint64_t count = Sibling(type, address, l.Text(f.argument), found);
                    if (!found)
                        okay = Fail(std::string("the rule names a field that is not a number: ") + l.Text(f.argument));
                    if (count > ft.b)
                        count = ft.b;
                    for (uint64_t i = 0; okay && i < count && !l.Flat(ft.a); ++i)
                    {
                        where.push_back({nullptr, (long long)i});
                        okay = Walk(ft.a, address + f.offset + i * l.types[ft.a].size);
                        where.pop_back();
                    }
                }
                else if (ft.kind == Pointer && f.rule == As)
                {
                    auto named = l.type_by_name.find(l.Text(f.argument));
                    okay = named == l.type_by_name.end() ? Fail(std::string("the rule names a type the table does not have: ") + l.Text(f.argument))
                                                         : Meet(*reinterpret_cast<const uint64_t *>(address + f.offset), named->second, None, 0, false);
                }
                else if (ft.kind == Pointer && (f.rule == Count || f.rule == Bytes || f.rule == Tail || f.rule == CString))
                {
                    const uint64_t value = *reinterpret_cast<const uint64_t *>(address + f.offset);
                    bool found = true, counted = f.rule == Count || f.rule == Bytes;
                    uint64_t count = 0;
                    if (counted)
                        count = Sibling(type, address, l.Text(f.argument), found);
                    else if (f.rule == Tail && *l.Text(f.argument) && value && malloc_size(reinterpret_cast<const void *>(value)))
                    {
                        // The head of the block says how much of what lies behind it is in use.
                        count = Sibling(ft.a, value, l.Text(f.argument), found);
                        counted = found;
                    }
                    if (!found)
                        okay = Fail(std::string("the rule names a field that is not a number: ") + l.Text(f.argument));
                    else
                        okay = Meet(value, ft.a, f.rule, count, counted);
                }
                else
                    okay = Walk(f.type, address + f.offset);
                where.pop_back();
                if (!okay)
                    return false;
            }
            return true;
        }
        if (t.kind == Union)
            return Fail("a union with a pointer in it");
        if (t.kind == Address)
            return Meet(*reinterpret_cast<const uint64_t *>(address), l.void_type, None, 0, false);
        if (t.kind == Vector)
        {
            // Its elements are an array with a count: the block keeps its
            // size and what lies behind the last element is unused.
            const uint64_t begin = Word(address), end = Word(address + 8), size = l.types[l.fields[t.field_begin].type].size;
            return !begin || !size || Meet(begin, l.fields[t.field_begin].type, Count, (end - begin) / size, true);
        }
        if (t.kind == Shared && Word(address) && !malloc_size(reinterpret_cast<const void *>(Word(address))) &&
            FindObject(Word(address)) < 0)
        {
            // make_shared keeps the object inside the block of its count: it
            // is an object of the state where it lies, as large as its type.
            const uint64_t object = Word(address), block = Word(address + 8);
            const uint32_t type = DynamicType(l.fields[t.field_begin].type, object);
            const size_t room = block ? malloc_size(reinterpret_cast<const void *>(block)) : 0;
            if (type == ~0u || !room || object < block || object + l.types[type].size > block + room)
                return Fail("a shared pointer to an object that is no block and lies in no count");
            Object made;
            made.address = object;
            made.type = type;
            made.count = 1;
            made.bytes = l.types[type].size;
            made.origin = Where();
            object_at[object] = (uint32_t)objects.size();
            objects.push_back(made);
            queue.push_back((uint32_t)objects.size() - 1);
            numbers.object_bytes += made.bytes;
            return true;
        }
        if (t.kind == Unique || t.kind == Shared)
            return Meet(Word(address), l.fields[t.field_begin].type, None, 0, false);
        if (t.kind == Map)
        {
            const auto &key = l.fields[t.field_begin], &mapped = l.fields[t.field_begin + 1];
            uint64_t count = 0;
            for (uint64_t node = Word(address + Tree::Begin); node != address + Tree::End; node = Tree::Next(node), ++count)
            {
                where.push_back({"", (long long)count});
                const bool okay = Walk(key.type, node + key.offset) && Walk(mapped.type, node + mapped.offset);
                where.pop_back();
                if (!okay || count > Word(address + Tree::Size))
                    return okay && Fail("a map whose nodes do not end");
            }
            return true;
        }
        return true; // a function or a vtable: a symbol, decided when written
    }

    // ---- writing ----
    Out out;
    std::map<uint32_t, uint32_t> type_ids; // type of this program -> number in the stream
    std::vector<uint32_t> type_order;
    uint32_t TypeId(uint32_t type)
    {
        auto found = type_ids.find(type);
        if (found != type_ids.end())
            return found->second;
        const uint32_t id = (uint32_t)type_order.size();
        type_ids[type] = id;
        type_order.push_back(type);
        const auto &t = l.types[type];
        if (t.kind == Pointer || t.kind == Array)
            TypeId(t.a);
        if (t.kind == Struct || t.kind == Union || t.kind >= Vector)
            for (uint32_t i = 0; i < t.field_count; ++i)
                TypeId(l.fields[t.field_begin + i].type);
        return id;
    }
    bool PutPointer(uint64_t value)
    {
        if (!value)
        {
            out.U8(Null);
            return true;
        }
        uint32_t index = 0;
        // A string a load copied for this process is that string, wherever the allocator put it.
        if (Interned().count(value) && (!field_end || EndObject(value) < 0))
        {
            out.U8(ToString);
            out.Str(reinterpret_cast<const char *>(value));
            ++numbers.strings;
            return true;
        }
        int object = FindObject(value);
        // The end of an object where another block of the allocator begins is that block, and not this object.
        // The end of one block is the start of the next when the allocator
        // put one there, and nothing in the two addresses tells them apart.
        // A field with the rule `end` means the end; every other
        // pointer means the block that starts there, which is what nearly
        // every pointer at the start of a block means (two buffers of one
        // structure, allocated one after the other, lie like that all the
        // time). A pointer to the end of a block with nothing behind it is
        // written as that end and counted by the load (EndPointers): that
        // is how a field that needs the rule is found.
        if (const int before = EndObject(value); before >= 0)
        {
            if (field_end)
                object = before;
            else if (object == before && malloc_size(reinterpret_cast<const void *>(value)))
                object = -1;
        }
        if (object >= 0)
        {
            const auto &o = objects[object];
            std::string path;
            const uint64_t typed = o.count * l.types[o.type].size;
            if (o.tail && value - o.address > typed)
                path = "~" + std::to_string(value - o.address - typed);
            else if (!PathOf(l, o.type, o.count, value - o.address, path))
                return Fail("a pointer into an object at no field of it");
            out.U8(ToObject);
            out.U32((uint32_t)object);
            out.Str(path);
            ++numbers.to_objects;
            return true;
        }
        if (const auto *variable = l.VariableAt(value, index))
        {
            std::string path;
            if (!PathOf(l, variable->type, 1, value - l.slide - variable->address, path))
                return Fail("a pointer into a variable at no field of it");
            out.U8(ToVariable);
            out.Str(l.Text(variable->name));
            out.Str(l.Text(variable->source));
            out.Str(path);
            ++numbers.to_variables;
            return true;
        }
        if (const auto *region = RegionAt(value))
        {
            out.U8(ToRegion);
            out.Str(region->name);
            out.U64(value - region->base);
            return true;
        }
        unsigned processor = 0, guest = 0;
        if (outside.guest && outside.guest(outside.context, reinterpret_cast<const void *>(value), processor, guest))
        {
            out.U8(ToGuest);
            out.U32(processor);
            out.U32(guest);
            ++numbers.to_guest;
            return true;
        }
        if (Interned().count(value))
        {
            const char *text = reinterpret_cast<const char *>(value);
            out.U8(ToString);
            out.Str(text);
            ++numbers.strings;
            return true;
        }
        if (value >= l.image_begin && value < l.image_end)
        {
            // A string of the program has no name of its own: its text is what it is.
            if (string_field && value < l.writable_begin)
            {
                out.U8(ToString);
                out.Str(reinterpret_cast<const char *>(value));
                ++numbers.strings;
                return true;
            }
            const auto *symbol = l.SymbolAt(value);
            if (!symbol)
                return Fail("a pointer into the program at no symbol: " + Hex(value));
            const uint64_t offset = value - l.slide - symbol->address;
            // A function is meant at its start, a table of a class by its
            // name, a variable as far as it reaches. Anything else is a
            // constant without a name, which another build has elsewhere.
            const char *name = l.Text(symbol->name);
            const bool table = !strncmp(name, "__ZTV", 5) && offset <= 4096;
            if (offset && !table && offset > symbol->size)
                return Fail(std::string("a pointer into the program behind the symbol ") + name + "+" + std::to_string(offset));
            out.U8(ToSymbol);
            out.Str(name);
            out.Str(l.Text(symbol->object));
            out.U32((uint32_t)offset);
            ++numbers.to_symbols;
            return true;
        }
        // A function or a variable of a library the program is linked with
        // (the engine, the system): its name there.
        Dl_info info;
        if (dladdr(reinterpret_cast<const void *>(value), &info) && info.dli_sname && info.dli_saddr == reinterpret_cast<const void *>(value) &&
            dlsym(RTLD_DEFAULT, info.dli_sname) == info.dli_saddr)
        {
            out.U8(ToLibrary);
            out.Str(info.dli_sname);
            ++numbers.to_symbols;
            return true;
        }
        // For the one who reads the refusal: the object that lies before it.
        std::string near;
        if (auto it = object_at.upper_bound(value); it != object_at.begin())
        {
            --it;
            const auto &o = objects[it->second];
            near = std::string(", ") + std::to_string(value - o.address) + " bytes into a block of " + std::to_string(o.bytes) +
                   " (" + l.TypeName(o.type) + ")";
        }
        return Fail("a pointer to memory that is neither the state's nor the program's: " + Hex(value) +
                    (malloc_size(reinterpret_cast<const void *>(value)) ? " (a block of the allocator)" : "") + near);
    }
    // Set while the pointer being written is one to characters.
    bool string_field = false;
    // The field being written has the rule `end`; the structure it is in.
    bool field_end = false;
    struct InStruct
    {
        Writer &w;
        const bool end;
        explicit InStruct(Writer &writer) : w(writer), end(w.field_end) { w.field_end = false; }
        ~InStruct() { w.field_end = end; }
    };
    // The object that ends exactly where a pointer points.
    int EndObject(uint64_t value) const
    {
        if (!value)
            return -1;
        const int object = FindObject(value - 1);
        return object >= 0 && objects[object].bytes && objects[object].address + objects[object].bytes == value ? object : -1;
    }
    // The third guard: a number among the bytes written as they are that is
    // an address of this process. Nothing typed it as a pointer, so it went
    // out as a number and means nothing in the process that loads.
    //
    // A number may look like an address by chance, and memory that was
    // allocated and never filled holds addresses someone left there. So
    // only what a kept pointer looks like counts: the start of a block of
    // the allocator, of a variable or of a symbol. An address into the
    // middle of something is listed as weak and does not count. Even so a
    // number can be the start of a block by chance; the test asks twice.
    std::vector<std::string> suspects, weak;
    // The allocator rounds a block up, and what lies behind the bytes that
    // were asked for is what the last owner of that memory left there.
    // How many bytes at the end of a block that may be: the block minus the
    // smallest request the allocator answers with a block of this size.
    static uint64_t Slack(uint64_t block)
    {
#ifdef __linux__
        // The Linux registry records requested bytes, without allocator padding.
        return 0;
#else
        uint64_t low = block / 2, high = block;
        while (low + 1 < high)
        {
            const uint64_t middle = (low + high) / 2;
            (malloc_good_size(middle) >= block ? high : low) = middle;
        }
        return block - high;
#endif
    }
    void Scan(uint64_t address, uint64_t bytes)
    {
        // Only a check reads the bytes again; a save does not pay for it.
        if (!collect)
            return;
        for (uint64_t at = (address + 7) & ~7ull; at + 8 <= address + bytes; at += 8)
        {
            const uint64_t value = *reinterpret_cast<const uint64_t *>(at);
            if (value < 0x100000000ull || value >= 0x800000000000ull)
                continue;
            uint32_t index = 0;
            const char *what = nullptr;
            bool strong = false;
            unsigned processor = 0, guest = 0;
            if (value >= l.image_begin && value < l.image_end)
            {
                const auto *variable = l.VariableAt(value, index);
                const auto *symbol = l.SymbolAt(value);
                what = variable ? "a variable of the port" : "the program";
                strong = (variable && variable->address + l.slide == value) || (symbol && symbol->address + l.slide == value);
            }
            else if (const int object = FindObject(value); object >= 0)
            {
                what = "an object of the state";
                strong = objects[object].address == value;
            }
            else if (outside.guest && outside.guest(outside.context, reinterpret_cast<const void *>(value), processor, guest))
                what = "guest memory";
            else if (malloc_size(reinterpret_cast<const void *>(value)))
            {
                what = "a block of the host";
                strong = true;
            }
            else if (RegionAt(value))
                what = "a region";
            if (!what)
                continue;
            auto &list = strong ? suspects : weak;
            ++(strong ? numbers.suspects : numbers.weak);
            if (list.size() < 4096)
                list.push_back(Where() + "+" + std::to_string(at - address) + " -> " + what);
        }
    }
    bool Put(uint32_t type, uint64_t address)
    {
        if (!failure.empty())
            return false;
        const auto &t = l.types[type];
        if (l.Flat(type))
        {
            out.Bytes(reinterpret_cast<const void *>(address), t.size);
            Scan(address, t.size);
            return true;
        }
        if (t.kind == Array)
        {
            const uint64_t size = l.types[t.a].size;
            for (uint32_t i = 0; i < t.b; ++i)
            {
                where.push_back({nullptr, (long long)i});
                const bool okay = Put(t.a, address + i * size);
                where.pop_back();
                if (!okay)
                    return false;
            }
            return true;
        }
        if (t.kind == Pointer)
        {
            // Text is `char` (int1s in the table). Bytes (`unsigned char`: a font, a table) in
            // the constants of the program are meant by their address, and
            // go by the symbol they lie in; taken for text they came back
            // as the empty string behind their first zero.
            string_field = l.types[t.a].kind == Scalar && l.types[t.a].size == 1 && !strcmp(l.TypeName(t.a), "int1s");
            const bool okay = PutPointer(*reinterpret_cast<const uint64_t *>(address));
            string_field = false;
            return okay;
        }
        if (t.kind == Address)
        {
            // A number that holds an address: what it points at. A small
            // number that points at nothing is a number (an identifier, a
            // guest address), and goes out as one.
            const uint64_t value = *reinterpret_cast<const uint64_t *>(address);
            uint32_t index = 0;
            unsigned processor = 0, guest = 0;
            if (value && value < 0x100000000ull && FindObject(value) < 0 && !l.VariableAt(value, index) &&
                !(outside.guest && outside.guest(outside.context, reinterpret_cast<const void *>(value), processor, guest)))
            {
                out.U8(ToNumber);
                out.U64(value);
                return true;
            }
            return PutPointer(value);
        }
        if (t.kind == Vector)
        {
            const uint64_t begin = Word(address), end = Word(address + 8), size = l.types[l.fields[t.field_begin].type].size;
            out.U64(begin && size ? (end - begin) / size : 0);
            return PutPointer(begin && size && end > begin ? begin : 0);
        }
        if (t.kind == Unique || t.kind == Shared)
            return PutPointer(Word(address));
        if (t.kind == Map)
        {
            const auto &key = l.fields[t.field_begin], &mapped = l.fields[t.field_begin + 1];
            out.U64(Word(address + Tree::Size));
            uint64_t count = 0;
            for (uint64_t node = Word(address + Tree::Begin); node != address + Tree::End; node = Tree::Next(node), ++count)
            {
                where.push_back({"", (long long)count});
                const bool okay = Put(key.type, node + key.offset) && Put(mapped.type, node + mapped.offset);
                where.pop_back();
                if (!okay)
                    return false;
            }
            return count == Word(address + Tree::Size) || Fail("a map that holds another number of nodes than it says");
        }
        if (t.kind == String)
        {
            out.Str(*reinterpret_cast<const std::string *>(address));
            return true;
        }
        if (t.kind == Function || t.kind == Vtable)
        {
            // A member function pointer of two words: the first is the function.
            const bool okay = PutPointer(*reinterpret_cast<const uint64_t *>(address));
            if (okay && t.size > 8)
                out.Bytes(reinterpret_cast<const void *>(address + 8), t.size - 8);
            return okay;
        }
        if (t.kind == Struct)
        {
            InStruct in_struct(*this);
            for (uint32_t i = 0; i < t.field_count; ++i)
            {
                const auto &f = l.fields[t.field_begin + i];
                if (f.rule == OfHost || f.rule == Skip || f.rule == Custom || f.rule == Bind)
                    continue;
                field_end = f.rule == End;
                where.push_back({l.Text(f.name), -1});
                bool okay;
                if (f.bit_size)
                {
                    // A bit field: its storage unit, as the bytes it lies in.
                    uint64_t unit = 0;
                    memcpy(&unit, reinterpret_cast<const void *>(address + f.bit_offset / 8), (f.bit_offset % 8 + f.bit_size + 7) / 8);
                    out.U64((unit >> (f.bit_offset % 8)) & (f.bit_size >= 64 ? ~0ull : (1ull << f.bit_size) - 1));
                    okay = true;
                }
                else if (l.types[f.type].kind == Array && f.rule == Count)
                {
                    bool found = false;
                    uint64_t count = Sibling(type, address, l.Text(f.argument), found);
                    const auto &ft = l.types[f.type];
                    if (count > ft.b)
                        count = ft.b;
                    out.U32((uint32_t)count);
                    okay = true;
                    for (uint64_t i = 0; okay && i < count; ++i)
                    {
                        where.push_back({nullptr, (long long)i});
                        okay = Put(ft.a, address + f.offset + i * l.types[ft.a].size);
                        where.pop_back();
                    }
                }
                else
                    okay = Put(f.type, address + f.offset);
                where.pop_back();
                if (!okay)
                    return false;
            }
            return true;
        }
        return Fail(std::string("a value of a kind that cannot be written: ") + l.TypeName(type));
    }
};

// The types of the stream as the stream describes them.
Writer *last_writer = nullptr;
bool collect_wanted = false;
struct StreamField
{
    std::string name;
    uint32_t type, offset, bit_offset, bit_size, rule;
};
struct StreamType
{
    std::string name;
    uint32_t kind, size, a, b;
    bool flat;
    std::vector<StreamField> fields;
    int here = -1;    // the type of this program with that name, or none
    int matched_for = -2;                     // the type here its fields were matched with
    std::vector<const FieldRecord *> matched; // each field's counterpart there, or null
    int same = -1;    // 1: the same bytes mean the same here
};
} // namespace

// ---- The table ---------------------------------------------------------------------------
bool SetLayout(const void *bytes, size_t size, char *error, size_t error_bytes)
{
    auto *made = new Layout;
    made->bytes.assign(static_cast<const unsigned char *>(bytes), static_cast<const unsigned char *>(bytes) + size);
    const auto *base = made->bytes.data();
    const auto *header = reinterpret_cast<const Header *>(base);
    unsigned char mine[16];
    BuildIdentity(mine);
    if (size < sizeof(Header) || memcmp(header->magic, "A98LAY04", 8))
    {
        delete made;
        Say(error, error_bytes, "the layout table beside the program is not one");
        return false;
    }
    if (memcmp(header->uuid, mine, 16))
    {
        delete made;
        Say(error, error_bytes, "the layout table beside the program belongs to another build of it");
        return false;
    }
    made->header = header;
    made->types = reinterpret_cast<const TypeRecord *>(base + sizeof(Header));
    made->fields = reinterpret_cast<const FieldRecord *>(made->types + header->types);
    made->variables = reinterpret_cast<const VariableRecord *>(made->fields + header->fields);
    made->symbols = reinterpret_cast<const SymbolRecord *>(made->variables + header->variables);
    made->strings = reinterpret_cast<const char *>(made->symbols + header->symbols);
    if (reinterpret_cast<const unsigned char *>(made->strings) + header->strings > base + size)
    {
        delete made;
        Say(error, error_bytes, "the layout table beside the program is cut short");
        return false;
    }
    made->flat.assign(header->types, 0);
    for (uint32_t i = 0; i < header->types; ++i)
    {
        if (made->types[i].kind == Scalar && made->types[i].size == 1 && !made->byte_type)
            made->byte_type = i;
        if (made->types[i].kind == Void)
            made->void_type = i;
    }
#ifdef __APPLE__
    made->slide = (uint64_t)_dyld_get_image_vmaddr_slide(0);
    const auto *image = reinterpret_cast<const mach_header_64 *>(_dyld_get_image_header(0));
    made->image_begin = reinterpret_cast<uint64_t>(image);
    const auto *command = reinterpret_cast<const load_command *>(image + 1);
    for (unsigned i = 0; i < image->ncmds; ++i)
    {
        if (command->cmd == LC_SEGMENT_64)
        {
            const auto *segment = reinterpret_cast<const segment_command_64 *>(command);
            const uint64_t begin = segment->vmaddr + made->slide, end = begin + segment->vmsize;
            if (strcmp(segment->segname, "__PAGEZERO") && strcmp(segment->segname, "__LINKEDIT") && end > made->image_end)
                made->image_end = end;
            if (!strcmp(segment->segname, "__DATA"))
                made->writable_begin = begin;
        }
        command = reinterpret_cast<const load_command *>(reinterpret_cast<const char *>(command) + command->cmdsize);
    }
#else
    const auto image = LinuxStatePlatform::MainImage();
    made->slide = image.slide; made->image_begin = image.begin;
    made->image_end = image.end; made->writable_begin = image.writable;
#endif
    for (uint32_t i = 0; i < header->variables; ++i)
    {
        made->by_address.push_back(i);
        made->variable_by_name[std::string(made->Text(made->variables[i].name)) + "@" + made->Text(made->variables[i].source) +
                               "#" + made->Text(made->variables[i].symbol)] = i;
    }
    std::sort(made->by_address.begin(), made->by_address.end(),
              [&](uint32_t a, uint32_t b) { return made->variables[a].address < made->variables[b].address; });
    for (uint32_t i = 0; i < header->types; ++i)
        made->type_by_name[made->Text(made->types[i].name)] = i;
    for (uint32_t i = 0; i < header->symbols; ++i)
        made->symbol_by_name[std::string(made->Text(made->symbols[i].name)) + "@" + made->Text(made->symbols[i].object)] =
            made->symbols[i].address;
    delete layout;
    layout = made;
    return true;
}
bool HaveLayout()
{
    return layout != nullptr;
}
void Region(const char *name, void *base, unsigned long long bytes)
{
    auto &regions = Regions();
    bool imported = false;
    for (auto it = regions.begin(); it != regions.end(); ++it)
        if (it->name == name)
        {
            imported = it->imported;
            regions.erase(it);
            break;
        }
    const uint64_t address = reinterpret_cast<uint64_t>(base);
    auto at = std::lower_bound(regions.begin(), regions.end(), address,
                               [](const RegionRecord &r, uint64_t value) { return r.base < value; });
    regions.insert(at, {name, address, bytes, imported});
}
FieldNumbers LastFields()
{
    return last_numbers;
}
const char *SymbolOf(unsigned long long address)
{
    if (!layout || address < layout->image_begin || address >= layout->image_end)
        return nullptr;
    const auto *symbol = layout->SymbolAt(address);
    return symbol ? layout->Text(symbol->name) : nullptr;
}
void ImportedRegion(const char *name, const void *base, unsigned long long bytes)
{
    Region(name, const_cast<void *>(base), bytes);
    for (auto &region : Regions())
        if (region.name == name) region.imported = true;
}
bool HaveImportedRegion(const char *name, unsigned long long bytes)
{
    for (const auto &region : Regions())
        if (region.imported && region.name == name && region.bytes == bytes) return true;
    return false;
}
unsigned EndPointers()
{
    return last_end_pointers;
}
void CollectRefusals(bool wanted)
{
    collect_wanted = wanted;
}
unsigned Refusals(void (*each)(void *context, const char *text), void *context)
{
    if (!last_writer)
        return 0;
    for (const auto &text : last_writer->failures)
        each(context, (text + " x" + std::to_string(last_writer->refusal_counts[text]) + last_writer->samples[text]).c_str());
    return (unsigned)last_writer->failures.size();
}
unsigned Suspects(void (*each)(void *context, const char *place, bool counts), void *context, bool all)
{
    if (!last_writer)
        return 0;
    for (const auto &text : last_writer->suspects)
        each(context, text.c_str(), true);
    if (all)
        for (const auto &text : last_writer->weak)
            each(context, text.c_str(), false);
    return (unsigned)last_writer->numbers.suspects;
}

// ---- Regions --------------------------------------------------------------------------------
// A region is address space with pages the port opened here and there. A
// state carries the open runs without their zero pages.
bool SaveRegions(Sink &sink)
{
    static constexpr uint64_t Page = 4096;
    auto put = [&](uint64_t value) { return sink.Write(&value, 8); };
    if (!put(Regions().size()))
        return false;
    // Address order speeds up pointer lookup, but names keep the stream
    // identical when ASLR and heap allocation place imports elsewhere.
    std::vector<const RegionRecord *> ordered;
    for (const auto &region : Regions()) ordered.push_back(&region);
    std::sort(ordered.begin(), ordered.end(), [](const auto *a, const auto *b) { return a->name < b->name; });
    for (const auto *entry : ordered)
    {
        const auto &region = *entry;
        const uint64_t length = region.name.size();
        if (!put(length) || !sink.Write(region.name.data(), length) || !put(region.bytes))
            return false;
        if (region.imported)
        {
            if (!put(0)) return false;
            continue;
        }
#ifdef __linux__
        for (const auto &run : LinuxStatePlatform::WritableRuns(region.base, region.bytes))
        {
            const uint64_t from = run.begin, to = run.end;
#else
        mach_vm_address_t address = region.base;
        while (address < region.base + region.bytes)
        {
            mach_vm_size_t size = 0;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object = MACH_PORT_NULL;
            if (mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                               reinterpret_cast<vm_region_info_t>(&info), &count, &object) != KERN_SUCCESS ||
                address >= region.base + region.bytes)
                break;
            uint64_t from = address < region.base ? region.base : address, to = address + size;
            if (to > region.base + region.bytes)
                to = region.base + region.bytes;
            address += size;
            if ((info.protection & (VM_PROT_READ | VM_PROT_WRITE)) != (VM_PROT_READ | VM_PROT_WRITE))
                continue;
#endif
            // The run is open; its pages go out in pieces without the empty ones.
            if (!put(1) || !put(from - region.base) || !put(to - from))
                return false;
            for (uint64_t at = from; at < to;)
            {
                auto zero = [&](uint64_t page) {
                    const auto *words = reinterpret_cast<const uint64_t *>(page);
                    for (uint64_t i = 0; i < Page / 8; ++i)
                        if (words[i])
                            return false;
                    return true;
                };
                if (zero(at))
                {
                    at += Page;
                    continue;
                }
                uint64_t end = at + Page;
                while (end < to && !zero(end))
                    end += Page;
                if (!put(2) || !put(at - region.base) || !put(end - at) || !sink.Write(reinterpret_cast<const void *>(at), end - at))
                    return false;
                at = end;
            }
        }
        if (!put(0))
            return false;
    }
    return true;
}
bool LoadRegions(Source &source, char *error, size_t error_bytes)
{
    auto get = [&](uint64_t &value) { return source.Read(&value, 8); };
    uint64_t count = 0;
    if (!get(count) || count > 65536)
        return false;
    for (uint64_t i = 0; i < count; ++i)
    {
        uint64_t length = 0, bytes = 0;
        char name[128] = {};
        if (!get(length) || length >= sizeof name || !source.Read(name, length) || !get(bytes))
            return false;
        RegionRecord *region = nullptr;
        for (auto &known : Regions())
            if (known.name == name)
                region = &known;
        if (!strncmp(name, "windows-import/", 15) && !HaveImportedRegion(name, bytes))
        {
            Say(error, error_bytes, "the state requires different imported Windows data from C:");
            return false;
        }
        if (!region)
        {
            // This process has not reserved it yet.
#ifdef __linux__
            void *reserved = LinuxSharedMemory::Allocate(bytes, nullptr, PROT_NONE);
            if (reserved)
#else
            void *reserved = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
            if (reserved != MAP_FAILED)
#endif
            {
                Region(name, reserved, bytes);
                region = RegionAt(reinterpret_cast<uint64_t>(reserved));
            }
        }
        if (!region || region->bytes < bytes)
        {
            Say(error, error_bytes, std::string("the state has a region this NEO98 does not have: ") + name);
            return false;
        }
        for (;;)
        {
            uint64_t kind = 0, offset = 0, size = 0;
            if (!get(kind))
                return false;
            if (!kind)
                break;
            if (region->imported) return false;
            if (!get(offset) || !get(size) || offset > bytes || size > bytes - offset)
                return false;
            void *place = reinterpret_cast<void *>(region->base + offset);
            if (kind == 1)
            {
                if (mprotect(place, size, PROT_READ | PROT_WRITE))
                    return false;
                // What this process had there is not the state's.
                madvise(place, size, MADV_FREE);
                memset(place, 0, size);
            }
            else if (!source.Read(place, size))
                return false;
        }
    }
    return true;
}

// ---- Save ---------------------------------------------------------------------------------
namespace
{
constexpr uint64_t FieldsMagic = 0x3130444c45494638ull; // "8FIELD01"
} // namespace
bool SaveFields(Sink &sink, const void *first, const Outside &outside, char *error, size_t error_bytes)
{
    if (!layout)
    {
        Say(error, error_bytes, "the host has no layout table (the file beside the program is missing)");
        return false;
    }
    Layout &l = *layout;
    const auto began = std::chrono::steady_clock::now();
    delete last_writer;
    auto *writer = last_writer = new Writer(l, outside);
    Writer &w = *writer;
    w.collect = collect_wanted;
    // The object the loading host makes itself comes first, whoever points at it.
    std::vector<uint32_t> roots;
    for (uint32_t i = 0; i < l.header->variables; ++i)
        if (l.variables[i].rule != OfHost && l.variables[i].rule != Skip && l.variables[i].rule != Custom &&
            !ImportedAddress(l.variables[i].address + l.slide))
            roots.push_back(i);
    int first_object = -1;
    auto drain = [&] {
        while (!w.queue.empty() && w.failure.empty())
        {
            const uint32_t index = w.queue.back();
            w.queue.pop_back();
            const Object o = w.objects[index];
            w.SetWhere(o.origin + " -> " + l.TypeName(o.type));
            const uint64_t size = l.types[o.type].size;
            if (!l.Flat(o.type))
                for (uint64_t i = 0; i < o.count && w.failure.empty(); ++i)
                    w.Walk(o.type, o.address + i * size);
        }
    };
    if (first)
    {
        // The object the loading host has made already, whoever points at it.
        auto found = l.type_by_name.find("X86::Application");
        if (found == l.type_by_name.end())
        {
            Say(error, error_bytes, "the layout table does not know a process");
            return false;
        }
        Object object;
        object.address = reinterpret_cast<uint64_t>(first);
        object.type = found->second;
        object.count = 1;
        object.bytes = l.types[object.type].size;
        object.origin = "the first process";
        w.object_at[object.address] = 0;
        w.objects.push_back(object);
        w.queue.push_back(0);
        first_object = 0;
        drain();
    }
    for (uint32_t index : roots)
    {
        if (!w.failure.empty())
            break;
        const auto &v = l.variables[index];
        w.SetWhere(std::string(l.Text(v.name)) + "@" + l.Text(v.source));
        if (v.rule == Count && l.types[v.type].kind == Pointer)
        {
            // An array whose length is another variable of the same file.
            const VariableRecord *length = nullptr;
            for (uint32_t i = 0; i < l.header->variables && !length; ++i)
                if (!strcmp(l.Text(l.variables[i].source), l.Text(v.source)) && l.types[l.variables[i].type].kind == Scalar &&
                    l.types[l.variables[i].type].size <= 8)
                {
                    const char *name = l.Text(l.variables[i].name), *last = strrchr(name, ':');
                    if (!strcmp(last ? last + 1 : name, l.Text(v.argument)))
                        length = &l.variables[i];
                }
            uint64_t count = 0;
            if (length)
                memcpy(&count, reinterpret_cast<const void *>(length->address + l.slide), l.types[length->type].size);
            if (!length)
                w.Fail(std::string("the rule names a variable that is not a number: ") + l.Text(v.argument));
            else
                w.Meet(*reinterpret_cast<const uint64_t *>(v.address + l.slide), l.types[v.type].a, Count, count, true);
        }
        else
            w.Walk(v.type, v.address + l.slide);
        drain();
    }
    if (!w.failure.empty())
    {
        Say(error, error_bytes, w.failure);
        return false;
    }
    // The values. Types are numbered as they come up; the table of them is
    // written last and stands first in the stream.
    const auto walked = std::chrono::steady_clock::now();
    Out &out = w.out;
    // As large as the last one was: a state of some fifty megabytes does not
    // grow there in doublings.
    static size_t last_bytes = 0;
    out.data.reserve(last_bytes + last_bytes / 8 + (1u << 20));
    out.U32((uint32_t)roots.size());
    for (uint32_t index : roots)
    {
        const auto &v = l.variables[index];
        w.SetWhere(std::string(l.Text(v.name)) + "@" + l.Text(v.source));
        out.Str(l.Text(v.name));
        out.Str(l.Text(v.source));
        out.Str(l.Text(v.symbol));
        out.U32(w.TypeId(v.type));
        // Whether a static that is made on first use has been made.
        out.U8(v.guard ? *reinterpret_cast<const unsigned char *>(v.guard + l.slide) & 1 : 1);
        if (!w.Put(v.type, v.address + l.slide))
            break;
    }
    out.U32((uint32_t)w.objects.size());
    for (size_t i = 0; i < w.objects.size() && w.failure.empty(); ++i)
    {
        const Object &o = w.objects[i];
        w.SetWhere(o.origin + " -> " + l.TypeName(o.type));
        out.U32(w.TypeId(o.type));
        out.U64(o.count);
        out.U64(o.tail);
        // How the block is aligned, as far as it matters: a block the port
        // hands to a processor as guest pages lies on a page of the host.
        unsigned aligned = 0;
        while (aligned < 16 && !((o.address >> aligned) & 1))
            ++aligned;
        out.U8(aligned);
        const uint64_t size = l.types[o.type].size;
        // What the scan leaves out: the end of the block, as far as the allocator may have rounded it.
        const uint64_t whole = w.collect && malloc_size(reinterpret_cast<const void *>(o.address)) == o.bytes
                                   ? o.bytes - Writer::Slack(o.bytes) : o.bytes;
        if (l.Flat(o.type))
        {
            out.Bytes(reinterpret_cast<const void *>(o.address), o.count * size);
            w.Scan(o.address, o.count * size < whole ? o.count * size : whole);
        }
        else
            for (uint64_t k = 0; k < o.count && w.failure.empty(); ++k)
                w.Put(o.type, o.address + k * size);
        if (o.tail && o.live < o.tail)
        {
            out.Bytes(reinterpret_cast<const void *>(o.address + o.count * size), o.live);
            out.data.insert(out.data.end(), o.tail - o.live, 0);
            w.Scan(o.address + o.count * size, o.live);
        }
        else if (o.tail)
        {
            out.Bytes(reinterpret_cast<const void *>(o.address + o.count * size), o.tail);
            if (o.count * size < whole)
                w.Scan(o.address + o.count * size, whole - o.count * size);
        }
    }
    if (!w.failure.empty())
    {
        Say(error, error_bytes, w.failure);
        return false;
    }
    Out head;
    head.U64(FieldsMagic);
    head.U32((uint32_t)(first_object < 0 ? ~0u : first_object));
    head.U32((uint32_t)w.type_order.size());
    for (uint32_t type : w.type_order)
    {
        const auto &t = l.types[type];
        head.Str(l.TypeName(type));
        head.U32(t.kind);
        head.U32(t.size);
        head.U32((t.kind == Pointer || t.kind == Array) ? w.type_ids[t.a] : t.a);
        head.U32(t.b);
        head.U8(l.Flat(type) ? 1 : 0);
        const bool record = t.kind == Struct || t.kind == Union || t.kind >= Vector;
        head.U32(record ? t.field_count : 0);
        for (uint32_t i = 0; record && i < t.field_count; ++i)
        {
            const auto &f = l.fields[t.field_begin + i];
            head.Str(l.Text(f.name));
            head.U32(w.type_ids[f.type]);
            head.U32(f.offset);
            head.U32(f.bit_offset);
            head.U32(f.bit_size);
            head.U32(f.rule);
        }
    }
    if (!w.failures.empty())
    {
        Say(error, error_bytes, w.failures[0]);
        return false;
    }
    w.numbers.variables = (unsigned)roots.size();
    w.numbers.objects = (unsigned)w.objects.size();
    w.numbers.types = (unsigned)w.type_order.size();
    last_numbers = w.numbers;
    last_bytes = out.data.size();
    if (getenv("NEO98_STATE_TIMES"))
        fprintf(stderr, "NEO98 STATE fields times: the walk %.1f ms, the values %.1f ms\n",
                std::chrono::duration<double, std::milli>(walked - began).count(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - walked).count());
    const uint64_t head_bytes = head.data.size(), body_bytes = out.data.size();
    return sink.Write(&head_bytes, 8) && sink.Write(&body_bytes, 8) && sink.Write(head.data.data(), head.data.size()) &&
           sink.Write(out.data.data(), out.data.size());
}
bool SavePointer(Sink &sink, const void *pointer, unsigned long long requested_bytes,
                 unsigned long long &mapped_bytes, char *error, size_t error_bytes)
{
    mapped_bytes = 0;
    if (!last_writer)
        return false;
    Writer &w = *last_writer;
    w.out.data.clear();
    w.SetWhere("host bytes behind guest pages");
    w.failure.clear();
    const uint64_t value = reinterpret_cast<uint64_t>(pointer);
    if (!w.PutPointer(value))
    {
        Say(error, error_bytes, w.failure);
        return false;
    }
    // The CPU joins pages of blocks that happen to lie next to each other
    // into one run. A load puts each block somewhere else, so the run is cut
    // where its block ends; the caller writes the rest as its own run.
    uint64_t room = requested_bytes;
    if (const int object = w.FindObject(value); object >= 0 && w.objects[object].address <= value)
        room = w.objects[object].address + w.objects[object].bytes - value;
    else
        for (const auto &region : Regions())
            if (value - region.base < region.bytes)
            {
                room = region.base + region.bytes - value;
                break;
            }
    uint64_t slice = requested_bytes;
    if (room < slice)
        slice = (value & 4095) ? 0 : room & ~uint64_t(4095);
    if (!slice)
    {
        Say(error, error_bytes, "shared guest pages reach past the end of their block");
        return false;
    }
    mapped_bytes = slice;
    const uint32_t bytes = (uint32_t)w.out.data.size();
    return sink.Write(&bytes, 4) && sink.Write(w.out.data.data(), bytes);
}

// ---- Load ---------------------------------------------------------------------------------
struct FieldReader
{
    Layout &l;
    Outside outside;
    std::vector<unsigned char> head_bytes, body_bytes;
    std::vector<StreamType> types;
    struct Loaded
    {
        uint32_t type = 0;
        uint64_t count = 0, tail = 0;
        unsigned aligned = 0;
        size_t data = 0;      // where its values stand in the body
        uint64_t address = 0; // where it lives in this process, once made
        bool filled = false;
    };
    std::vector<Loaded> objects;
    std::vector<std::pair<uint32_t, std::string>> object_references;
    unsigned parse_depth = 0;
    bool validating = false;
    bool observing = false;
    unsigned native_depth = 0;
    std::map<std::string, unsigned> observed_pointer_tags;
    std::map<std::string, uint64_t> observed;
    std::map<std::string, std::pair<uint32_t, std::string>> observed_objects;
    std::string ObservationPath() const
    {
        std::string result;
        for (const auto *part : path) { if (!result.empty()) result += '.'; result += *part; }
        return result;
    }
    bool ObserveFlat(uint32_t id, const unsigned char *data, const std::string &prefix, unsigned depth = 0)
    {
        if (depth > 256) return Fail("a flat type recurses beyond its input limit");
        const auto &t = types[id];
        if (t.kind == Scalar && t.size <= 8) {
            uint64_t value = 0; memcpy(&value, data, t.size); observed[prefix] = value;
        } else if (t.kind == Struct) {
            for (const auto &f : t.fields) {
                if (f.type >= types.size() || f.offset > t.size || types[f.type].size > t.size - f.offset)
                    return Fail("a flat field exceeds its structure");
                if (!ObserveFlat(f.type, data + f.offset, prefix + '.' + f.name, depth + 1)) return false;
            }
        } else if (t.kind == Array) {
            if (t.a >= types.size() || (types[t.a].size && t.b > t.size / types[t.a].size))
                return Fail("a flat array exceeds its type");
            for (unsigned i = 0; i < t.b; ++i)
                if (!ObserveFlat(t.a, data + (uint64_t)i * types[t.a].size, prefix + '.' + std::to_string(i), depth + 1)) return false;
        }
        return true;
    }
    unsigned end_pointers = 0, unknown_types = 0, unknown_variables = 0;
    std::vector<std::string> end_places;
    std::unordered_map<uint64_t, SharedCount *> kept; // an object behind shared pointers -> its count
    std::vector<uint32_t> queue;
    struct GuestFix
    {
        uint64_t slot;
        unsigned processor, guest;
    };
    std::vector<GuestFix> guests;
    std::string failure;
    // Where the reader stands: the variable or object, and the fields down
    // from it. Put together only when something has to be said.
    std::string where;
    std::vector<const std::string *> path;
    std::string Place() const
    {
        std::string text = where;
        for (const auto *name : path)
            text += "." + *name;
        return text;
    }
    explicit FieldReader(Layout &layout_) : l(layout_) {}

    bool Fail(const std::string &text)
    {
        if (failure.empty())
            failure = text + (Place().empty() ? "" : " (" + Place() + ")");
        return false;
    }
    // Whether the bytes of a flat type of the stream are the bytes of the
    // type here.
    bool Same(uint32_t id)
    {
        auto &t = types[id];
        if (t.same >= 0)
            return t.same == 1;
        t.same = 0;
        if (t.here < 0)
            return false;
        const auto &h = l.types[t.here];
        bool same = h.size == t.size && h.kind == t.kind;
        if (same && t.kind == Array)
            same = h.b == t.b && types[t.a].here == (int)h.a && Same(t.a);
        if (same && (t.kind == Struct || t.kind == Union))
        {
            same = h.field_count == t.fields.size();
            for (uint32_t i = 0; same && i < h.field_count; ++i)
            {
                const auto &hf = l.fields[h.field_begin + i];
                const auto &sf = t.fields[i];
                same = sf.name == l.Text(hf.name) && sf.offset == hf.offset && sf.bit_size == hf.bit_size &&
                       sf.bit_offset == hf.bit_offset && sf.rule == hf.rule &&
                       types[sf.type].here == (int)hf.type && Same(sf.type);
            }
        }
        t.same = same ? 1 : 0;
        return same;
    }
    // Flat bytes of the stream into a value here, field by field by name.
    void Convert(uint32_t id, const unsigned char *from, uint32_t here, uint64_t to)
    {
        auto &t = types[id];
        const auto &h = l.types[here];
        if (t.here == (int)here && Same(id))
        {
            memcpy(reinterpret_cast<void *>(to), from, t.size);
            return;
        }
        if (t.kind == Scalar && h.kind == Scalar)
        {
            // A number of another width: by its value.
            if ((t.a & 1) && (h.a & 1))
            {
                double value = 0;
                if (t.size == 4) { float f; memcpy(&f, from, 4); value = f; }
                else if (t.size == 8) memcpy(&value, from, 8);
                if (h.size == 4) { float f = (float)value; memcpy(reinterpret_cast<void *>(to), &f, 4); }
                else if (h.size == 8) memcpy(reinterpret_cast<void *>(to), &value, 8);
                return;
            }
            uint64_t value = 0;
            memcpy(&value, from, t.size < 8 ? t.size : 8);
            if ((t.a & 2) && t.size < 8 && (value >> (t.size * 8 - 1)) & 1)
                value |= ~0ull << (t.size * 8);
            memcpy(reinterpret_cast<void *>(to), &value, h.size < 8 ? h.size : 8);
            return;
        }
        if (t.kind == Array && h.kind == Array)
        {
            const uint32_t n = t.b < h.b ? t.b : h.b;
            for (uint32_t i = 0; i < n; ++i)
                Convert(t.a, from + (size_t)i * types[t.a].size, h.a, to + (uint64_t)i * l.types[h.a].size);
            return;
        }
        if ((t.kind == Struct || t.kind == Union) && (h.kind == Struct || h.kind == Union))
        {
            for (const auto &sf : t.fields)
                for (uint32_t i = 0; i < h.field_count; ++i)
                {
                    const auto &hf = l.fields[h.field_begin + i];
                    if (sf.name != l.Text(hf.name))
                        continue;
                    // A former flat record may now give this member to its
                    // host or codec. Read its old bytes without importing it.
                    if (sf.rule == OfHost || sf.rule == Skip || sf.rule == Custom || sf.rule == Bind ||
                        hf.rule == OfHost || hf.rule == Skip || hf.rule == Custom || hf.rule == Bind)
                        break;
                    if (sf.bit_size || hf.bit_size)
                    {
                        uint64_t unit = 0;
                        memcpy(&unit, from + sf.bit_offset / 8, (sf.bit_offset % 8 + sf.bit_size + 7) / 8);
                        SetBits(to, hf, (unit >> (sf.bit_offset % 8)) & (sf.bit_size >= 64 ? ~0ull : (1ull << sf.bit_size) - 1));
                    }
                    else
                        Convert(sf.type, from + sf.offset, hf.type, to + hf.offset);
                    break;
                }
            return;
        }
        // Another kind of thing under the same name: the value of this process stays.
    }
    void SetBits(uint64_t base, const FieldRecord &f, uint64_t value)
    {
        if (!f.bit_size)
            return;
        const unsigned bytes = (f.bit_offset % 8 + f.bit_size + 7) / 8;
        uint64_t unit = 0;
        memcpy(&unit, reinterpret_cast<const void *>(base + f.bit_offset / 8), bytes);
        const uint64_t mask = (f.bit_size >= 64 ? ~0ull : (1ull << f.bit_size) - 1) << (f.bit_offset % 8);
        unit = (unit & ~mask) | ((value << (f.bit_offset % 8)) & mask);
        memcpy(reinterpret_cast<void *>(base + f.bit_offset / 8), &unit, bytes);
    }
    // An object of the stream as memory of this process.
    uint64_t Make(uint32_t index, uint64_t present)
    {
        auto &o = objects[index];
        if (o.address)
            return o.address;
        const auto &t = types[o.type];
        if (t.here < 0)
        {
            Fail("the state holds an object of a type this NEO98 no longer has: " + t.name);
            return 0;
        }
        const uint64_t size = l.types[t.here].size, bytes = o.count * size + o.tail;
        // Always a block of its own, of the size the object had: the block a
        // place of this process points at already may be larger, or someone
        // else's, and a block that changed its size is another state when
        // it is saved again. Only the first process is taken over
        // (LoadFields), because the host holds it.
        (void)present;
        if (o.aligned >= 12 && bytes >= 16384)
        {
            // On a page, as it lay: see the writer. Only when the allocator
            // gives a block of the very size for it; a block that is larger
            // than its object would be another object when saved again.
            void *block = nullptr;
#ifdef __linux__
            if (o.aligned <= 12 && !(bytes & 4095)) block = LinuxSharedMemory::Allocate(bytes);
            else
#endif
            if (posix_memalign(&block, (size_t)1 << (o.aligned > 16 ? 16 : o.aligned), bytes)) block = nullptr;
            if (block && malloc_size(block) != bytes)
            {
#ifdef __linux__
                if (!LinuxSharedMemory::Free(block))
#endif
                free(block);
                block = nullptr;
            }
            if (block)
                memset(block, 0, bytes);
            o.address = reinterpret_cast<uint64_t>(block);
        }
        if (!o.address)
            o.address = reinterpret_cast<uint64_t>(calloc(1, bytes ? bytes : 1));
        if (!o.address)
        {
            Fail("no memory for an object of the state");
            return 0;
        }
        queue.push_back(index);
        return o.address;
    }
    bool GetPointer(In &in, uint64_t slot, bool write, bool declared_end = false)
    {
        const unsigned tag = in.U8();
        if (observing) observed_pointer_tags[ObservationPath()] = tag;
        uint64_t value = 0;
        const uint64_t present = slot && write ? *reinterpret_cast<const uint64_t *>(slot) : 0;
        if (tag == Null)
            value = 0;
        else if (tag == ToObject)
        {
            const uint32_t index = in.U32();
            const std::string path = in.Str();
            if (observing) observed_objects[ObservationPath()] = {index, path};
            if (validating && !write) object_references.emplace_back(index, path);
            if (write && index >= objects.size())
                return Fail("a pointer to an object the state does not have");
            if (write)
            {
                const uint64_t base = Make(index, path.empty() ? present : 0);
                if (!base)
                    return false;
                uint64_t offset = 0;
                if (!path.empty() && path[0] == '~')
                    offset = objects[index].count * l.types[types[objects[index].type].here].size +
                             strtoull(path.c_str() + 1, nullptr, 10);
                else if (!Resolve(l, (uint32_t)types[objects[index].type].here, path, offset, true))
                    return Fail("a pointer to a field this NEO98 no longer has: " + types[objects[index].type].name + path);
                // One past the end of the block: see Writer::FindObject.
                if (!declared_end && offset &&
                    offset == objects[index].count * l.types[types[objects[index].type].here].size + objects[index].tail)
                {
                    ++end_pointers;
                    if (end_places.size() < 16)
                        end_places.push_back(Place());
                }
                value = base + offset;
            }
        }
        else if (tag == ToVariable)
        {
            const std::string name = in.Str(), source = in.Str(), path = in.Str();
            if (write)
            {
                const VariableRecord *found = nullptr;
                for (uint32_t i = 0; i < l.header->variables && !found; ++i)
                    if (name == l.Text(l.variables[i].name) && source == l.Text(l.variables[i].source))
                        found = &l.variables[i];
                uint64_t offset = 0;
                if (!found || !Resolve(l, found->type, path, offset, false))
                    return Fail("a pointer to a variable this NEO98 no longer has: " + name + path);
                value = found->address + l.slide + offset;
            }
        }
        else if (tag == ToSymbol)
        {
            const std::string name = in.Str(), object = in.Str();
            const uint32_t offset = in.U32();
            if (write)
            {
                auto found = l.symbol_by_name.find(name + "@" + object);
                if (found != l.symbol_by_name.end())
                    value = found->second + l.slide + offset;
                else if (present)
                    value = present; // what this process installed stays
                else
                    return Fail("the state names a function or table this NEO98 no longer has: " + name);
            }
        }
        else if (tag == ToGuest)
        {
            const unsigned processor = in.U32(), guest = in.U32();
            if (write)
            {
                guests.push_back({slot, processor, guest});
                return !in.bad;
            }
        }
        else if (tag == ToRegion)
        {
            const std::string name = in.Str();
            const uint64_t offset = in.U64();
            if (write)
            {
                bool found = false;
                for (const auto &region : Regions())
                    if (region.name == name && offset < region.bytes)
                    {
                        value = region.base + offset;
                        found = true;
                    }
                if (!found)
                    return Fail("the state points into a region this NEO98 does not have: " + name);
            }
        }
        else if (tag == ToString)
        {
            const std::string text = in.Str();
            if (write)
            {
                // The same text the place holds already is the program's own.
                if (present && present >= l.image_begin && present < l.image_end &&
                    text == reinterpret_cast<const char *>(present))
                    value = present;
                else
                {
                    char *copy = static_cast<char *>(malloc(text.size() + 1));
                    memcpy(copy, text.c_str(), text.size() + 1);
                    value = reinterpret_cast<uint64_t>(copy);
                    Interned().insert(value);
                }
            }
        }
        else if (tag == ToNumber)
            value = in.U64();
        else if (tag == ToLibrary)
        {
            const std::string name = in.Str();
            if (write)
            {
                value = reinterpret_cast<uint64_t>(dlsym(RTLD_DEFAULT, name.c_str()));
                if (!value && present)
                    value = present;
                else if (!value)
                    return Fail("the state names a function of a library this NEO98 does not have: " + name);
            }
        }
        else
            return Fail("the stream holds a pointer of no known kind");
        if (in.bad)
            return Fail("the stream ends inside a pointer");
        if (write && slot)
            *reinterpret_cast<uint64_t *>(slot) = value;
        return true;
    }
    // Reads a value of a type of the stream. With `here` < 0 or `to` zero it
    // is only read over.
    bool Get(In &in, uint32_t id, int here, uint64_t to, bool declared_end = false)
    {
        struct Depth { unsigned &depth; ~Depth() { --depth; } } guard{parse_depth};
        ++parse_depth;
        if (validating && parse_depth > 256) return Fail("the field types recurse beyond their input limit");
        if (!failure.empty())
            return false;
        auto &t = types[id];
        const bool write = here >= 0 && to;
        if (t.flat)
        {
            const auto *from = in.Take(t.size);
            if (!from)
                return Fail("the stream ends inside a value");
            if (observing && !ObserveFlat(id, from, ObservationPath())) return false;
            if (write)
                Convert(id, from, (uint32_t)here, to);
            return true;
        }
        if (t.kind == Array)
        {
            const bool array = write && l.types[here].kind == Array;
            for (uint32_t i = 0; i < t.b; ++i)
            {
                const std::string index = std::to_string(i);
                if (observing) path.push_back(&index);
                const bool into = array && i < l.types[here].b;
                const bool okay = Get(in, t.a, into ? (int)l.types[here].a : -1, into ? to + (uint64_t)i * l.types[l.types[here].a].size : 0);
                if (observing) path.pop_back();
                if (!okay)
                    return false;
            }
            return true;
        }
        if (t.kind == Pointer || t.kind == Function || t.kind == Vtable || t.kind == Address)
        {
            const bool pointer = write && (l.types[here].kind == Pointer || l.types[here].kind == Function ||
                                           l.types[here].kind == Vtable || l.types[here].kind == Address);
            if (!GetPointer(in, pointer ? to : 0, pointer, declared_end))
                return false;
            if (t.size > 8)
            {
                const auto *rest = in.Take(t.size - 8);
                if (!rest)
                    return Fail("the stream ends inside a value");
                if (pointer && l.types[here].size == t.size)
                    memcpy(reinterpret_cast<void *>(to + 8), rest, t.size - 8);
            }
            return true;
        }
        if (t.kind >= Vector)
        {
            // The same kind of container here, or the value is read over.
            const bool same = write && l.types[here].kind == t.kind;
            const auto *mine = same ? &l.types[here] : nullptr;
            if (t.kind == Vector)
            {
                const uint64_t count = in.U64();
                if (!GetPointer(in, same ? to : 0, same))
                    return false;
                if (same)
                {
                    const uint64_t begin = Word(to), size = l.types[l.fields[mine->field_begin].type].size;
                    Word(to + 8) = Word(to + 16) = begin ? begin + count * size : 0;
                }
                return true;
            }
            if (t.kind == Unique)
                return GetPointer(in, same ? to : 0, same);
            if (t.kind == Shared)
            {
                if (!GetPointer(in, same ? to : 0, same))
                    return false;
                if (same)
                {
                    const uint64_t object = Word(to);
                    SharedCount *count = nullptr;
                    if (object)
                    {
                        auto found = kept.find(object);
                        if (found == kept.end())
                            count = kept[object] = new Kept;
                        else
                            {
                            count = found->second;
#ifdef __linux__
                            count->_M_add_ref_copy();
#else
                            count->__add_shared();
#endif
                        }
                    }
                    Word(to + 8) = reinterpret_cast<uint64_t>(count);
                }
                return true;
            }
            if (t.kind == Map)
            {
                const uint64_t count = in.U64();
                if (t.fields.size() < 2 || count > in.size)
                    return Fail("the stream holds a map of no known form");
                std::vector<uint64_t> nodes;
                for (uint64_t i = 0; i < count; ++i)
                {
                    uint64_t node = 0;
                    if (same)
                    {
                        node = reinterpret_cast<uint64_t>(calloc(1, l.fields[mine->field_begin + 2].offset));
                        nodes.push_back(node);
                    }
                    if (!Get(in, t.fields[0].type, same ? (int)l.fields[mine->field_begin].type : -1,
                             same ? node + l.fields[mine->field_begin].offset : 0) ||
                        !Get(in, t.fields[1].type, same ? (int)l.fields[mine->field_begin + 1].type : -1,
                             same ? node + l.fields[mine->field_begin + 1].offset : 0))
                        return false;
                }
                if (same)
                    Tree::Make(to, nodes);
                return !in.bad || Fail("the stream ends inside a map");
            }
            const std::string text = in.Str();
            if (same)
                new (reinterpret_cast<void *>(to)) std::string(text);
            return !in.bad || Fail("the stream ends inside a string");
        }
        if (t.kind == Struct)
        {
            const bool record = write && l.types[here].kind == Struct;
            // Which field here each field of the stream is: found by name
            // once for a type, not once for every value of it.
            if (record && t.matched_for != here)
            {
                t.matched.assign(t.fields.size(), nullptr);
                for (size_t k = 0; k < t.fields.size(); ++k)
                    for (uint32_t i = 0; i < l.types[here].field_count && !t.matched[k]; ++i)
                        if (t.fields[k].name == l.Text(l.fields[l.types[here].field_begin + i].name))
                            t.matched[k] = &l.fields[l.types[here].field_begin + i];
                t.matched_for = here;
            }
            for (size_t k = 0; k < t.fields.size(); ++k)
            {
                const auto &sf = t.fields[k];
                if (sf.rule == OfHost || sf.rule == Skip || sf.rule == Custom || sf.rule == Bind)
                    continue;
                const FieldRecord *hf = record ? t.matched[k] : nullptr;
                // A field this program gives to its host, or no longer has: read over.
                if (hf && (hf->rule == OfHost || hf->rule == Skip || hf->rule == Custom || hf->rule == Bind))
                    hf = nullptr;
                path.push_back(&sf.name);
                bool okay = true;
                if (sf.bit_size)
                {
                    const uint64_t value = in.U64();
                    if (hf)
                        SetBits(to, *hf, value);
                }
                else if (types[sf.type].kind == Array && sf.rule == Count)
                {
                    // As many elements as were alive; the rest of the array here stays.
                    const uint32_t count = in.U32(), element = types[sf.type].a;
                    if (validating && (count > types[sf.type].b || count > in.size))
                        return Fail("a counted array exceeds its declared capacity");
                    const bool array = hf && l.types[hf->type].kind == Array;
                    for (uint32_t i = 0; okay && i < count; ++i)
                    {
                        const std::string index = std::to_string(i);
                        if (observing) path.push_back(&index);
                        const bool into = array && i < l.types[hf->type].b;
                        okay = Get(in, element, into ? (int)l.types[hf->type].a : -1,
                                   into ? to + hf->offset + (uint64_t)i * l.types[l.types[hf->type].a].size : 0);
                        if (observing) path.pop_back();
                    }
                }
                else
                    okay = Get(in, sf.type, hf ? (int)hf->type : -1, hf ? to + hf->offset : 0,
                               hf && hf->rule == End);
                path.pop_back();
                if (!okay)
                    return false;
            }
            // A pointer to an object of the host that a variable of the port
            // names in every process (the rasterizer): the one of this process.
            if (record)
                for (uint32_t i = 0; i < l.types[here].field_count; ++i)
                {
                    const auto &f = l.fields[l.types[here].field_begin + i];
                    if (f.rule != Bind)
                        continue;
                    const VariableRecord *named = nullptr;
                    for (uint32_t v = 0; v < l.header->variables && !named; ++v)
                        if (!strcmp(l.Text(l.variables[v].name), l.Text(f.argument)))
                            named = &l.variables[v];
                    if (!named)
                        return Fail(std::string("the rule binds a field to a variable this NEO98 does not have: ") + l.Text(f.argument));
                    Word(to + f.offset) = Word(named->address + l.slide);
                }
            return !in.bad || Fail("the stream ends inside a structure");
        }
        return Fail("the stream holds a value of a kind that cannot be read: " + t.name);
    }
    // Fills every object made and not yet filled, and those they lead to.
    void Drain()
    {
        while (!queue.empty() && failure.empty())
        {
            const uint32_t index = queue.back();
            queue.pop_back();
            auto &o = objects[index];
            if (o.filled)
                continue;
            o.filled = true;
            const auto &t = types[o.type];
            const uint64_t size = l.types[t.here].size, address = o.address, count = o.count, tail = o.tail;
            const uint32_t type = o.type;
            In in{body_bytes.data(), body_bytes.size(), o.data};
            where = "object of " + t.name;
            if (t.flat && Same(type))
            {
                if (const auto *bytes = in.Take(count * t.size))
                    memcpy(reinterpret_cast<void *>(address), bytes, count * t.size);
            }
            else
                for (uint64_t k = 0; k < count && failure.empty(); ++k)
                    Get(in, type, types[type].here, address + k * size);
            if (tail)
                if (const auto *bytes = in.Take(tail))
                    memcpy(reinterpret_cast<void *>(address + count * size), bytes, tail);
        }
    }
};

FieldReader *LoadFields(Source &source, void *first, const Outside &outside, char *error, size_t error_bytes, bool validate_only)
{
    if (!layout)
    {
        Say(error, error_bytes, "the host has no layout table (the file beside the program is missing)");
        return nullptr;
    }
    auto *reader = new FieldReader(*layout);
    FieldReader &r = *reader;
    Layout &l = *layout;
    r.outside = outside;
    r.validating = validate_only;
    uint64_t head_bytes = 0, body_bytes = 0;
    auto fail = [&](const std::string &text) -> FieldReader * {
        Say(error, error_bytes, text);
        delete reader;
        return nullptr;
    };
    if (!source.Read(&head_bytes, 8) || !source.Read(&body_bytes, 8) || head_bytes > (1ull << 31) || body_bytes > (1ull << 33))
        return fail("the stream ends before the fields");
    r.head_bytes.resize(head_bytes);
    r.body_bytes.resize(body_bytes);
    if (!source.Read(r.head_bytes.data(), head_bytes) || !source.Read(r.body_bytes.data(), body_bytes))
        return fail("the stream ends inside the fields");
    In head{r.head_bytes.data(), r.head_bytes.size()};
    if (head.U64() != FieldsMagic)
        return fail("the fields of the state are of another format");
    const uint32_t first_object = head.U32(), type_count = head.U32();
    if (type_count > head_bytes / 25) return fail("the field type count exceeds its input");
    r.types.resize(type_count);
    for (auto &t : r.types)
    {
        t.name = head.Str();
        t.kind = head.U32();
        t.size = head.U32();
        t.a = head.U32();
        t.b = head.U32();
        t.flat = head.U8() != 0;
        const unsigned fields = head.U32();
        if (fields > head_bytes / 24) return fail("the field count exceeds its input");
        t.fields.resize(fields);
        for (auto &f : t.fields)
        {
            f.name = head.Str();
            f.type = head.U32();
            f.offset = head.U32();
            f.bit_offset = head.U32();
            f.bit_size = head.U32();
            f.rule = head.U32();
        }
        auto found = l.type_by_name.find(t.name);
        t.here = found == l.type_by_name.end() ? -1 : (int)found->second;
        if (head.bad)
            return fail("the stream ends inside its types");
    }
    // A field less or a type less is what an update brings. A state whose
    // names are mostly unknown here was written by a program that names
    // things another way (another compiler, another library), and reading
    // what little matches would make a machine out of leftovers.
    unsigned structures = 0, unknown = 0;
    for (const auto &t : r.types)
        if (t.kind == Struct)
        {
            ++structures;
            unknown += t.here < 0 ? 1 : 0;
        }
    if (structures && unknown * 10 > structures)
        return fail("the state names " + std::to_string(unknown) + " of its " + std::to_string(structures) +
                    " structures in a way this NEO98 does not know: it was written by a program built another way");
    r.unknown_types = unknown;
    for (auto &t : r.types)
    {
        if ((t.kind == Pointer || t.kind == Array) && t.a >= type_count)
            return fail("the types of the state name a type they do not have");
        for (const auto &f : t.fields)
            if (f.type >= type_count) return fail("a field names a type the state does not have");
    }
    // The objects first: where each one's values stand. That needs reading
    // over the variables once.
    In body{r.body_bytes.data(), r.body_bytes.size()};
    const uint32_t root_count = body.U32();
    if (root_count > body_bytes / 17) return fail("the variable count exceeds its input");
    struct Root
    {
        std::string name, source, symbol;
        uint32_t type;
        bool made;
        size_t data;
    };
    std::vector<Root> roots(root_count);
    for (auto &root : roots)
    {
        root.name = body.Str();
        root.source = body.Str();
        root.symbol = body.Str();
        root.type = body.U32();
        root.made = body.U8() != 0;
        root.data = body.at;
        if (body.bad || root.type >= type_count || !r.Get(body, root.type, -1, 0))
            return fail(r.failure.empty() ? "the stream ends inside the variables" : r.failure);
    }
    const unsigned object_count = body.U32();
    if (object_count > body_bytes / 21) return fail("the object count exceeds its input");
    r.objects.resize(object_count);
    for (auto &o : r.objects)
    {
        o.type = body.U32();
        o.count = body.U64();
        o.tail = body.U64();
        o.aligned = body.U8();
        o.data = body.at;
        if (body.bad || o.type >= type_count)
            return fail("the stream ends inside the objects");
        const auto &t = r.types[o.type];
        if (o.count > body_bytes || (t.flat && t.size && o.count > body_bytes / t.size))
            return fail("an object count exceeds its input");
        if (t.flat)
            body.Take(o.count * t.size);
        else
            for (uint64_t k = 0; k < o.count; ++k)
                if (!r.Get(body, o.type, -1, 0))
                    return fail(r.failure.empty() ? "the stream ends inside an object" : r.failure);
        body.Take(o.tail);
        if (body.bad)
            return fail("the stream ends inside an object");
    }
    if (body.bad || body.at != body.size || head.bad || head.at != head.size)
        return fail("the field stream has unconsumed or truncated data");
    if (validate_only)
    {
        if (first_object >= r.objects.size()) return fail("the state has no first process object");
        for (const auto &t : r.types)
            for (const auto &f : t.fields)
                if (f.type >= type_count) return fail("a field names a type the state does not have");
        for (const auto &reference : r.object_references)
        {
            if (reference.first >= r.objects.size()) return fail("a pointer names an absent object");
            const auto &o = r.objects[reference.first];
            const auto &t = r.types[o.type];
            if (t.here < 0) return fail("a pointer names an unknown object type");
            const uint64_t size = l.types[t.here].size;
            if ((size && o.count > UINT64_MAX / size) || o.tail > UINT64_MAX - o.count * size)
                return fail("an object size overflows");
            uint64_t offset = 0;
            const auto &path = reference.second;
            if (!path.empty() && path[0] == '~')
            {
                char *end = nullptr;
                const uint64_t tail = strtoull(path.c_str() + 1, &end, 10);
                if (end == path.c_str() + 1 || *end || tail > o.tail)
                    return fail("a pointer exceeds its object's tail");
                offset = o.count * size + tail;
            }
            else if (!Resolve(l, (uint32_t)t.here, path, offset, true))
                return fail("a pointer names an invalid object field");
            if (offset > o.count * size + o.tail) return fail("a pointer exceeds its object");
        }
        // Inspect the native DOS entry without creating objects or assigning
        // a global. A composite UI must have exactly this resumable root.
        const auto &entry = r.objects[first_object];
        if (entry.count != 1 || r.types[entry.type].name != "X86::Application")
            return fail("the combined UI has no native application root");
        r.observing = true;
        In entry_values{r.body_bytes.data(), r.body_bytes.size(), entry.data};
        if (!r.Get(entry_values, entry.type, -1, 0)) return fail(r.failure);
        auto value = [&](const char *name, uint64_t &out) {
            const auto found = r.observed.find(name);
            if (found == r.observed.end()) return false;
            out = found->second; return true;
        };
        uint64_t count = 0, kind = 0, window = 0, phase = 0, unused_b = 0, unused_c = 0;
        uint64_t thread = 0, depth = 0;
        if (!value("current_thread", thread) || thread || !value("threads.0.depth", depth) || depth > 32)
            return fail("the combined UI has an invalid native DOS callback depth");
        r.native_depth = (unsigned)depth;
        if (!value("threads.0.host_frame_count", count) || !count || count > 256 ||
            !value("threads.0.host_frames.0.kind", kind) || kind != 56 ||
            !value("threads.0.host_frames.0.a", window) || !window ||
            !value("threads.0.host_frames.0.b", unused_b) || unused_b ||
            !value("threads.0.host_frames.0.c", unused_c) || unused_c ||
            !value("threads.0.host_frames.0.d", phase) || phase != 1)
            return fail("the combined UI has an invalid native DOS continuation");
        // Structural contract of the remaining native chain. Values are
        // still wire values here; no application or USER global exists yet.
        auto thread_value = [&](const std::string &suffix, uint64_t &out) {
            return value(("threads.0." + suffix).c_str(), out);
        };
        uint64_t last_kind = 56;
        for (unsigned i = 1; i < count; ++i) {
            uint64_t frame_kind = 0, frame_phase = 0;
            const auto frame = std::string("host_frames.") + std::to_string(i) + '.';
            if (!thread_value(frame + "kind", frame_kind) || !thread_value(frame + "d", frame_phase) ||
                !MachineState::DosUiInnerFrameSupported((unsigned)frame_kind, (unsigned)frame_phase))
                return fail("the combined UI has an unsupported inner host continuation");
            last_kind = frame_kind;
        }
        struct FrameValues { unsigned kind=0,a=0,b=0,c=0,d=0,data[4]{}; };
        auto frame_values = [&](unsigned index, FrameValues &f) {
            const auto prefix = std::string("host_frames.") + std::to_string(index) + '.';
            const std::pair<const char *, unsigned *> fields[] = {{"kind",&f.kind},{"a",&f.a},{"b",&f.b},{"c",&f.c},{"d",&f.d},
                {"data.0",&f.data[0]},{"data.1",&f.data[1]},{"data.2",&f.data[2]},{"data.3",&f.data[3]}};
            for (const auto &field : fields) { uint64_t n=0;if(!thread_value(prefix+field.first,n)||n>UINT32_MAX)return false;*field.second=(unsigned)n; }
            return true;
        };
        std::map<unsigned, FrameValues> collected_sheets;
        for (unsigned i=1;i<count;++i) {
            uint64_t k=0,d=0;
            const auto prefix=std::string("host_frames.")+std::to_string(i)+'.';
            if(!thread_value(prefix+"kind",k)||!thread_value(prefix+"d",d))return fail("the combined UI frame is incomplete");
            if(k==61||(k==40&&d==2)) {
                const unsigned at=k==61?i-1:i;
                FrameValues api,collect,sheet,modal;
                if(at+3>=count||!frame_values(at,api)||!frame_values(at+1,collect)||
                   !frame_values(at+2,sheet)||!frame_values(at+3,modal)||
                   !MachineState::DosUiCollectedSheet(api,collect,sheet,modal))
                    return fail("the combined UI has an incomplete PIF collection continuation");
                collected_sheets[at+2]=sheet;
            }
        }
        uint64_t below = 0;
        for (unsigned k = 1; k <= depth; ++k) {
            const auto mark = std::string("call_marks.") + std::to_string(k) + '.';
            uint64_t frames = 0, gate = 0, tail = 0;
            if (!thread_value(mark + "frames", frames) || frames < below || frames > count ||
                !thread_value(mark + "gate", gate) || !thread_value(mark + "tail", tail) ||
                (!gate && !(k == 1 && frames > below)) ||
                (frames > below ? tail != 5 : tail != 2 && tail != 3))
                return fail("the combined UI has an invalid callback continuation chain");
            below = frames;
        }
        uint64_t waiting = 0, block = 0;
        if (!thread_value("host_waiting", waiting) || !thread_value("block", block) ||
            !(depth == 0 && count == 1 && block == 7
              ? !waiting
              : count > below ? waiting && (last_kind == 1 || last_kind == 6 || last_kind == 8) : !waiting))
            return fail("the combined UI has an invalid native wait boundary");
        for (unsigned i = 1; i < 16; ++i) {
            const auto prefix = std::string("threads.") + std::to_string(i) + '.';
            uint64_t extra_depth = 0, extra_frames = 0;
            if (!value((prefix + "depth").c_str(), extra_depth) || extra_depth ||
                !value((prefix + "host_frame_count").c_str(), extra_frames) || extra_frames)
                return fail("the combined UI has a continuation in another native thread");
        }
        const auto dos = r.observed_objects.find("dos_vm");
        if (dos == r.observed_objects.end() || dos->second.first >= r.objects.size())
            return fail("the combined UI has no DOS window owner");
        if (!dos->second.second.empty())
            return fail("the combined UI DOS owner does not point to the object start");
        const auto &owner = r.objects[dos->second.first];
        if (owner.count != 1 || r.types[owner.type].name != "X86::Application::DosVmState")
            return fail("the combined UI has an invalid DOS window owner type");
        r.observed.clear(); r.observed_objects.clear();
        In owner_values{r.body_bytes.data(), r.body_bytes.size(), owner.data};
        if (!r.Get(owner_values, owner.type, -1, 0)) return fail(r.failure);
        uint64_t owner_window = 0, finished = 0, closed = 0, child = 0;
        if (!value("window", owner_window) || owner_window != window ||
            !value("finished", finished) || finished || !value("closed", closed) || closed ||
            !value("waiting_process", child) || child)
            return fail("the combined UI DOS continuation does not own a live window");
        const auto overlay = r.observed.find("voodoo_overlay");
        if (overlay != r.observed.end() && overlay->second)
            return fail("the combined UI has an unsupported Voodoo overlay");
        const std::pair<const char *, unsigned> strings[] = {{"command",1024},{"directory",260},
            {"keyboard_layout",8},{"prelude",2048},{"options",2048}};
        for (const auto &text : strings) {
            bool terminated = false;
            for (unsigned i = 0; i < text.second; ++i) {
                const auto byte = r.observed.find(std::string("launch.") + text.first + '.' + std::to_string(i));
                if (byte == r.observed.end()) return fail("the combined UI launch text is incomplete");
                if (!byte->second) { terminated = true; break; }
            }
            if (!terminated) return fail("the combined UI launch text has no terminator");
        }
        if (!collected_sheets.empty())
        {
            // The integrated native sheet resumes through two permanent
            // registries. Check their typed owners before assigning globals.
            auto launch_values=std::move(r.observed);
            auto observe_object=[&](uint32_t index) {
                r.observed.clear();r.observed_objects.clear();r.observed_pointer_tags.clear();
                const auto&o=r.objects[index];In in{r.body_bytes.data(),r.body_bytes.size(),o.data};
                return r.Get(in,o.type,-1,0);
            };
            auto pointer=[&](const char*name,uint32_t&index) {
                const auto tag=r.observed_pointer_tags.find(name);
                const auto ref=r.observed_objects.find(name);
                if(tag==r.observed_pointer_tags.end()||tag->second!=ToObject||ref==r.observed_objects.end()||
                   !ref->second.second.empty()||ref->second.first>=r.objects.size())return false;
                index=ref->second.first;return true;
            };
            auto registry=[&](const char*name,const char*type,std::map<uint32_t,bool>&members) {
                const Root*head=nullptr;
                for(const auto&root:roots)if(root.name==name&&root.source=="x86_property_sheet.cpp")head=&root;
                if(!head||!head->made)return false;
                In in{r.body_bytes.data(),r.body_bytes.size(),head->data};
                if(in.U8()!=ToObject)return false;
                uint32_t index=in.U32();if(!in.Str().empty()||in.bad)return false;
                for(;;) {
                    if(index>=r.objects.size()||!members.emplace(index,true).second)return false;
                    const auto&o=r.objects[index];
                    if(o.count!=1||r.types[o.type].name!=type||!observe_object(index))return false;
                    uint32_t app=0;if(!pointer("app",app)||app!=first_object)return false;
                    const auto next=r.observed_pointer_tags.find("next");
                    if(next==r.observed_pointer_tags.end())return false;
                    if(next->second==Null)return true;
                    if(!pointer("next",index))return false;
                }
            };
            std::map<uint32_t,bool> works,sheets;
            if(!registry("X86::Application::PropertyWork::head","X86::Application::PropertyWork",works)||
               !registry("X86::(anonymous namespace)::SheetFrame::head",
                         "X86::(anonymous namespace)::SheetFrame@x86_property_sheet.cpp",sheets))
                return fail("the combined UI has an invalid PIF owner registry");
            for(const auto&needed:collected_sheets) {
                bool found=false;
                for(const auto&member:works) {
                    if(!observe_object(member.first))return fail(r.failure);
                    uint64_t thread=0,index=0,kind=0,phase=0,kept=0;
                    if(!value("thread",thread)||!value("index",index)||!value("kind",kind)||
                       !value("phase",phase)||!value("kept",kept))return fail("the combined UI PIF work is incomplete");
                    if(thread||index!=needed.first||kind!=41||phase!=1||kept!=1)continue;
                    uint32_t sheet=0;
                    if(!pointer("sheet",sheet)||!sheets.count(sheet)||!observe_object(sheet))
                        return fail("the combined UI PIF work has no registered sheet owner");
                    uint64_t state=0,hwnd=0;
                    if(!value("state",state)||state!=needed.second.a||!value("hwnd",hwnd)||hwnd!=needed.second.b)
                        return fail("the combined UI PIF work does not own its captured sheet");
                    if(found)return fail("the combined UI PIF work is registered twice");
                    found=true;
                }
                if(!found)return fail("the combined UI PIF sheet has no recorded owner");
            }
            r.observed=std::move(launch_values);
        }
        return reader;
    }
    // The first process is the object the host made.
    if (first && first_object < r.objects.size())
    {
        r.objects[first_object].address = reinterpret_cast<uint64_t>(first);
        r.queue.push_back(first_object);
    }
    r.Drain();
    for (const auto &root : roots)
    {
        if (!r.failure.empty())
            break;
        auto found = l.variable_by_name.find(root.name + "@" + root.source + "#" + root.symbol);
        if (found == l.variable_by_name.end())
        {
            // Another build may give a static another symbol: the name and the file decide then.
            for (uint32_t i = 0; i < l.header->variables; ++i)
                if (root.name == l.Text(l.variables[i].name) && root.source == l.Text(l.variables[i].source))
                {
                    found = l.variable_by_name.find(root.name + "@" + root.source + "#" + l.Text(l.variables[i].symbol));
                    break;
                }
        }
        if (found == l.variable_by_name.end())
        {
            // The static of an inline function is listed under the first file that has it.
            uint32_t only = ~0u, named = 0;
            for (uint32_t i = 0; i < l.header->variables; ++i)
                if (root.name == l.Text(l.variables[i].name) && root.symbol == l.Text(l.variables[i].symbol))
                    only = i, ++named;
            if (named == 1)
                found = l.variable_by_name.find(root.name + "@" + l.Text(l.variables[only].source) + "#" + root.symbol);
        }
        if (found == l.variable_by_name.end())
        {
            ++r.unknown_variables; // a variable this NEO98 no longer has
            continue;
        }
        const auto &v = l.variables[found->second];
        if (v.rule == OfHost || v.rule == Skip || v.rule == Custom || ImportedAddress(v.address + l.slide))
            continue;
        In in{r.body_bytes.data(), r.body_bytes.size(), root.data};
        r.where = root.name + "@" + root.source;
        // A static the saved process had not made yet stays what it is here.
        if (!root.made)
            continue;
        r.Get(in, root.type, (int)v.type, v.address + l.slide);
        if (v.guard)
            *reinterpret_cast<unsigned char *>(v.guard + l.slide) |= 1;
        r.Drain();
    }
    if (!r.failure.empty())
        return fail(r.failure);
    if (r.unknown_variables * 10 > roots.size())
        return fail("the state holds " + std::to_string(r.unknown_variables) + " of its " + std::to_string(roots.size()) +
                    " variables under names this NEO98 does not know: it was written by a program built another way");
    if (r.unknown_types || r.unknown_variables)
        fprintf(stderr, "NEO98 STATE load: %u structures and %u variables of the state are not known here and were read over\n",
                r.unknown_types, r.unknown_variables);
    r.where.clear();
    return reader;
}
bool ValidateFields(Source &source, char *error, size_t error_bytes, unsigned *native_depth)
{
    const Outside outside{};
    auto *reader = LoadFields(source, nullptr, outside, error, error_bytes, true);
    if (!reader) return false;
    if (native_depth) *native_depth = reader->native_depth;
    delete reader;
    return true;
}
bool ValidateDosUiFields(Source &source, const DosVm::Launch &expected, char *error, size_t error_bytes, unsigned *native_depth)
{
    const Outside outside{};
    std::unique_ptr<FieldReader> reader(LoadFields(source, nullptr, outside, error, error_bytes, true));
    if (!reader) return false;
    if (native_depth) *native_depth = reader->native_depth;
    const std::pair<const char *, const char *> texts[] = {{"command",expected.command},{"directory",expected.directory},
        {"keyboard_layout",expected.keyboard_layout},{"prelude",expected.prelude},{"options",expected.options}};
    for (const auto &text : texts)
        for (unsigned i = 0; i <= strlen(text.second); ++i) {
            const auto byte = reader->observed.find(std::string("launch.") + text.first + '.' + std::to_string(i));
            if (byte == reader->observed.end() || byte->second != (unsigned char)text.second[i]) {
                Say(error,error_bytes,"the DOS core launch does not match the UI launch text"); return false;
            }
        }
    const std::pair<const char *, uint64_t> values[] = {{"memory_mb",expected.memory_mb},{"xms",expected.xms},
        {"ems",expected.ems},{"fullscreen",expected.fullscreen},{"close_on_exit",expected.close_on_exit},
        {"idle_sensitivity",expected.idle_sensitivity},{"lines",expected.lines},{"fast_paste",expected.fast_paste},
        {"voodoo_host",expected.voodoo_host}};
    for (const auto &value : values) {
        const auto scalar = reader->observed.find(std::string("launch.") + value.first);
        if (scalar == reader->observed.end() || scalar->second != value.second) {
            Say(error,error_bytes,"the DOS core launch does not match the UI launch values"); return false;
        }
    }
    return true;
}
bool LoadPointer(FieldReader *reader, Source &source, void *&pointer, char *error, size_t error_bytes)
{
    uint32_t bytes = 0;
    unsigned char buffer[1024];
    if (!reader || !source.Read(&bytes, 4) || bytes > sizeof buffer || !source.Read(buffer, bytes))
        return false;
    In in{buffer, bytes};
    uint64_t value = 0;
    if (!reader->GetPointer(in, reinterpret_cast<uint64_t>(&value), true))
    {
        Say(error, error_bytes, reader->failure);
        return false;
    }
    // An object only this pointer reaches.
    reader->Drain();
    if (!reader->failure.empty())
    {
        Say(error, error_bytes, reader->failure);
        return false;
    }
    pointer = reinterpret_cast<void *>(value);
    return true;
}
bool FinishFields(FieldReader *reader, char *error, size_t error_bytes)
{
    if (!reader)
        return false;
    bool okay = true;
    for (const auto &place : reader->end_places)
        fprintf(stderr, "NEO98 STATE load: a pointer to the end of a block at %s\n", place.c_str());
    last_end_pointers = reader->end_pointers;
    for (const auto &fix : reader->guests)
    {
        void *host = reader->outside.host ? reader->outside.host(reader->outside.context, fix.processor, fix.guest) : nullptr;
        if (!host)
        {
            Say(error, error_bytes, "a pointer of the state into guest memory finds no processor");
            okay = false;
            break;
        }
        *reinterpret_cast<void **>(fix.slot) = host;
    }
    delete reader;
    return okay;
}
} // namespace MachineState
#else
namespace MachineState
{
bool SetLayout(const void *, size_t, char *, size_t) { return false; }
bool HaveLayout() { return false; }
void Region(const char *, void *, unsigned long long) {}
void ImportedRegion(const char *, const void *, unsigned long long) {}
bool HaveImportedRegion(const char *, unsigned long long) { return false; }
FieldNumbers LastFields() { return {}; }
unsigned EndPointers() { return 0; }
const char *SymbolOf(unsigned long long) { return nullptr; }
unsigned Suspects(void (*)(void *, const char *, bool), void *, bool) { return 0; }
void CollectRefusals(bool) {}
unsigned Refusals(void (*)(void *, const char *), void *) { return 0; }
bool SaveRegions(Sink &) { return false; }
bool LoadRegions(Source &, char *, size_t) { return false; }
bool SaveFields(Sink &, const void *, const Outside &, char *, size_t) { return false; }
bool SavePointer(Sink &, const void *, unsigned long long, unsigned long long &, char *, size_t) { return false; }
FieldReader *LoadFields(Source &, void *, const Outside &, char *, size_t, bool) { return nullptr; }
bool ValidateFields(Source &, char *, size_t, unsigned *) { return false; }
bool ValidateDosUiFields(Source &, const DosVm::Launch &, char *, size_t, unsigned *) { return false; }
bool LoadPointer(FieldReader *, Source &, void *&, char *, size_t) { return false; }
bool FinishFields(FieldReader *, char *, size_t) { return false; }
} // namespace MachineState
#endif
