// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 version-resource queries, ANSI/Unicode block conversion, and
// language-name handling.
#pragma once

namespace Win98VersionInfo
{
// Guest memory, so a routine can work in the caller's own buffer the way the
// 16-bit original does. Offsets are flat addresses of the running process.
struct Memory
{
    virtual ~Memory() = default;
    virtual unsigned Read8(unsigned address) = 0;
    virtual unsigned Read16(unsigned address) = 0;
    virtual unsigned Read32(unsigned address) = 0;
    virtual void Write8(unsigned address, unsigned value) = 0;
    virtual void Write16(unsigned address, unsigned value) = 0;
    virtual void Write32(unsigned address, unsigned value) = 0;
    // KERNEL.348 hmemcpy and the string moves of the original.
    virtual void Copy(unsigned to, unsigned from, unsigned bytes) = 0;
    // KERNEL.334 IsBadHugeReadPtr, inverted: true when the range may be read.
    virtual bool Readable(unsigned address, unsigned bytes) = 0;
    virtual bool Writable(unsigned address, unsigned bytes) = 0;
};

inline unsigned Align4(unsigned value) { return (value + 3) & ~3u; }
// This header is reached from x86_app.h and therefore from the kernel build,
// which has no C library headers of its own.
inline bool Magic(const unsigned char *bytes, const char *text, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if (bytes[i] != (unsigned char)text[i])
            return false;
    return true;
}

// repne scasw over a wide string, the byte length without the two
// zero bytes of the terminator.
inline unsigned WideBytes(Memory &memory, unsigned address)
{
    unsigned length = 0;
    while (length < 0x10000 && memory.Read16(address + length))
        length += 2;
    return length;
}
// lstrlen over an ANSI key.
inline unsigned AnsiBytes(Memory &memory, unsigned address)
{
    unsigned length = 0;
    while (length < 0x10000 && memory.Read8(address + length))
        ++length;
    return length;
}
// Measure the node key: a node whose word at +4 is above 0x1f
// carries an ANSI key at +4; every smaller value is the wType field of the
// 32-bit layout, whose Unicode key starts at +6. An ANSI key never begins
// below 0x20, so the two characters of a key are always above the test.
inline bool WideNode(Memory &memory, unsigned node) { return memory.Read16(node + 4) <= 0x1f; }

// --- The version block tree -------------------------------------------------
//
// Both layouts start with WORD wLength and WORD wValueLength. The word at +4
// decides the rest, per node, everywhere in the original: at most 0x1f it is
// the wType field of the 32-bit layout and a Unicode key follows at +6, above
// that it is already the first two characters of an ANSI key at +4. An ANSI
// key never starts below 0x20.
struct Node
{
    unsigned length = 0, value_length = 0, type = 0xffff, key = 0, value = 0;
    bool wide = false;
};
// and together.
inline Node ReadNode(Memory &memory, unsigned address)
{
    Node node;
    node.length = memory.Read16(address);
    node.value_length = memory.Read16(address + 2);
    node.wide = WideNode(memory, address);
    node.key = address + (node.wide ? 6 : 4);
    node.type = node.wide ? memory.Read16(address + 4) : 0xffffu;
    const unsigned key = node.wide ? WideBytes(memory, node.key) : AnsiBytes(memory, node.key);
    node.value = address + (node.wide ? Align4(key + 8) : Align4(key + 5));
    return node;
}

// the child walk both VerQueryValue and the conversion use. `end` is
// the last readable byte of the whole block, not of this node; `first_child`
// comes out of the node's own header unless the caller hands one in, which is
// what the conversion does with the header of the still unconverted source.
// The visitor returns true to stop; the answer is the child it stopped on, or
// 0. Offsets stay 16 bit wide, as in the original.
struct Header
{
    unsigned length = 0, value_length = 0, key_bytes = 0;
    bool wide = false;
};
template <class Visit>
unsigned Children(Memory &memory, unsigned node, unsigned end, const Header *given, Visit visit)
{
    const unsigned remaining = (end - node + 1) & 0xffffu; //
    if (remaining < 7)
        return 0;
    Header header;
    if (given)
        header = *given;
    else
    {
        header.length = memory.Read16(node);
        header.value_length = memory.Read16(node + 2);
        header.wide = WideNode(memory, node);
        header.key_bytes = header.wide ? WideBytes(memory, node + 6) : AnsiBytes(memory, node + 4);
    }
    if (header.length > remaining) //
        return 0;
    const unsigned value = header.wide ? Align4(header.key_bytes + 8) : Align4(header.key_bytes + 5);
    const unsigned first = value + Align4(header.value_length);
    if (header.length < first) //
        return 0;
    const unsigned limit = (node + header.length) & 0xffffffffu; //
    unsigned child = node + first;
    while (int(limit - child) > 7) // signed
    {
        if (!memory.Readable(child, 7)) // KERNEL.334 IsBadHugeReadPtr
            return 0;
        const unsigned length = memory.Read16(child);
        if (length <= 7) //
            return 0;
        if (int(limit - child) < int(length)) //
            return 0;
        const unsigned next = child + Align4(length); //
        if (visit(child, end))
            return child; //
        child = next;
    }
    return 0; //
}

// --- VerQueryValue, with its worker ---------------------------

// USER.437 AnsiUpperBuff over one byte, as the ANSI table of code page 1252
// has it; lstrcmpi (USER.471) folds the same way.
inline unsigned Upper(unsigned c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if ((c >= 0xe0 && c <= 0xf6) || (c >= 0xf8 && c <= 0xfe)) return c - 32;
    return c;
}
// `end` is the last readable byte of the whole block; a
// key that would reach past it counts as a mismatch, not as an error. The
// Unicode branch refuses any character whose high byte is set, so a key
// outside Latin-1 never matches.
inline bool KeyEquals(Memory &memory, unsigned child, unsigned end, const char *token, unsigned length)
{
    const int room = int((end - child) & 0xffffu);
    if (WideNode(memory, child))
    {
        const int need = int(WideBytes(memory, child + 6) + 2); //
        if (room - 6 < need)                                    //
            return false;
        unsigned i = 0;
        for (; i < length; ++i)
        {
            const unsigned wide = memory.Read16(child + 6 + i * 2);
            if (wide >> 8)
                return false;
            if (Upper(wide & 0xff) != Upper((unsigned char)token[i]))
                return false;
        }
        return memory.Read16(child + 6 + i * 2) == 0; // the full length
    }
    const int need = int(AnsiBytes(memory, child + 4) + 1); //
    if (room - 4 < need)                                    //
        return false;
    for (unsigned i = 0;; ++i)
    {
        const unsigned a = memory.Read8(child + 4 + i);
        const unsigned b = i < length ? (unsigned char)token[i] : 0u;
        if (Upper(a) != Upper(b))
            return false;
        if (!a)
            return true;
    }
}
// The 24 bytes the worker fills. wType is 0xffff for a 16-bit node, which is
// how the wrapper tells the two layouts apart.
struct Value
{
    unsigned node = 0, length = 0, value_length = 0, type = 0xffff, key = 0, value = 0;
};
// The path is cut at every backslash; runs of them collapse and a
// trailing one is ignored, so "" and "\\" both name the root.
inline bool Find(Memory &memory, unsigned block, const char *sub, Value &out)
{
    if (!block)                              //
        return false;
    const unsigned length = memory.Read16(block);
    if (length < 7)                          //
        return false;
    const unsigned end = block + length - 1; // the last byte of the block
    unsigned node = block;
    for (const char *p = sub ? sub : ""; ;)
    {
        while (*p == '\\')                   //
            ++p;
        if (!*p)                             //
            break;
        const char *token = p;
        while (*p && *p != '\\')             //
            ++p;
        const unsigned token_length = unsigned(p - token);
        const unsigned found = Children(memory, node, end, nullptr,
                                        [&](unsigned child, unsigned limit) {
                                            return KeyEquals(memory, child, limit, token, token_length);
                                        });
        if (!found)                          //
            return false;
        node = found;
    }
    const Node record = ReadNode(memory, node); //
    out.node = node;
    out.length = record.length;
    out.value_length = record.value_length;
    out.type = record.wide ? record.type : 0xffffu;
    out.key = record.key;
    out.value = record.value;
    return true;
}
// The buffer stays the caller's: the answer is a pointer into the
// block, never a copy. A 32-bit node with wType 1 is refused, because its
// value would be Unicode and no 16-bit caller can read it; a converted block
// has no wType at all and passes.
inline bool QueryValue(Memory &memory, unsigned block, const char *sub, unsigned &value, unsigned &value_length)
{
    Value found;
    if (!Find(memory, block, sub, found))
        return false;
    if (found.type == 1) //
        return false;
    value = found.value;          //
    value_length = found.value_length; // the stored length, unchanged
    return true;
}

// --- The 32-bit to 16-bit conversion, with -------------------

// Turns the Unicode block a PE carries into the ANSI form of a 16-bit
// resource, in place; the ANSI form is never longer. Returns the converted
// node's wLength. GetFileVersionInfo runs this over the buffer it just read,
// so VerQueryValue above only ever sees one layout.
inline unsigned Convert(Memory &memory, unsigned dest, unsigned source, unsigned end)
{
    Header header;                                       // the local copy
    header.length = memory.Read16(source);
    header.value_length = memory.Read16(source + 2);
    header.wide = true;                                  // the source always is
    const unsigned type = memory.Read16(source + 4);
    header.key_bytes = WideBytes(memory, source + 6);    //
    const unsigned source_value = source + Align4(header.key_bytes + 8);     //
    const unsigned dest_value = dest + Align4(header.key_bytes / 2 + 5);     //
    // KERNEL.434 UnicodeToAnsi. The ANSI key starts at +4 because the
    // 16-bit layout has no wType field.
    for (unsigned i = 0;; ++i)
    {
        const unsigned wide = memory.Read16(source + 6 + i * 2);
        memory.Write8(dest + 4 + i, wide & 0xff);
        if (!wide)
            break;
    }
    unsigned value_bytes = header.value_length;
    if (value_bytes)
    {
        if (type == 1) // a text value
        {
            unsigned i = 0;
            for (;; ++i)
            {
                const unsigned wide = memory.Read16(source_value + i * 2);
                memory.Write8(dest_value + i, wide & 0xff);
                if (!wide)
                    break;
            }
            value_bytes = i + 1;                   // characters including the null
            header.value_length = value_bytes * 2; // the bytes the source really spends
        }
        else // KERNEL.348 hmemcpy
            memory.Copy(dest_value, source_value, value_bytes);
    }
    memory.Write16(dest + 2, value_bytes); //
    unsigned cursor = dest_value + Align4(value_bytes); //
    Children(memory, source, end, &header, [&](unsigned child, unsigned limit) {
        cursor += Align4(Convert(memory, cursor, child, limit)); //
        return false;
    });
    const unsigned converted = cursor - dest; //
    memory.Write16(dest, converted);
    return converted;
}

// --- VerLanguageName, -----------------------------------------------

struct Language
{
    unsigned id;
    const char *name;
};
// The RT_STRING resources of this VER.DLL, 118 strings. An id the table does
// not name falls back to string 0 the way the second LoadString does.
inline const Language *Languages(unsigned &count)
{
    static const Language table[] = {
    {0x0000, "Language Neutral"},
    {0x0400, "Process Default Language"},
    {0x0401, "Arabic (Saudi Arabia)"},
    {0x0402, "Bulgarian"},
    {0x0403, "Catalan"},
    {0x0404, "Chinese (Taiwan)"},
    {0x0405, "Czech"},
    {0x0406, "Danish"},
    {0x0407, "German (Standard)"},
    {0x0408, "Greek"},
    {0x0409, "English (United States)"},
    {0x040a, "Spanish (Traditional Sort)"},
    {0x040b, "Finnish"},
    {0x040c, "French (Standard)"},
    {0x040d, "Hebrew"},
    {0x040e, "Hungarian"},
    {0x040f, "Icelandic"},
    {0x0410, "Italian (Standard)"},
    {0x0411, "Japanese"},
    {0x0412, "Korean"},
    {0x0413, "Dutch (Standard)"},
    {0x0414, "Norwegian (Bokmal)"},
    {0x0415, "Polish"},
    {0x0416, "Portuguese (Brazilian)"},
    {0x0418, "Romanian"},
    {0x0419, "Russian"},
    {0x041a, "Croatian"},
    {0x041b, "Slovak"},
    {0x041c, "Albanian"},
    {0x041d, "Swedish"},
    {0x041e, "Thai"},
    {0x041f, "Turkish"},
    {0x0420, "Urdu"},
    {0x0421, "Indonesian"},
    {0x0422, "Ukrainian"},
    {0x0423, "Belarusian"},
    {0x0424, "Slovenian"},
    {0x0425, "Estonian"},
    {0x0426, "Latvian"},
    {0x0427, "Lithuanian"},
    {0x0429, "Farsi"},
    {0x042a, "Vietnamese"},
    {0x042d, "Basque"},
    {0x042f, "Macedonian (FYROM)"},
    {0x0436, "Afrikaans"},
    {0x0438, "Faeroese"},
    {0x0439, "Hindi"},
    {0x043e, "Malay (Malaysia)"},
    {0x0441, "Swahili"},
    {0x0801, "Arabic (Iraq)"},
    {0x0804, "Chinese (PRC)"},
    {0x0807, "German (Swiss)"},
    {0x0809, "English (United Kingdom)"},
    {0x080a, "Spanish (Mexico)"},
    {0x080c, "French (Belgian)"},
    {0x0810, "Italian (Swiss)"},
    {0x0812, "Korean (Johab)"},
    {0x0813, "Dutch (Belgian)"},
    {0x0814, "Norwegian (Nynorsk)"},
    {0x0816, "Portuguese (Standard)"},
    {0x081a, "Serbian (Latin)"},
    {0x081d, "Swedish (Finland)"},
    {0x0827, "Lithuanian (Classic)"},
    {0x083e, "Malay (Brunei Darussalam)"},
    {0x0c01, "Arabic (Egypt)"},
    {0x0c04, "Chinese (Hong Kong)"},
    {0x0c07, "German (Austrian)"},
    {0x0c09, "English (Australian)"},
    {0x0c0a, "Spanish (International Sort)"},
    {0x0c0c, "French (Canadian)"},
    {0x0c1a, "Serbian (Cyrillic)"},
    {0x1001, "Arabic (Libya)"},
    {0x1004, "Chinese (Singapore)"},
    {0x1007, "German (Luxembourg)"},
    {0x1009, "English (Canadian)"},
    {0x100a, "Spanish (Guatemala)"},
    {0x100c, "French (Swiss)"},
    {0x1401, "Arabic (Algeria)"},
    {0x1404, "Chinese (Macau)"},
    {0x1407, "German (Liechtenstein)"},
    {0x1409, "English (New Zealand)"},
    {0x140a, "Spanish (Costa Rica)"},
    {0x140c, "French (Luxembourg)"},
    {0x1801, "Arabic (Morocco)"},
    {0x1809, "English (Ireland)"},
    {0x180a, "Spanish (Panama)"},
    {0x180c, "French (Monaco)"},
    {0x1c01, "Arabic (Tunisia)"},
    {0x1c09, "English (South Africa)"},
    {0x1c0a, "Spanish (Dominican Republic)"},
    {0x2001, "Arabic (Oman)"},
    {0x2009, "English (Jamaica)"},
    {0x200a, "Spanish (Venezuela)"},
    {0x2401, "Arabic (Yemen)"},
    {0x2409, "English (Caribbean)"},
    {0x240a, "Spanish (Colombia)"},
    {0x2801, "Arabic (Syria)"},
    {0x2809, "English (Belize)"},
    {0x280a, "Spanish (Peru)"},
    {0x2c01, "Arabic (Jordan)"},
    {0x2c09, "English (Trinidad)"},
    {0x2c0a, "Spanish (Argentina)"},
    {0x3001, "Arabic (Lebanon)"},
    {0x3009, "English (Zimbabwe)"},
    {0x300a, "Spanish (Ecuador)"},
    {0x3401, "Arabic (Kuwait)"},
    {0x3409, "English (Philippines)"},
    {0x340a, "Spanish (Chile)"},
    {0x3801, "Arabic (U.A.E.)"},
    {0x380a, "Spanish (Uruguay)"},
    {0x3c01, "Arabic (Bahrain)"},
    {0x3c0a, "Spanish (Paraguay)"},
    {0x4001, "Arabic (Qatar)"},
    {0x400a, "Spanish (Bolivia)"},
    {0x440a, "Spanish (El Salvador)"},
    {0x480a, "Spanish (Honduras)"},
    {0x4c0a, "Spanish (Nicaragua)"},
    {0x500a, "Spanish (Puerto Rico)"},
    };
    count = sizeof table / sizeof *table;
    return table;
}
inline const char *LanguageString(unsigned id)
{
    unsigned count = 0;
    const Language *table = Languages(count);
    for (unsigned i = 0; i < count; ++i)
        if (table[i].id == id)
            return table[i].name;
    return table[0].name; // LoadString of id 0
}
// Copies the name into the caller's buffer and answers the length of the
// untruncated string, exactly as LoadString reported it. A size that does not
// fit cuts the copy but not the answer.
inline unsigned LanguageName(Memory &memory, unsigned language, unsigned buffer, unsigned size)
{
    const char *name = LanguageString(language);
    unsigned length = 0;
    while (name[length])
        ++length;
    unsigned copy = length + 1;
    if (size <= length) // buf[size - 1] = 0 before the copy
        copy = size;
    for (unsigned i = 0; i + 1 < copy; ++i)
        memory.Write8(buffer + i, (unsigned char)name[i]);
    if (copy)
        memory.Write8(buffer + copy - 1, 0);
    return length; //
}

// --- The file services the last two routines need --------------------------

// What VER.DLL reaches for outside its own segment: OpenFile, the two DOS
// calls AH=43h and AH=56h, LZOpenFile/LZCopy, GetModuleHandle and the two
// directory queries. One implementation lives in src/x86_version16.cpp.
struct FileSystem
{
    virtual ~FileSystem() = default;
    // DOS AH=43h : the attribute word, or 0xffffffff when the
    // file is not there. Only "there or not" and bit 0 are used.
    virtual unsigned Attributes(const char *path) = 0;
    // OpenFile(OF_EXIST), with the fully qualified name it found.
    virtual bool Exists(const char *path, char *full, unsigned size) = 0;
    // LZOpenFile(OF_READ); false means VIF_CANNOTREADSRC.
    virtual bool Readable(const char *path) = 0;
    // OpenFile(OF_CREATE) plus LZCopy. Returns 1 when the
    // copy ran, 0 when the create was refused, with `error` taking the
    // OFSTRUCT.nErrCode, and otherwise the negative LZ error the original
    // switches over.
    virtual int Copy(const char *from, const char *to, unsigned &error) = 0;
    // OpenFile(OF_DELETE). False fails; `error` takes nErrCode.
    virtual bool Remove(const char *path, unsigned &error) = 0;
    // DOS AH=56h. Returns the DOS error, 0 on success.
    virtual unsigned Rename(const char *from, const char *to) = 0;
    // GetModuleHandle(leaf) above 1 whose GetModuleFileName is `full`.
    virtual bool Loaded(const char *leaf, const char *full) = 0;
    virtual void WindowsDirectory(char *out, unsigned size) = 0;
    virtual void SystemDirectory(char *out, unsigned size) = 0;
    // LoadVerInfo: the whole version block of one file, or 0 bytes.
    virtual unsigned VersionBlock(const char *path, unsigned char *out, unsigned size) = 0;
    // The HKLM CheckVerDLLs value, whose data is 0x13e bytes long.
    virtual bool CheckVerDll(const char *leaf) = 0;
};
// Memory over a plain buffer, for the two blocks VerInstallFile compares.
struct BufferMemory final : Memory
{
    // A block always starts above zero, because the original refuses a null
    // far pointer and would refuse a buffer that begins at address zero too.
    static constexpr unsigned Base = 0x10000;
    unsigned char *bytes = nullptr;
    unsigned size = 0;
    BufferMemory(unsigned char *b, unsigned n) : bytes(b), size(n) {}
    bool In(unsigned a, unsigned n) const { return a >= Base && a - Base + n <= size; }
    unsigned Read8(unsigned a) override { return In(a, 1) ? bytes[a - Base] : 0u; }
    unsigned Read16(unsigned a) override
    {
        return In(a, 2) ? unsigned(bytes[a - Base]) | unsigned(bytes[a - Base + 1]) << 8 : 0u;
    }
    unsigned Read32(unsigned a) override { return Read16(a) | Read16(a + 2) << 16; }
    void Write8(unsigned a, unsigned v) override { if (In(a, 1)) bytes[a - Base] = (unsigned char)v; }
    void Write16(unsigned a, unsigned v) override { Write8(a, v); Write8(a + 1, v >> 8); }
    void Write32(unsigned a, unsigned v) override { Write16(a, v); Write16(a + 2, v >> 16); }
    void Copy(unsigned to, unsigned from, unsigned n) override
    {
        for (unsigned i = 0; i < n; ++i) Write8(to + i, Read8(from + i));
    }
    bool Readable(unsigned a, unsigned n) override { return In(a, n); }
    bool Writable(unsigned a, unsigned n) override { return In(a, n); }
};

// Small string helpers; the original calls lstrlen, lstrcpy and lstrcmpi.
inline unsigned Length(const char *s) { unsigned n = 0; while (s && s[n]) ++n; return n; }
inline void Copy(char *out, const char *in, unsigned capacity)
{
    unsigned i = 0;
    if (in) for (; in[i] && i + 1 < capacity; ++i) out[i] = in[i];
    if (capacity) out[i] = 0;
}
inline bool SameText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        const unsigned x = Upper((unsigned char)*a), y = Upper((unsigned char)*b);
        if (x != y) return false;
        if (!x) return true;
    }
}
// Joins a directory and a file name and answers where the file part
// begins. A directory that already ends in a backslash or a colon gets none.
inline unsigned JoinPath(char *out, const char *directory, const char *file, unsigned capacity)
{
    if (directory) Copy(out, directory, capacity);
    else if (capacity) out[0] = 0;
    unsigned n = Length(out);
    if (n)
    {
        const char last = out[n - 1];
        if (last != '\\' && last != ':' && n + 1 < capacity) out[n++] = '\\';
    }
    Copy(out + n, file, capacity > n ? capacity - n : 0);
    return n;
}
// three upper case hex digits of the counter.
inline void Hex3(char *out, unsigned value)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = digits[(value >> 8) & 15];
    out[1] = digits[(value >> 4) & 15];
    out[2] = digits[value & 15];
    out[3] = 0;
}
// the two DOS errors the original turns into result bits.
inline unsigned DosErrorBits(unsigned error)
{
    if (error == 5) return 0x200u;   // VIF_ACCESSVIOLATION
    if (error == 0x20) return 0x400u; // VIF_SHARINGVIOLATION
    return 0;
}

