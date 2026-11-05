// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
// Local original SHLWAPI export ordinals.
struct ShellSearchExport {const char*name;unsigned ordinal,argc;};
static constexpr ShellSearchExport OriginalShellSearchExports[]={
 {"StrChrA",632,2},
 {"StrChrW",635,2},
 {"StrRChrA",659,3},
 {"StrRChrW",662,3},
 {"StrPBrkA",657,2},
 {"StrPBrkW",658,2},
 {"StrSpnA",669,2},
 {"StrSpnW",670,2},
 {"StrCSpnA",625,2},
 {"StrCSpnW",628,2},
};
inline int ShellSearchOperation(const char*name){for(unsigned i=0;i<10;++i)if(!strcmp(name,OriginalShellSearchExports[i].name))return i;return -1;}
