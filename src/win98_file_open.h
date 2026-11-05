// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_semaphore.h"
namespace Win98FileOpen {
using U=unsigned;
// Windows 98 adds attribute-write access for GENERIC_WRITE;
// Windows 98 maps the public mask into the Win98 handle table.
inline U HandleAccess(U requested){
 if(requested&0x40000000u)requested|=0x100;
 return ((requested&~0x30u)|(requested>>26))&0x130;
}
// KERNEL32. Clear the DOS access nibble even on error.
inline U Access(U requested,unsigned short&mode){
 requested&=0xfffefeffu;mode&=0xfff0;
 if(!requested||requested==0x80000000u)return 0;
 if(requested==0x40000000u){mode|=1;return 0;}
 if(requested==0xc0000000u){mode|=2;return 0;}
 return 87;
}
// The original ORs the DOS share mode, preserving other bits.
inline U Share(U requested,unsigned short&mode){
 if(requested>3)return 87;
 mode|=(requested+1)*16;return 0;
}
// Local-file branch. SECURITY_ATTRIBUTES uses the same
// helper already implemented for semaphore creation; bad length
// maps to ERROR_INVALID_DATA here, rather than the semaphore's error87.
template<class E> U Parameters(E&e,U security,U access,U share,unsigned short&mode,U&flags){
 flags=Win98Semaphore::SecurityFlags(e,security);
 if(flags==~0u)return 13;
 U error=Access(access,mode);return error?error:Share(share,mode);
}
}
