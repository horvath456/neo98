// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
API void S ExitProcess(U);API U S GetCurrentThreadId(void);API U S CreateEventA(void*,U,U,const char*);API U S SetEvent(U);API U S WaitForSingleObject(U,U);API U S CloseHandle(U);API U S Sleep(U);
API U S ordinal_16(U(S*)(void*),void*,U,U(S*)(void*));
API U S SHCreateThread(U(S*)(void*),void*,U,U(S*)(void*));
API U S SHGetThreadRef(void**);API U S SHSetThreadRef(void*);
static volatile U parent,prep_id,worker_id,phase,references,adds,releases,fail,entered,verify_output;static U unblock,finished;
static void *observed;
static U S query(void*p,void*i,void**o){(void)p;(void)i;*o=0;return 0x80004002u;}
static U S add(void*p){if(verify_output&&observed!=p)fail=27200; ++adds;return ++references;}
static U S release(void*p){(void)p;++releases;return --references;}
static const void*vt[]={query,add,release};static struct Obj{const void**table;} object={vt};
static U S prepare(void*p){if(p!=(void*)99||phase)fail=27201;prep_id=GetCurrentThreadId();if(prep_id==parent)fail=27202;phase=1;return 0xdeadbeef;}
static U S work(void*p){void*ref=(void*)1;if(p!=(void*)99||phase!=1)fail=27203;worker_id=GetCurrentThreadId();if(worker_id!=prep_id)fail=27204;
 if(SHGetThreadRef(&ref)!=0x80004002u||ref)fail=27205; // acquired ref is retained, not installed into child TLS
 phase=2;SetEvent(entered);WaitForSingleObject(unblock,~0u);phase=3;SetEvent(finished);return 0x76543210;}
static U S plain(void*p){if(p!=(void*)101||GetCurrentThreadId()==parent)fail=27206;SetEvent(finished);return 5;}
static U S exit_worker(void*p){(void)p;SetEvent(finished);return 9;}
U run_shell_threads_test(void){void*ref=(void*)1;parent=GetCurrentThreadId();references=1;
 if(SHGetThreadRef(&ref)!=0x80004002u||ref)return 27210;
 if(SHSetThreadRef(&object)||adds||releases)return 27211;
 observed=0;verify_output=1;if(SHGetThreadRef(&observed)||observed!=&object||references!=2||adds!=1)return 27212;verify_output=0;release(observed);
 unblock=CreateEventA(0,1,0,0);finished=CreateEventA(0,0,0,0);entered=CreateEventA(0,0,0,0);if(!unblock||!finished||!entered)return 27213;
 if(!ordinal_16(work,(void*)99,2,prepare))return 27214;
 // Return means preparation completed. It must not wait for the blocked body.
 if(!phase||phase>2||prep_id==parent||references!=2||adds!=2)return 27215;
 if(WaitForSingleObject(entered,5000)||phase!=2)return 27216;
 if(SHSetThreadRef(0)||references!=2||releases!=1)return 27217;
 SetEvent(unblock);if(WaitForSingleObject(finished,5000))return 27218;
 for(U i=0;i<100&&references!=1;++i)Sleep(1);
 if(fail||phase!=3||references!=1||releases!=2)return fail?fail:27219;
 if(!SHCreateThread(plain,(void*)101,0,0)||WaitForSingleObject(finished,5000))return 27220;
 // Unknown high flag has no branch in the original low-byte decisions.
 if(!SHCreateThread(exit_worker,0,0x100,0)||WaitForSingleObject(finished,5000))return 27221;
 CloseHandle(unblock);CloseHandle(finished);CloseHandle(entered);return fail;
}
#ifdef SHELL_THREADS_STANDALONE
void mainCRTStartup(void){ExitProcess(run_shell_threads_test());}
#endif
