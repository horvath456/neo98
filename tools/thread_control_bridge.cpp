// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_thread_control.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using U=unsigned;
namespace W=Win98ThreadControl;
constexpr U THREAD=0x30000400,PROCESS=0x30000100,RUNTIME=0x30003000,BUFFER=0x30006000,OUTPUT=0x30001800;
struct Environment
{
    const U *a; U *out,events=0,error,process_base,iterator=0,deferred,waits=0;
    U flags[3]{},counts[3]{},relative[3]{},exit_code[3]{};
    W::SuspendState suspend[3]; W::PriorityState priority[3];
    Environment(const U *input,U *output):a(input),out(output),error(a[12]),process_base(a[4]),deferred(a[21])
    {
        for(U i=0;i<3;++i)
        {
            flags[i]=a[1];counts[i]=a[2];relative[i]=i?a[23+i]:a[3];exit_code[i]=a[13];
            suspend[i]={a[5],a[7],a[6]};priority[i]={a[15],a[16],a[17],a[18],a[19],a[20]};
        }
    }
    U index(U t){assert(t>=THREAD && (t-THREAD)%0x400==0 && (t-THREAD)/0x400<3);return (t-THREAD)/0x400;}
    void event(U kind,U a=0,U b=0,U c=0,U d=0,U e=0)
    {
        assert(events+6<900);const U values[]={kind,a,b,c,d,e};
        std::memcpy(out+100+events,values,sizeof values);events+=6;
    }
    void EnterThreads(){event(1);} void LeaveThreads(){event(2);}
    void EnterGlobal(){event(10);} void LeaveGlobal(){event(11);}
    U Lookup(U handle,U type)
    {
        event(3,handle,type);if(!a[9]){Error(6);return 0;}
        return (type&0x40)?THREAD:PROCESS;
    }
    void Error(U code){error=code;event(4,code);}
    U ThreadFlags(U t){return flags[index(t)];}
    U RuntimeThread(U t){return a[8]?RUNTIME+index(t)*0x400:0;}
    U SuspendCount(U t){return counts[index(t)];}
    void SetSuspendCount(U t,U count){counts[index(t)]=count;}
    U SuspendRuntime(U t){event(19,0x1a,RuntimeThread(t));return W::SuspendVm(*this,t,suspend[index(t)]);}
    U ResumeRuntime(U t){event(19,0x1b,RuntimeThread(t));return W::ResumeVm(*this,t,suspend[index(t)]);}
    U ScheduleSuspend(U t){event(5,t,a[11]);return a[11];}
    void CancelSuspend(U t,U event_id){event(6,t,event_id);}
    void WakeSuspend(U t){event(7,t);}
    bool WaitSuspend(U t)
    {
        event(20,t);++waits;
        if(waits>a[22]){suspend[index(t)].count=0;suspend[index(t)].flags|=8;}
        return true;
    }
    U RelativePriority(U t){return relative[index(t)];}
    void SetRelativePriority(U t,U value){relative[index(t)]=value;}
    U ThreadProcess(U){return PROCESS;}
    U ProcessBase(U){return process_base;}
    void SetProcessBase(U,U base){process_base=base;}
    void RequeuePriority(U t,U value){event(8,t,value);}
    void PropagatePriority(U,U){} // Empty original priority-inheritance list.
    void ReschedulePriority(){if(!deferred)event(9);}
    void DeferPriority(bool enter){if(enter)++deferred;else --deferred;}
    void PublishPriority(U t,U value){event(19,0x21,RuntimeThread(t),value);W::SetBasePriority(*this,t,priority[index(t)],value);}
    U FirstThread(U){iterator=0;event(14);return a[23]?1:0;}
    U NextThread(U){event(15);return ++iterator<a[23]?iterator+1:0;}
    U IteratorThread(U i){return THREAD+(i-1)*0x400;}
    U ProcessThreadCount(U){return a[23];}
    U AllocatePriorityPairs(U bytes){event(16,bytes);return a[14]?0:BUFFER;}
    U pairs[6]{};
    void PriorityPair(U,U i,U thread,U value){pairs[i*2]=thread;pairs[i*2+1]=value;}
    void PublishPriorityPairs(U buffer,U count)
    {
        event(19,0x64,buffer,count);
        for(U i=0;i<count;++i)W::SetBasePriority(*this,pairs[i*2],priority[index(pairs[i*2])],pairs[i*2+1],i+1<count);
    }
    void FreePriorityPairs(U p){event(18,p);}
    U ThreadExitCode(U t){return exit_code[index(t)];}
    void WriteExitCode(U p,U value){out[3]=value;}
    void ReleaseThreadReference(U t){event(12,t);}
};
extern "C" void thread_control_run(const U *a,U *out)
{
    for(U i=0;i<1024;++i)out[i]=0;
    out[3]=0xcccccccc;Environment e(a,out);U result=0;
    switch(a[0])
    {
        case 0:result=W::Suspend(e,0x1234);break;
        case 1:result=W::Resume(e,0x1234);break;
        case 2:result=W::GetPriority(e,0x1234);break;
        case 3:result=W::SetPriority(e,0x1234,a[10]);break;
        case 4:result=W::GetProcessPriority(e,0x1234);break;
        case 5:result=W::SetProcessPriority(e,0x1234,a[10]);break;
        case 6:result=W::ExitCode(e,0x1234,OUTPUT);break;
        case 7:result=W::Affinity(e,0x1234,a[10]);break;
        case 8:result=W::BasePriority(a[3],a[4]);break;
        case 9:result=W::SuspendVm(e,THREAD,e.suspend[0]);break;
        case 10:result=W::ResumeVm(e,THREAD,e.suspend[0]);break;
        case 11:result=W::CancelSuspend(e,THREAD,e.suspend[0]);break;
        case 12:W::SuspendCallback(e,THREAD,e.suspend[0]);break;
        case 13:result=W::SetBasePriority(e,THREAD,e.priority[0],a[10]);break;
        case 14:result=W::RecomputePriority(e,THREAD,e.priority[0]);break;
        default:assert(false);
    }
    out[0]=result;out[1]=e.error;out[2]=e.process_base;
    for(U i=0;i<3;++i)
    {
        U *o=out+4+i*16;
        o[0]=e.flags[i];o[1]=e.counts[i];o[2]=e.relative[i];o[3]=e.suspend[i].count;
        o[4]=e.suspend[i].event;o[5]=e.suspend[i].flags;o[6]=e.priority[i].base;
        o[7]=e.priority[i].effective;o[8]=e.priority[i].inherited;o[9]=e.priority[i].boost;
        o[10]=e.priority[i].decay;o[11]=e.priority[i].adjustment;
    }
    out[52]=e.waits;out[53]=e.deferred;out[99]=e.events;
}
#ifdef THREAD_CONTROL_STANDALONE
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);U count;assert(fread(&count,4,1,f)==1);
    for(U i=0;i<count;++i)
    {
        U a[32],expected[1024],out[1024];assert(fread(a,4,32,f)==32 && fread(expected,4,1024,f)==1024);
        thread_control_run(a,out);if(memcmp(out,expected,sizeof out)){fprintf(stderr,"Thread control mismatch %u op%u\n",i,a[0]);return 1;}
    }
    assert(fgetc(f)==EOF);fclose(f);printf("THREAD CONTROL ASAN/UBSAN PASS %u\n",count);
}
#endif
