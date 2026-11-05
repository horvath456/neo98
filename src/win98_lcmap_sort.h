// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 (SBCS sort keys) and (character removal).
// Weights, expansions, character types and accent mappings come from the
// original code-page tables. Compression and East Asian weight classes need
// their own provider; they must not silently use the SBCS implementation.
#pragma once
#include <stdlib.h>
namespace Win98LcMapSort {
using U = unsigned;
template<class E> U Remove(E &e,U descriptor,U flags,U source,U length,U destination,U capacity)
{
    U written=0;
    for(U i=0;i<length && (!capacity || written<capacity);++i) {
        U c=e.Read8(source+i),weight=e.Weight(descriptor,c),kind=(weight>>8)&255;
        if((flags&4)&&kind>=6&&kind<=11)continue;
        if((flags&2)&&(kind==1||((weight>>16)&255)>2)) {
            if(!(e.Type(descriptor,c)&0x100))continue;
            const U mapped=e.Unaccent(descriptor,c);
            if(mapped)c=mapped;
        }
        if(capacity)e.Write8(destination+written,c);
        ++written;
    }
    // Unlike case conversion, this original route returns a truncated byte
    // count without changing LastError when the destination fills up.
    return written;
}
template<class E> U Sort(E &e,U descriptor,U flags,U source,U length,U destination,U capacity)
{
    if(length>0x0fffffffu){e.Error(14);return 0;}
    auto *storage=static_cast<unsigned char*>(malloc(size_t(length)*12+1));
    if(!storage){e.Error(14);return 0;}
    auto *primary=storage,*accent=storage+length*4,*cases=storage+length*6,*special=storage+length*8;
    U np=0,na=0,nc=0,ns=0;
    const U mask=0x3f & ~(flags&1?0x18:0) & ~(flags&0x10000?0x20:0) & ~(flags&0x20000?1:0);
    auto ordinary=[&](U w){primary[np++]=(w>>8)&255;primary[np++]=w&255;
        accent[na++]=(w>>16)&255;cases[nc++]=(w>>24)&mask;};
    for(U i=0;i<length;++i) {
        const U w=e.Weight(descriptor,e.Read8(source+i)),kind=(w>>8)&255;
        if(kind==1){if(na)accent[na-1]+=w>>16;else accent[na++]=(w>>16)&255;}
        else if(kind==2){ordinary(e.Weight(descriptor,e.Expansion(descriptor,w&255,0)));
            ordinary(e.Weight(descriptor,e.Expansion(descriptor,w&255,1)));}
        else if(kind==6&&!(flags&0x1000)) {
            const U position=np*2+4;
            special[ns++]=(position>>8)|0x80;special[ns++]=position|3;
            special[ns++]=kind;special[ns++]=w&255;
        }
        else if(kind>=6&&kind<=11){if(!(flags&4))ordinary(w);}
        else if(kind>11)ordinary(w);
        else if(kind==3){free(storage);e.Unsupported("LCMapStringA East Asian sort weights are not installed");return 0;}
    }
    if(flags&2)na=0;
    else if(e.Backward(descriptor)) {
        U first=0;while(first<na&&accent[first]<=2)++first;
        for(U a=first,b=na;a<b && a<--b;++a){const auto c=accent[a];accent[a]=accent[b];accent[b]=c;}
        na-=first;for(U i=0;i<na;++i)accent[i]=accent[first+i];
    }
    else while(na&&accent[na-1]<=2)--na;
    if((flags&0x20001)==0x20001)nc=0;
    else while(nc&&cases[nc-1]<=2)--nc;
    if(flags&4)ns=0;
    const U needed=np+na+nc+ns+5;
    if(!capacity){free(storage);return needed;}
    U written=0;
    auto segment=[&](const unsigned char *bytes,U n,U separator){
        if(n>=capacity-written)return false;
        for(U i=0;i<n;++i)e.Write8(destination+written++,bytes[i]);
        e.Write8(destination+written++,separator);return true;
    };
    const bool ok=segment(primary,np,1)&&segment(accent,na,1)&&segment(cases,nc,1)&&
                  segment(nullptr,0,1)&&segment(special,ns,0);
    free(storage);
    if(!ok){e.Error(122);return 0;}
    return written;
}
}
