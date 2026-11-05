/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Two processes share a critical section. Its creator holds it recursively and exits without releasing it; the waiting process must acquire it rather than wait forever. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void InitializeCriticalSection(void*);
API void MakeCriticalSectionGlobal(void*);
API void DeleteCriticalSection(void*);
API void EnterCriticalSection(void*);
API void LeaveCriticalSection(void*);
API void Sleep(U);
API void ExitProcess(U);
API char *GetCommandLineA(void);
static volatile U *shared=(volatile U*)0x50000000;
#define SECTION ((void*)0x50000100)
static void check(int value,U code){if(!value)ExitProcess(code);}
void mainCRTStartup(void){
 if(GetCommandLineA()[0]=='p'){
  ((volatile U*)SECTION)[0]=0xaabbcc00;
  InitializeCriticalSection(SECTION);
  shared[1]=((volatile U*)SECTION)[1];
  MakeCriticalSectionGlobal(SECTION);
  /* Release the entire recursive ownership, not just one level. */
  EnterCriticalSection(SECTION);
  EnterCriticalSection(SECTION);
  shared[0]=1;
  while(shared[0]!=2)Sleep(0);
  /* Exit while holding the section, without LeaveCriticalSection or DeleteCriticalSection. */
  ExitProcess(0);
 }else{
  while(shared[0]!=1)Sleep(0);
  shared[0]=2;
  EnterCriticalSection(SECTION);
  check(((volatile U*)SECTION)[1]==shared[1],32810);
  shared[2]=((volatile U*)SECTION)[1];
  /* Acquire it again to check the inherited recursion count. */
  EnterCriticalSection(SECTION);
  LeaveCriticalSection(SECTION);
  LeaveCriticalSection(SECTION);
  EnterCriticalSection(SECTION);
  LeaveCriticalSection(SECTION);
  DeleteCriticalSection(SECTION);
  check(((volatile U*)SECTION)[0]==0xaabbcc00&&((volatile U*)SECTION)[1]==0,32811);
  shared[0]=7;
  ExitProcess(0);
 }
}
