// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_character_case.h"
#include <cstdlib>
#include <cstring>
using U=unsigned;using Callback=U(*)(U,const U*,U,void*);
struct E{Callback cb;U Call(U op,U a=0,U b=0,U c=0,U d=0,void*p=nullptr){U v[]={a,b,c,d};return cb(op,v,4,p);}U UserLocale(){return Call(1);}U Length(U p){return Call(2,p);}bool BadString(U p,U n){return Call(3,p,n);}bool BadWrite(U p,U n){return Call(4,p,n);}unsigned char*Allocate(U n){return Call(5,n)?static_cast<unsigned char*>(malloc(n)):nullptr;}void Free(unsigned char*p){Call(6);free(p);}U MapCharacter(U locale,bool lower,U c,unsigned char&out){return Call(7,locale,lower,c,1,&out);}U MapBuffer(U locale,bool lower,U p,U n,unsigned char*out){return Call(8,locale,lower,p,n,out);}void Write8(U p,U c){Call(9,p,c);}void Error(U code){Call(10,code);}};
extern "C" U public_case(Callback cb,const U*a){E e{cb};return Win98CharacterCase::Public(e,a[0],a[1],a[2],a[3]);}
struct K{const unsigned char*table;unsigned char*memory;U error=0;U Read32(U at){return U(table[at])|U(table[at+1])<<8|U(table[at+2])<<16|U(table[at+3])<<24;}U SystemLocale(){return 0x409;}U UserLocale(){return 0x409;}U SystemTable(){return 1;}U LookupLocale(U locale){return locale==0x409?1:0;}U Length(U p){return strlen(reinterpret_cast<char*>(memory+(p-0x120000)));}U Read8(U p){return memory[p-0x120000];}void Write8(U p,U v){memory[p-0x120000]=v;}bool Multibyte(U){return false;}bool Lead(U,U){return false;}U LocaleNumber(U){return 0x409;}U CaseByte(U,bool lower,U c){return table[Read32((lower?7:6)*4)*2+c];}U CaseWord(U,bool,U){return 0;}void Error(U v){error=v;}};
extern "C" U kernel_case(const unsigned char*table,unsigned char*memory,const U*a,U*error){K e{table,memory,*error};U result=Win98CharacterCase::KernelCase(e,a[0],a[1]!=0,a[2],a[3],a[4],a[5]);*error=e.error;return result;}

#ifdef CHARACTER_CASE_SANITIZE
#include <cassert>
static unsigned char fixture[64]="abcdefghijklmnopqrstuvwxyz";
static U service(U op,const U*a,U,void*out){switch(op){case 1:return 0x409;case 2:return strlen((char*)fixture);case 3:case 4:case 6:case 10:return 0;case 5:return 1;case 7:*(unsigned char*)out=(a[2]>='a'&&a[2]<='z')?a[2]-32:a[2];return 1;case 8:for(U i=0;i<a[3];++i)((unsigned char*)out)[i]=fixture[i];return a[3];case 9:fixture[a[0]-0x300000]=a[1];return 0;}return 0;}
int main(){U scalar[]={97,0,0,0};assert(public_case(service,scalar)==65);U buffer[]={0x300000,26,1,1};assert(public_case(service,buffer)==26);buffer[1]=18;assert(public_case(service,buffer)==18);return 0;}
#endif
