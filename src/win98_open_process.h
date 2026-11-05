// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 OpenProcess looks up the requested process, masks access rights
// with 0x1f0fff, adds the inheritance bit when requested, and allocates a
// handle in the caller's table. A missing process returns null without
// changing LastError; an object of the wrong type sets error 87. An allocator
// failure also returns null. Lookup and handle storage are native providers.
#pragma once
namespace Win98OpenProcess {
using U=unsigned;
enum:U{
 AccessMask=0x001f0fffu,   //
 InheritBit=0x80000000u,   //
 ProcessTag=6,             // object type byte
 InvalidParameter=87,      //
};
enum Result{NotFound,NotAProcess,Open};
// A missing process is the only path that leaves LastError
// alone; a wrong object type sets 87 and still returns null.
inline Result Classify(bool found,U tag){
 if(!found)return NotFound;
 return tag==ProcessTag?Open:NotAProcess;}
// Inheritance becomes the top bit of the stored handle flags.
inline U Flags(U access,U inherit){return (access&AccessMask)|(inherit?InheritBit:0u);}
inline U Access(U flags){return flags&AccessMask;}
inline bool Inherit(U flags){return (flags&InheritBit)!=0;}
// Every allocator failure of -1 is reported as a null handle.
inline U Handle(U allocated){return allocated==0xffffffffu?0u:allocated;}
}
