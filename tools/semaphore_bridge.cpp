/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_semaphore.h"
#include <cstring>
using Win98Semaphore::U;
namespace {
struct Env {
    const U *c; U *out, error, n=0; unsigned char memory[4096]{};
    Env(const U *config,U *output):c(config),out(output),error(c[14]) {
        WriteRaw(0x200010,c[5]);WriteRaw(0x200018,c[6]);memory[0x40]=c[7];
        WriteRaw(0x200108,c[16]);WriteRaw(0x200110,c[17]);WriteRaw(0x200200,0x12345678);
    }
    void Event(U id,U a=0,U b=0,U d=0){out[8+n++]=id;out[8+n++]=a;out[8+n++]=b;out[8+n++]=d;}
    U Read8(U p){return memory[p-0x200000];}
    U Read32(U p){U v;std::memcpy(&v,memory+p-0x200000,4);return v;}
    void WriteRaw(U p,U v){std::memcpy(memory+p-0x200000,&v,4);}
    void Error(U v){Event(1,v);error=v;}
    U LastError(){Event(2);return error;}
    void LockNamespace(){Event(3);}
    void UnlockNamespace(){Event(4);}
    U OpenNamed(U name,U type,U flags){Event(5,name,type,flags);if(!c[8])error=c[15];return c[8];}
    U AllocateObject(U size,U type){Event(6,size,type);if(!c[9])error=c[15];return c[9];}
    void InitializeObject(U p,U initial,U maximum){WriteRaw(p+4,0);WriteRaw(p+8,initial);WriteRaw(p+16,maximum);}
    U CreateHandle(U obj,U flags){Event(7,obj,flags);if(c[10]==~0u)error=c[15];return c[10];}
    void DestroyObject(U obj){Event(8,obj);error=99;}
    U BindName(U obj,U name){Event(9,obj,name);WriteRaw(obj+12,c[11]);if(!c[11])error=c[15];return c[11];}
    void CloseHandle(U h){Event(10,h);error=99;}
    void EnterCritical(){Event(11);}
    void LeaveCritical(){Event(12);}
    U ResolveHandle(U h,U flags,U zero){Event(13,h,flags,zero);if(!c[12])error=c[15];return c[12];}
    U Signal(U obj,U amount,U previous){Event(14,obj,amount,previous);if(!c[13])error=c[15];return c[13];}
    void ReleaseReference(U obj){Event(15,obj);}
    U EnterCount(){Event(16);return 0x42;}
    void LeaveCount(U cookie){Event(17,cookie&255);}
    void Write32(U p,U value){Event(18,p,value);WriteRaw(p,value);}
    void WakeWaiters(Win98Semaphore::State &s){Event(19,s.count);U count=c[18];while(count--&&Win98Semaphore::Acquire(s)){} }
};
}
extern "C" U semaphore(const U *config,U *output)
{
    Env e(config,output);const U *a=config+1;U result=0;
    Win98Semaphore::State state{config[16],config[17]};
    switch(config[0]) {
    case 0:result=Win98Semaphore::Create(e,a[0],a[1],a[2],a[3]);break;
    case 1:result=Win98Semaphore::Open(e,a[0],a[1],a[2]);break;
    case 2:result=Win98Semaphore::Release(e,a[0],a[1],a[2]);break;
    case 3:result=Win98Semaphore::Construct(e,a[0],a[1]);break;
    case 4:result=Win98Semaphore::ReleaseCount(e,state,a[1],a[2]);break;
    case 5:result=Win98Semaphore::CanAcquire(state);break;
    case 6:result=Win98Semaphore::Acquire(state);break;
    }
    if(config[0]>=4)e.WriteRaw(0x200108,state.count);
    output[0]=e.error;output[1]=e.Read32(0x200104);output[2]=e.Read32(0x200108);
    output[3]=e.Read32(0x20010c);output[4]=e.Read32(0x200110);output[5]=e.Read32(0x200200);
    output[6]=e.n;output[7]=0;return result;
}
