// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_compare_string.h"
#include <cstring>
#include <cstdio>
#include <cassert>
#include <vector>
struct E {
 const unsigned char*cp;const unsigned char*left;const unsigned char*right;
 unsigned error=0x76543210,unsupported=0;
 unsigned Dword(unsigned p){return cp[p]|(cp[p+1]<<8)|(cp[p+2]<<16)|(unsigned(cp[p+3])<<24);}
 unsigned Half(unsigned p){return cp[p]|(cp[p+1]<<8);}
 bool Aborted() const {return false;}
 unsigned SystemLocale(){return 0x409;}unsigned UserLocale(){return 0x409;}
 unsigned SystemTable(){return 1;}unsigned LookupLocale(unsigned n){return n==0x409?1:0;}
 bool FastTable(unsigned){return true;}
 unsigned Read8(unsigned p){return p>=0x20000?right[p-0x20000]:left[p-0x10000];}
 unsigned Weight(unsigned,unsigned c){return Dword(Dword(13*4)*2+c*4);}
 unsigned SystemWeight(unsigned c){return Weight(1,c);}
 unsigned Expansion(unsigned,unsigned n,unsigned c){return Half(Dword(14*4)*2+n*4+c*2);}
 void Error(unsigned e){error=e;}void Unsupported(const char*){unsupported=1;}
};
extern "C" unsigned compare_string(const unsigned char*cp,unsigned locale,unsigned flags,const unsigned char*a,unsigned an,const unsigned char*b,unsigned bn,unsigned*out){
 E e{cp,a,b};unsigned result=Win98CompareString::Compare(e,locale,flags,a?0x10000:0,an,b?0x20000:0,bn);out[0]=e.error;out[1]=e.unsupported;return result;
}
#ifdef COMPARE_SANITIZE
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);std::vector<unsigned char>cp(9866);assert(fread(cp.data(),1,cp.size(),f)==cp.size());fclose(f);const char*strings[]={"","a","A","abc","a-b","ab","a'b","a--b","a'-b","\xc6","AE","\xe9","e","\xdf","ss"};unsigned count=0;for(auto*a:strings)for(auto*b:strings){unsigned out[2];auto r=compare_string(cp.data(),0x409,0,(const unsigned char*)a,~0u,(const unsigned char*)b,~0u,out);assert(r>=1&&r<=3&&!out[1]);++count;}printf("NLS COMPARE SANITIZERS PASS %u bounded portable comparisons\n",count);}
#endif
