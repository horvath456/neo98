// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_locale.h"
extern "C" unsigned calendar_year(unsigned calendar,unsigned year,unsigned month,unsigned day,unsigned *has_era)
{
    const char *era=nullptr;
    unsigned result=Win98Locale::CalendarYear(calendar,year,month,day,era);
    *has_era=era!=nullptr;return result;
}
extern "C" const char *calendar_maximum(unsigned calendar)
{ return Win98Locale::CalendarYearMaximum(calendar); }
