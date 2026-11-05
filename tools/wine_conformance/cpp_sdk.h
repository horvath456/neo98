// SPDX-License-Identifier: GPL-2.0-or-later
// libstdc++ headers declare these C++11 names even with Win98 CRT headers.
// Declarations only: tests using them still need a real linker implementation.
#include <bits/c++config.h>
// libstdc++ enables MinGW's ANSI stdio wrappers unconditionally. This
// toolchain's wrappers depend on UCRT, which Win98 does not provide. Keep
// the campaign's selected native MSVCRT stdio declarations for C++ too.
#undef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 0
extern "C" int at_quick_exit(void (*callback)(void));
extern "C" void quick_exit(int) __attribute__((noreturn));