// --- VerFindFile, ---------------------------------------------------

// The shared directory VerFindFile answers with is worked out once per loaded
// DLL and kept. In NEO98 that is once per process.
struct FindState
{
    char shared[260] = {};
    unsigned shared_length = 0;
};
struct FindRequest
{
    unsigned flags = 0;
    const char *file = nullptr, *app_dir = nullptr;
    char *current_dir = nullptr;
    unsigned current_length = 0;
    char *destination_dir = nullptr;
    unsigned destination_length = 0;
};
// the directory list walk. Answers the untruncated length of the
// directory the file was found in and fills `out` with it.
inline unsigned SearchDirectories(FileSystem &files, const char *file, char *out, unsigned capacity,
                                  const char *const *directories, unsigned count)
{
    char path[0x192] = {}, full[0x192] = {};
    const unsigned name = Length(file);
    bool found = false;
    for (unsigned i = 0; i <= count && !found; ++i)
    {
        if (i < count)
        {
            if (!directories[i]) break;             // the list ends
            if (Length(directories[i]) + name >= 0x103) continue; //
            JoinPath(path, directories[i], file, sizeof path);
        }
        else
            Copy(path, file, sizeof path);          // the bare name
        found = files.Exists(path, full, sizeof full);
    }
    if (!found)
    {
        // The original still tries the bare name after the list; Exists does
        // the same search OpenFile would.
        Copy(path, file, sizeof path);
        found = files.Exists(path, full, sizeof full);
    }
    if (!found)
    {
        if (capacity) out[0] = 0;
        return 0;
    }
    unsigned cut = 0;                               // the last backslash
    for (unsigned i = 0; full[i]; ++i)
        if (full[i] == '\\') cut = i;
    if (cut <= 3) ++cut;                            // keep "C:\"
    unsigned take = capacity && capacity - 1 < cut ? capacity - 1 : cut; //
    for (unsigned i = 0; i < take; ++i) out[i] = full[i];
    if (capacity) out[take] = 0;
    return cut;                                     // untruncated
}
// szWinDir is read nowhere: the original overwrites its own stack slot
// with GetWindowsDirectory and never looks at the caller's value again.
inline unsigned FindFile(FileSystem &files, FindState &state, FindRequest &request,
                         unsigned &current_length_out, unsigned &destination_length_out)
{
    char windows[260] = {}, system[260] = {}, found[260] = {};
    unsigned flags = 0;
    files.WindowsDirectory(windows, sizeof windows);   //
    files.SystemDirectory(system, sizeof system);      //
    if (!system[0]) Copy(system, windows, sizeof system);
    const bool shared = (request.flags & 1) != 0;      // VFFF_ISSHAREDFILE
    const char *order[3];
    if (shared) { order[0] = windows; order[1] = system; order[2] = request.app_dir; } //
    else { order[0] = request.app_dir; order[1] = windows; order[2] = system; }        //
    const unsigned length = SearchDirectories(files, request.file, found, sizeof found, order, 3);
    if (request.current_length > length)               //
        Copy(request.current_dir, found, request.current_length);
    else
        flags |= 4;                                   // VFF_BUFFTOOSMALL
    current_length_out = length + 1;                  //
    if (request.destination_dir)                      //
    {
        const char *destination = nullptr;
        if (shared)
        {
            if (!state.shared_length)                 // worked out once
            {
                const unsigned n = Length(windows);
                bool compare = false, use_system = false;
                if (n && windows[n - 1] == '\\')
                    compare = system[n - 1] == '\\';
                else
                    compare = system[n] == '\\';
                if (compare)
                {
                    char saved = system[n];
                    system[n] = 0;
                    use_system = SameText(windows, system); //
                    system[n] = saved;
                }
                Copy(state.shared, use_system ? system : windows, sizeof state.shared);
                state.shared_length = Length(state.shared);
            }
            destination = state.shared;
        }
        else
            destination = request.app_dir ? request.app_dir : ""; //
        const unsigned size = Length(destination);
        if (request.destination_length > size)        //
        {
            Copy(request.destination_dir, destination, request.destination_length);
            const unsigned m = Length(request.destination_dir);
            if (m && request.destination_dir[m - 1] == '\\')
                request.destination_dir[m - 1] = 0;   //
            if (!SameText(request.current_dir ? request.current_dir : "", request.destination_dir))
                flags |= 1;                           // VFF_CURNEDEST
        }
        else
            flags |= 4;                               //
        destination_length_out = size + 1;            // before the strip
    }
    if (found[0])                                     //
    {
        char path[260] = {};
        JoinPath(path, found, request.file, sizeof path);
        if (files.Loaded(request.file, path))
            flags |= 2;                               // VFF_FILEINUSE
    }
    return flags;                                     // AX alone
}

