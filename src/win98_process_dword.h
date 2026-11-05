// SPDX-License-Identifier: GPL-2.0-or-later
// GetProcessDword reads Windows 98 process fields through ordinal 18. Process
// ID zero selects the caller; an unknown ID returns zero. Multiples of four
// from -56 through zero select fields. Other offsets return the process
// database pointer.
#pragma once

namespace Win98ProcessDword
{
// Process or Win16 task fields exposed by GetProcessDword.
enum Field : int
{
    TaskSlot4e = -56,      // task database word at 0x4e
    DatabaseWordBc = -52,  //
    TaskSlot1c = -48,      // the task's hInstance, KRNL386 GETTASKDS
    TaskSlot1a = -44,      // the expected Windows version, GETEXEVERSION
    CurrentThread = -40,
    ProcessIdentity = -36, // the database mapped to an identity
    Flagged3c = -32,       // only when byte 0x2d has bit 2
    Flagged38 = -28,       // only when byte 0x2d has bit 1
    Word30 = -24,          //
    Pair18 = -20,          // two packed words from offset 0x18
    Pair10 = -16,          // two packed words from offset 0x10
    Value2c = -12,         //
    ParentIdentity = -8,   // database at 0x48 mapped, else zero
    Value20 = -4,          //
    Value90 = 0,           //
};

// the range test is unsigned on offset + 0x38.
inline bool InRange(unsigned field)
{
    return (field + 0x38u) <= 0x38u;
}
// only the multiples of four carry a case.
inline bool Selected(unsigned field)
{
    return InRange(field) && !(field & 3);
}

// a packed pair takes two dwords, turns 0x80000000 into 0x8000,
// keeps the low word of each and returns the second in the high half.
inline unsigned Pack(unsigned low, unsigned high)
{
    const unsigned first = low == 0x80000000u ? 0x8000u : (low & 0xffffu);
    const unsigned second = high == 0x80000000u ? 0x8000u : (high & 0xffffu);
    return ((second & 0xffffu) << 16) | (first & 0xffffu);
}

struct Environment
{
    virtual ~Environment() = default;
    // The process database of an identifier, or zero when it is unknown.
    // Zero selects the caller's own data.
    virtual unsigned Database(unsigned identifier) = 0;
    // One implemented field, or false when this NEO98 build has no
    // counterpart for it.
    virtual bool Read(unsigned database, int field, unsigned &value) = 0;
};

inline unsigned Get(Environment &e, unsigned identifier, unsigned field, bool &known)
{
    known = true;
    const unsigned database = e.Database(identifier);
    if (!database)
        return 0; // an unknown process returns zero.
    if (!Selected(field))
        return database; // and the gap entries share the epilogue.
    unsigned value = 0;
    known = e.Read(database, int(field), value);
    return known ? value : 0;
}
} // namespace Win98ProcessDword
