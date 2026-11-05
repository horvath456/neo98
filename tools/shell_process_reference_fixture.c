// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
API void S ExitProcess(U);API U S CreateEventA(void*,U,U,const char*);API U S SetEvent(U);API U S WaitForSingleObject(U,U);API U S CloseHandle(U);API void S Sleep(U);
API void S ordinal_176(void*);API U S SHGetInstanceExplorer(void**);API U S SHCreateThread(U(S*)(void*),void*,U,U(S*)(void*));
struct Obj{const void**table;volatile U refs,adds,releases;};
static struct Obj first,second;static void*published;static volatile U mode,failure;static U entered,unblock,finished;
static U S query(void*p,void*i,void**o){(void)p;(void)i;*o=0;return 0x80004002u;}
static U S add(struct Obj*p){++p->adds;++p->refs;if(mode){if(published!=p)failure=28400;ordinal_176(mode==1?0:&second);}return p->refs;}
static U S release(struct Obj*p){++p->releases;return --p->refs;}
static const void*vt[]={query,add,release};
static U S worker(void*p){void*o=0;if(p!=&first||SHGetInstanceExplorer(&o)||o!=&first)failure=28401;else release(o);SetEvent(entered);WaitForSingleObject(unblock,~0u);SetEvent(finished);return 7;}
U run_shell_process_reference_test(void){void*o=(void*)1;
 first.table=second.table=vt;first.refs=second.refs=1;first.adds=first.releases=second.adds=second.releases=0;mode=failure=0;
 if(SHGetInstanceExplorer(&o)!=0x80004005u||o)return 28410;
 ordinal_176(&first);if(first.adds||first.releases)return 28411;
 mode=2;published=0;if(SHGetInstanceExplorer(&published)||published!=&first||first.refs!=2||failure)return failure?failure:28412;mode=0;release(published);
 o=0;if(SHGetInstanceExplorer(&o)||o!=&second)return 28413;release(o);
 ordinal_176(&first);mode=1;published=0;if(SHGetInstanceExplorer(&published)||published!=&first||failure)return failure?failure:28414;mode=0;release(published);
 o=(void*)1;if(SHGetInstanceExplorer(&o)!=0x80004005u||o)return 28415;
 if(first.refs!=1||second.refs!=1)return 28416;
 entered=CreateEventA(0,0,0,0);unblock=CreateEventA(0,1,0,0);finished=CreateEventA(0,0,0,0);if(!entered||!unblock||!finished)return 28417;
 ordinal_176(&first);if(!SHCreateThread(worker,&first,4,0)||WaitForSingleObject(entered,5000))return 28418;
 if(first.refs!=2||failure)return failure?failure:28419;
 ordinal_176(&second);if(first.refs!=2||second.refs!=1)return 28420;
 SetEvent(unblock);if(WaitForSingleObject(finished,5000))return 28421;
 for(U i=0;i<100&&first.refs!=1;++i)Sleep(1);
 if(first.refs!=1||first.adds!=first.releases)return 28422;
 CloseHandle(entered);CloseHandle(unblock);CloseHandle(finished);
 // Leave a borrowed object set; a new Application must start empty after teardown.
 return failure;
}
#ifdef SHELL_PROCESS_REFERENCE_STANDALONE
void mainCRTStartup(void){ExitProcess(run_shell_process_reference_test());}
#endif
