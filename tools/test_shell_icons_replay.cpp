// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include <cstdlib>
#include <vector>
using U=unsigned;
extern "C" U shell_icons_call(U,const U*,const unsigned char*,U,U*,U(*)(U,U,U,U,U,U,U));
static std::vector<U> events;
static unsigned position;
static U callback(U op,U a,U b,U c,U d,U e,U f) {
    const U args[]={op,a,b,c,d,e,f};
    if(position+8>events.size()) std::abort();
    for(U i=0;i<7;++i) if(events[position+i]!=args[i]) std::abort();
    const U result=events[position+7];position+=8;return result;
}
static void read(FILE *file,void *out,size_t bytes) { if(fread(out,1,bytes,file)!=bytes) std::abort(); }
int main(int argc,char **argv) {
    if(argc!=2)return 1;FILE *file=fopen(argv[1],"rb");if(!file)return 1;
    U operation,cases=0;
    while(fread(&operation,4,1,file)==1) {
        U args[10],size,expected[66],event_count;read(file,args,sizeof args);read(file,&size,4);
        std::vector<unsigned char> data(size);read(file,data.data(),size);read(file,expected,sizeof expected);read(file,&event_count,4);
        events.resize(event_count*8);read(file,events.data(),events.size()*4);position=0;
        U out[66];for(U &v:out)v=0xa5a5a5a5;out[64]=0xabcdef01;out[65]=1;
        const U result=shell_icons_call(operation,args,data.data(),size,out,callback);
        if(result!=expected[0])std::abort();
        for(U i=0;i<65;++i)if(out[i]!=expected[i+1])std::abort();
        if(position!=events.size())std::abort();++cases;
    }
    fclose(file);printf("SHELL ICONS SANITIZERS PASS: %u original cases\n",cases);
}
