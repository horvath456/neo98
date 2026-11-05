// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_property_sheet.h"
#include <cstring>
#include <vector>
using U=unsigned;
static constexpr U BASE=0x20000000,HEAP=BASE+0x8000;
struct Environment{
    unsigned char* memory;U* row;std::vector<U>trace;U next=HEAP,allocations=0,creates=0;
    U Read(U p){U v;std::memcpy(&v,memory+p-BASE,4);return v;}
    U Word(U p){unsigned short v;std::memcpy(&v,memory+p-BASE,2);return v;}
    U Byte(U p){return memory[p-BASE];}
    void Write(U p,U v){std::memcpy(memory+p-BASE,&v,4);}
    void WriteWord(U p,U v){unsigned short w=v;std::memcpy(memory+p-BASE,&w,2);}
    void Copy(U d,U s,U n){std::memmove(memory+d-BASE,memory+s-BASE,n);}
    void Zero(U d,U n){std::memset(memory+d-BASE,0,n);}
    void Event(U a,U b=0,U c=0,U d=0){trace.insert(trace.end(),{a,b,c,d});}
    U Allocate(U n){U p=0;if(++allocations!=row[5]){p=next;next+=(n+15)&~15;Zero(p,n);}Event(1,n,p);return p;}
    void Free(U p){Event(2,p);}
    U CloneA(U p){Event(3,p);U n=0;while(Byte(p+n))++n;U out=Allocate(2*(n+1));if(out)for(U i=0;i<=n;++i)WriteWord(out+i*2,Byte(p+i));return out;}
    void DestroyPage(U p){Event(4,p);}
    U CreatePage(U p,U ansi,U extra){Event(5,p,ansi,extra);if(++creates==row[6])return 0;U shadow=BASE+0x6000+creates*256,page=shadow+128;Write(shadow-8,page);Write(page-8,page);Write(page+4,Read(p+4));return shadow;}
    U LocalSize(U p){Event(6,p);return p>=BASE+0x4000&&p<BASE+0x5000?p:0;}
    U Present(U p){Event(7,p,Read(p+0x1c),Read(p+8));return 0x12345678;}
    U FindDialog(U instance,U name,U language){Event(8,instance,name,language);return row[8]==1?0:BASE+0x7000;}
    U LoadResource(U instance,U id){Event(9,instance,id);return row[8]==2?0:BASE+0x7000;}
    U LockResource(U id){Event(10,id);return row[8]==3?0:BASE+0x3000;}
    void FreeResource(U id){Event(11,id);}
    U LoadIcon(U instance,U name){Event(12,instance,name);return 0x1234;}
    U WideLength(U p){U n=0;while(Word(p+n*2))++n;return n;}
    void CopyWide(U d,U s,U capacity){U n=WideLength(s);if(n>=capacity)n=capacity-1;for(U i=0;i<n;++i)WriteWord(d+i*2,Word(s+i*2));WriteWord(d+n*2,0);}
    void LoadString(U instance,U name,U out,U cap,U language){Event(13,instance,name,language);CopyWide(out,BASE+0x2800,cap);}
    U LegacyInfo(U p,U out,U language,U wanted){Event(14,p,language,wanted);return 0;}
    U TabPage(U tab,U index){Event(15,tab,index);return row[8];}
    U Notify(U target,U source,U code,U param){Event(16,target,source,code);Event(17,param);return 0x1234;}
};
extern "C" void property_sheet(U* row,unsigned char* memory,U* result){
    Environment e{memory,row};U value=0,old=0,p=BASE+0x100,out=BASE+0x1000;
    switch(row[0]){
    case 0:value=Win98PropertySheet::ConvertHeader(e,p,out);break;
    case 1:Win98PropertySheet::FreeHeader(e,p);break;
    case 2:value=Win98PropertySheet::Collect(e,p,row[7]);break;
    case 3:value=Win98PropertySheet::Ansi(e,p,out);break;
    case 4:value=Win98PropertySheet::Wide(e,p);break;
    case 5:value=Win98PropertySheet::PageInfo(e,p,out,0x409,row[4]);break;
    case 6:value=Win98PropertySheet::PageStyle(e,BASE+0x3000,old);e.Write(out,old);break;
    case 7:value=Win98PropertySheet::Notify(e,p,row[3],row[4],0xabc);break;
    }
    result[0]=value;result[1]=e.trace.size();std::memcpy(result+2,e.trace.data(),e.trace.size()*4);
}
