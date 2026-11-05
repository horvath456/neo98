/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include <initializer_list>
#include "win98_message_box.h"
using namespace Win98MessageBox;
using Fn=U(*)(U,const U*,U);static Fn fn;
extern "C" void callbacks(Fn f){fn=f;}
struct E {
 U call(U op,std::initializer_list<U> a){return fn(op,a.begin(),a.size());}
 struct Scratch {U address=0x180000;Scratch(E&,U){} }; // virtual scratch, intentionally independent of x86 stack placement
 U Read(U p){return call(1,{p});}
 U LoadString(U m,U id,U p,U n){return call(2,{m,id,p,n});}
 U Show(const Parameters&p){return fn(3,reinterpret_cast<const U*>(&p),10);}
 U Convert(U p,U out,U n){return call(4,{0,0,p,0xffffffff,out,n,0,0});}
 U Allocate(U n){return call(5,{0,n});}
 void Free(U p){call(6,{p});}
};
extern "C" U indirect(U p){E e;return Indirect(e,p);}
extern "C" U wide(U owner,U text,U caption,U style,U lang){E e;return Wide(e,owner,text,caption,style,lang);}
extern "C" U command(U a,U b){return Command(a,b);}
extern "C" U actual(U a,U b){return ActualButton(a,b);}
extern "C" U result(U a,U b){return Result(a,b);}
extern "C" U buttons(U style,U*out){auto b=SelectButtons(style);out[0]=b.selected;for(U i=0;i<b.count;++i){out[1+3*i]=b.ids[i];out[2+3*i]=b.labels[i];out[3+3*i]=i==b.selected;}return b.count;}
