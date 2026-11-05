// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_common_init.h"
using U=unsigned;
using Read=bool(*)(U,U*);using Write=bool(*)(U,U);using Pair=U(*)(U,U);
using Register=U(*)(U,U,U);
struct Environment final:Win98CommonInit::Environment {
    ::Read read;::Write write;Pair probe,exists,cursor;::Register registration;
    bool Read(U p,U &v)override{return read(p,&v);}
    bool Write(U p,U v)override{return write(p,v);}
    bool BadWrite(U p,U n)override{return probe(p,n);}
    U Exists(U i,U c)override{return exists(i,c);}
    U LoadCursor(U i,U r)override{return cursor(i,r);}
    U Register(U i,U c,U h)override{return registration(i,c,h);}
};
extern "C" U common_init_call(U op,U p,U instance,Read r,Write w,Pair probe,
                              Pair exists,Pair cursor,Register registration){
    Environment e;e.read=r;e.write=w;e.probe=probe;e.exists=exists;
    e.cursor=cursor;e.registration=registration;
    if(op<18)return Win98CommonInit::RegisterGroup(e,op,instance);
    if(op==18)return Win98CommonInit::Initialize(e,p,instance);
    if(op==19)return Win98CommonInit::Version(e,p);
    return Win98CommonInit::Legacy(p);
}
extern "C" const Win98CommonInit::Class *common_init_classes(){return Win98CommonInit::Classes;}
