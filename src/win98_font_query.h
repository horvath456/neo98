// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98FontQuery {
using U=unsigned;
inline int S16(U n){return int(n&32767)-int(n&32768);}
struct Face {U flags,font_type,logical,physical,bitmap_atom,outline_primary,outline_alias;};
// GDI. Handle lookup, font realization, atoms and compatibility
// metadata are providers. Face strings and copy decisions stay in this core.
template<class E> U TextFace(E&e,const Face&f,U count,U output){
 count&=65535;if((f.flags&0x40)&&!e.Realize())return 0;
 const U requested=e.FindAtom(f.logical);U alias=0,charset=1;e.Alias(requested,alias,charset);
 U atom=0;
 if(!(f.flags&0x1400))atom=f.bitmap_atom;
 else if((f.flags&0x1000)||(f.font_type&3)==3)atom=requested==f.outline_alias?requested:f.outline_primary;
 else {
  atom=e.FindAtom(f.physical);
  if(!atom){U remaining=(count-1)&65535;if(S16(count)<=1)return 0;U n=0;
   do{const U c=e.Read8(f.physical+n);e.Write8(output+n,c);if(!c)return n;++n;}while(--remaining);
   e.Write8(output+n,0);return n;
  }
 }
 bool logical=false;
 if(atom!=requested&&atom==alias){
  if(e.SpecialAtom(requested))logical=true;
  else {const U version=e.ExpectedVersion();logical=version<0x300||(version<0x400&&charset&&charset!=1);}
 }
 if(!logical)return e.AtomName(atom,count,output);
 U remaining=count,n=0;
 do{const U c=e.Read8(f.logical+n);e.Write8(output+n,c);if(!c)return n;++n;}while(--remaining);
 e.Write8(output+n-1,0);return n;
}
// GDI32. Snapshot records are 0x108 bytes; LOGFONT/extended
// names start at zero, metrics at BC, and FontType is the DWORD at 104.
template<class E> U Enumerate(E&e,U dc,U face,U charset,U callback,U param,U mode){
 U count=0,records=0,lease=0;U result=e.Snapshot(dc,face,charset,mode,count,records,lease);
 if(!result)return 0;
 for(U i=0;i<count;++i){const U row=records+i*264;result=e.Callback(callback,row,row+188,e.Read32(row+260),param);if(!result)break;}
 e.Release(lease);return result;
}
// GDI. The no-face path deduplicates family/charset pairs for
// DEFAULT_CHARSET, or filters an explicit charset and deduplicates face atoms.
inline bool FamilyCandidate(U atom,U charset,U requested,bool all,const U*seen,U count){
 if(!all&&charset!=requested)return false;
 for(U i=0;i<count;++i)if(seen[i*2]==atom&&(all?seen[i*2+1]==charset:true))return false;
 return true;
}
// GDI32. Widen signed driver WORDs in place from the
// end, including when the preceding driver call returned failure.
template<class E> void ExpandWidths(E&e,U output,U count){
 if(!output || !count || count>0x3fffffff)return;
 for(U i=count;i;--i)e.Write32(output+(i-1)*4,U(S16(e.Read16(output+(i-1)*2))));
}
} // namespace Win98FontQuery
