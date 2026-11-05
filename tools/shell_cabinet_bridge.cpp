/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_shell_cabinet.h"
#include <stdint.h>
#include <initializer_list>
using U=unsigned;using P=unsigned long long;
struct Wire{U op,bytes,mask,nullptr_input,initial_flags,initial_settings,clean,low,other,noactive,classic,result;Win98ShellCabinet::Settings settings;unsigned char input[32],output[32];};
static U(*provider)(U,const P*,U);
struct E{Wire&w;U Call(U op,std::initializer_list<P>a){return provider(op,a.begin(),a.size());}
 void Lock(){Call(1,{});}void Unlock(){Call(2,{});}U ExplorerKey(){return Call(3,{});}U Query(U key,const char*name,U*type,void*data,U*bytes){return Call(4,{key,(uintptr_t)name,(uintptr_t)type,(uintptr_t)data,(uintptr_t)bytes});}
 U Restricted(U p){Call(5,{p});return p==0x40000004?w.noactive:w.classic;}U Open(U root,const char*name,U access,U&key){return Call(6,{root,(uintptr_t)name,access,(uintptr_t)&key});}void Close(U key){Call(7,{key});}
 bool OtherPlatform(){return w.other;}bool LowMemory(){Call(8,{});return w.low;}U InitialFlags(){return w.initial_flags;}U InitialSettingsBits(){return w.initial_settings;}U CleanBoot(){Call(9,{});return w.clean;}
 U Create(const char*name,U&key){return Call(10,{(uintptr_t)name,(uintptr_t)&key});}U Set(U key,const char*name,U type,const void*data,U bytes){return Call(11,{key,(uintptr_t)name,type,(uintptr_t)data,bytes});}void CopyResult(void*out,const void*data,U n){memcpy(out,data,n);}
};
extern "C" void callbacks(U(*p)(U,const P*,U)){provider=p;}
extern "C" void run(Wire*w){E e{*w};using namespace Win98ShellCabinet;switch(w->op){case 0:w->result=Read(e,w->settings,w->nullptr_input?nullptr:w->output,w->bytes);break;case 1:w->result=Write(e,w->settings,w->nullptr_input?nullptr:reinterpret_cast<Record*>(w->input));break;case 2:{U result=GetSettings(e,w->settings,w->mask);memcpy(w->output,&result,4);break;}case 3:Refresh(e,w->settings);break;case 4:w->result=Read(e,w->settings,w->nullptr_input?nullptr:w->output,12);break;}}
