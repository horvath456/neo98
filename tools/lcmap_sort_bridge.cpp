// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_lcmap_sort.h"
#include <cstring>
using U=unsigned;
struct E {
 const unsigned char *cp;unsigned char *memory;U error;bool backward;
 U Half(U at){return cp[at]|U(cp[at+1])<<8;}
 U Offset(U i){return (Half(i*4)|Half(i*4+2)<<16)*2;}
 U Weight(U,U c){U at=Offset(13)+c*4;return Half(at)|Half(at+2)<<16;}
 U Expansion(U,U i,U part){return Half(Offset(14)+i*4+part*2);}
 U Type(U,U c){return Half(Offset(15)+c*2);}
 U Unaccent(U,U c){return cp[Offset(12)+c];}
 U Read8(U p){return memory[p-0x120000];}
 void Write8(U p,U c){memory[p-0x120000]=c;}
 bool Backward(U){return backward;}
 void Error(U v){error=v;}void Unsupported(const char*){error=0xdead;}
};
extern "C" U lcmap_sort(const unsigned char *cp,unsigned char *memory,const U*a,U*error,U backward){
 E e{cp,memory,*error,bool(backward)};
 U n=int(a[3])<0?strlen(reinterpret_cast<char*>(memory+a[2]-0x120000))+1:a[3];
 U result=a[1]&0x400?Win98LcMapSort::Sort(e,1,a[1],a[2],n,a[4],a[5]):Win98LcMapSort::Remove(e,1,a[1],a[2],n,a[4],a[5]);
 *error=e.error;return result;
}
