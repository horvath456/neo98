// SPDX-License-Identifier: GPL-2.0-or-later
#include "mouse_capture_bridge.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
static State*s;static unsigned action,depth,count,trace[128][4];
static unsigned service(unsigned op,unsigned h,unsigned a,unsigned b){assert(count<128);unsigned*t=trace[count++];t[0]=op;t[1]=h;t[2]=a;t[3]=b;auto&q=s->queues[s->current];
 if(op==0&&s->globals.tracked&&s->windows[s->globals.tracked].queue==h)s->globals.tracked=0;
 if(op==3&&!depth){depth=1;if(action==1)capture_run(s,service,1,6,0);if(action==2)capture_run(s,service,2,0,0);if(action==3)s->windows[h].flags=0;if(action==4)q.flags|=0x800;depth=0;}
 if(op==8){q.menu_present=0;q.flags&=~0x800u;if(action==5)capture_run(s,service,1,6,0);else capture_run(s,service,2,0,0);}return 0;
}
static void read(FILE*f,void*p,size_t n){assert(fread(p,1,n,f)==n);}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);unsigned n,total=0;read(f,&n,4);for(unsigned i=0;i<n;++i){State input,expected;unsigned args[5],length,want[128][4];read(f,&input,sizeof input);read(f,args,sizeof args);read(f,&expected,sizeof expected);read(f,&length,4);assert(length<=128);read(f,want,length*16);s=&input;action=args[3];count=depth=0;assert(capture_run(s,service,args[0],args[1],args[2])==args[4]);assert(!memcmp(s,&expected,sizeof expected));assert(count==length&&!memcmp(trace,want,length*16));total+=count;}assert(fgetc(f)==EOF);fclose(f);printf("Mouse capture sanitizer replay PASS cases=%u provider_calls=%u\n",n,total);}