// --- VerInstallFile, ------------------------------------------------

struct InstallRequest
{
    unsigned flags = 0;
    const char *source = nullptr, *destination = nullptr;
    const char *source_dir = nullptr, *destination_dir = nullptr, *current_dir = nullptr;
    char *temp = nullptr;
    unsigned temp_length = 0;
};
// The fixed part of a version block, reached at +0x14 of the root node.
inline unsigned Fixed(BufferMemory &memory, unsigned field)
{
    return memory.Read32(BufferMemory::Base + 0x14 + field);
}
// The whole install: copy into a temp file beside the destination,
// compare the two version resources, then delete and rename. Returns the
// VIF_* mask, zero when the file is installed.
inline unsigned InstallFile(FileSystem &files, InstallRequest &request, unsigned &temp_length_out)
{
    char work[260] = {}, destination[260] = {}, current[260] = {};
    unsigned result = 0, error = 0;
    temp_length_out = request.temp_length;
    JoinPath(work, request.source_dir, request.source, sizeof work); //
    if (!files.Readable(work))                                       //
        return 0x10000;                                              // VIF_CANNOTREADSRC
    char source_path[260] = {};
    Copy(source_path, work, sizeof source_path);
    const unsigned directory = JoinPath(destination, request.destination_dir, request.destination,
                                        sizeof destination); //
    Copy(work, destination, sizeof work);                     //
    const unsigned attributes = files.Attributes(work);       //
    const bool present = attributes != 0xffffffffu;
    if (present)
    {
        if (attributes & 1)                                   //
            return 0x40;                                      // VIF_WRITEPROT
        if (files.Loaded(request.destination, work))          //
            return 0x80;                                      // VIF_FILEINUSE
    }
    unsigned name_at = directory;                             //
    bool adopted = false;
    if ((request.flags & 1) && request.temp && request.temp[0]) // VIFF_FORCEINSTALL
    {
        JoinPath(work, request.destination_dir, request.temp, sizeof work);
        adopted = files.Attributes(work) != 0xffffffffu;       //
        if (adopted) name_at = directory;
    }
    if (!adopted)
    {
        Copy(work + directory, "temp.", sizeof work - directory); //
        unsigned counter = 0;
        for (;; ++counter)
        {
            if (counter > 0xfff)                              //
                return 0x800;                                 // VIF_CANNOTCREATE
            Hex3(work + directory + 5, counter);              //
            if (files.Attributes(work) == 0xffffffffu) break; //
        }
        const int copied = files.Copy(source_path, work, error); // and
        if (copied == -1 || copied == -3 || copied == -7 || copied == -8)
            result = 0x10000;                                 // VIF_CANNOTREADSRC
        else if (copied == -5 || copied == -6)
            result = 0x8000;                                  // VIF_OUTOFMEMORY
        else if (copied == -2 || copied == -4)
            result = 0x100;                                   // VIF_OUTOFSPACE
        else if (!copied)
            result = 0x800 | DosErrorBits(error);             // create refused
        if (result)
        {
            files.Remove(work, error);                        //
            return result;
        }
    }
    unsigned flags = request.flags;
    if (present)
    {
        // a value of exactly 0x13e bytes under CheckVerDLLs takes the
        // force flag away again. The length test is the original's, literally.
        if (files.CheckVerDll(request.destination))
            flags &= ~1u;
        if (!(flags & 1))                                     //
        {
            unsigned char old_block[0x4000], new_block[0x4000];
            const unsigned old_size = files.VersionBlock(destination, old_block, sizeof old_block);
            if (old_size)                                     //
            {
                const unsigned new_size = files.VersionBlock(work, new_block, sizeof new_block);
                BufferMemory old_memory(old_block, old_size), new_memory(new_block, new_size);
                if (!new_size)
                    result = 6;                               // MISMATCH|SRCOLD
                else
                {
                    const unsigned old_high = Fixed(old_memory, 8), new_high = Fixed(new_memory, 8);
                    const unsigned old_low = Fixed(old_memory, 12), new_low = Fixed(new_memory, 12);
                    if (old_high > new_high || (old_high == new_high && old_low > new_low))
                        result = 6;                           // an assignment
                    if (!SameText(request.destination, "mplayer.exe") &&
                        (Fixed(old_memory, 0x24) != Fixed(new_memory, 0x24) ||
                         Fixed(old_memory, 0x28) != Fixed(new_memory, 0x28)))
                        result |= 0x22;                       // MISMATCH|DIFFTYPE
                    unsigned old_value = 0, old_bytes = 0, new_value = 0, new_bytes = 0;
                    if (QueryValue(old_memory, BufferMemory::Base, "\\VarFileInfo\\Translation", old_value, old_bytes) &&
                        QueryValue(new_memory, BufferMemory::Base, "\\VarFileInfo\\Translation", new_value, new_bytes))
                    {
                        const unsigned old_count = old_bytes >> 2, new_count = new_bytes >> 2;
                        for (unsigned i = 0; i < old_count; ++i)
                        {
                            const unsigned language = old_memory.Read16(old_value + i * 4);
                            const unsigned page = old_memory.Read16(old_value + i * 4 + 2);
                            bool match = false;
                            for (unsigned j = 0; j < new_count && !match; ++j)
                            {
                                if (new_memory.Read16(new_value + j * 4) != language) continue;
                                const unsigned other = new_memory.Read16(new_value + j * 4 + 2);
                                match = page == other || (!page && other != 0x4b0);
                            }
                            if (!match) { result |= 0xa; break; } // MISMATCH|DIFFLANG
                        }
                    }
                }
            }
        }
    }
    if (!result && present && !files.Remove(destination, error)) //
    {
        result |= DosErrorBits(error) | 0x1000;                  // VIF_CANNOTDELETE
        files.Remove(work, error);
        return result;
    }
    if (result)                                                  // the install is refused
    {
        const char *leaf = work + name_at;
        const unsigned n = Length(leaf);
        if (request.temp_length > n)                             //
        {
            Copy(request.temp, leaf, request.temp_length);
            result |= 1;                                         // VIF_TEMPFILE
        }
        else
        {
            result |= 0x40000;                                   // VIF_BUFFTOOSMALL
            files.Remove(work, error);                           //
        }
        temp_length_out = n + 1;                                 //
        return result;
    }
    if (!(request.flags & 2) && request.current_dir && request.current_dir[0] &&
        !SameText(request.current_dir, request.destination_dir ? request.destination_dir : ""))
    {
        JoinPath(current, request.current_dir, request.destination, sizeof current); //
        error = 0;
        if (files.Attributes(current) != 0xffffffffu && !files.Loaded(request.destination, current) &&
            !files.Remove(current, error))
            result |= DosErrorBits(error) | 0x4000;              // VIF_CANNOTDELETECUR
    }
    const unsigned renamed = files.Rename(work, destination);    //
    if (renamed)
    {
        result |= DosErrorBits(renamed) | 0x2000;                // VIF_CANNOTRENAME
        files.Remove(work, error);                               //
    }
    return result;
}

