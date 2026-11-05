// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32's timezone cache comes from the machine's own hive, the way the
// original publishes it at startup. Two halves: the stored state and the guest
// clock agree, so Explorer's daylight check reports no change; and a clock that
// runs across a transition still makes it report one.
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>
// The timezone entries are internal, so reach them the way
// tools/test_time_native.cpp reaches the conversion entries.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"

static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, f) == size_t(n));
    fclose(f);
    return bytes;
}

// Win98 TIME_ZONE_INFORMATION field offsets, from src/win98_timezone.h.
enum { BIAS = 0, STANDARD_NAME = 4, STANDARD = 68, STANDARD_BIAS = 84,
       DAYLIGHT_NAME = 88, DAYLIGHT = 152, DAYLIGHT_BIAS = 168, ZONE_BYTES = 172 };
enum { YEAR = 0, MONTH = 2, DAY = 6, HOUR = 8, MINUTE = 10, SECOND = 12, MILLISECOND = 14 };

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    // NOTEPAD.EXE imports SHELL32, which comes from the Win98 tree.
    assert(SystemTreeHost::Mount());
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "NOTEPAD.EXE"));
    const unsigned zone = app.Allocate(ZONE_BYTES + 64), stamp = zone + ZONE_BYTES;
    assert(zone);
    auto word = [&](unsigned base, unsigned offset) { return app.Read32(base + offset) & 0xffff; };
    const unsigned zone_args[] = {zone}, stamp_args[] = {stamp};

    // The stored zone reaches the cache without anyone calling SetTimeZoneInformation.
    const unsigned flag = app.TimezoneApi("GetTimeZoneInformation", zone_args);
    assert(flag == 2); // DaylightFlag 2: the machine was on summer time.
    assert(app.Read32(zone + BIAS) == 0xffffffc4u);          // -60, W. Europe
    assert(app.Read32(zone + STANDARD_BIAS) == 0);
    assert(app.Read32(zone + DAYLIGHT_BIAS) == 0xffffffc4u); // -60
    assert(word(zone, STANDARD + MONTH) == 10 && word(zone, STANDARD + DAY) == 5 &&
           word(zone, STANDARD + HOUR) == 3); // last Sunday in October, 03:00
    assert(word(zone, DAYLIGHT + MONTH) == 3 && word(zone, DAYLIGHT + DAY) == 5 &&
           word(zone, DAYLIGHT + HOUR) == 2); // last Sunday in March, 02:00
    assert((app.Read32(zone + STANDARD_NAME) & 0xffff) == 'W'); // W. Europe Standard Time
    assert((app.Read32(zone + DAYLIGHT_NAME) & 0xffff) == 'W');
    // ActiveTimeBias -120 is Bias plus DaylightBias, and the runtime uses it.
    assert(app.EffectiveTimeBias() == 0xffffff88u);

    // The guest clock starts on 25 June 1998, between the two transitions, so
    // the classification already matches the stored flag.
    app.TimezoneApi("GetLocalTime", stamp_args);
    assert(word(stamp, YEAR) == 1998 && word(stamp, MONTH) == 6 && word(stamp, DAY) == 25);
    const unsigned hour = word(stamp, HOUR);
    const unsigned adjust[] = {1};
    assert(app.TimezoneApi("ordinal_99", adjust) == 0); // 0: nothing to change.
    app.TimezoneApi("GetLocalTime", stamp_args);
    assert(word(stamp, HOUR) == hour); // The clock did not move.
    printf("TIMEZONE HIVE PASS zone=W.Europe bias=-120 flag=2 check=0 hour=%u\n", hour);

    // Negative half: put the clock just before the autumn transition, where the
    // stored flag and ActiveTimeBias still say summer time, and let it run
    // across 03:00. Now the check must report the change and move the clock.
    unsigned short before[8] = {1998, 10, 0, 25, 2, 59, 59, 900};
    assert(app.Store(stamp, before, sizeof before));
    assert(app.TimezoneApi("SetLocalTime", stamp_args) == 1);
    assert(app.TimezoneApi("GetTimeZoneInformation", zone_args) == 2); // still summer
    assert(app.EffectiveTimeBias() == 0xffffff88u);                    // still -120
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    assert(app.TimezoneApi("ordinal_99", adjust) == 2); // 2: the clock was adjusted.
    assert(app.TimezoneApi("GetTimeZoneInformation", zone_args) == 1); // standard time
    assert(app.EffectiveTimeBias() == 0xffffffc4u);                    // Bias alone, -60
    app.TimezoneApi("GetLocalTime", stamp_args);
    assert(word(stamp, DAY) == 25 && word(stamp, HOUR) == 2 && word(stamp, MINUTE) == 0);
    printf("TIMEZONE HIVE PASS transition=autumn check=2 local=%02u:%02u bias=-60 calls=%u\n",
           word(stamp, HOUR), word(stamp, MINUTE), app.ApiCalls());
    return 0;
}
