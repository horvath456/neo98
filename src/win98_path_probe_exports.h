// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
inline int PathProbeOperation(const char*name){
 const char*names[]={"ordinal_45","PathFileExistsA","PathFileExistsW","PathIsDirectoryA","PathIsDirectoryW","PathIsDirectoryEmptyA","PathIsDirectoryEmptyW","PathIsUNCServerA","PathIsUNCServerW","PathIsUNCServerShareA","PathIsUNCServerShareW"};
 for(unsigned i=0;i<11;++i)if(!strcmp(name,names[i]))return int(i);return -1;
}
