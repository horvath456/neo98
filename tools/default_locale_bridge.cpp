/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_default_locale.h"
using Win98DefaultLocale::U;
namespace {
struct Environment {
    const U *config; U *out; U n=0; char replacement[128]{}; char *buffer=nullptr;
    void Event(U a,U b=0,U c=0){out[2+n++]=a;out[2+n++]=b;out[2+n++]=c;}
    U Pointer(const char *p) const{return !p?0:p==buffer?1:2;}
    U ReadUserLocale(char *&text,U size,U &allocated){
        Event(1,size,allocated);buffer=text;allocated=config[5];
        if(config[6]==1)text=replacement;else if(config[6]==2)text=nullptr;
        return config[3];
    }
    U ParseHex(const char *p){Event(2,Pointer(p));return config[4];}
    void FreeLocale(char *p){Event(3,Pointer(p));}
    U SystemLocale(){Event(4);return config[2];}
};
}
extern "C" U default_locale(const U *config,U *out)
{
    Environment e{config,out};Win98DefaultLocale::State state{config[1]};
    U result=0;
    if(!config[0])result=Win98DefaultLocale::GetUser(e,state);
    else Win98DefaultLocale::Initialize(e,state);
    out[0]=state.user;out[1]=e.n;return result;
}
