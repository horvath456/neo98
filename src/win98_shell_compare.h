// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ShellCompare {
using U=unsigned;
struct Converted{U pointer=0,length=0,owned=0;};
template<class E> Converted Convert(E&e,U p,U n,U scratch){Converted r;if(!(p>>16)){r.pointer=p;return r;}r.pointer=scratch;if(!n){e.Put(scratch,0,1);return r;}U size=e.Narrow(p,n,scratch,519);if(int(size)>0){e.Put(scratch+size,0,1);r.length=size-(e.Get(scratch+size-1,1)==0);return r;}
 U capacity=e.Narrow(p,n,0,0)+1;r.owned=e.Allocate(capacity);if(!r.owned){e.Put(scratch,0,1);return r;}r.pointer=r.owned;size=e.Narrow(p,n,r.pointer,capacity);e.Put(r.pointer+size,0,1);r.length=size-(e.Get(r.pointer+size-1,1)==0);return r;}
template<class E> U Limit(E&e,U p,U n){U size=0;while(int(size)<int(n)&&e.Get(p+size*2,2))++size;return size;}
template<class E> U WideOnce(E&e,U locale,U flags,U a,U na,U b,U nb){if(!a||!b)return 0;if(flags&0x10000000){na=Limit(e,a,na);nb=Limit(e,b,nb);flags&=~0x10000000u;}auto x=Convert(e,a,na,e.Scratch(0)),y=Convert(e,b,nb,e.Scratch(1));U result=e.Compare(locale,flags,x.pointer,x.length,y.pointer,y.length);if(y.owned)e.Free(y.owned);if(x.owned)e.Free(x.owned);return result;}
template<class E> U Wide(E&e,U flags,U a,U na,U b,U nb){U r=WideOnce(e,e.Locale(),flags,a,na,b,nb);if(!r)r=WideOnce(e,0x800,flags,a,na,b,nb);return r-2;}
// ChrCmpIA packs the first character according to its own lead-byte status.
// The second argument already carries its caller's complete WORD character.
template<class E> U Character(E&e,U a,U b,bool wide){U x=e.Scratch(2),y=x+8;if(wide){e.Put(x,a,2);e.Put(x+2,0,2);e.Put(y,b,2);e.Put(y+2,0,2);return Wide(e,1,x,~0u,y,~0u);}e.Put(x,e.Lead(a&255)?a&65535:a&255,2);e.Put(x+2,0,1);e.Put(y,b,2);e.Put(y+2,0,1);return e.LocalCompare(x,y,true);}
template<class E> U Sensitive(E&e,U a,U b,U n){U end=a+n;bool dbcs=e.Dbcs();while(a<end){U ca=e.Get(a,1),cb=e.Get(b,1);if(!ca&&!cb)return 0;U wa=ca,wb=cb;if(dbcs){if(ca)wa=e.Get(a,2);if(cb)wb=e.Get(b,2);}bool equal=ca==cb&&(!dbcs||!e.Lead(ca)||wa==wb);if(!equal){U x=e.Scratch(2),y=x+8;e.Put(x,wa,2);e.Put(y,wb,2);e.Put(x+2,0,1);e.Put(y+2,0,1);if(dbcs){e.Put(e.Next(x),0,1);e.Put(e.Next(y),0,1);}else{e.Put(x+1,0,1);e.Put(y+1,0,1);}return e.LocalCompare(x,y,false);}a=dbcs?e.Next(a):a+1;b=dbcs?e.Next(b):b+1;}return 0;}
template<class E> U Insensitive(E&e,U a,U b,U n){U end=a+n;while(a<end){U ca=e.Get(a,1),cb=e.Get(b,1);if(!ca&&!cb)return 0;int difference;if((ca|cb)&128){U wa=ca?e.Get(a,2):0,wb=e.Lead(cb)?e.Get(b,2):cb;difference=int(Character(e,wa,wb,false));}else{U x=ca>='A'&&ca<='Z'?ca+32:ca,y=cb>='A'&&cb<='Z'?cb+32:cb;difference=int(x)-int(y);}if(difference)return difference<0?~0u:1;a=e.Next(a);b=e.Next(b);}return 0;}
struct Export{const char*name;U argc;};constexpr Export Exports[]={{"StrCmpW",2},{"StrCmpIW",2},{"StrCmpNW",3},{"StrCmpNIW",3},{"StrCmpNA",3},{"StrCmpNIA",3},{"ChrCmpIA",2},{"ChrCmpIW",2}};
template<class E> U Call(E&e,U op,const U*a){if(op<4)return Wide(e,(op&1?1:0)|(op>=2?0x10000000:0),a[0],op>=2?a[2]:~0u,a[1],op>=2?a[2]:~0u);if(op==4)return Sensitive(e,a[0],a[1],a[2]);if(op==5)return Insensitive(e,a[0],a[1],a[2]);return Character(e,a[0],a[1],op==7);}
}
