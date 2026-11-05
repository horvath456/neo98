// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dc_map.h"
#include <cstdio>
#include <vector>
#include <cstring>
using namespace Win98DcMap;
static State Load(const U *p)
{
    return {{S(p[0]),S(p[1])},{S(p[2]),S(p[3])},{S(p[4]),S(p[5])},
            {S(p[6]),S(p[7])},{S(p[8]),S(p[9])},{S(p[10]),S(p[11])},
            {S(p[12]),S(p[13])},{S(p[14]),S(p[15])},W(p[16]),W(p[17]),p[18]&255};
}
static void Save(const State &s,U *p)
{
    const Point pairs[]={s.window_origin,s.window_extent,s.viewport_effective,s.viewport_extent,
                         s.viewport_origin,s.device_origin,s.translation,s.shifts};
    for(U i=0;i<8;++i){p[i*2]=W(U(pairs[i].x));p[i*2+1]=W(U(pairs[i].y));}
    p[16]=s.mode;p[17]=s.flags;p[18]=s.transform_flags;
}
static U Pack(Point p) { return W(U(p.x))|(W(U(p.y))<<16); }
struct Environment
{
    const U *data;
    Units DeviceUnits(U mode)
    { const auto *p=data+(mode-1)*4;return {{S(p[0]),S(p[1])},{S(p[2]),S(p[3])}}; }
    Point DeviceAspect() { return {S(data[0]),S(data[1])}; }
    bool ObjectAffectsMapping() { return data[32]!=0; }
};
extern "C" void dc_map_call(U operation,const U *initial,const U *device,const U *args,U *out)
{
    State s=Load(initial);Environment e{device};U result=0;
    switch(operation)
    {
        case 0:Recompute(s);break;
        case 1:result=SetMode(e,s,args[0]);break;
        case 2:result=Pack(SetOrigin(s,args[0]!=0,{S(args[1]),S(args[2])},args[3]!=0));break;
        case 3:result=Pack(SetExtent(e,s,args[0]!=0,{S(args[1]),S(args[2])}));break;
        case 4:result=Pack(ScaleExtent(e,s,args[0]!=0,{S(args[1]),S(args[3])},{S(args[2]),S(args[4])}));break;
        case 5:result=Pack(Transform(s,{S(args[0]),S(args[1])},args[2]!=0,args[3]!=0));break;
        case 6:{const auto p=MultiplyDivide(args[0],args[1],args[2]);result=W(U(p.value))|(p.success<<16);break;}
        case 7:result=W(U(FullTransform(S(args[0]),S(args[1]),S(args[2]),S(args[3]),S(args[4]))));break;
        case 8:Isotropic(s,e.DeviceAspect());break;
    }
    out[0]=result;Save(s,out+1);
}
extern "C" void dc_map_ex(U operation,const U *initial,const U *device,const U *args,U *out)
{
    State s=Load(initial);Environment e{device};
    const auto result=ChangeEx(e,s,Change(operation),args);
    out[0]=result.result;out[1]=result.written;out[2]=Pack(result.previous);
    Save(s,out+3);
}
extern "C" void dc_map_points(const U *initial,U count,U inverse,unsigned char *bytes)
{
    struct Memory
    {
        unsigned char *p;
        U Read16(U at) { return p[at]|(U(p[at+1])<<8); }
        U Read32(U at) { return Read16(at)|(Read16(at+2)<<16); }
        void Write16(U at,U v) { p[at]=v;p[at+1]=v>>8; }
        void Write32(U at,U v) { Write16(at,v);Write16(at+2,v>>16); }
        U ReadPacked(U at) { return Read16(at); }
        void WritePacked(U at,U v) { Write16(at,v); }
        void BeginPacked() {}
        void EndPacked() {}
    } memory{bytes};
    ConvertPoints(memory,Load(initial),count,inverse!=0);
}
#ifdef DC_MAP_STANDALONE
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    FILE *file=std::fopen(argv[1],"rb");if(!file)return 3;
    U input[61],expected[20],actual[20],count=0;
    while(std::fread(input,sizeof input,1,file)==1)
    {
        if(std::fread(expected,sizeof expected,1,file)!=1)return 4;
        dc_map_call(input[0],input+1,input+20,input+53,actual);
        for(U i=0;i<20;++i)if(actual[i]!=expected[i])
        {std::fprintf(stderr,"DC map sanitizer case %u word %u\n",count,i);return 5;}
        ++count;
    }
    std::fclose(file);std::printf("DC MAP SANITIZER PASS %u cases\n",count);
}
#endif
#ifdef DC_MAP_PUBLIC_STANDALONE
int main(int argc,char **argv)
{
    if(argc!=2)return 2;
    FILE *file=std::fopen(argv[1],"rb");if(!file)return 3;
    U kind,count=0;
    while(std::fread(&kind,4,1,file)==1)
    {
        if(kind==0)
        {
            U input[57],expected[22],actual[22];
            if(std::fread(input,sizeof input,1,file)!=1 || std::fread(expected,sizeof expected,1,file)!=1)return 4;
            dc_map_ex(input[0],input+1,input+20,input+53,actual);
            if(std::memcmp(actual,expected,sizeof actual))return 5;
        }
        else
        {
            U input[22];if(std::fread(input,sizeof input,1,file)!=1)return 6;
            std::vector<unsigned char> bytes(input[2]),expected(input[2]);
            if(std::fread(bytes.data(),bytes.size(),1,file)!=1 || std::fread(expected.data(),expected.size(),1,file)!=1)return 7;
            dc_map_points(input+3,input[0],input[1],bytes.data());
            if(bytes!=expected)return 8;
        }
        ++count;
    }
    std::fclose(file);std::printf("DC MAP PUBLIC SANITIZER PASS %u cases\n",count);
}
#endif
