// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 string comparison for an installed US/SBCS locale.
// Other sort descriptors
// and combining/compression classes remain explicit provider boundaries.
#pragma once
#include "win98_system_locale.h"
#include "win98_compare_string_general.h"
namespace Win98CompareString {
using U = unsigned;
inline U Order(U left,U right) { return left < right ? 1 : left > right ? 3 : 2; }
struct Cursor {
 U address, saved=0, index=0;
 unsigned char expansion[2]{};
 bool expanded=false;
 explicit Cursor(U p):address(p){}
 template<class E> U Byte(E&e) const { return expanded ? expansion[index] : e.Read8(address); }
 void Skip() { if(expanded) ++index; else ++address; }
 void Advance() {
  if(expanded && ++index==2) { expanded=false; address=saved+1; }
  else if(!expanded) ++address;
 }
 template<class E> void Expand(E&e,U table,U pair) {
  saved=address; index=0; expanded=true;
  expansion[0]=e.Expansion(table,pair,0); expansion[1]=e.Expansion(table,pair,1);
 }
};
// Direct transcription of the fast body's decisions for the categories present
// in original CP1252. Deferred values preserve primary/accent/case/punctuation
// precedence; expansion cursors preserve the original two-character lookaside.
template<class E> U Fast(E&e,U table,U flags,U left,U right) {
 Cursor l(left),r(right);
 while(l.Byte(e)==r.Byte(e) && l.Byte(e)) { ++l.address; ++r.address; }
 if(l.Byte(e)==r.Byte(e)) return 2; //
 U accent=0,lettercase=0,leftPunctuation=0,rightPunctuation=0;
 bool punctuationDone=false;
 U pending=Backward(e,table,0)?7:5; // descriptor+20 selects reversed secondary weights.
 const U mask=flags ? 0xe7ffffffu : ~0u;
 while(l.Byte(e) && r.Byte(e)) {
  U a=e.Weight(table,l.Byte(e))&mask,b=e.Weight(table,r.Byte(e))&mask;
  if(a!=b) {
   const U ac=(a>>8)&255,bc=(b>>8)&255;
   if(ac==1 || ac==3 || bc==1 || bc==3) { e.Unsupported("NLS comparison category requires the general collation provider");return 0; }
   if((a&65535)!=(b&65535)) {
    bool skipped=false;
    if(!ac) { l.Skip();skipped=true; }
    if(!bc) { r.Skip();skipped=true; }
    if(skipped) continue; //
    if(ac==2) { l.Expand(e,table,a&255);skipped=true; }
    else if(ac==6) { //
     if(punctuationDone) { l.Skip();skipped=true; }
     else if(bc!=6) {
      if(rightPunctuation) { rightPunctuation=3;punctuationDone=true; }
      else leftPunctuation=3;
      l.Skip();skipped=true;
     }
    }
    if(bc==2) { r.Expand(e,table,b&255);skipped=true; }
    else if(bc==6) { //
     if(punctuationDone) { r.Skip();skipped=true; }
     else if(ac!=6) {
      if(leftPunctuation) { leftPunctuation=1;punctuationDone=true; }
      else rightPunctuation=1;
      r.Skip();skipped=true;
     } else {
      if(leftPunctuation) leftPunctuation=1;
      else if(rightPunctuation) rightPunctuation=3;
      else leftPunctuation=Order(a&255,b&255);
      l.Skip();r.Skip();punctuationDone=true;skipped=true;
     }
    }
    if(skipped) continue;
    return Order(a&65535,b&65535); //
   }
   // CP1252 has no class-1 combining marks, so the following-byte class-1
   // accumulation cannot be selected with this provider.
   if((pending&3) && ((a>>16)&255)!=((b>>16)&255)) {
    accent=Order((a>>16)&255,(b>>16)&255);pending&=~1u;
   }
   if((pending&4) && (((a^b)>>24)&63)) {
    lettercase=Order((a>>24)&63,(b>>24)&63);pending&=~4u;
   }
  }
  l.Advance();r.Advance(); //
 }
 // A remaining real primary weight outranks every deferred
 // result, including punctuation. The original uses the system table here.
 if(l.Byte(e) || r.Byte(e)) {
  Cursor&tail=l.Byte(e)?l:r;
  const U result=r.Byte(e)?1:3;
  while(tail.Byte(e)) {
   if((e.SystemWeight(tail.Byte(e))>>8)&255) return result;
   tail.Skip();
  }
 }
 if(accent) return accent;
 if(lettercase) return lettercase;
 if(leftPunctuation) return leftPunctuation;
 if(rightPunctuation) return rightPunctuation;
 return 2;
}
template<class E> U Compare(E&e,U locale,U flags,U left,U leftCount,U right,U rightCount) {
 flags&=0xbfffffffu;
 locale=Win98SystemLocale::ResolveLocale(locale,e.SystemLocale(),[&](){return e.UserLocale();});
 U table=locale==e.SystemLocale()?e.SystemTable():((locale&0xfff00000u)?0:e.LookupLocale(locale));
 // Fast body and general entry share these errors; missing installed tables
 // are distinguished by the native LookupLocale provider before this point.
 if(!table || !left || !right) {e.Error(87);return 0;}
 if(flags&0xbffceff8u) {e.Error(1004);return 0;}
 if(!e.FastTable(table)) {e.Unsupported("CompareStringA locale/codepage sort provider is not installed");return 0;}
 if(int(leftCount)>=0 || int(rightCount)>=0 || flags>1)return General(e,table,flags,left,leftCount,right,rightCount);
 return Fast(e,table,flags,left,right);
}
}
