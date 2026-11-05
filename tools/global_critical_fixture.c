/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void InitializeCriticalSection(void*);
API void MakeCriticalSectionGlobal(void*);
API void DeleteCriticalSection(void*);
API void EnterCriticalSection(void*);
API void LeaveCriticalSection(void*);
API void Sleep(U);
API void ExitProcess(U);
API void SetLastError(U);
API U GetLastError(void);
API char *GetCommandLineA(void);
static volatile U *shared=(volatile U*)0x50000000;
static U private_section[6];
#define SECTION ((void*)0x50000100)
static void check(int value,U code){if(!value)ExitProcess(code);}
void mainCRTStartup(void){
 InitializeCriticalSection(private_section);
 shared[GetCommandLineA()[0]=='p'?8:9]=private_section[1];
 DeleteCriticalSection(private_section);
 if(GetCommandLineA()[0]=='p'){
  ((volatile U*)SECTION)[0]=0xaabbcc00;
  InitializeCriticalSection(SECTION);
  U id=((volatile U*)SECTION)[1];shared[1]=id;
  EnterCriticalSection(SECTION);EnterCriticalSection(SECTION);
  SetLastError(0x12345678);MakeCriticalSectionGlobal(SECTION);MakeCriticalSectionGlobal(SECTION);
  check(GetLastError()==0x12345678,32800);
  check(((volatile U*)SECTION)[0]==0xaabbcc04&&((volatile U*)SECTION)[1]==id,32801);
  shared[0]=1;
  while(shared[0]!=2)Sleep(0);
  shared[0]=3;LeaveCriticalSection(SECTION);Sleep(0);
  check(shared[0]==3,32802);LeaveCriticalSection(SECTION);
  while(shared[0]!=5)Sleep(0);
  check(shared[2]==id&&shared[8]!=shared[9],32803);
  ExitProcess(0);
 }else{
  while(shared[0]!=1)Sleep(0);
  shared[0]=2;EnterCriticalSection(SECTION);
  check(shared[0]==3,32804);check(((volatile U*)SECTION)[1]==shared[1],32805);
  shared[2]=((volatile U*)SECTION)[1];shared[0]=4;Sleep(0);
  LeaveCriticalSection(SECTION);shared[0]=5;
  while(shared[0]!=6)Sleep(0);
  EnterCriticalSection(SECTION);EnterCriticalSection(SECTION);
  check(((volatile U*)SECTION)[1]==shared[1],32806);
  LeaveCriticalSection(SECTION);LeaveCriticalSection(SECTION);
  DeleteCriticalSection(SECTION);
  check(((volatile U*)SECTION)[0]==0xaabbcc00&&((volatile U*)SECTION)[1]==0,32807);
  shared[0]=7;ExitProcess(0);
 }
}
