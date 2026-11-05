// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_common_init.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U=unsigned;
using Event=std::array<U,12>;
struct Environment final:Win98CommonInit::Environment {
    U config[16]{},registered=0,attempts=0;
    unsigned char memory[4096];bool fault=false;
    std::vector<Event> events;
    Environment(){std::memset(memory,0xa5,sizeof memory);}
    bool Range(U p){if(p<0x100000||p>0x100ffc){fault=true;return false;}return true;}
    bool Read(U p,U&value)override{if(!Range(p))return false;std::memcpy(&value,memory+(p-0x100000),4);return true;}
    bool Write(U p,U value)override{if(!Range(p))return false;std::memcpy(memory+(p-0x100000),&value,4);return true;}
    void EventCall(Event e){events.push_back(e);if(events.size()==config[9])Write(0x100004,config[10]);}
    bool BadWrite(U p,U bytes)override{EventCall({3,p,bytes,config[11]});return config[11];}
    U Exists(U instance,U index)override{
        U result=registered&(1u<<index)?config[5]:0;
        EventCall({0,instance,index,result});return result;
    }
    U LoadCursor(U instance,U resource)override{EventCall({1,instance,resource,config[6]});return config[6];}
    U Register(U instance,U index,U cursor)override{
        const auto&c=Win98CommonInit::Classes[index];
        U result=++attempts==config[8]?0:config[7];
        if(result&65535)registered|=1u<<index;
        EventCall({2,c.style,c.procedure,0,c.window_extra,instance,0,cursor,c.background,index,0,result});
        return result;
    }
};
int main(int argc,char**argv){
    assert(argc==2);FILE*f=std::fopen(argv[1],"rb");assert(f);U count=0;
    for(;;){
        Environment e;
        const auto n=std::fread(e.config,sizeof(U),16,f);
        if(!n){assert(std::feof(f));break;}assert(n==16);
        e.registered=e.config[4];e.Write(0x100000,e.config[2]);e.Write(0x100004,e.config[3]);
        unsigned char expected[24];assert(std::fread(expected,1,24,f)==24);
        std::vector<Event> events(e.config[14]);
        assert(std::fread(events.data(),sizeof(Event),events.size(),f)==events.size());
        U op=e.config[0],p=e.config[1];
        U result=op<18?Win98CommonInit::RegisterGroup(e,op,0x45670000):
                 op==18?Win98CommonInit::Initialize(e,p,0x45670000):
                 op==19?Win98CommonInit::Version(e,p):Win98CommonInit::Legacy(p);
        assert(e.fault==bool(e.config[13]));if(!e.fault)assert(result==e.config[12]);
        assert(e.registered==e.config[15]);assert(events==e.events);
        assert(!std::memcmp(e.memory,expected,24));
        for(U i=24;i<4096;++i)assert(e.memory[i]==0xa5);
        ++count;
    }
    std::fclose(f);std::printf("COMMON INIT ASan/UBSan PASS %u cases\n",count);
}
