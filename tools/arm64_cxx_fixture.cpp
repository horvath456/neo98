/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// C++ without a runtime library in an ARM64 program: objects with static
// lifetime are constructed before main and destroyed after it, virtual calls
// work. The exit code is the number of failed checks.
// tools/test_arm64_apps_native.py.
#include <windows.h>

namespace
{
int order, failures;
void Print(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)lstrlenA(text), &written, NULL);
}
struct Counter
{
    int value, made;
    explicit Counter(int start) : value(start + (int)GetCurrentProcessId() * 0), made(++order) {}
    ~Counter() { Print(made == 1 ? "first destroyed\r\n" : "second destroyed\r\n"); }
};
Counter first(40), second(2);
struct Shape
{
    virtual int Corners() const = 0;
};
struct Square final : Shape
{
    int Corners() const override { return 4; }
};
struct Triangle final : Shape
{
    int Corners() const override { return 3; }
};
} // namespace

int main(int argc, char **)
{
    if (first.made != 1 || second.made != 2 || first.value + second.value != 42) ++failures;
    Square square;
    Triangle triangle;
    const Shape *shapes[2] = {&square, &triangle};
    if (shapes[argc & 1]->Corners() + shapes[(argc + 1) & 1]->Corners() != 7) ++failures;
    Print(failures ? "cxx failed\r\n" : "cxx ok\r\n");
    return failures;
}
