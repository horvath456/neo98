/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// upstream print and tzdb, and CRT cleanup for Win32 threads.
#define _LIBCPP_ENABLE_EXPERIMENTAL
#include <chrono>
#include <print>
#include <cstdio>
#include <cstring>
#include <system_error>
#include <windows.h>
static unsigned checks, failures;
static void Check(bool ok, const char *name) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", name); } }
static bool Resource(int id, const char *path)
{
    HRSRC resource = FindResourceA(nullptr, MAKEINTRESOURCEA(id), RT_RCDATA);
    if (!resource) return false;
    const DWORD size = SizeofResource(nullptr, resource);
    const void *data = LockResource(LoadResource(nullptr, resource));
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD done = 0;
    bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, data, size, &done, nullptr) && done == size;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return ok;
}
static int destroyed[4], count, dll_values[3];
extern "C" __declspec(dllimport) void Touch(int*);
struct Local { int id; ~Local() { if (count < 4) destroyed[count++] = id; } };
static DWORD WINAPI Worker(void *explicit_exit)
{
    thread_local Local first{1}, second{2};
    if (first.id + second.id != 3) return 99;
    Touch(dll_values);
    if (explicit_exit) ExitThread(72);
    return 71;
}
int main()
{
    try {
        std::println("PRINT {} {:.2f}", 42, 1.25);
        FILE *file = std::fopen("C:\\PRINT4.TXT", "w+b");
        Check(file != nullptr, "print file");
        if (file) {
            std::print(file, "{} {:04x} {:.2f} {}", "abc", 42, 1.25, "Gr\xc3\xbc\xc3\x9f" "e");
            std::fflush(file); std::rewind(file);
            char text[80] = {}; std::fread(text, 1, sizeof(text)-1, file);
            Check(std::strcmp(text, "abc 002a 1.25 Gr\xc3\xbc\xc3\x9f" "e") == 0, "print UTF8 file bytes");
            std::fclose(file);
        }
        file = std::fopen("C:\\PRINT4.TXT", "rb");
        bool threw = false;
        try { std::print(file, "{}", "cannot write"); } catch (const std::system_error&) { threw = true; }
        Check(threw, "print reports write error"); std::fclose(file); DeleteFileA("C:\\PRINT4.TXT");
        for (int n = 0; n < 40; ++n) {
            count = 0; dll_values[0] = dll_values[1] = dll_values[2] = 0;
            HANDLE thread = CreateThread(nullptr, 0, Worker, (void*)(ULONG_PTR)(n & 1), 0, nullptr);
            Check(thread != nullptr, "CreateThread");
            Check(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0, "thread finished");
            DWORD code = 0; GetExitCodeThread(thread, &code); CloseHandle(thread);
            Check(code == (n & 1 ? 72u : 71u), "thread return code");
            Check(count == 2 && destroyed[0] == 2 && destroyed[1] == 1, "thread_local destruction order");
            Check(dll_values[0] == 2 && dll_values[1] == 2 && dll_values[2] == 1, "DLL thread_local destruction order");
        }
        CreateDirectoryA("C:\\TZDATA4", nullptr);
        Check(Resource(1, "C:\\TZDATA4\\tzdata.zi"), "install IANA zones");
        Check(Resource(2, "C:\\TZDATA4\\leap-seconds.list"), "install IANA leaps");
        SetEnvironmentVariableA("NEO98_TZDIR", "C:\\TZDATA4"); SetEnvironmentVariableA("TZ", "Europe/Vienna");
        using namespace std::chrono;
        const auto& db = get_tzdb();
        Check(db.version == "2025b" && db.zones.size() > 300 && db.links.size() > 100, "IANA database");
        const auto *vienna = locate_zone("Europe/Vienna");
        Check(current_zone() == vienna, "current zone from TZ");
        Check(vienna->get_info(sys_days{2024y/January/15}).offset == 1h, "Vienna winter");
        Check(vienna->get_info(sys_days{2024y/July/15}).offset == 2h, "Vienna summer");
        Check(vienna->get_info(local_days{2024y/March/31} + 2h + 30min).result == local_info::nonexistent, "spring gap");
        Check(vienna->get_info(local_days{2024y/October/27} + 2h + 30min).result == local_info::ambiguous, "autumn overlap");
        Check(locate_zone("US/Eastern") == locate_zone("America/New_York"), "IANA link");
        Check(locate_zone("Asia/Kathmandu")->get_info(sys_days{2024y/July/15}).offset == 5h + 45min, "fractional offset");
        Check(db.leap_seconds.size() == 27, "leap second table");
        Check(vienna->to_sys(local_days{2024y/July/15} + 12h) == sys_days{2024y/July/15} + 10h, "local to system time");
        threw = false; try { (void)locate_zone("Missing/Zone"); } catch (const std::runtime_error&) { threw = true; }
        Check(threw, "unknown zone rejected");
        DeleteFileA("C:\\TZDATA4\\tzdata.zi"); DeleteFileA("C:\\TZDATA4\\leap-seconds.list"); RemoveDirectoryA("C:\\TZDATA4");
    } catch (const std::exception& error) { ++failures; std::printf("FAIL exception: %s\n", error.what()); }
    std::printf("CXXREMAIN checks=%u failures=%u\n", checks, failures);
    return failures != 0;
}
