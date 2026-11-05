// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/gdi_object_tables.h"
#endif
namespace Win98GdiObjectType {
using U=unsigned;
// GDI32. Selector validity/access rights are a provider.
inline bool Metafile(U rights,U type,U header_size,U version){return (rights&0xe000)==0xe000&&U((type-1)&65535)<=1&&header_size==9&&(version==0x100||version==0x300);}
// GDI32, including original overlapping final table byte.
inline U Type(U signature,U enhanced,U dc_flags,bool&invalid){
 U index=((signature&0x5fff)-0x4f47)&65535;
 if(index>12){invalid=true;return 0;}
 if(index==6){if(enhanced)return 12;if(dc_flags&1)return 10;}
#ifdef NEO98_RUNTIME_CONTENT
 const auto &types = OriginalGdiObjectTypes;
#else
 const auto &types = GdiFixture::Types;
#endif
 return types[index];
}
// GDI32 selected-object offsets, including the type-zero sentinel.
inline U CurrentOffset(U type){
#ifdef NEO98_RUNTIME_CONTENT
 const auto &offsets = OriginalGdiCurrentOffsets;
#else
 const auto &offsets = GdiFixture::Offsets;
#endif
 return type<=7?offsets[type]:0;
}
// GDI32 shared ordinary-DC signature gate.
inline bool DC(U signature){return (((signature&0x5fff)-0x4f4d)&65535)<=3;}
template<class E>U Current(E&e,U signature,U type){U offset=CurrentOffset(type);return offset&&DC(signature)?e.Selected(offset):0;}
// GDI32: original segmented heap-handle resolution.
// Native generation handles use their existing object store instead.
template<class E>U Resolve(E&e,U handle){U address=handle+e.HeapBase();U flags=e.Read8(address+2),value=e.Read16(address);if(flags&0x40)value=e.Read32(value+e.HandleBase());return value+e.HeapBase();}
}
