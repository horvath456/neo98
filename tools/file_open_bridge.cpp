// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_file_open.h"
extern "C" unsigned file_open_parameters(unsigned present,unsigned length,unsigned descriptor,unsigned inherit,unsigned access,unsigned share,unsigned short*mode,unsigned*flags){
 struct E {unsigned values[3];unsigned Read32(unsigned p){return values[(p-0x200000)/4];}}e{{length,descriptor,inherit}};
 return Win98FileOpen::Parameters(e,present?0x200000:0,access,share,*mode,*flags);
}

extern "C" unsigned file_open_access_rights(unsigned access){return Win98FileOpen::HandleAccess(access);}
