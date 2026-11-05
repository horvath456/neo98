/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_gdi_object_type.h"
using namespace Win98GdiObjectType;
extern "C" U type(U signature,U enhanced,U flags,U*bad){bool invalid=false;U result=Type(signature,enhanced,flags,invalid);*bad=invalid;return result;}
extern "C" U current(U signature,U selected_type){struct E{U Selected(U offset){return 0x2000+offset;}}e;return Current(e,signature,selected_type);}
extern "C" U meta(U rights,U a,U b,U c){return Metafile(rights,a,b,c);}
extern "C" U resolve(const unsigned char*memory,U h){struct E{const unsigned char*p;U HeapBase(){return 0x10000;}U HandleBase(){return 0x18000;}U Read8(U at){return p[at];}U Read16(U at){return Read8(at)|(Read8(at+1)<<8);}U Read32(U at){return Read16(at)|(Read16(at+2)<<16);}}e{memory};return Resolve(e,h);}
