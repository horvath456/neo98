// SPDX-License-Identifier: GPL-2.0-or-later
#include "crt_multibyte_bridge.cpp"
#include "../src/win98_crt_set_search.h"
#include <cstdio>
extern "C" unsigned set_search_run(Input *input,unsigned op,unsigned a,unsigned b) {
    struct Reader:Bridge {
        bool enabled;
        Reader(Input&s,unsigned op):Bridge{s},enabled(op<4&&s.state.multibyte){}
        bool Multibyte(){return enabled;}
    } e(*input,op);
    return Win98CrtSetSearch::Search(e,a,b,op==2||op==3||op==6,op==0||op==3||op==4);
}
#ifdef SET_SEARCH_REPLAY
int main(int argc,char**argv){
    assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);unsigned count=0;assert(fread(&count,4,1,f)==1);
    for(unsigned i=0;i<count;++i){Input input{},expected{};unsigned args[4];assert(fread(&input,sizeof input,1,f)==1);assert(fread(args,sizeof args,1,f)==1);assert(fread(&expected,sizeof expected,1,f)==1);assert(set_search_run(&input,args[0],args[1],args[2])==args[3]);assert(!memcmp(&input,&expected,sizeof input));}
    fclose(f);printf("Set search sanitizer replay PASS cases=%u\n",count);
}
#endif