// --- GetFileResourceSize and GetFileResource, and ------------

// Where the resource bytes come from. VER.DLL opens the file with LZOpenFile,
// so an LZ compressed file is expanded on the way in; `Image` delivers what
// LZRead would have read. A file that is neither NE nor LE is handed to the
// 32-bit half of VERSION.DLL through the SL01 block. That half calls
// LoadLibraryEx as a data file, FindResource, SizeofResource, LoadResource and
// RtlMoveMemory. NEO98 answers those from the same native KERNEL32.
struct Resources
{
    virtual ~Resources() = default;
    virtual const unsigned char *Image(const char *path, unsigned &size) = 0;
};
inline unsigned Word(const unsigned char *p, unsigned at) { return unsigned(p[at]) | unsigned(p[at + 1]) << 8; }
inline unsigned Long(const unsigned char *p, unsigned at) { return Word(p, at) | Word(p, at + 2) << 16; }

// The Portable Executable resource directory, which is what the 32-bit half of
// VERSION.DLL reaches through LoadLibraryEx as a data file, FindResource,
// SizeofResource and LoadResource. NEO98 walks the directory in the file image
// instead: this is a native provider that never enters the guest, and the
// bytes are the same ones LoadResource would have handed over. The guest path
// is implemented (src/x86_datafile.cpp).
// One directory level: the entry for an integer id, or the first entry at all
// when `any` is set, the way FindResource takes the first language it finds.
inline bool PeDirectoryEntry(const unsigned char *image, unsigned size, unsigned table, unsigned id,
                             bool any, unsigned &offset, bool &directory)
{
    if (table + 16 > size)
        return false;
    const unsigned named = Word(image, table + 12), ids = Word(image, table + 14);
    for (unsigned i = 0; i < named + ids; ++i)
    {
        const unsigned entry = table + 16 + i * 8;
        if (entry + 8 > size)
            return false;
        const unsigned name = Long(image, entry), value = Long(image, entry + 4);
        if (!any && ((name & 0x80000000u) || name != id))
            continue;
        if (any && i < named)
            continue;
        directory = (value & 0x80000000u) != 0;
        offset = value & 0x7fffffffu;
        return true;
    }
    return false;
}
inline unsigned PeFileOffset(const unsigned char *image, unsigned size, unsigned header, unsigned rva)
{
    const unsigned sections = Word(image, header + 6), optional = Word(image, header + 20);
    const unsigned table = header + 24 + optional;
    for (unsigned i = 0; i < sections; ++i)
    {
        const unsigned entry = table + i * 40;
        if (entry + 40 > size)
            return 0;
        // This resolves bytes in the file, not the zero-filled virtual tail.
        // Watcom images can leave VirtualSize zero (NFS III), while
        // SizeOfRawData still supplies the section mapped by LoadLibraryEx.
        const unsigned start = Long(image, entry + 12), bytes = Long(image, entry + 16);
        if (rva >= start && rva - start < bytes)
            return Long(image, entry + 20) + (rva - start);
    }
    return 0;
}
// FindResource, SizeofResource and LoadResource in one: the file offset and
// the length of one resource, or false.
inline bool PeResource(const unsigned char *image, unsigned size, unsigned header, unsigned type,
                       unsigned id, unsigned &offset, unsigned &length)
{
    const unsigned optional = Word(image, header + 20);
    if (optional < 0x80 || header + 24 + optional > size)
        return false;
    const unsigned magic = Word(image, header + 24);
    const unsigned directories = header + 24 + (magic == 0x20b ? 112u : 96u);
    const unsigned root = Long(image, directories + 2 * 8);
    if (!root)
        return false;
    const unsigned base = PeFileOffset(image, size, header, root);
    if (!base)
        return false;
    unsigned at = 0;
    bool nested = false;
    if (!PeDirectoryEntry(image, size, base, type, false, at, nested) || !nested)
        return false;
    if (!PeDirectoryEntry(image, size, base + at, id, false, at, nested) || !nested)
        return false;
    if (!PeDirectoryEntry(image, size, base + at, 0, true, at, nested) || nested)
        return false;
    if (base + at + 16 > size)
        return false;
    const unsigned data = Long(image, base + at);
    length = Long(image, base + at + 4);
    offset = PeFileOffset(image, size, header, data);
    return offset != 0 && length != 0 && offset + length <= size;
}

