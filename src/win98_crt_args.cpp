// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_crt_args.h"
#include <string.h>
namespace Win98CrtArgs
{
Counts Parse(const char *input, char **argv, char *strings, bool markers)
{
    Counts counts = {1, 0};
    auto put = [&](char c) {
        if (strings)
            *strings++ = c;
        ++counts.characters;
    };
    auto argument = [&]() {
        if (argv)
            *argv++ = strings;
        if (markers)
            put(*input);
    };
    argument();
    // argv[0] uses its own rule: only an INITIAL quote is special. Backslashes
    // and embedded quotes in an unquoted executable name are copied verbatim.
    if (*input == '"')
    {
        ++input;
        while (*input && *input != '"')
            put(*input++);
        put(0);
        if (*input == '"')
            ++input;
    }
    else
    {
        char c;
        do
        {
            c = *input++;
            put(c);
        } while (c && c != ' ' && c != '\t');
        if (!c)
            --input;
        else if (strings)
            strings[-1] = 0;
    }
    bool quoted = false;
    while (*input)
    {
        while (*input == ' ' || *input == '\t')
            ++input;
        if (!*input)
            break;
        argument();
        ++counts.pointers;
        for (;;)
        {
            bool copy = true;
            unsigned slashes = 0;
            while (*input == '\\')
            {
                ++input;
                ++slashes;
            }
            if (*input == '"')
            {
                if (!(slashes & 1))
                {
                    if (quoted && input[1] == '"')
                        ++input;
                    else
                        copy = false;
                    // The old CRT toggles quote state even for the double-quote
                    // case above. This differs from later Microsoft parsers.
                    quoted = !quoted;
                }
                slashes >>= 1;
            }
            while (slashes--)
                put('\\');
            if (!*input || (!quoted && (*input == ' ' || *input == '\t')))
                break;
            if (copy)
                put(*input);
            ++input;
        }
        put(0);
    }
    if (argv)
        *argv = nullptr;
    ++counts.pointers;
    return counts;
}
Status GetMainArgs(Environment &e, State &s, const char *command_line, bool wildcards, unsigned new_mode)
{
    // Public7800B50F updates _newmode before choosing the two startup paths.
    s.new_mode = new_mode;
    if (!command_line)
        return Status::InvalidInput;
    e.ModuleName(s.program_name, sizeof s.program_name);
    s.program_name[sizeof s.program_name - 1] = 0;
    const char *input = *command_line ? command_line : s.program_name;
    const Counts need = Parse(input, nullptr, nullptr, wildcards);
    if (need.pointers > (~0u - need.characters) / sizeof(char *))
        return Status::ArgumentMemory;
    auto **argv = static_cast<char **>(e.Allocate(need.pointers * sizeof(char *) + need.characters));
    if (!argv)
        return Status::ArgumentMemory;
    Parse(input, argv, reinterpret_cast<char *>(argv + need.pointers), wildcards);
    s.argv = argv;
    s.argc = need.pointers - 1;
    return wildcards ? e.ExpandWildcards(s) : Status::Ok;
}
Status InitializeEnvironment(Environment &e, State &s, const char *block)
{
    if (!block)
        return Status::InvalidInput;
    unsigned count = 0;
    for (const char *at = block; *at; at += strlen(at) + 1)
        if (*at != '=')
            ++count;
    if (count > ~0u / sizeof(char *) - 1)
        return Status::EnvironmentMemory;
    s.environ = static_cast<char **>(e.Allocate((count + 1) * sizeof(char *)));
    if (!s.environ)
        return Status::EnvironmentMemory;
    unsigned index = 0;
    for (const char *at = block; *at; at += strlen(at) + 1)
        if (*at != '=')
        {
            const auto size = strlen(at) + 1;
            if (size > ~0u)
                return Status::EnvironmentMemory;
            auto *copy = static_cast<char *>(e.Allocate(size));
            s.environ[index] = copy;
            if (!copy)
                return Status::EnvironmentMemory;
            memcpy(copy, at, size);
            ++index;
        }
    s.environ[index] = nullptr;
    // __initenv is assigned by the program's startup code, not this initializer.
    return Status::Ok;
}
} // namespace Win98CrtArgs
