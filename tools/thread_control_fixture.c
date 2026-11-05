/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateThread(void *, U, U(__stdcall *)(void *), void *, U, U *);
API U GetCurrentThread(void);
API U GetCurrentProcess(void);
API U SuspendThread(U);
API U ResumeThread(U);
API int GetThreadPriority(U);
API U SetThreadPriority(U,int);
API U GetPriorityClass(U);
API U SetPriorityClass(U,U);
API U SetThreadAffinityMask(U,U);
API U GetExitCodeThread(U,U *);
API U GetThreadTimes(U,void *,void *,void *,void *);
API U GetThreadPriorityBoost(U,U *);
API U SetThreadPriorityBoost(U,U);
API U SetThreadIdealProcessor(U,U);
API U SwitchToThread(void);
API U CreateEventA(void *,U,U,const char *);
API U SetEvent(U);
API U WaitForSingleObject(U,U);
API U CloseHandle(U);
API U DuplicateHandle(U,U,U,U *,U,U,U);
API U InterlockedIncrement(volatile U *);
API void Sleep(U);
API void SetLastError(U);
API U GetLastError(void);
API void ExitProcess(U);
typedef U (__cdecl *ONEXIT)(void);
__declspec(dllimport) ONEXIT __cdecl _onexit(ONEXIT);
__declspec(dllimport) void __cdecl _cexit(void);
static void check(int okay,U code){if(!okay)ExitProcess(code);}
static volatile U stage,sequence,order[3],self_return,self_error;
static U gate;
static U __stdcall worker(void *p){(void)p;stage=1;check(WaitForSingleObject(gate,5000)==0,6800);stage=2;return 98;}
static U __stdcall self_worker(void *p)
{
    (void)p;SetLastError(0x7654);stage=1;
    self_return=SuspendThread(GetCurrentThread());self_error=GetLastError();stage=2;return 99;
}
static U __stdcall ordered_worker(void *p)
{
    U index=InterlockedIncrement(&sequence)-1;check(index<3,6801);order[index]=(U)p;return (U)p;
}
static void until_stage(U value)
{
    for(U i=0;i<5000 && stage!=value;++i)Sleep(1);
    check(stage==value,6802);
}
/* The thread Sleep(0) has to let run. It touches no API at all, so nothing but
   the scheduler can move its counter. */