// the resource table of a New Executable. Type and id are integers;
// the original compares the low word of the far pointer and masks bit 15, so
// a string name never matches.
inline bool NeResource(const unsigned char *image, unsigned size, unsigned header, unsigned type,
                       unsigned id, unsigned &offset, unsigned &length)
{
    const unsigned table = header + Word(image, header + 0x24);
    if (table + 2 > size)
        return false;
    const unsigned shift = Word(image, table); //
    if (shift > 0xc)                           //
        return false;
    unsigned at = table + 2;
    while (at + 8 <= size)
    {
        const unsigned kind = Word(image, at);
        if (!kind) // the end of the type table
            return false;
        const unsigned count = Word(image, at + 2);
        if ((kind & 0x7fff) == type) //
        {
            for (unsigned i = 0; i < count; ++i)
            {
                const unsigned entry = at + 8 + i * 12;
                if (entry + 12 > size)
                    return false;
                if ((Word(image, entry + 6) & 0x7fff) != id) //
                    continue;
                offset = Word(image, entry) << shift;
                length = Word(image, entry + 2) << shift;
                return true;
            }
            return false;
        }
        at += 8 + count * 12; //
    }
    return false;
}
// the resource stream a Linear Executable carries at LE+0xb8. Entries
// are a type, a name, a flag word and a length, types and names either 0xff
// and a word or a zero terminated string. Only the type is compared, the way
// the original does it.
inline bool LeResource(const unsigned char *image, unsigned size, unsigned header, unsigned type,
                       unsigned &offset, unsigned &length)
{
    unsigned at = Long(image, header + 0xb8); //
    while (at < size)
    {
        unsigned kind = 0;
        const unsigned tag = image[at++];
        if (tag == 0xff) //
        {
            if (at + 2 > size)
                return false;
            kind = Word(image, at) & 0x7fff;
            at += 2;
        }
        else if (tag) // a string type never matches an integer
        {
            while (at < size && image[at])
                ++at;
            ++at;
            kind = 0xffffffffu;
        }
        // step over the name, then the flag word, then read the size.
        if (at < size && image[at] == 0xff)
            at += 3;
        else if (at < size && image[at])
        {
            while (at < size && image[at])
                ++at;
            ++at;
        }
        else
            ++at;
        at += 2; // the flag word
        if (at + 4 > size)
            return false;
        const unsigned bytes = Long(image, at);
        at += 4;
        if (!bytes)
            return false; // and
        if (kind == type)
        {
            offset = at;       // the position right behind the header
            length = bytes + 4; //
            return true;
        }
        at += bytes; //
    }
    return false;
}
// The type and the id are the caller's LPCSTR values as they came, a far
// pointer or MAKEINTRESOURCE. The NE and LE walks compare only the low word;
// the 32-bit side hands them to FindResource, where a
// value with a selector is a name. Named resources of a PE file are not
// looked up here: no caller in the tree asks for one, and the lookup answers
// "not found".
inline bool PeInteger(unsigned type, unsigned id) { return !(type >> 16) && !(id >> 16); }

