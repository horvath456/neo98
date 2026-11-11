// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/windows_runtime_locale.h"
#include <deque>
namespace HostWindowsData {
inline std::deque<std::string> localeStrings;
inline const char *keep(std::string text) { localeStrings.push_back(std::move(text)); return localeStrings.back().c_str(); }
// The texts of the format lists stand apart, so the retained strings above
// keep their order and with it their names in a saved state.
inline std::deque<std::string> formatStrings;
inline unsigned narrow(unsigned c) {
    auto half = [](unsigned at) { return at + 1 < sizeof OriginalCodePage1252 ? unsigned(OriginalCodePage1252[at]) | unsigned(OriginalCodePage1252[at + 1]) << 8 : 0u; };
    unsigned base = (half(20) | half(22) << 16) * 2;
    unsigned first = half(base + (c >> 8) * 2), second = half(base + (first + ((c >> 4) & 15)) * 2);
    return half(base + (second + (c & 15)) * 2) & 255;
}
inline std::string localeText(Reader &r, unsigned at) {
    std::string text;
    for (unsigned i = 0; i < 4096 && r.ok; ++i, at += 2) {
        unsigned c = r.get(at, 2); if (!c) return text; text.push_back(char(narrow(c)));
    }
    r.ok = false; return {};
}
inline unsigned field(Reader &r, unsigned base, unsigned type) {
    switch (type) {
    case 1: return base + 146;
    case 5: return base + 156;
    case 9: return base + 168;
    case 10: return base + 178;
    case 11: return base + 202;
    case 13: return base + 238;
    case 17: return base + 246;
    case 18: return base + 252;
    case 25: return base + 260;
    case 26: return base + 266;
    case 27: return base + 272;
    case 28: return base + 276;
    case 33: return base + 318;
    case 34: return base + 334;
    case 35: return base + 306;
    case 36: return base + 322;
    case 37: return base + 310;
    case 38: return base + 326;
    case 39: return base + 330;
    case 82: return base + 282;
    case 83: return base + 294;
    case 84: return base + 286;
    case 85: return base + 290;
    case 86: return base + 298;
    case 87: return base + 302;
    case 4100: return base + 190;
    case 4101: return base + 314;
    case 4105: return base + 338;
    case 4106: return base + 242;
    case 4108: return base + 342;
    case 4109: return base + 346;
    case 4112: return base + 256;
    case 4113: return base + 214;
    case 4114: return base + 226;
    case 3: return base + r.get(base + 2, 2) * 2 + 0;
    case 4: return base + r.get(base + 6, 2) * 2 + 0;
    case 7: return base + r.get(base + 10, 2) * 2 + 0;
    case 8: return base + r.get(base + 14, 2) * 2 + 0;
    case 12: return base + r.get(base + 16, 2) * 2 + 0;
    case 14: return base + r.get(base + 18, 2) * 2 + 0;
    case 15: return base + r.get(base + 20, 2) * 2 + 0;
    case 16: return base + r.get(base + 22, 2) * 2 + 0;
    case 19: return base + r.get(base + 24, 2) * 2 + 0;
    case 20: return base + r.get(base + 26, 2) * 2 + 0;
    case 21: return base + r.get(base + 28, 2) * 2 + 0;
    case 22: return base + r.get(base + 34, 2) * 2 + 0;
    case 23: return base + r.get(base + 36, 2) * 2 + 0;
    case 24: return base + r.get(base + 38, 2) * 2 + 0;
    case 29: return base + r.get(base + 54, 2) * 2 + 0;
    case 30: return base + r.get(base + 46, 2) * 2 + 0;
    case 31: return base + r.get(base + 52, 2) * 2 + 0;
    case 32: return base + r.get(base + 58, 2) * 2 + 0;
    case 40: return base + r.get(base + 48, 2) * 2 + 0;
    case 41: return base + r.get(base + 50, 2) * 2 + 0;
    case 42: return base + r.get(base + 62, 2) * 2 + 0;
    case 43: return base + r.get(base + 64, 2) * 2 + 0;
    case 44: return base + r.get(base + 66, 2) * 2 + 0;
    case 45: return base + r.get(base + 68, 2) * 2 + 0;
    case 46: return base + r.get(base + 70, 2) * 2 + 0;
    case 47: return base + r.get(base + 72, 2) * 2 + 0;
    case 48: return base + r.get(base + 74, 2) * 2 + 0;
    case 49: return base + r.get(base + 76, 2) * 2 + 0;
    case 50: return base + r.get(base + 78, 2) * 2 + 0;
    case 51: return base + r.get(base + 80, 2) * 2 + 0;
    case 52: return base + r.get(base + 82, 2) * 2 + 0;
    case 53: return base + r.get(base + 84, 2) * 2 + 0;
    case 54: return base + r.get(base + 86, 2) * 2 + 0;
    case 55: return base + r.get(base + 88, 2) * 2 + 0;
    case 56: return base + r.get(base + 90, 2) * 2 + 0;
    case 57: return base + r.get(base + 92, 2) * 2 + 0;
    case 58: return base + r.get(base + 94, 2) * 2 + 0;
    case 59: return base + r.get(base + 96, 2) * 2 + 0;
    case 60: return base + r.get(base + 98, 2) * 2 + 0;
    case 61: return base + r.get(base + 100, 2) * 2 + 0;
    case 62: return base + r.get(base + 102, 2) * 2 + 0;
    case 63: return base + r.get(base + 104, 2) * 2 + 0;
    case 64: return base + r.get(base + 106, 2) * 2 + 0;
    case 65: return base + r.get(base + 108, 2) * 2 + 0;
    case 66: return base + r.get(base + 110, 2) * 2 + 0;
    case 67: return base + r.get(base + 112, 2) * 2 + 0;
    case 68: return base + r.get(base + 116, 2) * 2 + 0;
    case 69: return base + r.get(base + 118, 2) * 2 + 0;
    case 70: return base + r.get(base + 120, 2) * 2 + 0;
    case 71: return base + r.get(base + 122, 2) * 2 + 0;
    case 72: return base + r.get(base + 124, 2) * 2 + 0;
    case 73: return base + r.get(base + 126, 2) * 2 + 0;
    case 74: return base + r.get(base + 128, 2) * 2 + 0;
    case 75: return base + r.get(base + 130, 2) * 2 + 0;
    case 76: return base + r.get(base + 132, 2) * 2 + 0;
    case 77: return base + r.get(base + 134, 2) * 2 + 0;
    case 78: return base + r.get(base + 136, 2) * 2 + 0;
    case 79: return base + r.get(base + 138, 2) * 2 + 0;
    case 80: return base + r.get(base + 40, 2) * 2 + 0;
    case 81: return base + r.get(base + 42, 2) * 2 + 0;
    case 89: return base + r.get(base + 4, 2) * 2 + 0;
    case 90: return base + r.get(base + 12, 2) * 2 + 0;
    case 4097: return base + r.get(base + 0, 2) * 2 + 0;
    case 4098: return base + r.get(base + 8, 2) * 2 + 0;
    case 4099: return base + r.get(base + 44, 2) * 2 + 0;
    case 4102: return base + r.get(base + 56, 2) * 2 + 0;
    case 4103: return base + r.get(base + 30, 2) * 2 + 0;
    case 4104: return base + r.get(base + 32, 2) * 2 + 0;
    case 4110: return base + r.get(base + 114, 2) * 2 + 0;
    case 4111: return base + r.get(base + 140, 2) * 2 + 0;
    case 4107: return base + r.get(base + 60, 2) * 2 + 4;
    default: r.ok = false; return 0;
    }
}
inline bool loadLocale(const Bytes &data, const Bytes &crt) {
    Reader r{data}, c{crt};
    unsigned count = r.get(0, 4), calendars = r.get(4, 4), directory = r.get(8, 4) * 2;
    if (count > 1024 || calendars > 256) return false;
    for (unsigned i = 0; i < count && r.ok; ++i) {
        unsigned lcid = r.get(12 + i * 8, 4), base = r.get(16 + i * 8, 4) * 2;
        std::string standard = localeText(r, field(r, base, 0x1009));
        unsigned number = 0;
        for (char digit : standard) { if (digit < '0' || digit > '9') return false; number = number * 10 + digit - '0'; }
        OriginalLocaleCalendars.push_back({lcid, number, r.get(base + 2 * r.get(base + 0x3c, 2), 2)});
        if (localeText(r, field(r, base, 0x1004)) != "1252") continue;
        LocaleData entry{lcid, {}};
        for (unsigned j = 0; j < sizeof OriginalLocaleTypes / sizeof *OriginalLocaleTypes; ++j)
            entry.values[j] = keep(localeText(r, field(r, base, OriginalLocaleTypes[j])));
        OriginalLocaleData.push_back(entry);
        // Each list runs from its own offset to the next one of the record:
        // sTimeFormat, sShortDate, sYearMonth and sLongDate head theirs.
        LocaleFormats formats{lcid, {}, {}};
        const unsigned heads[4] = {0x2c, 0x34, 0x38, 0x3a};
        for (unsigned list = 0; list < 4 && r.ok; ++list) {
            unsigned at = base + r.get(base + heads[list], 2) * 2;
            const unsigned end = base + r.get(base + heads[list] + 2, 2) * 2;
            while (at < end && r.ok && r.get(at, 2)) {
                if (formats.counts[list] == 12) return false;
                std::string text = localeText(r, at);
                at += (unsigned(text.size()) + 1) * 2;
                formatStrings.push_back(std::move(text));
                formats.values[list][formats.counts[list]++] = formatStrings.back().c_str();
            }
        }
        OriginalLocaleFormats.push_back(formats);
    }
    for (unsigned i = 0; i < calendars && r.ok; ++i) {
        unsigned number = r.get(directory + i * 4, 2), base = directory + 2 * r.get(directory + i * 4 + 2, 2);
        OriginalCalendars.push_back({number, keep(localeText(r, base + 2 * r.get(base + 6, 2)))});
        unsigned count = r.get(base, 2), at = base + 2 * r.get(base + 8, 2), end = base + 2 * r.get(base + 10, 2);
        if (count > 1024 || end > data.size()) return false;
        for (unsigned j = 0; j < count && r.ok; ++j) {
            if (at < base || at >= end) return false;
            unsigned name = at + 8;
            while (r.get(name, 2) && r.ok) name += 2;
            OriginalCalendarEras.push_back({number, r.get(at + 4, 2), r.get(at, 2), r.get(at + 2, 2), keep(localeText(r, name + 2))});
            unsigned step = r.get(at + 6, 2); if (!step) return false; at += step * 2;
        }
    }
    Bytes cpBytes(OriginalCodePage1252, OriginalCodePage1252 + sizeof OriginalCodePage1252); Reader cp{cpBytes};
    unsigned classification = cp.get(60, 4) * 2;
    for (unsigned i = 0; i < 256; ++i) OriginalSystemCType[i + 1] = cp.get(classification + i * 2, 2);
    for (unsigned i = 0; i < 256; ++i) OriginalSystemWideType[i + 1] = OriginalSystemCType[narrow(i) + 1];
    if (!copy(OriginalUpper, cp.slice(cp.get(24, 4) * 2, 256)) || !copy(OriginalLower, cp.slice(cp.get(28, 4) * 2, 256))) return false;
    unsigned pe = c.get(60, 4);
    if (c.get(pe + 8, 4) != 917937501u) return false;
    if (!copy(OriginalCType, c.peData(225708, 514))) return false;
    for (unsigned i = 0; i < 43 && c.ok; ++i) {
        Bytes pointer = c.peData(233848 + i * 4, 4); Reader p{pointer};
        unsigned address = p.get(0, 4) - 0x78000000;
        Bytes value = c.peData(address, 128);
        auto end = std::find(value.begin(), value.end(), 0);
        if (end == value.end()) return false;
        OriginalCTime[i] = keep(std::string(value.begin(), end));
    }
    return r.ok && c.ok && cp.ok;
}
} // namespace HostWindowsData
