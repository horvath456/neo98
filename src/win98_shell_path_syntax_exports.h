// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
struct ShellPathSyntaxExport{const char*name;unsigned ordinal,argc;};
static constexpr ShellPathSyntaxExport OriginalShellPathSyntaxExports[]={
 {"PathGetArgsA",483,1},
 {"PathGetArgsW",484,1},
 {"PathRemoveArgsA",531,1},
 {"PathRemoveArgsW",532,1},
 {"PathQuoteSpacesA",527,1},
 {"PathQuoteSpacesW",528,1},
 {"PathUnquoteSpacesA",557,1},
 {"PathUnquoteSpacesW",558,1},
 {"PathRemoveBlanksA",535,1},
 {"PathRemoveBlanksW",536,1},
 {"PathFindExtensionA",473,1},
 {"PathFindExtensionW",474,1},
 {"PathFindFileNameA",475,1},
 {"PathFindFileNameW",476,1},
 {"PathFindNextComponentA",477,1},
 {"PathFindNextComponentW",478,1},
 {"PathAddExtensionA",453,2},
 {"PathAddExtensionW",454,2},
 {"PathRenameExtensionA",541,2},
 {"PathRenameExtensionW",542,2},
 {"PathRemoveExtensionA",537,1},
 {"PathRemoveExtensionW",538,1},
 {"PathGetDriveNumberA",487,1},
 {"PathGetDriveNumberW",488,1},
 {"PathIsFileSpecA",495,1},
 {"PathIsFileSpecW",496,1},
 {"PathParseIconLocationA",525,1},
 {"PathParseIconLocationW",526,1},
 {"PathGetCharTypeA",485,1},
 {"PathGetCharTypeW",486,1},
};
inline int ShellPathSyntaxOperation(const char*name){if(!strcmp(name,"ordinal_473"))return 10;if(!strcmp(name,"ordinal_475"))return 12;for(unsigned i=0;i<30;++i)if(!strcmp(name,OriginalShellPathSyntaxExports[i].name))return i;return -1;}
