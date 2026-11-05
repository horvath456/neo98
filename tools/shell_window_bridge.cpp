// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_window.h"
#include <assert.h>
#include <stdio.h>
using U = unsigned;
struct Environment
{
    const U *input;
    U *output, count = 0, record;
    Environment(const U *in, U *out) : input(in), output(out), record(in[0]) {}
    void Event(U op, U a = 0, U b = 0)
    { assert(count < 4); output[2 + count * 3] = op; output[3 + count * 3] = a; output[4 + count * 3] = b; ++count; }
    void Lock() { Event(1); record = input[1]; }
    U ShellWindowRecord() { Event(2, 0xf6, record); return record; }
    U WindowHandle(U window)
    {
        assert(window == 0x5000 || window == 0x18000);
        const U handle = input[window == 0x5000 ? 2 : 3] & 0xffffu;
        Event(3, window, handle); return handle;
    }
    void Unlock() { Event(4); }
};
extern "C" void shell_window_get(const U *input, U *output)
{
    for (U i = 0; i < 14; ++i) output[i] = 0;
    Environment environment(input, output);
    output[0] = Win98ShellWindow::Get(environment);
    output[1] = environment.count;
}
#ifdef SHELL_WINDOW_MAIN
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb"); assert(file);
    U input[6], expected[14], actual[14], count = 0;
    while (fread(input, sizeof input, 1, file) == 1)
    {
        assert(fread(expected, sizeof expected, 1, file) == 1);
        shell_window_get(input, actual);
        for (U i = 0; i < 14; ++i) assert(expected[i] == actual[i]);
        ++count;
    }
    assert(!ferror(file)); fclose(file);
    printf("SHELL WINDOW ASAN/UBSAN PASS: %u calls\n", count);
}
#endif