static volatile U yield_counter,yield_stop;
static U __stdcall yield_worker(void *p){(void)p;while(!yield_stop)++yield_counter;return 7;}
static U exit_worker_handle,exit_gate;
static U __stdcall exit_worker(void *p)
{
    (void)p;check(WaitForSingleObject(exit_gate,5000)==0,6863);
    check(SetThreadPriority(GetCurrentThread(),0) && GetThreadPriority(GetCurrentThread())==0,6864);
    return 17;
}
static U __cdecl exit_callback(void)
{
    /* The original CRT may revisit this callback during later process cleanup. */
    if (!exit_gate) return 0;
    check(SetEvent(exit_gate),6865);
    check(WaitForSingleObject(exit_worker_handle,2000)==0,6866);
    return 0;
}
void run_thread_control_test(void)
{
    U self=GetCurrentThread(),process=GetCurrentProcess(),code=0,tid=0;
    check(GetThreadPriority(self)==0 && GetPriorityClass(process)==0x20,6803);
    const int priorities[]={-15,-2,-1,0,1,2,15};
    for(U i=0;i<7;++i)
    {
        SetLastError(0x9898);
        check(SetThreadPriority(self,priorities[i]) && GetThreadPriority(self)==priorities[i] && GetLastError()==0x9898,6804+i);
    }
    SetLastError(0);check(!SetThreadPriority(self,-3) && GetLastError()==1800 && GetThreadPriority(self)==15,6811);
    check(SetThreadPriority(self,0),6812);
    const U classes[]={0x40,0x80,0x100,0x20};
    for(U i=0;i<4;++i)check(SetPriorityClass(process,classes[i]) && GetPriorityClass(process)==classes[i],6813+i);
    SetLastError(0);check(!SetPriorityClass(process,0) && GetLastError()==1800 && GetPriorityClass(process)==0x20,6817);
    SetLastError(0x1234);check(ResumeThread(self)==0xffffffffu && GetLastError()==0x1234,6818);
    check(SetThreadAffinityMask(self,1)==1,6819);
    SetLastError(0);check(!SetThreadAffinityMask(self,2) && GetLastError()==13,6820);
    SetLastError(0);check(!SetThreadAffinityMask(0,1) && GetLastError()==6,6821);
    SetLastError(0);check(GetThreadPriority(0)==0x7fffffff && GetLastError()==6,6822);
    SetLastError(0);check(SuspendThread(0)==0xffffffffu && GetLastError()==6,6823);
    SetLastError(0);check(!GetExitCodeThread(self,0) && GetLastError()==87,6824);
    check(GetExitCodeThread(self,&code) && code==259,6825);
    /* These are original Win98 Error120 exports; no fabricated CPU accounting. */
    U untouched[8];for(U i=0;i<8;++i)untouched[i]=0x98989898;
    SetLastError(0);check(!GetThreadTimes(self,untouched,untouched+2,untouched+4,untouched+6) && GetLastError()==120,6826);
    for(U i=0;i<8;++i)check(untouched[i]==0x98989898,6827);
    SetLastError(0);check(!GetThreadPriorityBoost(self,untouched) && GetLastError()==120 && untouched[0]==0x98989898,6828);
    SetLastError(0);check(!SetThreadPriorityBoost(self,1) && GetLastError()==120,6829);
    SetLastError(0);check(!SetThreadIdealProcessor(self,0) && GetLastError()==120,6830);
    SetLastError(0);check(!SwitchToThread() && GetLastError()==120,6831);
    gate=CreateEventA(0,1,0,0);check(gate!=0,6832);stage=0;
    U h=CreateThread(0,0,worker,0,4,&tid);check(h && tid,6833);
    for(U n=1;n<127;++n)check(SuspendThread(h)==n,6834);
    SetLastError(0);check(SuspendThread(h)==0xffffffffu && GetLastError()==259 && !stage,6835);
    for(U n=127;n>0;--n)check(ResumeThread(h)==n,6836);
    SetLastError(0x1234);check(ResumeThread(h)==0xffffffffu && GetLastError()==0x1234,6837);
    until_stage(1);
    check(SuspendThread(h)==0 && SuspendThread(h)==1 && GetExitCodeThread(h,&code) && code==259,6838);
    check(SetEvent(gate),6839);Sleep(10);check(stage==1,6840);
    check(ResumeThread(h)==2,6841);Sleep(10);check(stage==1,6842);
    check(ResumeThread(h)==1 && WaitForSingleObject(h,5000)==0 && stage==2,6843);
    check(GetExitCodeThread(h,&code) && code==98 && GetThreadPriority(h)==0,6844);
    SetLastError(0);check(SuspendThread(h)==0xffffffffu && GetLastError()==259,6845);
    SetLastError(0x1234);check(ResumeThread(h)==0xffffffffu && GetLastError()==0x1234,6846);
    check(CloseHandle(h) && CloseHandle(gate),6847);
    stage=0;h=CreateThread(0,0,self_worker,0,0,&tid);check(h!=0,6848);until_stage(1);
    check(GetExitCodeThread(h,&code) && code==259 && ResumeThread(h)==1,6849);
    check(WaitForSingleObject(h,5000)==0 && stage==2 && self_return==0 && self_error==0x7654,6850);
    check(CloseHandle(h),6851);
    /* A suspended start must not leave a stale wake bit for later self-suspension. */
    for (U nested = 0; nested < 2; ++nested)
    {
        stage = 0; self_return = self_error = 0xffffffffu;
        h = CreateThread(0,0,self_worker,0,4,&tid);check(h!=0,6872);
        if (nested)
        {
            check(SuspendThread(h)==1 && ResumeThread(h)==2,6873);
            check(WaitForSingleObject(h,10)==258 && stage==0,6874);
        }
        check(ResumeThread(h)==1,6875);
        /* Waiting on the thread gives it time to execute its self-suspend call.
           A stale wake bit completes the worker and returns zero here. */
        check(WaitForSingleObject(h,30)==258 && stage==1,6876);
        check(GetExitCodeThread(h,&code) && code==259 && ResumeThread(h)==1,6877);
        check(WaitForSingleObject(h,5000)==0 && stage==2 && self_return==0 && self_error==0x7654,6878);
        check(CloseHandle(h),6879);
    }
    /* The high-priority worker must actually execute first. */
    U handles[3];sequence=0;check(SetThreadPriority(self,15),6852);
    for(U i=0;i<3;++i)
    {
        handles[i]=CreateThread(0,0,ordered_worker,(void *)(i+1),4,&tid);check(handles[i]!=0,6853);
        check(SetThreadPriority(handles[i],i==0?-1:(int)i),6854);
        check(ResumeThread(handles[i])==1,6855);
    }
    check(SetPriorityClass(process,0x80) && SetPriorityClass(process,0x100) &&
          SetPriorityClass(process,0x20),6861);
    check(GetThreadPriority(handles[0])==-1 && GetThreadPriority(handles[2])==2,6862);
    check(!sequence && SetThreadPriority(self,-15),6856);
    check(sequence==3 && order[0]==3 && order[1]==2 && order[2]==1,6857);
    check(SetThreadPriority(self,0),6858);
    for(U i=0;i<3;++i)check(WaitForSingleObject(handles[i],5000)==0 && GetExitCodeThread(handles[i],&code) && code==i+1 && CloseHandle(handles[i]),6859);
    check(GetPriorityClass(process)==0x20 && GetThreadPriority(self)==0,6860);
    /* Sleep(0) gives the rest of the time slice to another thread that is ready
       to run and has the same priority. The worker of this check runs a loop
       without a single API call, so the only thing that can move its counter is
       the scheduler; and between the Sleep(0) and the reading below stand a
       handful of instructions, far too few for a slice of its own to end. A
       Sleep(0) that does not hand over leaves the counter where it was, and a
       program that polls a flag another thread sets would never leave its
       loop. */
    yield_stop=0;yield_counter=0;
    U yielder=CreateThread(0,0,yield_worker,0,0,&tid);check(yielder!=0,6892);
    check(SetThreadPriority(yielder,0) && GetThreadPriority(self)==0,6893);
    /* The worker has to reach its loop once; Sleep(1) is a wait, not the
       handover this check is about. */
    for(U i=0;i<2000 && !yield_counter;++i)Sleep(1);
    check(yield_counter!=0,6894);
    {
        U before;
        yield_counter=0;
        Sleep(0);
        before=yield_counter;
        check(before!=0,6895);
        /* SwitchToThread is unimplemented in Windows 98. It sets
           ERROR_CALL_NOT_IMPLEMENTED and returns zero without removing any
           arguments. A caller must receive that result rather than yield. */
        SetLastError(0);
        check(!SwitchToThread() && GetLastError()==120,6896);
    }
    yield_stop=1;
    check(WaitForSingleObject(yielder,5000)==0 && GetExitCodeThread(yielder,&code) && code==7,6898);
    check(CloseHandle(yielder),6899);
    /* The CRT holds its exit lock while this callback waits for kernel controls. */
    exit_gate=CreateEventA(0,1,0,0);check(exit_gate!=0,6867);
    exit_worker_handle=CreateThread(0,0,exit_worker,0,0,&tid);check(exit_worker_handle!=0,6868);
    check(_onexit(exit_callback)==exit_callback,6869);_cexit();
    check(GetExitCodeThread(exit_worker_handle,&code) && code==17,6870);
    check(CloseHandle(exit_worker_handle) && CloseHandle(exit_gate),6871);
    exit_gate = 0;
    /* The pseudo-handles of KERNEL32's resolver.
       GetCurrentProcess answers, GetCurrentThread
       FFFFFFFEh, and the resolver knows both before it looks at the
       handle table. WS2HELP duplicates the running thread that way. */
    check(self==0xfffffffeu && process==0x7fffffffu,6880);
    U duplicate=0;
    check(DuplicateHandle(process,self,process,&duplicate,0,0,2),6881);
    check(duplicate!=0 && duplicate!=self,6882);
    /* The duplicate names this very thread, so a priority set through it is
       the priority the pseudo-handle reports. */
    check(GetThreadPriority(duplicate)==0,6883);
    check(SetThreadPriority(duplicate,2) && GetThreadPriority(self)==2,6884);
    check(SetThreadPriority(self,0) && GetThreadPriority(duplicate)==0,6885);
    /* A running thread is not signalled. */
    check(WaitForSingleObject(duplicate,0)==0x102,6886);
    check(CloseHandle(duplicate),6887);
    /* Closing a pseudo-handle answers TRUE and changes
       nothing, so the thread still answers afterwards. */
    check(CloseHandle(self) && CloseHandle(process),6888);
    check(GetThreadPriority(self)==0,6889);
    /* FFFFFFFFh is no pseudo-handle: Windows 98 keeps it as a table index and
       the bounds test turns it into ERROR_INVALID_HANDLE. */
    SetLastError(0);
    check(!DuplicateHandle(process,0xffffffffu,process,&duplicate,0,0,2),6890);
    check(GetLastError()==6,6891);
}
#ifdef THREAD_CONTROL_STANDALONE
void mainCRTStartup(void){run_thread_control_test();ExitProcess(0);}
#endif