// with : identify the file and find the resource. A file that is
// neither NE nor LE goes to the module path. `offset` is the file offset the
// original hands back as the caller's handle; it stays zero for the module
// path, exactly as VERSION.DLL leaves it. `handed` tells whether
// the original writes that handle at all: only for a resource it
// found in an NE or LE file always, before it even loads the
// file, for every file it opened that is neither. A file that does
// not open and a resource an NE or LE file lacks leave it untouched.
inline unsigned FileResourceSize(Resources &resources, const char *path, unsigned type, unsigned id,
                                 unsigned &offset, bool *handed = nullptr)
{
    offset = 0;
    if (handed)
        *handed = false;
    unsigned size = 0;
    const unsigned char *image = resources.Image(path, size);
    if (!image) // LZOpenFile and OpenFile both failed
        return 0;
    unsigned length = 0;
    unsigned header = 0;
    const bool mz = size >= 0x40 && image[0] == 'M' && image[1] == 'Z'; //
    if (mz)
        header = Long(image, 0x3c);
    if (mz && header + 0x40 <= size && image[header] == 'N' && image[header + 1] == 'E') //
    {
        if (!NeResource(image, size, header, type & 0xffff, id & 0xffff, offset, length))
            return 0;
        if (handed)
            *handed = true;
        return length;
    }
    if (mz && header + 0xc4 <= size && image[header] == 'L' && image[header + 1] == 'E') //
    {
        if (!LeResource(image, size, header, type & 0xffff, offset, length))
            return 0;
        if (handed)
            *handed = true;
        return length;
    }
    // VER.DLL : everything else is the 32-bit side's work.
    // Its answer for the caller's handle is zero.
    if (handed)
        *handed = true;
    if (mz && header + 24 <= size && Magic(image + header, "PE\0\0", 4) && PeInteger(type, id) &&
        PeResource(image, size, header, type, id, offset, length))
    {
        offset = 0;
        return length;
    }
    offset = 0;
    return 0;
}
// Copies the resource into the caller's buffer. A handle other than
// zero is the file offset GetFileResourceSize found, and the original then
// only opens, seeks and reads; the lookup runs again only for handle zero,
// which is what the module path always hands back.
inline unsigned FileResource(Resources &resources, Memory &memory, const char *path, unsigned type,
                             unsigned id, unsigned offset, unsigned bytes, unsigned buffer)
{
    unsigned size = 0;
    const unsigned char *image = resources.Image(path, size);
    if (!offset) // the search path
    {
        unsigned length = 0;
        if (!image || size < 0x40 || image[0] != 'M' || image[1] != 'Z') //
            return 0;
        const unsigned header = Long(image, 0x3c);
        bool found = false;
        if (header + 0x40 <= size && image[header] == 'N' && image[header + 1] == 'E')
            found = NeResource(image, size, header, type & 0xffff, id & 0xffff, offset, length);
        else if (header + 0xc4 <= size && image[header] == 'L' && image[header + 1] == 'E')
            found = LeResource(image, size, header, type & 0xffff, offset, length);
        else if (header + 24 <= size && Magic(image + header, "PE\0\0", 4) && PeInteger(type, id))
            found = PeResource(image, size, header, type, id, offset, length);
        if (!found) //
            return 0;
        if (bytes > length) // never more than the resource holds
            bytes = length;
    }
    // the read has to deliver every byte asked for.
    if (!image || !offset || offset + bytes > size)
        return 0;
    for (unsigned i = 0; i < bytes; ++i)
        memory.Write8(buffer + i, image[offset + i]);
    return 1;
}

