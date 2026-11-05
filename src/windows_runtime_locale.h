// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows_runtime_data.h"
#include <vector>
struct MultimediaMessage { unsigned id; const char *text; };
inline std::vector<MultimediaMessage> OriginalMultimediaMessages;
struct LocaleData { unsigned locale; const char *values[105]; };
inline std::vector<LocaleData> OriginalLocaleData;
struct Calendar { unsigned id; const char *maximum; };
inline std::vector<Calendar> OriginalCalendars;
struct CalendarEra { unsigned calendar, year, month, day; const char *name; };
inline std::vector<CalendarEra> OriginalCalendarEras;
struct LocaleCalendar { unsigned locale, standard, alternate; };
inline std::vector<LocaleCalendar> OriginalLocaleCalendars;
inline unsigned short OriginalCType[257]{}, OriginalSystemCType[257]{}, OriginalSystemWideType[257]{};
inline unsigned char OriginalUpper[256]{}, OriginalLower[256]{};
inline const char *OriginalCTime[43]{};
// Locale selectors describe the binary interface, not localized contents.
inline constexpr unsigned OriginalLocaleTypes[] = {1,3,4,5,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,89,90,4097,4098,4099,4100,4101,4102,4103,4104,4105,4106,4107,4108,4109,4110,4111,4112,4113,4114};

template<class Visit> void VisitWindowsRuntimeLocale(Visit visit)
{
    visit("OriginalMultimediaMessages", &OriginalMultimediaMessages, sizeof OriginalMultimediaMessages);
    visit("OriginalLocaleData", &OriginalLocaleData, sizeof OriginalLocaleData);
    visit("OriginalCalendars", &OriginalCalendars, sizeof OriginalCalendars);
    visit("OriginalCalendarEras", &OriginalCalendarEras, sizeof OriginalCalendarEras);
    visit("OriginalLocaleCalendars", &OriginalLocaleCalendars, sizeof OriginalLocaleCalendars);
    visit("OriginalCType", &OriginalCType, sizeof OriginalCType);
    visit("OriginalSystemCType", &OriginalSystemCType, sizeof OriginalSystemCType);
    visit("OriginalSystemWideType", &OriginalSystemWideType, sizeof OriginalSystemWideType);
    visit("OriginalUpper", &OriginalUpper, sizeof OriginalUpper);
    visit("OriginalLower", &OriginalLower, sizeof OriginalLower);
    visit("OriginalCTime", &OriginalCTime, sizeof OriginalCTime);
}
