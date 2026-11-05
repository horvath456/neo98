/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// The program of the CMake fixture: what a project gets from the toolchain
// file of the SDK. tools/test_arm64_cmake_native.py.
#include <windows.h>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int PartLength(const char *text);
extern "C" int PlainValue(void);
extern "C" unsigned PlainTime(void);

static int checks, failures;
static void Check(int id, bool condition)
{
    ++checks;
    if (condition) return;
    ++failures;
    std::printf("FAIL %d\n", id);
}
struct Global { std::string text; Global() : text("made before main") {} };
static Global global;

int main(int argc, char **argv)
{
    Check(1, PartLength("static library") == 14);
    Check(2, PlainValue() == 42);
    Check(3, global.text == "made before main" && argc >= 1 && argv[0]);
    std::map<std::string, std::vector<int>> table;
    for (int i = 0; i < 100; ++i) table[i % 2 ? "odd" : "even"].push_back(i);
    Check(4, table.size() == 2 && table["odd"].size() == 50 && table["even"].back() == 98);
    bool caught = false;
    try { throw std::runtime_error("thrown"); } catch (const std::exception &error) { caught = std::string(error.what()) == "thrown"; }
    Check(5, caught);
    const unsigned before = PlainTime();
    Sleep(20);
    Check(6, PlainTime() - before >= 10);
    // The resource the project compiled is in the image.
    char text[64] = "";
    Check(7, LoadStringA(GetModuleHandleA(nullptr), 7, text, sizeof text) == 13 && std::string(text) == "from resource");
    Check(8, sizeof(void *) == 8 && sizeof(long) == 4);
    std::printf("CMAKE checks=%d failures=%d\n", checks, failures);
    return failures;
}
