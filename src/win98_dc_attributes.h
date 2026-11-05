// SPDX-License-Identifier: GPL-2.0-or-later
// GDI32 direct DC field-query bodies.
#pragma once
#include "win98_gdi_object_type.h"
namespace Win98DcAttributes {
using U=unsigned;
// The caller supplies the
// original field width/offset and invalid-DC result. The resolver/SEH
// boundary remains an adapter; the signature gate is shared original code.
template<class E>U Query(E&e,U signature,U offset,bool dword,U invalid){
 if(!Win98GdiObjectType::DC(signature))return invalid;
 return dword?e.Read32(offset):(e.Read16(offset)&65535);
}
template<class E>U BkMode(E&e,U signature){return Query(e,signature,0x50,false,0);}
template<class E>U BkColor(E&e,U signature){return Query(e,signature,0x66,true,~0u);}
template<class E>U TextColor(E&e,U signature){return Query(e,signature,0x6a,true,~0u);}
}
