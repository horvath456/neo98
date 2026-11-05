// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 MSVCRT.DLL 887eb5ce93edb7192ca3e9220f07f9ca0f94db02af5862ebcbdfcb852db99fd1.
#pragma once
namespace Win98CrtArgs
{
struct Counts
{
    unsigned pointers, characters; // Pointer count includes the final null.
};
struct State
{
    unsigned argc = 0;
    char **argv = nullptr, **environ = nullptr, **initenv = nullptr;
    unsigned new_mode = 0;
    char program_name[260] = {};
};
enum class Status : unsigned
{
    Ok = 0,
    ArgumentMemory = 8, // Original _amsg_exit message number.
    EnvironmentMemory = 9,
    UnsupportedWildcards = 0x100,
    InvalidInput = 0x101
};
struct Environment
{
    virtual ~Environment() = default;
    // Ownership remains with the provider until process teardown. Repeated
    // __getmainargs calls leave earlier arrays valid in the original CRT.
    virtual void *Allocate(unsigned bytes) = 0;
    virtual void ModuleName(char *out, unsigned capacity) = 0;
    // Called with the original wildcard parser's leading provenance byte on
    // EVERY argument. A successful expander must remove those marker bytes.
    virtual Status ExpandWildcards(State &) = 0;
};
// Original SBCS parser7800B6F9; markers select its wildcard sibling7800AF02.
// Null output pointers measure the required storage. Input must be NUL-terminated.
Counts Parse(const char *input, char **argv, char *strings, bool markers = false);
Status GetMainArgs(Environment &, State &, const char *command_line, bool wildcards, unsigned new_mode);
// Original78004485: borrow a double-NUL block, omit entries starting with '=',
// allocate an environment pointer table and a separate copy of each entry.
Status InitializeEnvironment(Environment &, State &, const char *block);
} // namespace Win98CrtArgs
