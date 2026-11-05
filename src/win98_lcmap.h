// SPDX-License-Identifier: GPL-2.0-or-later
// LCMapStringA flag validation and selection of case, width, kana, and sort-key
// conversions. Unsupported combinations retain Windows 98 error behavior.
#pragma once
namespace Win98LcMap {
using U=unsigned;
enum:U{
 Lowercase=0x00000100,Uppercase=0x00000200,SortKey=0x00000400,
 InvalidFlags=0x3ec,InvalidParameter=0x57,
};
enum Route{Rejected,SortKeyRoute,RemovalRoute,LowercaseRoute,UppercaseRoute,GeneralRoute};
// The original clears these three bits before any test.
inline U Normalized(U flags){return flags&~U(0x01000000|0x40000000|0x80000000);}
// Overlapping input and output is only allowed for a case mapping.
inline bool InPlaceAllowed(U flags){return (flags&(Lowercase|Uppercase))&&!(flags&0x00f00000);}
// Returns 0 when the flag word is acceptable, otherwise the
// original flag error code.
inline U CheckFlags(U raw){
 const U f=Normalized(raw);
 if(f&0xbf0ce0f8u)return InvalidFlags;
 if(!f)return InvalidFlags;
 if((f&0x00031007u)&&(f&0x00f00300u))return InvalidFlags;
 if((f&0x00031001u)&&!(f&SortKey))return InvalidFlags;
 // x & (x-1) clears the lowest set bit; the original applies it to the flag
 // word with its low byte removed and then looks at one group at a time.
 const U high=f&~U(0xff),cleared=(high-1)&f;
 if(cleared&0x00000700u)return InvalidFlags;
 if(((f&0xfff00400u)-1)&f&0x00300400u)return InvalidFlags;
 if(((f&0xffc00400u)-1)&f&0x00c00400u)return InvalidFlags;
 return 0;}
// The route is decided on the normalized flag word.
inline Route Select(U raw){
 const U f=Normalized(raw);
 if(f&SortKey)return SortKeyRoute;
 if(f==2||f==4||f==6)return RemovalRoute;
 if(f==Lowercase)return LowercaseRoute;
 if(f==Uppercase)return UppercaseRoute;
 return GeneralRoute;}
// all four reported as error 87.
inline bool Arguments(U source,int count,U destination,int capacity){
 if(!source||!count||capacity<0)return false;
 if(capacity&&!destination)return false;
 return true;}
}
