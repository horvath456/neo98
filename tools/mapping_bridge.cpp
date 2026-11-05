// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_mapping.h"
#include <cstring>
#include <vector>
using namespace Win98Mapping;
namespace {
struct Env {
    const U *c; U *out; State state; Object object, dummy[8]; File file; View views[8];
    U error=0,n=0,refs=0,alive=0,view_count=0; std::vector<Object **> tables;
    explicit Env(const U *config,U *output):c(config),out(output),error(c[34]),refs(c[39]) {
        object.id=1;object.size=c[25];object.base=c[26];object.protection=c[27];object.pager=c[28];
        object.index=c[29];object.name=c[30];object.backing_info=c[31];object.file=c[32];object.file_owner=c[33];
        alive=c[0]>=2&&c[0]<=5;
        file={c[11],c[12],c[13]};
        state.capacity=c[17];
        if(state.capacity){state.slots=NewTable(state.capacity);for(U i=0;i<state.capacity;++i){dummy[i].id=i+2;if(c[18]&(1u<<i))state.slots[i]=dummy+i;}}
        if(alive&&state.capacity>object.index)state.slots[object.index]=&object;
        for(U i=0;i<c[37];++i){auto &v=views[view_count++];v.address=object.base+(i?0x10000u:0);v.object=&object;v.next=state.views;state.views=&v;}
    }
    ~Env(){for(auto *p:tables)delete[]p;}
    Object **NewTable(U count){auto *p=new Object*[count]{};tables.push_back(p);return p;}
    void Log(U id,U a=0,U b=0,U d=0,U f=0,U g=0){out[64+n++]=id;out[64+n++]=a;out[64+n++]=b;out[64+n++]=d;out[64+n++]=f;out[64+n++]=g;}
    bool Fail(U id){if(c[6]&(1u<<id)){error=73;return true;}return false;}
    void Mutate(U stage){if(c[35]==stage){if(stage==1)object.pager=c[36];else object.base=c[36];}}
    U Read32(U p){return p==0x202100?c[8]:c[9];}
    void Error(U e){Log(3,e);error=e;}
    void Lock(){Log(1);}void Unlock(){Log(2);}
    File *ResolveFile(U h,U t,U a){Log(4,h,t,a);return Fail(4)?nullptr:&file;}
    U OpenNamed(U name,U type,U inherit){Log(5,name,type,inherit);if(c[7]==2||Fail(5)){error=73;return 0;}if(c[7]==1){++refs;return 0x60000002;}return~0u;}
    Object *AllocateObject(U size,U type){Log(6,size,type);if(Fail(6))return nullptr;object={};object.id=1;alive=1;return &object;}
    U BindName(Object &o,U name){Log(7,o.id,name);return Fail(7)?0:0x203700;}
    U DuplicateBackingName(U name){Log(8,name);return Fail(8)?0:0x203800;}
    void FreeBackingName(U name){Log(9,name);if(c[40])error=91;}
    U FileSize(U h){Log(10,h);return Fail(10)?~0u:c[14];}
    U TranslateFile(U &dos,U &flags){Log(11,dos);dos=c[15];flags=c[16];return Fail(11)?6:0;}
    U FileOwner(){return 0x1234;}void RetainFileOwner(U owner){Log(12,owner&65535);}
    U SetFilePointer(U h,U position){Log(13,h,position);return Fail(13)?~0u:position;}
    bool SetEndOfFile(U h){Log(14,h);return !Fail(14);}
    U PagerRead(){return 2;}U PagerWrite(){return 3;}U PagerCopy(){return 4;}
    Object **AllocateTable(U count){Log(15,count);return Fail(15)?nullptr:NewTable(count);}
    void FreeTable(Object **old){Log(16,old?1:0);}
    U CreateHandle(Object &o,U flags){Log(17,o.id,flags);if(Fail(17))return ~0u;++refs;return 0x60000001;}
    void CloseFile(Object &o,U dos){Log(18,o.id,dos);if(c[40])error=91;}
    void FreeObject(Object &o){Log(19,o.id);alive=0;if(c[40])error=91;}
    void ReleaseName(U name){Log(20,name);Mutate(4);}
    Object *ResolveMapping(U h,U type,U access){Log(21,h,type,access);return Fail(21)?nullptr:&object;}
    U Reserve(U page,U count,U flags){Log(22,page,count,flags);Mutate(1);return Fail(22)?~0u:page==0x80060000?0x80010000:page<<12;}
    U Commit(U page,U count,U pager,U key,U flags){Log(23,page,count,pager,key,flags);Mutate(2);return Fail(23)?0:1;}
    void ReleasePages(U base,U flags){Log(24,base,flags);Fail(24);}
    View *AllocateView(){Log(25);Mutate(3);if(Fail(25))return nullptr;return views+view_count++;}
    U ViewId(View &v){return static_cast<U>(&v-views)+1;}
    void FreeView(View &v){Log(26,ViewId(v));if(c[40])error=91;}
    void AddReference(Object &o){Log(27,o.id);++refs;}
    void ReleaseReference(Object &o){Log(28,o.id);if(!--refs&&c[38])Destroy(*this,state,o);}
    void UnmapPages(U page,U count){Log(29,page,count);Fail(29);}
    void CloseHandle(){Log(30);if(!--refs)Destroy(*this,state,object);}
    U Run(){U result=0;
        switch(c[0]){
        case 0:case 7:result=Create(*this,state,c[10]?123:~0u,c[1],c[2],c[3],c[4],c[5]?0x202000:0);break;
        case 1:result=Open(*this,c[20],c[9],c[5]?0x202000:0);break;
        case 2:case 3:result=Map(*this,state,0x60000001,c[20],c[21],c[22],c[23],c[0]==3?0:c[24]);break;
        case 4:result=Unmap(*this,state,c[24]);break;
        case 5:Destroy(*this,state,object);break;
        case 6:{
            result=Create(*this,state,~0u,0,4,0,0x400,0x202000);
            out[40]=result;
            const U a=Map(*this,state,result,2,0,0,0),b=Map(*this,state,result,2,0,0,0);
            out[41]=a;out[42]=b;CloseHandle();out[43]=alive;
            out[44]=Unmap(*this,state,a);out[45]=alive;
            out[46]=Unmap(*this,state,b);out[47]=alive;result=out[46];break;
        }}return result;
    }
    void Report(){
        out[0]=error;out[1]=state.capacity;out[2]=alive;out[3]=refs;
        const U fields[]={object.name,object.size,object.base,object.index,object.backing,object.backing_info,
            object.file,object.old_size,object.protection,object.pager,object.file_owner};
        std::memcpy(out+4,fields,sizeof fields);
        U at=15,count=0;for(auto *v=state.views;v;v=v->next){++count;out[at++]=ViewId(*v);out[at++]=v->address;out[at++]=v->object->id;}
        out[39]=count;for(U i=0;i<state.capacity&&i<8;++i)out[48+i]=state.slots[i]?state.slots[i]->id:0;
        out[56]=n;
    }
};
}
extern "C" U mapping(const U *config,U *output){std::memset(output,0,2048*sizeof(U));Env e(config,output);U r=e.Run();e.Report();return r;}
