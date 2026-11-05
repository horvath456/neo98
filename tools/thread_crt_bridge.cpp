// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_thread_crt.h"
#include <cassert>
#include <cstdio>
#include <vector>
using U = unsigned;
struct Environment
{
    Win98ThreadCrt::State state{0xffffffffu, 0x780373c0, 0, 0};
    unsigned char memory[65536]{};
    U thread = 0, error[2]{0xa5a5, 0x5a5a}, tls[2]{}, exit_code[2]{0xffffffffu,0xffffffffu};
    U allocations = 0, mode = 0, os_error = 8, fatal = 0;
    std::vector<U> events;
    std::vector<unsigned char> snapshot;
    void Event(U op, U a=0, U b=0, U c=0, U d=0, U e=0, U f=0, U g=0)
    { events.insert(events.end(), {op,a,b,c,d,e,f,g}); }
    U Read(U pointer)
    {
        assert(pointer >= 0x20000000 && pointer - 0x20000000 <= sizeof memory - 4);
        const U at = pointer - 0x20000000;
        return U(memory[at]) | U(memory[at+1])<<8 | U(memory[at+2])<<16 | U(memory[at+3])<<24;
    }
    void Write(U pointer, U value)
    {
        assert(pointer >= 0x20000000 && pointer - 0x20000000 <= sizeof memory - 4);
        for (U i=0; i<4; ++i) memory[pointer - 0x20000000 + i] = value >> (i*8);
    }
    U Calloc(U count, U bytes)
    {
        assert(count == 1 && bytes == 0x74);
        const U pointer = mode == 1 ? 0 : 0x20000000 + allocations * 0x100;
        if (pointer)
        {
            assert(allocations < 128); ++allocations;
            for (U i=0; i<bytes; ++i) memory[pointer - 0x20000000 + i] = 0;
        }
        Event(1,count,bytes,pointer); return pointer;
    }
    void Free(U pointer)
    {
        Event(2,pointer);
        if (!pointer) return;
        assert(pointer >= 0x20000000 && pointer - 0x20000000 < allocations * 0x100 && !(pointer & 255));
        for (U i=0; i<0x74; ++i) memory[pointer - 0x20000000 + i] = 0xdd;
    }
    void SetLastError(U value) { error[thread]=value; Event(3,value); }
    U GetLastError() { Event(4,error[thread]); return error[thread]; }
    U TlsAlloc() { const U slot=mode==4 ? 0xffffffffu : 0; Event(5,slot); if(mode==4)error[thread]=259; return slot; }
    void TlsFree(U slot) { Event(8,slot); tls[0]=tls[1]=0; }
    U TlsGet(U slot)
    {
        const U data=slot==0 ? tls[thread] : 0; error[thread]=slot==0 ? 0 : 87;
        Event(6,slot,data); return data;
    }
    bool TlsSet(U slot,U data)
    {
        const bool ok=slot==0 && mode!=3;
        if(ok)tls[thread]=data; else error[thread]=87;
        Event(7,slot,data,ok); return ok;
    }
    U CurrentThreadId() { const U id=thread ? 0x2222 : 0x1111; Event(9,id); return id; }
    void InitializeLocks() { Event(10); }
    void TerminateLocks() { Event(11); }
    U CreateCrtThread(U security,U stack,U data,U flags,U id)
    {
        const U handle=mode==2 ? 0 : 0xcafe0011;
        Event(12,security,stack,0x780025ff,data,flags,id,handle);
        if(handle && id)Write(id,0x2222);
        if(!handle)error[thread]=os_error;
        return handle;
    }
    void InvokeNoArgs(U function) { Event(13,function); error[thread]=function==0x70000300 ? 0x8877 : 0x9988; }
    bool InvokeThread(U function,U argument,U &result)
    {
        result=0x1234abcd; Event(14,function,argument,result);
        if(mode==5) { Win98ThreadCrt::EndEx(*this,state,result); return false; }
        return true;
    }
    void ExitThread(U result) { exit_code[thread]=result; Event(15,result); }
    void Fatal(U code) { fatal=code; Event(16,code); }
    U Run(const U *a)
    {
        switch(a[0])
        {
            case 0:return Win98ThreadCrt::Initialize(*this,state);
            case 1:return Win98ThreadCrt::GetData(*this,state);
            case 2:return Win98ThreadCrt::BeginEx(*this,state,a[1],a[2],a[3],a[4],a[5],a[6]);
            case 3:Win98ThreadCrt::RunEx(*this,state,a[1]);return 0;
            case 4:Win98ThreadCrt::EndEx(*this,state,a[1]);return 0;
            case 5:Win98ThreadCrt::FreeData(*this,state,a[1]);return 0;
            case 6:Win98ThreadCrt::Terminate(*this,state);return 0;
            case 7:assert(a[1]<2);thread=a[1];return 0;
            case 8:Write(a[1],a[2]);return 0;
            case 9:mode=a[1];os_error=a[2];return 0;
            case 10:state.fp_init=a[1];state.fp_term=a[2];return 0;
            case 11:Win98ThreadCrt::MapError(*this,state,a[1]);return 0;
            case 12:error[thread]=a[1];return 0;
            case 13:return Calloc(1,0x74);
            default:assert(false);return 0;
        }
    }
    void Word(U value) { for(U i=0;i<4;++i)snapshot.push_back(value>>(i*8)); }
    void Snapshot()
    {
        snapshot.clear();
        for(U v:{state.tls_index,state.action_table,state.fp_init,state.fp_term,thread,error[0],error[1],tls[0],tls[1],
                 exit_code[0],exit_code[1],allocations,fatal})Word(v);
        Word(events.size());for(U value:events)Word(value);
        snapshot.insert(snapshot.end(),memory,memory+allocations*0x100);Word(Read(0x2000ff00));
    }
};
extern "C" {
void *thread_crt_new() { return new Environment; }
void thread_crt_delete(void *p) { delete static_cast<Environment*>(p); }
U thread_crt_run(void *p,const U *a) { return static_cast<Environment*>(p)->Run(a); }
const unsigned char *thread_crt_snapshot(void *p,U *bytes)
{ auto &e=*static_cast<Environment*>(p);e.Snapshot();*bytes=e.snapshot.size();return e.snapshot.data(); }
}
#ifdef THREAD_CRT_STANDALONE
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=std::fopen(argv[1],"rb");assert(f);U count;
    assert(std::fread(&count,4,1,f)==1);
    for(U i=0;i<count;++i)
    {
        Environment e;U steps;assert(std::fread(&steps,4,1,f)==1);
        for(U j=0;j<steps;++j)
        {
            U a[8],expected,bytes;assert(std::fread(a,4,8,f)==8);
            assert(std::fread(&expected,4,1,f)==1 && std::fread(&bytes,4,1,f)==1);
            std::vector<unsigned char> original(bytes);assert(std::fread(original.data(),1,bytes,f)==bytes);
            assert(e.Run(a)==expected);e.Snapshot();assert(e.snapshot==original);
        }
    }
    assert(std::fgetc(f)==EOF);std::fclose(f);
    std::printf("THREAD CRT ASAN/UBSAN PASS: %u sequences\n",count);
}
#endif
