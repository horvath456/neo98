// SPDX-License-Identifier: GPL-2.0-or-later
// A volume of the Mac that the host already serves as a drive does not get a
// second letter from the hardware service: "Open with NEO98" on a
// disc image attaches it with hdiutil and puts it behind E:; the mounted
// volume is ejectable, and the service gave it F: as well.
#include "host_devices/platform.h"
#include <cstdio>
#include <string>
using namespace HostDeviceService;
static int failures = 0;
static void Check(bool okay, const char *what)
{
    printf("%s %s\n", okay ? "ok  " : "FAIL", what);
    if (!okay) ++failures;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const std::string disc = argv[1], stick = argv[2];
    auto found = [&](AH_Status &out, std::map<unsigned, Medium> &media) {
        out = AH_Status{};
        media.clear();
        const std::string paths[2] = {disc + "/", stick};
        for (unsigned i = 0; i < 2; ++i)
        {
            Medium m;
            m.id = 10 + i;
            m.path = paths[i];
            media[m.id] = m;
            out.medium[out.media++].id = m.id;
        }
    };
    AH_Status out;
    std::map<unsigned, Medium> media;
    found(out, media);
    DropClaimed(out, media);
    Check(out.media == 2 && media.size() == 2, "nothing claimed: both volumes stay");
    std::string roots[26];
    roots[2] = "/";
    roots[4] = disc;
    Claim(roots, 26);
    found(out, media);
    DropClaimed(out, media);
    Check(out.media == 1 && out.medium[0].id == 11 && media.size() == 1 && media.count(11),
          "the volume behind E: is left out, the other one stays");
    roots[4].clear();
    Claim(roots, 26);
    found(out, media);
    DropClaimed(out, media);
    Check(out.media == 2, "after the disc leaves E: the volume is offered again");
    roots[4] = disc + "/image.iso";
    Claim(roots, 26);
    found(out, media);
    media[10].image_source = roots[4];
    DropClaimed(out, media);
    Check(out.media == 1 && media.count(11), "an image and its mounted volume are one disc");
    roots[4].clear();
    Claim(roots, 26);
    found(out, media);
    media[10].image_source = disc + "/image.iso";
    DropClaimed(out, media);
    Check(out.media == 2, "an unclaimed image remains available");
    auto multiple_discs = [&] {
        found(out, media);
        media[10].flags = AH_MEDIA_OPTICAL;
        media[10].name = "INSTALL";
        media[10].image_source = disc + "/first.iso";
        Medium second;
        second.id = 12;
        second.flags = AH_MEDIA_OPTICAL;
        second.name = "INSTALL"; // Matching labels do not identify a medium.
        second.path = stick + "/second-mount";
        second.image_source = stick + "/second.iso";
        media[second.id] = second;
        out.medium[out.media++].id = second.id;
    };
    multiple_discs();
    DropClaimed(out, media);
    Check(out.media == 3 && media.count(10) && media.count(11) && media.count(12),
          "two mounted CDs with the same label and a USB drive all remain available");
    roots[4] = disc + "/first.iso";
    Claim(roots, 26);
    multiple_discs();
    DropClaimed(out, media);
    Check(out.media == 2 && !media.count(10) && media.count(11) && media.count(12),
          "claiming the first ISO preserves the second mounted CD");
    roots[5] = stick + "/second.iso";
    Claim(roots, 26);
    multiple_discs();
    DropClaimed(out, media);
    Check(out.media == 1 && media.count(11),
          "two explicit ISOs suppress only their own mounted aliases");
    return failures ? 1 : 0;
}
