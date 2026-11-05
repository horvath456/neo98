// SPDX-License-Identifier: GPL-2.0-or-later
// Native local SHLWAPI copy/concatenation/duplication family.
#pragma once
namespace Win98ShellCopy {
using U=unsigned;
template<class E> U Length(E&e,U p,U unit){U n=0;if(p)while(e.Get(p+n*unit,unit))++n;return n;}
template<class E> U Copy(E&e,U d,U s,U unit){U result=d,c;do{c=e.Get(s,unit);e.Put(d,c,unit);d+=unit;s+=unit;}while(c);return result;}
// Capacity is signed; a null source produces an empty string.
template<class E> void CopyNW(E&e,U d,U s,U n){if(int(n)<=0)return;if(!s){e.Put(d,0,2);return;}while(--n){U c=e.Get(s,2);e.Put(d,c,2);if(!c)return;s+=2;d+=2;}e.Put(d,0,2);}
// Imported KERNEL32 lstrcpynA core : unsigned capacity,
// full source scan, DWORD copies followed by bytes, and a terminator.
template<class E> void CopyNA(E&e,U d,U s,U n){U count=Length(e,s,1)+1;if(count>n)count=n;if(!count)return;--count;while(count>=4){e.Put(d,e.Get(s,4),4);d+=4;s+=4;count-=4;}while(count--){e.Put(d,e.Get(s,1),1);++d;++s;}e.Put(d,0,1);}
template<class E> U Append(E&e,U d,U s,U n,U unit,bool buffer){U tail=d;while(e.Get(tail,unit)){tail+=unit;if(buffer)--n;}if(!buffer||int(n)>0){if(unit==2)CopyNW(e,tail,s,n);else CopyNA(e,tail,s,n);}return d;}
template<class E> U Duplicate(E&e,U s,U unit,bool task,U out=0){U n=Length(e,s,unit);U p=e.Allocate((n+1)*unit,task);if(task)e.Put(out,p,4);if(p)Copy(e,p,s,unit);return task?(p?0:0x8007000eu):p;}
// SHStrDupA allocates an extra WCHAR beyond the conversion's NUL-inclusive
// count; conversion failures are ignored after successful COM allocation.
template<class E> U DuplicateAnsiToWide(E&e,U s,U out){U n=e.Wide(s,0,0),p=e.Allocate(n*2+2,true);e.Put(out,p,4);if(!p)return 0x8007000eu;e.Wide(s,p,n);return 0;}
struct Export{const char*name;U argc;};constexpr Export Exports[]={
 {"StrCpyW",2},{"StrCpyNW",3},{"StrCatW",2},{"StrCatBuffA",3},{"StrCatBuffW",3},{"StrNCatA",3},{"StrNCatW",3},{"StrDupA",1},{"StrDupW",1},{"SHStrDupA",2},{"SHStrDupW",2}};
template<class E> U Call(E&e,U op,const U*a){switch(op){case 0:return Copy(e,a[0],a[1],2);case 1:CopyNW(e,a[0],a[1],a[2]);return a[0];case 2:Copy(e,a[0]+Length(e,a[0],2)*2,a[1],2);return a[0];case 3:case 4:case 5:case 6:return Append(e,a[0],a[1],a[2],(op==4||op==6)?2:1,op<5);case 7:case 8:return Duplicate(e,a[0],op==7?1:2,false);case 9:return DuplicateAnsiToWide(e,a[0],a[1]);case 10:return Duplicate(e,a[0],2,true,a[1]);}return 0;}
}
