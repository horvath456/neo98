// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_modules.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
struct E {
 unsigned char memory[131072]{};unsigned heap=0x18000,error=99,live=0,calls=0;bool fail=false;
 bool IsNt(){return false;}unsigned NativeWide(unsigned,unsigned,unsigned,unsigned){return 0;}
 unsigned Alloc(unsigned n,unsigned){if(fail)return 0;assert(heap+n<sizeof(memory));unsigned p=heap;heap+=n;live++;return p;}
 void Free(unsigned){assert(live);--live;}unsigned Byte(unsigned p){assert(p<sizeof(memory));return memory[p];}void Byte(unsigned p,unsigned v){assert(p<sizeof(memory));memory[p]=v;}
 unsigned Word(unsigned p){return Byte(p)|(Byte(p+1)<<8);}void Word(unsigned p,unsigned v){Byte(p,v);Byte(p+1,v>>8);}
 unsigned Get(unsigned p,unsigned n){return n==2?Word(p):Byte(p);}void Put(unsigned p,unsigned v,unsigned n){if(n==2)Word(p,v);else Byte(p,v);}
 unsigned Length(unsigned p){unsigned n=0;while(Byte(p+n))++n;return n;}
 unsigned Wide(unsigned p,unsigned,unsigned o,unsigned cap){unsigned n=0;do{++n;}while(Word(p+(n-1)*2));if(!cap)return n;if(cap<n){error=122;return 0;}for(unsigned i=0;i<n;++i)Byte(o+i,Word(p+i*2));return n;}
 unsigned Multi(unsigned p,unsigned n,unsigned o,unsigned cap){if(!cap)return n;if(cap<n){error=122;return 0;}for(unsigned i=0;i<n;++i)Word(o+i*2,Byte(p+i));return n;}
 unsigned Error(){return error;}void Error(unsigned n){error=n;}void ConversionFailure(){assert(false);}
 unsigned Module(unsigned op,unsigned,unsigned out,unsigned cap){++calls;if(op!=80)return 0x77000000;const char*s="C:\\USER32.DLL";unsigned n=strlen(s);if(cap){if(n>=cap)n=cap-1;for(unsigned i=0;i<n;++i)Byte(out+i,s[i]);Byte(out+n,0);}return n;}
};
int main(){for(unsigned len: {0u,259u,260u,518u,519u,600u})for(unsigned op:{83u,105u}){E e;for(unsigned i=0;i<len;++i)e.Word(0x10000+i*2,'x');unsigned r=Win98ShellModules::InputCall(e,op,0x10000,0,8,0x11000);assert(!e.live);assert(r==((op==105&&len>=260)?0u:0x77000000u));}for(unsigned cap:{0u,1u,4u,14u,260u,261u}){E e;unsigned r=Win98ShellModules::FileName(e,1,0x10000,cap,0x11000);assert(!e.live);assert(r==(cap<14?cap:14));}puts("PASS shell module core sanitizer 18 cases");}
