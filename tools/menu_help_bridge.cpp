// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_menu_help.h"
#include <cstdio>
#include <cstring>
using U=unsigned;using W=unsigned short;
struct Event{U kind,a,b;W text[256];};
struct Wire{U args[7],ids[260],type,found,restore,count;Event events[12];};
struct Env:Win98MenuHelp::Environment{Wire&w;Env(Wire&v):w(v){}void EventOf(U k,U a=0,U b=0,const W*t=nullptr){auto&e=w.events[w.count++];e.kind=k;e.a=a;e.b=b;if(t)for(U n=0;n<255&&t[n];++n)e.text[n]=t[n];}U CommonModule()override{return 0x3333;}bool Read(U p,U&v)override{if(p<0x20000||p>=0x20410||p&3)return false;v=w.ids[(p-0x20000)/4];return true;}bool ItemType(U m,U i,U&t)override{EventOf(1,m,i);t=w.type;return w.found;}bool LastIsRestore(U m)override{EventOf(2,m);return w.restore;}void LoadString(U m,U r,W(&t)[256])override{EventOf(3,m,r);t[0]='R';t[1]=W(r);t[2]=0x20ac;t[3]=0;}void Text(U h,U f,const W*t)override{EventOf(4,h,f,t);}void Simple(U h,bool s)override{EventOf(5,h,s);}void Update(U h)override{EventOf(6,h);}};
extern "C" void menu_help(Wire*w){Env e(*w);Win98MenuHelp::Help(e,w->args);}
#ifdef MENU_HELP_REPLAY
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 3;Wire before,expected;unsigned n=0;while(fread(&before,sizeof before,1,f)==1){if(fread(&expected,sizeof expected,1,f)!=1)return 4;menu_help(&before);if(memcmp(&before,&expected,sizeof before)){fprintf(stderr,"MenuHelp replay mismatch%u\n",n);return 5;}++n;}fclose(f);printf("MENU HELP ORIGINAL REPLAY PASS: %u cases\n",n);}
#endif