// --- GetFileVersionInfoSize and GetFileVersionInfo, and ------

// The resource the two look for: VS_FILE_INFO as the type, VS_VERSION_INFO as
// the name, both as integers.
enum : unsigned { VersionType = 16, VersionName = 1, FixedSignature = 0xfeef04bdu };

// Answers the length the version block declares for itself, after
// checking that it fits into the resource and that the fixed part carries the
// signature. `handle` receives the file offset the resource sits at, which
// GetFileVersionInfo takes as its own argument.
inline unsigned InfoSize(Resources &resources, const char *path, unsigned &handle)
{
    handle = 0;
    unsigned offset = 0;
    const unsigned resource = FileResourceSize(resources, path, VersionType, VersionName, offset);
    if (!resource) //
        return 0;
    handle = offset;
    // The original reads the first 92 bytes of the resource onto its own
    // stack, so a resource shorter than that makes the read fail and the
    // answer zero. That stays true here.
    unsigned char bytes[0x5c] = {};
    BufferMemory memory(bytes, sizeof bytes);
    const unsigned scratch = BufferMemory::Base;
    if (!FileResource(resources, memory, path, VersionType, VersionName, offset, 0x5c, scratch))
        return 0; //
    const unsigned length = memory.Read16(scratch); //
    if (length > resource)                          // the block must fit
        return 0;
    // a word above 0x1f at +4 is already the ANSI key, so the fixed
    // part sits at 0x14; every smaller value is wType and it sits at 0x28.
    const unsigned fixed = memory.Read16(scratch + 4) > 0x1f ? 0x14u : 0x28u;
    if (memory.Read32(scratch + fixed) != FixedSignature)
        return 0;
    return length; //
}
// The worker shared by GetFileVersionInfo and
// GetFileVersionInfoRaw. Raw hands the resource over untouched; the ordinary
// entry turns a 32-bit Unicode block into the 16-bit ANSI form, so everything
// above only ever sees one layout.
inline bool Info(Resources &resources, Memory &memory, const char *path, unsigned handle,
                 unsigned bytes, unsigned buffer, bool raw)
{
    if (!FileResource(resources, memory, path, VersionType, VersionName, handle, bytes, buffer))
        return false; //
    if (raw || bytes < 7) // and
        return true;
    const unsigned length = memory.Read16(buffer);
    if (length > bytes) //
        return true;
    if (memory.Read16(buffer + 4) > 0x1f) // already ANSI
        return true;
    return Convert(memory, buffer, buffer, buffer + bytes) != 0; //
}
} // namespace Win98VersionInfo
