// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_short_path.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U=unsigned;
struct Environment
{
    static constexpr U base=0x200000, size=0x6000, input=base+0x100, temporary=base+0x2000, converted=base+0x4000;
    unsigned char bytes[size];U cfg[16],error,finds=0,attributes=0;std::vector<U> events;
    Environment(const U *c,const unsigned char *b):error(c[5]) {std::memcpy(cfg,c,sizeof cfg);std::memcpy(bytes,b,size);}
    U Read8(U p) const {assert(p>=base && p-base<size);return bytes[p-base];}
    void Write8(U p,U v) {assert(p>=base && p-base<size);bytes[p-base]=v;}
    void Event(U id,U a=0,U b=0) {events.insert(events.end(),{id,a,b});}
    void Path(U id,U p) {U n=0;while(Read8(p+n))++n;Event(id,p,n);for(U i=0;i<n;++i)events.push_back(Read8(p+i));}
    U LastError(){Event(1,error);return error;}
    void Error(U v){error=v;Event(2,v);}
    bool FileApiMode(U v){Event(3,v);return cfg[4]!=0;}
    U ConvertInput(U p,U mode){Event(4,p,mode);U n=0;do{Write8(converted+n,Read8(p+n));}while(Read8(p+n++));return converted;}
    void FreeInput(U p,U mode){Event(5,p,mode);error=cfg[12];}
    void ClearFileApiState(U v){Event(6,v);}
    U AllocateZero(U n){Event(7,n);if(cfg[6]){error=cfg[6];return 0;}std::memset(bytes+(temporary-base),0,n);return temporary;}
    void Free(U p){Event(8,p);error=cfg[11];}
    U Attributes(U p){Path(9,p);++attributes;if(cfg[7]==attributes){error=cfg[8];return ~0u;}error=cfg[13];return 0x10;}
    U FindFirst(U p,Win98ShortPath::Names &out)
    {
        Path(10,p);++finds;if(cfg[9]==finds){error=cfg[10];return ~0u;}
        std::memset(&out,0,sizeof out);U n=0,part=0;while(Read8(p+n)){if(Win98ShortPath::Separator(Read8(p+n))||Read8(p+n)==':')part=n+1;++n;}
        switch(cfg[14])
        {
        case 0:for(U i=0;i<n-part && i<259;++i)out.long_name[i]=Read8(p+part+i);break;
        case 1:std::snprintf(reinterpret_cast<char*>(out.long_name),260,"Long component %u.txt",finds);std::snprintf(reinterpret_cast<char*>(out.short_name),14,"LONGCO~%u.TXT",finds);break;
        case 2:break;
        case 3:std::memset(out.long_name,'X',cfg[15]);break;
        default:assert(false);
        }
        error=cfg[13];return 0x7700+finds;
    }
    void FindClose(U h){Event(11,h);error=cfg[3];}
    U Run()
    {
        if(cfg[0]==2){std::memset(bytes+(temporary-base),0,260);return Win98ShortPath::Resolve(*this,input,temporary,cfg[1]!=0);}
        return Win98ShortPath::Get(*this,input,cfg[2],cfg[1],cfg[0]!=0);
    }
};
extern "C" {
void *short_path_new(const U *cfg,const unsigned char *bytes){return new Environment(cfg,bytes);}
U short_path_run(void *p){return static_cast<Environment*>(p)->Run();}
void short_path_delete(void *p){delete static_cast<Environment*>(p);}
const unsigned char *short_path_bytes(void *p){return static_cast<Environment*>(p)->bytes;}
const U *short_path_events(void *p,U *size){auto &v=static_cast<Environment*>(p)->events;*size=v.size();return v.data();}
U short_path_error(void *p){return static_cast<Environment*>(p)->error;}
}
#ifdef SHORT_PATH_STANDALONE
int main(int argc,char **argv)
{
    assert(argc==2);FILE*f=std::fopen(argv[1],"rb");assert(f);U count;assert(std::fread(&count,4,1,f)==1);
    for(U i=0;i<count;++i){U cfg[16];unsigned char bytes[Environment::size];assert(std::fread(cfg,4,16,f)==16);assert(std::fread(bytes,1,sizeof bytes,f)==sizeof bytes);Environment e(cfg,bytes);e.Run();}
    assert(std::fgetc(f)==EOF);std::fclose(f);std::printf("Short path ASan/UBSan: %u cases passed\n",count);
}
#endif
