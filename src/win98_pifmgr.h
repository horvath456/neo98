// SPDX-License-Identifier: GPL-2.0-or-later
// PIF property services used by SHELL32. Open, query, update, and close property
// handles; preserve existing PIF sections and report unsupported operations
// through their defined results.
#pragma once
namespace Win98PifMgr
{
constexpr unsigned HeaderBytes = 0x171, SectionHeader = 0x16, ImageCapacity = 0x1000;
constexpr unsigned Section386Bytes = 0x68, Vmm40Bytes = 0x1ac;
constexpr unsigned PathCapacity = 260;
constexpr unsigned GroupPrg = 1, GroupTsk = 2, GroupVid = 3, GroupMem = 4, GroupKbd = 5, GroupMse = 6,
                   GroupSnd = 7, GroupFnt = 8, GroupWin = 9, GroupEnv = 10, GroupIcon = 11;
constexpr unsigned GroupBytes[11] = {0, 0x292, 14, 12, 16, 18, 4, 4, 78, 38, 88};
// Where each copied group lives inside the VMM 4.0 section.
constexpr unsigned GroupVmmOffset[11] = {0, 0, 0xaa, 0xb8, 0, 0xc4, 0xd6, 0xda, 0xde, 0x12c, 0x152};
constexpr unsigned PrgBytes = 0x292;
enum PrgField
{
    PrgFlags = 0x000, PrgFlagsInit = 0x002, PrgTitle = 0x004, PrgCmdLine = 0x022, PrgWorkDir = 0x0a2,
    PrgHotKey = 0x0e2, PrgIconFile = 0x0e4, PrgIconIndex = 0x134, PrgEnhModeFlags = 0x136,
    PrgRealModeFlags = 0x13a, PrgOtherFile = 0x13e, PrgPifFile = 0x18e
};
constexpr unsigned PrgCloseOnExit = 0x0001;
constexpr unsigned PrgInitNoPif = 0x1000, PrgInitDefaultPif = 0x2000, PrgInitInfSettings = 0x4000;
// flOpt of the four entries.
constexpr unsigned OpenRawIo = 0x0001, OpenNoPifSearch = 0x0002, OpenInhibitPif = 0x8000;
constexpr unsigned GetRawIo = 0x0001, GetNoConversion = 0x0008;
constexpr unsigned SetRawIo = 0x0001, SetNoFlush = 0x0002, SetNoConversion = 0x0008;
constexpr unsigned CloseDiscard = 0x0001;
// The flag word at +8 of the original handle record.
constexpr unsigned RecordDirty = 0x0001, RecordDirtyLocked = 0x0002, RecordShrunk = 0x0004, RecordRawIo = 0x0008,
                   RecordIniA = 0x0020, RecordNoFile = 0x0040, RecordFileNamed = 0x0080, RecordLoaded = 0x0100,
                   RecordStale = 0x0200, RecordFromPifDirectory = 0x0800, RecordNoPif = 0x1000,
                   RecordDefaultPif = 0x2000, RecordInfSettings = 0x4000, RecordInhibitPif = 0x8000;

struct Environment
{
    virtual ~Environment() = default;
    // The whole file, or false when it does not exist. `size` is the count read.
    virtual bool ReadFile(const char *path, unsigned char *out, unsigned capacity, unsigned &size) = 0;
    // Create or overwrite the file with the bytes.
    virtual bool WriteFile(const char *path, const unsigned char *data, unsigned size) = 0;
    virtual bool FileExists(const char *path) = 0;
    // The full path of a name, as KERNEL.353 lstrcpyn after GetFullPathName does it here.
    virtual void FullPath(const char *path, char (&out)[PathCapacity]) = 0;
    // "C:\WINDOWS".
    virtual void WindowsDirectory(char (&out)[PathCapacity]) = 0;
    // The file name of COMSPEC; the default PIF closes on exit only for it.
    virtual const char *ComSpecName() { return "COMMAND.COM"; }
    virtual void Log(const char *) {}
};

namespace Detail
{
inline unsigned Word(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
inline unsigned Dword(const unsigned char *p) { return Word(p) | Word(p + 2) << 16; }
inline void PutWord(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
inline void PutDword(unsigned char *p, unsigned v) { PutWord(p, v); PutWord(p + 2, v >> 16); }
inline unsigned Length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
inline unsigned Fold(unsigned c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
inline bool SameText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        if (Fold((unsigned char)*a) != Fold((unsigned char)*b)) return false;
        if (!*a) return true;
    }
}
// KERNEL.353 lstrcpyn: at most n - 1 characters and a NUL.
inline void CopyN(char *dst, const char *src, unsigned n)
{
    if (!n) return;
    unsigned i = 0;
    for (; i + 1 < n && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}
inline void CopyN(unsigned char *dst, const char *src, unsigned n) { CopyN(reinterpret_cast<char *>(dst), src, n); }
inline void Copy(char *dst, const char *src) { while ((*dst++ = *src++)) {} }
inline void Zero(unsigned char *p, unsigned n) { for (unsigned i = 0; i < n; ++i) p[i] = 0; }
inline void Move(unsigned char *dst, const unsigned char *src, unsigned n) { for (unsigned i = 0; i < n; ++i) dst[i] = src[i]; }
inline bool Equal(const unsigned char *a, const unsigned char *b, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}
inline const char *Leaf(const char *path)
{
    const char *leaf = path;
    for (const char *p = path; *p; ++p)
        if (*p == '\\' || *p == ':') leaf = p + 1;
    return leaf;
}
inline bool SectionName(const unsigned char *p, const char *name)
{
    for (unsigned i = 0; i < 16; ++i)
    {
        if (name[i] != (char)p[i]) return false;
        if (!name[i]) return true;
    }
    return true;
}
} // namespace Detail

// The PIF in memory, the way loads it: the file, and the PIFEX
// header appended when the file is a bare 3.x header.
struct Image
{
    unsigned char bytes[ImageCapacity] = {};
    unsigned size = 0;
    void Clear() { Detail::Zero(bytes, sizeof bytes); size = 0; }
    bool Load(const unsigned char *data, unsigned length)
    {
        Clear();
        if (length < HeaderBytes || length > ImageCapacity - SectionHeader) return false;
        Detail::Move(bytes, data, length);
        size = length;
        if (size == HeaderBytes)
        {
            // "MICROSOFT PIFEX", next 0xffff, data 0, size 0x171.
            Detail::Copy(reinterpret_cast<char *>(bytes + HeaderBytes), "MICROSOFT PIFEX");
            Detail::PutWord(bytes + HeaderBytes + 16, 0xffff);
            Detail::PutWord(bytes + HeaderBytes + 18, 0);
            Detail::PutWord(bytes + HeaderBytes + 20, HeaderBytes);
            size += SectionHeader;
        }
        return true;
    }
    // the data of a named section, or 0.
    unsigned Find(const char *name, unsigned &length) const
    {
        length = 0;
        unsigned offset = HeaderBytes;
        for (unsigned guard = 0; guard < 32 && offset + SectionHeader <= size; ++guard)
        {
            const unsigned char *header = bytes + offset;
            const unsigned next = Detail::Word(header + 16), at = Detail::Word(header + 18), n = Detail::Word(header + 20);
            if (Detail::SectionName(header, name) && at + n <= size && n)
            {
                length = n;
                return at;
            }
            if (next == 0xffff || next <= offset) break;
            offset = next;
        }
        return 0;
    }
    // append a section at the end of the chain, zeroed or with data.
    unsigned Append(const char *name, unsigned length, const unsigned char *data)
    {
        if (size + SectionHeader + length > ImageCapacity) return 0;
        unsigned offset = HeaderBytes;
        for (unsigned guard = 0; guard < 32; ++guard)
        {
            const unsigned next = Detail::Word(bytes + offset + 16);
            if (next == 0xffff || next <= offset || next + SectionHeader > size) break;
            offset = next;
        }
        Detail::PutWord(bytes + offset + 16, size);
        unsigned char *header = bytes + size;
        Detail::Zero(header, SectionHeader);
        Detail::CopyN(header, name, 16);
        Detail::PutWord(header + 16, 0xffff);
        Detail::PutWord(header + 18, size + SectionHeader);
        Detail::PutWord(header + 20, length);
        const unsigned at = size + SectionHeader;
        if (data) Detail::Move(bytes + at, data, length);
        else Detail::Zero(bytes + at, length);
        size = at + length;
        return at;
    }
    // take a section out of the chain. The data and the header go;
    // every header and data offset behind them moves down, the header before
    // it takes over its link.
    bool Remove(const char *name)
    {
        using namespace Detail;
        unsigned chain[32], count = 0, found = 0;
        for (unsigned offset = HeaderBytes; count < 32 && offset + SectionHeader <= size;)
        {
            chain[count++] = offset;
            const unsigned next = Word(bytes + offset + 16);
            if (offset != HeaderBytes && !found && SectionName(bytes + offset, name)) found = offset;
            if (next == 0xffff || next <= offset) break;
            offset = next;
        }
        if (!found) return false;
        const unsigned header = found, data = Word(bytes + found + 18), n = Word(bytes + found + 20);
        if (data + n > size) return false;
        // Every offset behind a cut moves down by the cut.
        auto moved = [&](unsigned x) { return x - (x > data ? n : 0) - (x > header ? SectionHeader : 0); };
        const unsigned after = Word(bytes + header + 16);
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned char *h = bytes + chain[i];
            if (chain[i] == header) continue;
            unsigned link = Word(h + 16);
            if (link == header) link = after;
            if (link != 0xffff) link = moved(link);
            PutWord(h + 16, link);
            if (chain[i] != HeaderBytes) PutWord(h + 18, moved(Word(h + 18)));
        }
        // The larger range first, so the smaller one stays where it was.
        const unsigned first = data > header ? data : header, first_n = data > header ? n : SectionHeader;
        const unsigned second = data > header ? header : data, second_n = data > header ? SectionHeader : n;
        for (unsigned i = first; i + first_n < size; ++i) bytes[i] = bytes[i + first_n];
        size -= first_n;
        for (unsigned i = second; i + second_n < size; ++i) bytes[i] = bytes[i + second_n];
        size -= second_n;
        Zero(bytes + size, ImageCapacity - size);
        return true;
    }
    unsigned Section386() const { unsigned n; unsigned at = Find("WINDOWS 386 3.0", n); return n >= Section386Bytes ? at : 0; }
    unsigned SectionVmm40() const { unsigned n; unsigned at = Find("WINDOWS VMM 4.0", n); return n >= Vmm40Bytes ? at : 0; }
};

// The handle record: what keeps in its 0x241 bytes and its path
// buffer, without the local heap.
struct Record
{
    bool used = false;
    unsigned flags = 0;                // the word at +8
    char application[PathCapacity] = {}; // +0x13d, the full path of the program
    char pif[PathCapacity] = {};         // +0x35, the PIF the handle reads and writes
    bool is_pif = false;               // the program itself is the PIF
    Image image;
};

namespace Detail
{
// The default groups of every PIF of this tree, TSK to ENV of the VMM 4.0
// section as WINDOWS\Start Menu\Programs\MS-DOS Prompt.pif carries them.
// PIFMGR builds them from SYSTEM.INI [386Enh] and [NonWindowsApp]
// and the getters' defaults; these are the values that come out of it here.
inline void DefaultVmm40(unsigned char *v)
{
    Zero(v, Vmm40Bytes);
    Copy(reinterpret_cast<char *>(v + 0x58), "PIFMGR.DLL");
    static const unsigned char tsk[14] = {0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x32, 0};
    static const unsigned char vid[12] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static const unsigned char kbd[18] = {0x01, 0, 0, 0, 0x05, 0, 0x19, 0, 0x03, 0, 0xc8, 0, 0xe8, 0x03, 0x02, 0, 0x0a, 0};
    static const unsigned char mse[4] = {0x01, 0, 0, 0};
    static const unsigned char fnt[16] = {0x1c, 0, 0, 0, 0, 0, 0, 0, 0x07, 0, 0x0c, 0, 'T', 'e', 'r', 'm'};
    static const unsigned char win[38] = {0x03, 0, 0, 0, 0x50, 0, 0x19, 0, 0x30, 0x02, 0x2c, 0x01, 0x3c, 0x02, 0x67, 0x01, 0x16, 0, 0, 0,
                                          0x01, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0};
    Move(v + 0xaa, tsk, sizeof tsk);
    Move(v + 0xb8, vid, sizeof vid);
    Move(v + 0xc4, kbd, sizeof kbd);
    Move(v + 0xd6, mse, sizeof mse);
    Move(v + 0xde, fnt, 12);
    Copy(reinterpret_cast<char *>(v + 0xde + 12), "Terminal");
    Copy(reinterpret_cast<char *>(v + 0xde + 12 + 32), "Lucida Console");
    Move(v + 0x12c, win, sizeof win);
    PutWord(v + 0x1aa, 1);
}
// a space padded field of the 3.x header as a C string, the
// trailing spaces dropped.
inline void ReadPadded(char *out, const unsigned char *in, unsigned n)
{
    unsigned length = 0;
    for (unsigned i = 0; i < n && in[i]; ++i) out[length++] = (char)in[i];
    while (length && out[length - 1] == ' ') --length;
    out[length] = 0;
}
// the reverse, space padded to n.
inline void WritePadded(unsigned char *out, const char *in, unsigned n)
{
    unsigned i = 0;
    for (; i < n && in[i]; ++i) out[i] = (unsigned char)in[i];
    for (; i < n; ++i) out[i] = ' ';
}
// a name of the header into a field of the group, quoted when it
// has a space inside. Returns the length written.
inline unsigned ReadQuoted(char *out, unsigned capacity, const unsigned char *in)
{
    const char *text = reinterpret_cast<const char *>(in);
    unsigned space = 0;
    while (text[space] && text[space] != ' ') ++space;
    const bool quote = text[space] != 0;
    unsigned n = 0;
    if (quote) { out[n++] = '"'; capacity -= 2; }
    unsigned i = 0;
    for (; i + 1 < capacity && text[i]; ++i) out[n++] = text[i];
    if (quote) out[n++] = '"';
    out[n] = 0;
    return n;
}
// the first word of a command line into a field, the quotes taken
// off; answers where the rest begins.
inline unsigned WriteQuoted(unsigned char *out, unsigned capacity, const char *in)
{
    unsigned i = 0, n = 0;
    if (in[0] == '"')
    {
        ++i;
        for (; in[i] && in[i] != '"'; ++i) if (n + 1 < capacity) out[n++] = (unsigned char)in[i];
        if (in[i] == '"') ++i;
    }
    else
        for (; in[i] && in[i] != ' '; ++i) if (n + 1 < capacity) out[n++] = (unsigned char)in[i];
    out[n] = 0;
    while (in[i] == ' ') ++i;
    return i;
}
// KEYBOARD.131 MapVirtualKey of the US layout, the keys a DOS hot key can
// name: kind 0 is virtual key to scan code, kind 1 the reverse.
inline unsigned MapVirtualKey(unsigned code, unsigned kind)
{
    static const unsigned char pairs[][2] = {
        {0x08, 0x0e}, {0x09, 0x0f}, {0x0d, 0x1c}, {0x10, 0x2a}, {0x11, 0x1d}, {0x12, 0x38}, {0x13, 0x45}, {0x14, 0x3a},
        {0x1b, 0x01}, {0x20, 0x39}, {0x21, 0x49}, {0x22, 0x51}, {0x23, 0x4f}, {0x24, 0x47}, {0x25, 0x4b}, {0x26, 0x48},
        {0x27, 0x4d}, {0x28, 0x50}, {0x2c, 0x54}, {0x2d, 0x52}, {0x2e, 0x53}, {0x30, 0x0b}, {0x31, 0x02}, {0x32, 0x03},
        {0x33, 0x04}, {0x34, 0x05}, {0x35, 0x06}, {0x36, 0x07}, {0x37, 0x08}, {0x38, 0x09}, {0x39, 0x0a}, {0x41, 0x1e},
        {0x42, 0x30}, {0x43, 0x2e}, {0x44, 0x20}, {0x45, 0x12}, {0x46, 0x21}, {0x47, 0x22}, {0x48, 0x23}, {0x49, 0x17},
        {0x4a, 0x24}, {0x4b, 0x25}, {0x4c, 0x26}, {0x4d, 0x32}, {0x4e, 0x31}, {0x4f, 0x18}, {0x50, 0x19}, {0x51, 0x10},
        {0x52, 0x13}, {0x53, 0x1f}, {0x54, 0x14}, {0x55, 0x16}, {0x56, 0x2f}, {0x57, 0x11}, {0x58, 0x2d}, {0x59, 0x15},
        {0x5a, 0x2c}, {0x60, 0x52}, {0x61, 0x4f}, {0x62, 0x50}, {0x63, 0x51}, {0x64, 0x4b}, {0x65, 0x4c}, {0x66, 0x4d},
        {0x67, 0x47}, {0x68, 0x48}, {0x69, 0x49}, {0x6a, 0x37}, {0x6b, 0x4e}, {0x6d, 0x4a}, {0x6e, 0x53}, {0x6f, 0x35},
        {0x70, 0x3b}, {0x71, 0x3c}, {0x72, 0x3d}, {0x73, 0x3e}, {0x74, 0x3f}, {0x75, 0x40}, {0x76, 0x41}, {0x77, 0x42},
        {0x78, 0x43}, {0x79, 0x44}, {0x7a, 0x57}, {0x7b, 0x58}, {0x90, 0x45}, {0x91, 0x46}, {0xba, 0x27}, {0xbb, 0x0d},
        {0xbc, 0x33}, {0xbd, 0x0c}, {0xbe, 0x34}, {0xbf, 0x35}, {0xc0, 0x29}, {0xdb, 0x1a}, {0xdc, 0x2b}, {0xdd, 0x1b},
        {0xde, 0x28}};
    // The scan codes of the keypad map back to the cursor keys, as the
    // keyboard driver answers without Num Lock; the keypad bit tells them apart.
    for (const auto &p : pairs)
        if (kind == 0 ? p[0] == code : p[1] == code && !(p[0] >= 0x60 && p[0] <= 0x6f)) return kind == 0 ? p[1] : p[0];
    return 0;
}
// the hot key of the 386 section (scan code word, shift state
// word, mask word, flag byte 1 extended, 2 keypad) as the VK word a hot key
// control takes: the key in the low byte, HOTKEYF_SHIFT 1, CONTROL 2, ALT 4
// and EXT 8 in the high byte.
inline unsigned HotKeyFromPif(const unsigned char *h)
{
    if (!Word(h)) return 0;
    unsigned key = MapVirtualKey(Word(h), 1);
    if (h[6] & 2)
    {
        // The keypad table at : the scan codes of VK_NUMPAD0 to 9.
        static const unsigned char pad[10] = {0x52, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49};
        for (unsigned i = 0; i < 10; ++i)
            if (pad[i] == Word(h)) { key = 0x60 + i; break; }
    }
    if (h[6] & 1) key |= 0x800;
    if (Word(h + 2) & 3) key |= 0x100;
    if (Word(h + 2) & 0xc004) key |= 0x200;
    if (Word(h + 2) & 0x3008) key |= 0x400;
    return key;
}
// the reverse, into the seven bytes of the 386 section.
inline void HotKeyToPif(unsigned char *h, unsigned key)
{
    Zero(h, 7);
    if (!key) return;
    PutWord(h, MapVirtualKey(key & 0xff, 0));
    PutWord(h + 4, 0xf);
    if (key & 0x800) h[6] = 1;
    if ((key & 0xff) - 0x60 < 10) h[6] |= 2;
    if (key & 0x100) PutWord(h + 2, 3);
    if (key & 0x200) PutWord(h + 2, Word(h + 2) | 4);
    if (key & 0x400) PutWord(h + 2, Word(h + 2) | 8);
}
} // namespace Detail

// GETPROPERTIES group 1, the PRG getter.
inline unsigned GetPrg(const Record &r, unsigned char *out, unsigned cb, unsigned flags)
{
    using namespace Detail;
    if (cb < PrgBytes || !r.image.size) return 0;
    const unsigned char *raw = r.image.bytes;
    const unsigned s386 = r.image.Section386(), vmm = r.image.SectionVmm40();
    Zero(out, PrgBytes);
    PutWord(out + PrgFlagsInit, r.flags & 0x7000);
    Copy(reinterpret_cast<char *>(out + PrgIconFile), "PIFMGR.DLL");
    CopyN(out + PrgPifFile, r.pif, 0x104);
    if (raw[0x63] & 0x10) PutWord(out + PrgFlags, Word(out + PrgFlags) | PrgCloseOnExit);
    ReadPadded(reinterpret_cast<char *>(out + PrgTitle), raw + 2, 0x1e);
    unsigned length = ReadQuoted(reinterpret_cast<char *>(out + PrgCmdLine), 0x80, raw + 0x24);
    const unsigned char *parameters = raw + 0xa5;
    if (s386)
    {
        const unsigned char *p = raw + s386;
        parameters = p + 0x28;
        const unsigned w10 = Word(p + 0x10), w12 = Word(p + 0x12);
        unsigned init = Word(out + PrgFlagsInit);
        if (w12 & 0x100) init |= 0x0004;
        if (w12 & 0x400) PutWord(out + PrgFlags, Word(out + PrgFlags) | 0x0400);
        init |= (w12 >> 4) & 0x70b;
        PutWord(out + PrgFlagsInit, init);
        //  calls when the option word says there is a hot key.
        PutWord(out + PrgHotKey, (w10 & 0x4000) ? HotKeyFromPif(p + 0x18) : 0);
    }
    // The field is 64 bytes, padded with blanks and not always closed by a
    // zero: the section header behind it must not become a parameter.
    char padded[0x41];
    ReadPadded(padded, parameters, 0x40);
    if (padded[0])
    {
        const unsigned n = Length(padded);
        if (0x7f - length > n)
        {
            char *line = reinterpret_cast<char *>(out + PrgCmdLine);
            line[length++] = ' ';
            CopyN(line + length, padded, 0x80 - length);
        }
    }
    ReadQuoted(reinterpret_cast<char *>(out + PrgWorkDir), 0x40, raw + 0x65);
    if (vmm)
    {
        const unsigned char *v = raw + vmm;
        if (v[0x58])
        {
            CopyN(out + PrgIconFile, reinterpret_cast<const char *>(v + 0x58), 0x50);
            PutWord(out + PrgIconIndex, Word(v + 0xa8));
        }
        PutDword(out + PrgEnhModeFlags, Dword(v));
        PutDword(out + PrgRealModeFlags, Dword(v + 4));
        CopyN(out + PrgOtherFile, reinterpret_cast<const char *>(v + 8), 0x50);
    }
    (void)flags; // GetNoConversion: OemToAnsi of title, command line and work dir, an identity for this tree
    return PrgBytes;
}

// The PRG setter. Needs the 386 and the VMM 4.0 section.
inline unsigned SetPrg(Record &r, const unsigned char *in, unsigned cb, unsigned flags)
{
    using namespace Detail;
    if (cb < PrgBytes || !r.image.size) return 0;
    unsigned char *raw = r.image.bytes;
    const unsigned s386 = r.image.Section386(), vmm = r.image.SectionVmm40();
    if (!s386 || !vmm) return 0;
    unsigned char *p = raw + s386, *v = raw + vmm;
    raw[0x63] &= 0xef;
    if (Word(in + PrgFlags) & PrgCloseOnExit) raw[0x63] |= 0x10;
    PutDword(p + 0x10, Dword(p + 0x10) & 0x8a4fbfffu);
    if (Word(in + PrgHotKey)) p[0x11] |= 0x40;
    if (Word(in + PrgFlagsInit) & 0x0004) p[0x13] |= 1;
    if (Word(in + PrgFlags) & 0x0400) p[0x13] |= 4;
    PutWord(p + 0x12, Word(p + 0x12) | ((Word(in + PrgFlagsInit) & 0x70b) << 4));
    WritePadded(raw + 2, reinterpret_cast<const char *>(in + PrgTitle), 0x1e);
    const char *line = reinterpret_cast<const char *>(in + PrgCmdLine);
    const unsigned rest = WriteQuoted(raw + 0x24, 0x3f, line);
    CopyN(raw + 0xa5, line + rest, 0x40);
    CopyN(p + 0x28, line + rest, 0x40);
    const char *directory = reinterpret_cast<const char *>(in + PrgWorkDir);
    if (directory[0] != '"') CopyN(raw + 0x65, directory, 0x40);
    else WriteQuoted(raw + 0x65, 0x40, directory);
    //  calls : the VK word back into the 386 hot key bytes.
    HotKeyToPif(p + 0x18, Word(in + PrgHotKey));
    CopyN(v + 0x58, reinterpret_cast<const char *>(in + PrgIconFile), 0x50);
    PutWord(v + 0xa8, Word(in + PrgIconIndex));
    PutDword(v, Dword(in + PrgEnhModeFlags));
    PutDword(v + 4, Dword(in + PrgRealModeFlags));
    CopyN(v + 8, reinterpret_cast<const char *>(in + PrgOtherFile), 0x50);
    CopyN(r.pif, reinterpret_cast<const char *>(in + PrgPifFile), 0x104);
    (void)flags; // SetNoConversion: AnsiToOem of the four names, an identity here
    r.flags |= RecordDirty;
    return PrgBytes;
}

// The MEM getter at : PROPMEM {flMem, flMemInit, wMinLow, wMaxLow,
// wMinEMS, wMaxEMS, wMinXMS, wMaxXMS} from the 386 section.
inline unsigned GetMem(const Record &r, unsigned char *out, unsigned cb)
{
    using namespace Detail;
    if (cb < 16) return 0;
    Zero(out, 16);
    const unsigned s386 = r.image.Section386();
    if (!s386) return 16;
    const unsigned char *p = r.image.bytes + s386;
    const unsigned w12 = Word(p + 0x12);
    unsigned init = 0;
    if (p[0x11] & 0x20) init |= 1;
    if (w12 & 4) init |= 2;
    if (p[0x11] & 0x80) init |= 4;
    if (w12 & 1) init |= 8;
    if (w12 & 8) init |= 0x10;
    if (w12 & 0x800) init |= 0x40;
    if (w12 & 0x200) init |= 0x20;
    PutWord(out + 2, init);
    PutWord(out + 4, Word(p + 2));
    PutWord(out + 6, Word(p + 0));
    PutWord(out + 8, Word(p + 0xa));
    PutWord(out + 0xa, Word(p + 8));
    PutWord(out + 0xc, Word(p + 0xe));
    PutWord(out + 0xe, Word(p + 0xc));
    return 16;
}
// The mirror of GetMem; the setter remains incomplete.
inline unsigned SetMem(Record &r, const unsigned char *in, unsigned cb)
{
    using namespace Detail;
    if (cb < 16) return 0;
    const unsigned s386 = r.image.Section386();
    if (!s386) return 0;
    unsigned char *p = r.image.bytes + s386;
    const unsigned init = Word(in + 2);
    p[0x11] &= (unsigned char)~0xa0u;
    unsigned w12 = Word(p + 0x12) & ~0xa0du;
    if (init & 1) p[0x11] |= 0x20;
    if (init & 2) w12 |= 4;
    if (init & 4) p[0x11] |= 0x80;
    if (init & 8) w12 |= 1;
    if (init & 0x10) w12 |= 8;
    if (init & 0x40) w12 |= 0x800;
    if (init & 0x20) w12 |= 0x200;
    PutWord(p + 0x12, w12);
    PutWord(p + 2, Word(in + 4));
    PutWord(p + 0, Word(in + 6));
    PutWord(p + 0xa, Word(in + 8));
    PutWord(p + 8, Word(in + 0xa));
    PutWord(p + 0xe, Word(in + 0xc));
    PutWord(p + 0xc, Word(in + 0xe));
    r.flags |= RecordDirty;
    return 16;
}

// The TSK and VID getters return the defaults of
// the module (ds:0x2fe, ds:0x30c), the group of the VMM 4.0 section over
// them, and the bits the 386 section decides: flTsk takes the two low bits
// of the option word 0x10 and loses its idle seconds without bit 0x1000;
// flVid takes bits 0x81 of the video word 0x14 and full screen, 0x100, from
// option bit 0x0008.
inline unsigned GetTsk(const Record &r, unsigned char *out, unsigned cb)
{
    using namespace Detail;
    if (cb < 14) return 0;
    unsigned char v[Vmm40Bytes];
    DefaultVmm40(v);
    Move(out, v + 0xaa, 14);
    if (const unsigned vmm = r.image.SectionVmm40()) Move(out, r.image.bytes + vmm + 0xaa, 14);
    if (const unsigned s386 = r.image.Section386())
    {
        const unsigned w10 = Word(r.image.bytes + s386 + 0x10);
        PutWord(out, (Word(out) & ~7u) | (w10 & 3));
        if (!(w10 & 0x1000)) PutWord(out + 0xc, 0);
    }
    return 14;
}
inline unsigned GetVid(const Record &r, unsigned char *out, unsigned cb)
{
    using namespace Detail;
    if (cb < 12) return 0;
    unsigned char v[Vmm40Bytes];
    DefaultVmm40(v);
    Move(out, v + 0xb8, 12);
    if (const unsigned vmm = r.image.SectionVmm40()) Move(out, r.image.bytes + vmm + 0xb8, 12);
    if (const unsigned s386 = r.image.Section386())
    {
        const unsigned char *p = r.image.bytes + s386;
        unsigned flags = (Word(out) & 0xfe7e) | (p[0x14] & 0x81);
        if (p[0x10] & 8) flags |= 0x100;
        PutWord(out, flags);
    }
    return 12;
}
// The mirrors of the two getters into the 386 section beside the copy into
// the VMM 4.0 section; the setters remain incomplete.
inline void SetTskVid386(Record &r, unsigned group, const unsigned char *in)
{
    using namespace Detail;
    const unsigned s386 = r.image.Section386();
    if (!s386) return;
    unsigned char *p = r.image.bytes + s386;
    if (group == GroupTsk) PutWord(p + 0x10, (Word(p + 0x10) & ~3u) | (Word(in) & 3));
    else if (group == GroupVid)
    {
        p[0x14] = (unsigned char)((p[0x14] & ~0x81u) | (Word(in) & 0x81));
        if (Word(in) & 0x100) p[0x10] |= 8; else p[0x10] &= (unsigned char)~8u;
    }
}

//  after the handle check: the first sight of a VMM 4.0 section
// whose marker is not 1 resets the reserved words and marks the record dirty.
inline void Convert(Record &r)
{
    using namespace Detail;
    const unsigned vmm = r.image.SectionVmm40();
    if (!vmm) return;
    unsigned char *v = r.image.bytes + vmm;
    if (Word(v + 0x1aa) == 1) return;
    PutWord(v + 0x1aa, 1);
    r.flags |= RecordDirty;
    static const unsigned words[] = {0xae, 0xb0, 0xb2, 0xb4, 0xbc, 0xbe, 0xc0, 0x1a8, 0x152, 0x154};
    for (unsigned at : words) PutWord(v + at, 0);
    if (const unsigned s386 = r.image.Section386()) r.image.bytes[s386 + 0x12] &= 0xbf;
}

// the VMM 4.0 section built from what the PIF has, for a PIF that
// predates Windows 95.
inline void EnsureVmm40(Record &r)
{
    if (r.image.SectionVmm40()) return;
    unsigned char prg[PrgBytes], v[Vmm40Bytes];
    Detail::DefaultVmm40(v);
    if (GetPrg(r, prg, sizeof prg, 0))
    {
        Detail::PutDword(v, Detail::Dword(prg + PrgEnhModeFlags));
        Detail::PutDword(v + 4, Detail::Dword(prg + PrgRealModeFlags));
        Detail::CopyN(v + 8, reinterpret_cast<const char *>(prg + PrgOtherFile), 0x50);
        Detail::CopyN(v + 0x58, reinterpret_cast<const char *>(prg + PrgIconFile), 0x50);
        Detail::PutWord(v + 0xa8, Detail::Word(prg + PrgIconIndex));
    }
    r.image.Append("WINDOWS VMM 4.0", Vmm40Bytes, v);
    //  marks the record dirty only when it copies data in; a section
    // built in memory is written when something else changes the file.
}
inline void Ensure386(Record &r)
{
    if (r.image.Section386()) return;
    unsigned char p[Section386Bytes];
    Detail::Zero(p, sizeof p);
    Detail::PutWord(p + 0, Detail::Word(r.image.bytes + 0x20));
    Detail::PutWord(p + 2, Detail::Word(r.image.bytes + 0x22));
    Detail::PutWord(p + 4, 100);
    Detail::PutWord(p + 6, 50);
    Detail::PutWord(p + 8, 0xffff);
    Detail::PutWord(p + 0xc, 0xffff);
    Detail::PutWord(p + 0x10, 0x1002);
    Detail::PutWord(p + 0x12, 0x0002);
    Detail::PutWord(p + 0x14, 0x001f);
    r.image.Append("WINDOWS 386 3.0", Section386Bytes, p);
}

// the PIF of a program that has none. Title from the name, the
// program's full path, the memory and video defaults, close on exit only for
// the command interpreter, a 386 section and a VMM 4.0 section.
inline void DefaultImage(Record &r, Environment &e)
{
    using namespace Detail;
    unsigned char raw[HeaderBytes];
    Zero(raw, sizeof raw);
    raw[1] = 0x78;
    char title[32] = {};
    CopyN(title, Leaf(r.application), 31);
    for (char *p = title; *p; ++p) if (*p == '.') { *p = 0; break; }
    WritePadded(raw + 2, title, 0x1e);
    PutWord(raw + 0x20, 0x0200);
    PutWord(raw + 0x22, 0x0080);
    CopyN(raw + 0x24, r.application, 0x3f);
    raw[0x63] = SameText(Leaf(r.application), e.ComSpecName()) ? 0x10 : 0;
    raw[0xe6] = 1;
    raw[0xe8] = 0xff;
    raw[0xe9] = 25;
    raw[0xea] = 80;
    PutWord(raw + 0xed, 7);
    r.image.Load(raw, HeaderBytes); // appends the PIFEX header
    Ensure386(r);
    EnsureVmm40(r);
}

// the file. Nothing to do without a change or with the PIF inhibited.
inline bool Flush(Record &r, Environment &e, bool discard)
{
    if (!(r.flags & RecordDirty) || (r.flags & RecordInhibitPif)) return true;
    if (discard)
    {
        r.flags &= ~(RecordDirty | RecordShrunk);
        return true;
    }
    if (r.flags & RecordStale) return false;
    if (!e.WriteFile(r.pif, r.image.bytes, r.image.size)) return false;
    r.flags |= RecordFileNamed;
    r.flags &= ~(RecordDirty | RecordShrunk | RecordNoFile | RecordNoPif | RecordDefaultPif);
    return true;
}

// OPENPROPERTIES. Answers 1 with the record filled, 0 refused.
inline unsigned Open(Record &r, Environment &e, const char *application, const char *pif_name, unsigned inf,
                     unsigned flags)
{
    using namespace Detail;
    (void)inf; // APPS.INF is not read
    r = Record();
    r.used = true;
    if (flags & OpenRawIo) r.flags |= RecordRawIo;
    if (flags & OpenInhibitPif) r.flags |= RecordInhibitPif;
    char full[PathCapacity];
    e.FullPath(application, full);
    CopyN(r.application, full, sizeof r.application);
    // The candidate PIF: the program's path with.PIF in place of.EXE,.COM
    // or.BAT; a name without an extension gets.PIF appended; the program
    // may be the PIF itself; any other extension is refused.
    char candidate[PathCapacity];
    CopyN(candidate, full, sizeof candidate);
    char *name = candidate + (Leaf(candidate) - candidate);
    char *extension = name;
    while (*extension && *extension != '.') ++extension;
    if (!*extension)
    {
        if (Length(candidate) + 4 >= sizeof candidate) return 0;
        Copy(extension, ".PIF");
    }
    else if (SameText(extension, ".EXE") || SameText(extension, ".COM") || SameText(extension, ".BAT")) Copy(extension, ".PIF");
    else if (SameText(extension, ".PIF")) r.is_pif = true;
    else return 0;
    const bool has_directory = name != candidate;
    bool found = false;
    unsigned char file[ImageCapacity];
    unsigned size = 0;
    if (!(flags & OpenNoPifSearch))
    {
        r.flags |= RecordFileNamed;
        // Beside the program, or the PIF itself.
        if ((has_directory || r.is_pif) && e.ReadFile(candidate, file, sizeof file, size)) found = true;
        // WINDOWS\PIF\<name>.PIF.
        if (!found && !r.is_pif)
        {
            char windows[PathCapacity], path[PathCapacity];
            e.WindowsDirectory(windows);
            CopyN(path, windows, sizeof path);
            const unsigned n = Length(path);
            if (n + 5 + Length(name) < sizeof path)
            {
                Copy(path + n, "\\PIF\\");
                Copy(path + n + 5, name);
                if (e.ReadFile(path, file, sizeof file, size))
                {
                    found = true;
                    r.flags |= RecordFromPifDirectory;
                    CopyN(candidate, path, sizeof candidate);
                }
            }
        }
        if (found)
        {
            CopyN(r.pif, candidate, sizeof r.pif);
            if (!r.image.Load(file, size)) return 0;
            r.flags |= RecordLoaded;
        }
        else if (r.is_pif) return 0;
    }
    if (!found)
    {
        // _DEFAULT.PIF of WINDOWS\PIF, else the defaults of the
        // module; the PIF the handle would write is the candidate.
        r.flags &= ~RecordFileNamed;
        char windows[PathCapacity], path[PathCapacity];
        e.WindowsDirectory(windows);
        CopyN(path, windows, sizeof path);
        const unsigned n = Length(path);
        bool defaults = false;
        if (n + 18 < sizeof path)
        {
            Copy(path + n, "\\PIF\\_DEFAULT.PIF");
            if (e.ReadFile(path, file, sizeof file, size) && r.image.Load(file, size)) defaults = true;
        }
        CopyN(r.pif, candidate, sizeof r.pif);
        if (defaults)
        {
            // the default PIF gets the program's name and path.
            r.flags |= RecordDefaultPif | RecordLoaded;
            char title[32] = {};
            CopyN(title, Leaf(r.application), 31);
            for (char *p = title; *p; ++p) if (*p == '.') { *p = 0; break; }
            WritePadded(r.image.bytes + 2, title, 0x1e);
            CopyN(r.image.bytes + 0x24, r.application, 0x3f);
            EnsureVmm40(r);
        }
        else
        {
            r.flags |= RecordNoPif | RecordNoFile;
            DefaultImage(r, e);
        }
    }
    else EnsureVmm40(r);
    if (pif_name && pif_name[0])
    {
        // a PIF named by the caller becomes the file of the handle
        // and is written at once.
        CopyN(r.pif, pif_name, sizeof r.pif);
        r.flags |= RecordDirty;
        r.flags &= ~RecordFileNamed;
        if (!Flush(r, e, false)) { r.used = false; return 0; }
    }
    return 1;
}

// GETPROPERTIES, for a group named by ordinal.
inline unsigned Get(Record &r, unsigned group, unsigned char *out, unsigned cb, unsigned flags)
{
    using namespace Detail;
    if (!r.used || !out) return 0;
    if (flags & GetRawIo)
    {
        if (!(r.flags & RecordRawIo)) return 0;
        const unsigned n = cb < r.image.size ? cb : r.image.size;
        Move(out, r.image.bytes, n);
        return n;
    }
    Convert(r);
    if (group < 1 || group > 10 || cb < GroupBytes[group]) return 0;
    Zero(out, cb);
    if (group == GroupPrg) return GetPrg(r, out, cb, flags);
    if (group == GroupMem) return GetMem(r, out, cb);
    if (group == GroupTsk) return GetTsk(r, out, cb);
    if (group == GroupVid) return GetVid(r, out, cb);
    const unsigned vmm = r.image.SectionVmm40();
    if (vmm) Move(out, r.image.bytes + vmm + GroupVmmOffset[group], GroupBytes[group]);
    else
    {
        unsigned char v[Vmm40Bytes];
        DefaultVmm40(v);
        Move(out, v + GroupVmmOffset[group], GroupBytes[group]);
    }
    return GroupBytes[group];
}

// SETPROPERTIES, for a group named by ordinal. An unchanged group
// changes nothing; the file is written unless the caller says SetNoFlush.
inline unsigned Set(Record &r, Environment &e, unsigned group, const unsigned char *in, unsigned cb, unsigned flags)
{
    using namespace Detail;
    if (!r.used || !in) return 0;
    if (flags & SetRawIo)
    {
        if (!(r.flags & RecordRawIo) || cb < HeaderBytes || cb > ImageCapacity) return 0;
        if (cb < r.image.size) r.flags |= RecordShrunk;
        Move(r.image.bytes, in, cb);
        r.image.size = cb;
        r.flags |= RecordDirty;
        if (!(flags & SetNoFlush)) Flush(r, e, false);
        return cb;
    }
    if (group < 1 || group > 10 || cb < GroupBytes[group]) return 0;
    unsigned char current[PrgBytes];
    const unsigned have = Get(r, group, current, sizeof current, flags & SetNoConversion);
    unsigned result = have;
    if (!have || !Equal(current, in, GroupBytes[group]))
    {
        if (group < 5) Ensure386(r);
        if (group != GroupMem) EnsureVmm40(r);
        if (group == GroupPrg) result = SetPrg(r, in, cb, flags);
        else if (group == GroupMem) result = SetMem(r, in, cb);
        else
        {
            const unsigned vmm = r.image.SectionVmm40();
            if (!vmm) return 0;
            Move(r.image.bytes + vmm + GroupVmmOffset[group], in, GroupBytes[group]);
            SetTskVid386(r, group, in);
            r.flags |= RecordDirty;
            result = GroupBytes[group];
        }
    }
    if (!(flags & SetNoFlush) && !Flush(r, e, false)) return 0;
    return result;
}

// NEO98's own section: the options of the machine that runs the program,
// which no Win98 page has, as "name=value" lines (CR LF) ending in a zero.
// Win98's PIFMGR reads and writes the file as a whole and keeps a section it
// does not know, so the PIF stays valid there. src/x86_pif_options.cpp.
constexpr char OptionSection[] = "MACHINE OPTIONS";
// The two sections of MS-DOS mode (Advanced dialog of the Program page).
constexpr char ConfigSection[] = "CONFIG  SYS 4.0", AutoexecSection[] = "AUTOEXECBAT 4.0";
// GETPROPERTIES and SETPROPERTIES with a far pointer in lpszGroup: the
// group is a section of the file by its name, "CONFIG SYS 4.0" or
// "AUTOEXECBAT 4.0" for the MS-DOS mode of the Advanced dialog.
// Get answers the size of the section and copies at most `cb`
// bytes; a null buffer asks for the size alone.
inline unsigned GetNamed(const Record &r, const char *name, unsigned char *out, unsigned cb)
{
    if (!r.used) return 0;
    unsigned length = 0;
    const unsigned at = r.image.Find(name, length);
    if (!at) return 0;
    if (out) Detail::Move(out, r.image.bytes + at, cb < length ? cb : length);
    return length;
}
// The section writer: the same size is written over, another size takes the
// section out and appends it anew; zero bytes remove it.
inline unsigned SetNamed(Record &r, Environment &e, const char *name, const unsigned char *in, unsigned cb, unsigned flags)
{
    if (!r.used || Detail::Length(name) > 15) return 0;
    unsigned length = 0;
    const unsigned at = r.image.Find(name, length);
    if (at && length == cb && in)
    {
        if (!Detail::Equal(r.image.bytes + at, in, cb))
        {
            Detail::Move(r.image.bytes + at, in, cb);
            r.flags |= RecordDirty;
        }
    }
    else
    {
        if (at) { r.image.Remove(name); r.flags |= RecordDirty; }
        if (cb && in)
        {
            if (!r.image.Append(name, cb, in)) return 0;
            r.flags |= RecordDirty;
        }
    }
    if (!(flags & SetNoFlush) && !Flush(r, e, false)) return 0;
    return cb;
}

// CLOSEPROPERTIES. Writes a changed PIF unless told to discard.
inline unsigned Close(Record &r, Environment &e, unsigned flags)
{
    if (!r.used) return 0;
    if (flags & CloseDiscard) r.flags |= RecordNoFile;
    if ((r.flags & RecordDirty) && !(r.flags & RecordInhibitPif)) Flush(r, e, (flags & CloseDiscard) != 0);
    r.used = false;
    return 0;
}
} // namespace Win98PifMgr
