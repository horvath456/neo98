// SPDX-License-Identifier: GPL-2.0-or-later
// COMCTL32 / page objects.
#pragma once
namespace Win98PropertyPages {
using U=unsigned;
// Original header: [-24] public handle; [-8] Unicode page; [-4] ANSI shadow.
template<class E> U Allocate(E&e,U size){U p=e.Allocate(size+24);if(p){p+=24;e.Write(p-8,p);e.Write(p-24,p);}return p;}
template<class E> bool CloneFields(E&e,U p,U encoding){
    bool ok=true;
    for(U index=0;index<5;++index){U offset=index==0?12:index==1?16:index==2?20:index==3?40:44;
        U flags=e.Read(p+4);bool selected=index==0?!(flags&1):flags&(index==1?4:index==2?8:index==3?0x1000:0x2000);
        if(selected){U old=e.Read(p+offset);if(old&0xffff0000u){U next=e.Clone(old,encoding);e.Write(p+offset,next);if(!next)ok=false;}}
    }return ok;
}
template<class E> void FreeFields(E&e,U p){
    for(U index=0;index<5;++index){U offset=index==0?12:index==1?16:index==2?20:index==3?40:44;
        U flags=e.Read(p+4);bool selected=index==0?!(flags&1):flags&(index==1?4:index==2?8:index==3?0x1000:0x2000);
        if(selected){U value=e.Read(p+offset);if(value&0xffff0000u)e.Free(value);}
    }
}
// + : size 40 suppresses ADDREF (0), not RELEASE (1)/CREATE (2).
template<class E> U Callback(E&e,U page,U reason){
    U flags=e.Read(page+4),proc=e.Read(page+32);if(!(flags&0x80)||!proc)return 1;
    if(e.Read(page)<=40&&reason!=1&&reason!=2)return 1;
    U ansi=e.Read(page-4);return e.Callback(proc,reason,ansi?ansi:page,page);
}
// encoding: 0 duplicates UTF16; 1 converts ANSI->UTF16; 2 duplicates ANSI.
template<class E> U Create(E&e,U source,bool ansi){
    U size=e.Read(source);if(size<40||size>4096||(e.Read(source+4)&0xffff0000u))return 0;
    U bytes=ansi?48:size,page=Allocate(e,bytes);if(!page)return 0;
    U copy=e.Read(source);if(copy>bytes)copy=bytes;e.Copy(page,source,copy);
    if(!CloneFields(e,page,ansi?1:0)){FreeFields(e,page);e.Free(page-24);return 0;}
    if(ansi){U shadow=Allocate(e,e.Read(source));if(!shadow){FreeFields(e,page);e.Free(page-24);return 0;}
        e.Copy(shadow,source,e.Read(source));e.Write(page-4,shadow);e.Write(shadow-4,shadow);e.Write(shadow-8,page);e.Write(page-24,shadow);
        if(!CloneFields(e,shadow,2)){FreeFields(e,shadow);e.Free(shadow-24);FreeFields(e,page);e.Free(page-24);return 0;}
    }
    if(e.Read(page+4)&0x40){U ref=e.Read(page+36);if(ref)e.Reference(ref,1);}
    Callback(e,page,0);return e.Read(page-24);
}
template<class E> U FinishDestroy(E&e,U page){
    if(e.Read(page+4)&0x40){U ref=e.Read(page+36);if(ref)e.Reference(ref,-1);}
    U shadow=e.Read(page-4);if(shadow){FreeFields(e,shadow);e.Free(shadow-24);}
    FreeFields(e,page);e.Free(page-24);return 1;
}
template<class E> U Destroy(E&e,U handle){
    U page=e.Read(handle-8);
    if(e.Read(page+4)&0x8000){if(!e.LegacyRelease(page))return 0;}
    else{Callback(e,page,1);return FinishDestroy(e,page);}
    FreeFields(e,page);e.Free(page-24);return 1;
}
}
