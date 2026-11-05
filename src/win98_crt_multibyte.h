// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98CrtMultibyte {
using U=unsigned;
struct State {U page=0,multibyte=0,locale=0,system_default=0;unsigned char ranges[12]={},type[257]={};};
// The multibyte locale cache contains five 48-byte records.
inline constexpr unsigned char Builtin[]={
 0xa4,0x03,0x00,0x00,0x60,0x82,0x79,0x82,0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0xa6,0xdf,0x00,0x00,0x00,0x00,0x00,0x00,0xa1,0xa5,0x00,0x00,0x00,0x00,0x00,0x00,
 0x81,0x9f,0xe0,0xfc,0x00,0x00,0x00,0x00,0x40,0x7e,0x80,0xfc,0x00,0x00,0x00,0x00,
 0xa8,0x03,0x00,0x00,0xc1,0xa3,0xda,0xa3,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x81,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,
 0xb5,0x03,0x00,0x00,0xc1,0xa3,0xda,0xa3,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x81,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x41,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,
 0xb6,0x03,0x00,0x00,0xcf,0xa2,0xe4,0xa2,0x1a,0x00,0xe5,0xa2,0xe8,0xa2,0x5b,0x00,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x81,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x7e,0xa1,0xfe,0x00,0x00,0x00,0x00,
 0x51,0x05,0x00,0x00,0x51,0xda,0x5e,0xda,0x20,0x00,0x5f,0xda,0x6a,0xda,0x32,0x00,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
 0x81,0xd3,0xd8,0xde,0xe0,0xf9,0x00,0x00,0x31,0x7e,0x81,0xfe,0x00,0x00,0x00,0x00,
};
inline U Word(const unsigned char*p){return U(p[0])|(U(p[1])<<8);}
inline U Page(const unsigned char*p){return Word(p)|(Word(p+2)<<16);}
inline U Locale(U page){return page==932?0x411:page==936?0x804:page==949?0x412:page==950?0x404:0;}
inline U GetPage(const State&s){return s.multibyte?s.page:0;}
inline bool Lead(const State&s,U c){return s.type[(c&255)+1]&4;}
inline bool Trail(const State&s,U c){return s.type[(c&255)+1]&8;}
// Providers: Lock/Unlock(25), Ansi/Oem/LocalePage, CpInfo(page,max,lead[12]), CaseTable(State&).
// CaseTable is explicit: case-sensitive operations below never consult case mapping.
template<class E>U SetPage(E&e,State&s,U requested){
 e.Lock(25);s.system_default=0;U page=requested;
 if(requested==U(-2)){s.system_default=1;page=e.Oem();}
 else if(requested==U(-3)){s.system_default=1;page=e.Ansi();}
 else if(requested==U(-4)){s.system_default=1;page=e.LocalePage();}
 if(page==s.page){e.Unlock(25);return 0;}
 const unsigned char*b=nullptr;for(U i=0;i<5;++i)if(Page(Builtin+i*48)==page)b=Builtin+i*48;
 if(!page){s.page=s.multibyte=s.locale=0;for(auto&x:s.type)x=0;for(auto&x:s.ranges)x=0;}
 else if(b){
  for(auto&x:s.type)x=0;static constexpr unsigned char mask[]={1,2,4,8};
  for(U group=0;group<4;++group){const auto*r=b+16+group*8;for(U pair=0;pair<8&&r[pair]&&r[pair+1];pair+=2)for(U c=r[pair];c<=r[pair+1];++c)s.type[c+1]|=mask[group];}
  s.page=page;s.multibyte=1;s.locale=Locale(page);for(U i=0;i<12;++i)s.ranges[i]=b[4+i];
 }else{
  U maximum=0;unsigned char lead[12]={};if(!e.CpInfo(page,maximum,lead)){if(!s.system_default){e.Unlock(25);return U(-1);}s.page=s.multibyte=s.locale=0;for(auto&x:s.type)x=0;for(auto&x:s.ranges)x=0;}
  else{for(auto&x:s.type)x=0;s.page=page;s.locale=0;
   if(maximum>1){for(U p=0;p<12&&lead[p]&&lead[p+1];p+=2)for(U c=lead[p];c<=lead[p+1];++c)s.type[c+1]|=4;for(U c=1;c<255;++c)s.type[c+1]|=8;s.locale=Locale(page);s.multibyte=1;}else s.multibyte=0;
   for(auto&x:s.ranges)x=0;
  }
 }
 e.CaseTable(s);e.Unlock(25);return 0;
}
// Providers for string bodies: Read8/Read32, Multibyte, LeadByte, Lock/Unlock.
// Address arithmetic remains unsigned 32-bit guest arithmetic.
template<class E>U Next(E&e,U p){U c=e.Read8(p++);if(e.LeadByte(c)){U trail=e.Read8(p);if(!trail)return 0;c=(c<<8)|trail;}return c;}
template<class E>U Take(E&e,U&p){U c=e.Read8(p++);if(e.LeadByte(c)){U trail=e.Read8(p);if(!trail)return 0;c=(c<<8)|trail;++p;}return c;}
inline U Order(U a,U b){return a==b?0:a<b?U(-1):1;}
template<class E>U SingleCompare(E&e,U a,U b,U n){while(n--){U x=e.Read8(a++),y=e.Read8(b++);if(x!=y)return Order(x,y);if(!x)return 0;}return 0;}
template<class E>U Compare(E&e,U a,U b,U n,U mode){
 if(mode&&n==0)return 0;if(!e.Multibyte())return SingleCompare(e,a,b,mode?n:U(-1));
 e.Lock(25);U result=0;
 if(mode!=2){for(;;){U x=Take(e,a),y=Take(e,b);result=Order(x,y);if(result||!x||(mode&&!--n))break;}}
 else{--n;for(;;){U x=e.Read8(a++),y=0;
   if(e.LeadByte(x)){if(!n){U first=e.Read8(b);if(e.LeadByte(first))break;y=first;x=0;result=Order(x,y);break;}U trail=e.Read8(a);if(trail){x=(x<<8)|trail;++a;}else x=0;}
   y=e.Read8(b++);if(e.LeadByte(y)){if(n){U trail=e.Read8(b);--n;if(trail){y=(y<<8)|trail;++b;}else y=0;}else y=0;}
   result=Order(x,y);if(result||!x||!n)break;--n;
  }}
 e.Unlock(25);return result;
}
template<class E>U Increment(E&e,U p){return p+1+(e.LeadByte(e.Read8(p))?1:0);}
template<class E>U Character(E&e,U p){U c=e.Read8(p);return e.LeadByte(c)?(c<<8)|e.Read8(p+1):c;}
template<class E>U CharacterBytes(E&e,U p){return e.LeadByte(e.Read8(p))?2:1;}
template<class E>U Length(E&e,U p){if(!e.Multibyte()){U start=p;while(e.Read8(p))++p;return p-start;}e.Lock(25);U n=0;while(e.Read8(p)){if(e.LeadByte(e.Read8(p))&&!e.Read8(++p))break;++p;++n;}e.Unlock(25);return n;}
template<class E>U ByteCount(E&e,U p,U n){e.Lock(25);U start=p;while(n--&&e.Read8(p)){if(e.LeadByte(e.Read8(p))&&!e.Read8(++p)){--p;break;}++p;}e.Unlock(25);return p-start;}
template<class E>U CharacterCount(E&e,U p,U n){e.Lock(25);U count=0;while(n--&&e.Read8(p)){if(e.LeadByte(e.Read8(p))){if(!n)break;--n;if(!e.Read8(++p))break;}++p;++count;}e.Unlock(25);return count;}
template<class E>U Advance(E&e,U p,U n){return p?p+ByteCount(e,p,n):0;}
template<class E>U Decrement(E&e,U start,U current){if(start>=current)return 0;if(!e.Multibyte())return current-1;e.Lock(25);U p=current-1,result;
 if(e.LeadByte(e.Read8(p)))result=p-1;
 else{--p;while(p>=start&&e.LeadByte(e.Read8(p)))--p;result=current-((current-p)&1)-1;}
 e.Unlock(25);return result;
}
template<class E>U Boundary(E&e,U start,U at,bool trail){if(!e.Multibyte())return 0;e.Lock(25);U result=0;while(start<=at&&e.Read8(start)){if(e.LeadByte(e.Read8(start))){U lead=start++;if((trail?start:lead)==at){result=U(-1);break;}if(!e.Read8(start))break;}++start;}e.Unlock(25);return result;}
}
