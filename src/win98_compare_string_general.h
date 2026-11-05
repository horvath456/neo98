// SPDX-License-Identifier: GPL-2.0-or-later
// Original KERNEL32 specialized for the installed CP1252
// descriptor (no DBCS, remapping, class1/3, or reversed accent ordering).
#pragma once
namespace Win98CompareString {
template<class E> auto Backward(E &e,unsigned table,int)->decltype(e.Backward(table)){return e.Backward(table);}
template<class E> bool Backward(E &,unsigned,long){return false;}

struct BoundedCursor{
 unsigned address,remaining,saved=0,index=0;unsigned char expansion[2]{};bool expanded=false,terminated;
 BoundedCursor(unsigned p,unsigned n):address(p),remaining(n),terminated(int(n)<0){}
 template<class E> unsigned Byte(E&e)const{return expanded?expansion[index]:e.Read8(address);}
 template<class E> bool Has(E&e)const{if(e.Aborted())return false;return expanded||(!terminated?remaining!=0:e.Read8(address)!=0);}
 void Advance(){if(expanded){if(++index<2)return;expanded=false;address=saved+1;}else ++address;if(!terminated)--remaining;}
 template<class E> void Expand(E&e,unsigned table,unsigned pair){saved=address;index=0;expanded=true;expansion[0]=e.Expansion(table,pair,0);expansion[1]=e.Expansion(table,pair,1);}
};
inline unsigned GeneralOrder(unsigned a,unsigned b){return a<b?1:a>b?3:2;}
template<class E> unsigned GeneralRun(E&e,unsigned table,unsigned flags,unsigned left,unsigned leftCount,unsigned right,unsigned rightCount){
 BoundedCursor l(left,leftCount),r(right,rightCount);
 // skips identical code units without reading past either
 // explicit byte count. NUL is a class0 unit in explicit-length strings.
 while(l.Has(e)&&r.Has(e)&&l.Byte(e)==r.Byte(e)){l.Advance();r.Advance();}
 if(!l.Has(e)&&!r.Has(e))return 2;
 unsigned mask=~0u,pending=(flags&2)?4:(Backward(e,table,0)?7:5);
 if(flags&1)mask&=0xe7ffffffu;if(flags&2)mask&=0xff00ffffu;
 if(flags&0x10000)mask&=0xdfffffffu;if(flags&0x20000){mask&=0xfeffffffu;if(flags&1)pending&=~4u;}
 unsigned accent=0,lettercase=0,leftPunctuation=0,rightPunctuation=0;
 bool punctuationDone=false,ignoreSymbols=(flags&4)!=0,stringSort=(flags&0x1000)!=0;
 while(l.Has(e)&&r.Has(e)){
  unsigned a=e.Weight(table,l.Byte(e))&mask,b=e.Weight(table,r.Byte(e))&mask;
  unsigned ac=(a>>8)&255,bc=(b>>8)&255;
  if(ignoreSymbols){bool skip=false;if(ac>=6&&ac<=11){l.Advance();skip=true;}if(bc>=6&&bc<=11){r.Advance();skip=true;}if(skip)continue;}
  if(a!=b){
   if(ac==1||ac==3||bc==1||bc==3){e.Unsupported("CompareStringA combining/compression table class is not installed");return 0;}
   if((a&65535)!=(b&65535)){
    bool skipped=false;
    if(!ac){l.Advance();skipped=true;}if(!bc){r.Advance();skipped=true;}if(skipped)continue;
    if(ac==2){l.Expand(e,table,a&255);skipped=true;}
    else if(ac==6&&!stringSort){if(punctuationDone){l.Advance();skipped=true;}else if(bc!=6){if(rightPunctuation){rightPunctuation=3;punctuationDone=true;}else leftPunctuation=3;l.Advance();skipped=true;}}
    if(bc==2){r.Expand(e,table,b&255);skipped=true;}
    else if(bc==6&&!stringSort){if(punctuationDone){r.Advance();skipped=true;}else if(ac!=6){if(leftPunctuation){leftPunctuation=1;punctuationDone=true;}else rightPunctuation=1;r.Advance();skipped=true;}else{if(leftPunctuation)leftPunctuation=1;else if(rightPunctuation)rightPunctuation=3;else leftPunctuation=GeneralOrder(a&255,b&255);l.Advance();r.Advance();punctuationDone=true;skipped=true;}}
    if(skipped)continue;
    return GeneralOrder(a&65535,b&65535);
   }
   if((pending&3)&&((a>>16)&255)!=((b>>16)&255)){accent=GeneralOrder((a>>16)&255,(b>>16)&255);pending&=~1u;}
   if((pending&4)&&(((a^b)>>24)&63)){lettercase=GeneralOrder((a>>24)&63,(b>>24)&63);pending&=~4u;}
  }
  l.Advance();r.Advance();
 }
 if(l.Has(e)||r.Has(e)){bool rightRemains=r.Has(e);auto&tail=rightRemains?r:l;unsigned result=rightRemains?1:3;
  while(tail.Has(e)){unsigned cls=(e.Weight(table,tail.Byte(e))>>8)&255;if(cls&&!(ignoreSymbols&&cls>=6&&cls<=11))return result;tail.Advance();}
 }
 if(accent)return accent;if(lettercase)return lettercase;if(leftPunctuation)return leftPunctuation;if(rightPunctuation)return rightPunctuation;return 2;
}
// Native memory/read-budget failure is a provider boundary, not an original
// collation branch. Has stops counted scans; no partial result is published.
template<class E> unsigned General(E&e,unsigned table,unsigned flags,unsigned left,unsigned leftCount,unsigned right,unsigned rightCount){unsigned result=GeneralRun(e,table,flags,left,leftCount,right,rightCount);return e.Aborted()?0:result;}

}
