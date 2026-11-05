// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_path_family.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U = unsigned;
using State = Win98PathFamily::State;
struct PathEnvironment
{
    static constexpr U base=0x200000, size=0x20000;
    unsigned char bytes[size];
    State state;
    U cfg[24], error=0, allocations=0, creates=0, next=base+0x10000;
    std::vector<U> events;
    PathEnvironment(const U *c,const U *s,const unsigned char *b)
    {std::memcpy(cfg,c,sizeof cfg);std::memcpy(&state,s,sizeof state);std::memcpy(bytes,b,size);error=cfg[8];}
    U Read8(U p) const {assert(p>=base && p-base<size);return bytes[p-base];}
    void Write8(U p,U v){assert(p>=base && p-base<size);bytes[p-base]=v;}
    void Write32(U p,U v){for(U i=0;i<4;++i)Write8(p+i,(v>>(i*8))&255);}
    U LastError(){Event(1,error);return error;}
    void Error(U v){error=v;Event(2,v);}
    void Event(U id,U a=0,U b=0,U c=0){events.insert(events.end(),{id,a,b,c});}
    void Path(U id,U p,U extra=0){U n=Win98PathFamily::Length(*this,p);Event(id,p,extra,n);for(U i=0;i<n;++i)events.push_back(Read8(p+i));}
    void Lock(U id){Event(3,id);}
    void Unlock(U id){Event(4,id);}
    U Environment(const char *name)
    {
        U id=!std::strcmp(name,"TMP")?0:!std::strcmp(name,"TEMP")?1:2;
        U result=cfg[9+id];Event(5,id,result,id==2?static_cast<unsigned char>(name[1]):0);return result;
    }
    U Attributes(U p){Path(6,p);error=cfg[6];return cfg[5];}
    U Allocate(U n,U flags){Event(7,n,flags);if(++allocations==cfg[7]){error=8;return 0;}U p=next;next+=0x400;assert(n<0x400);if(flags&8)for(U i=0;i<n;++i)Write8(p+i,0);return p;}
    U Duplicate(U p){Path(8,p);if(++allocations==cfg[7]){error=8;return 0;}U to=next;next+=0x400;Win98PathFamily::Copy(*this,to,p);return to;}
    U Free(U p){Event(9,p);error=cfg[22];return cfg[20];}
    U ConvertInput(U p,U mode){Event(10,p,mode);if(mode || !p || !Read8(p))return p;U to=next;next+=0x400;Win98PathFamily::Copy(*this,to,p);return to;}
    U FreeInput(U p,U mode){Event(11,p,mode);error=cfg[22];return cfg[20];}
    void ConvertString(U to,U from,U direction){Event(12,to,from,direction);Win98PathFamily::Copy(*this,to,from);}
    void ConvertBytes(U to,U from,U n,U mode){Event(13,to,from,n);events.push_back(mode);for(U i=0;i<n;++i)Write8(to+i,Read8(from+i));}
    void DosSelectDrive(U drive){Event(14,drive);}
    void DosSetDirectory(U path){Path(15,path);}
    U DosCurrentDirectory(U drive,U output){Event(16,drive,output);Win98PathFamily::Copy(*this,output,cfg[19]);return cfg[18];}
    U DosCurrentDrive(){Event(17,cfg[17]);return cfg[17];}
    void DosClock(U &cx,U &dx){cx=cfg[15];dx=cfg[16];Event(18,cx,dx);}
    U CreateFile(U p,U access,U share,U security,U create,U flags,U templ)
    {
        Path(19,p);events.insert(events.end(),{access,share,security,create,flags,templ});
        ++creates;
        if(creates<=cfg[13]){error=creates&1?183:80;return ~0u;}
        if(cfg[14]){error=cfg[14];return ~0u;}
        return cfg[23];
    }
    U CloseFile(U p){Event(20,p);error=cfg[22];return cfg[12];}
    U Run()
    {
        using namespace Win98PathFamily;
        switch(cfg[0])
        {
        case 0:return Normalize(*this,cfg[1]);
        case 1:return UncRoot(*this,cfg[1]);
        case 2:return FullPath(*this,state,cfg[1],cfg[2],cfg[3],cfg[4]);
        case 3:return CurrentDirectory(*this,state,cfg[2],cfg[3]);
        case 4:return SetCurrentDirectory(*this,state,cfg[1]);
        case 5:return TempPath(*this,state,cfg[2],cfg[3]);
        case 6:return TempFileName(*this,state,cfg[1],cfg[4],cfg[2],cfg[3]);
        case 7:return UnsupportedWide(*this);
        case 8:return EnvironmentDirectory(*this,state,"TMP",cfg[3],cfg[2]);
        case 9:return FilePart(*this,cfg[1]);
        case 10:return FileApiMode(state,cfg[1]);
        case 11:{bool carry;U n=BoundedCopy(*this,cfg[1],cfg[3],cfg[2],carry);return n|(carry?0x80000000:0);}
        case 12:return ValidCharacters(*this,cfg[1]);
        case 13:{U result=SetCurrentDirectory(*this,state,cfg[1]);return result?CurrentDirectory(*this,state,cfg[2],cfg[3]):0;}
        default:assert(false);return 0;
        }
    }
};
extern "C" {
void *path_family_new(const U*c,const U*s,const unsigned char*b){return new PathEnvironment(c,s,b);}
U path_family_run(void*p){return static_cast<PathEnvironment*>(p)->Run();}
void path_family_delete(void*p){delete static_cast<PathEnvironment*>(p);}
const unsigned char *path_family_bytes(void*p){return static_cast<PathEnvironment*>(p)->bytes;}
const U *path_family_state(void*p){return reinterpret_cast<const U*>(&static_cast<PathEnvironment*>(p)->state);}
const U *path_family_events(void*p,U*size){auto&v=static_cast<PathEnvironment*>(p)->events;*size=v.size();return v.data();}
U path_family_error(void*p){return static_cast<PathEnvironment*>(p)->error;}
}
#ifdef PATH_FAMILY_STANDALONE
int main(int argc,char**argv)
{
    assert(argc==2);FILE*f=std::fopen(argv[1],"rb");assert(f);U count;assert(std::fread(&count,4,1,f)==1);
    for(U i=0;i<count;++i){U cfg[24],s[13];std::vector<unsigned char>bytes(PathEnvironment::size);assert(std::fread(cfg,4,24,f)==24);assert(std::fread(s,4,13,f)==13);assert(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());PathEnvironment e(cfg,s,bytes.data());e.Run();}
    assert(std::fgetc(f)==EOF);std::fclose(f);std::printf("Path family ASan/UBSan: %u cases passed\n",count);
}
#endif
