// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API const char*GetCommandLineA(void);
API U PrintDlgA(void*);API U PrintDlgW(void*);API U CommDlgExtendedError(void);API U FindTextA(void*);API int GetFileTitleA(const char*,char*,U);
API U WriteProfileStringA(const char*,const char*,const char*);API U GlobalAlloc(U,U);API U GlobalFree(U);
API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);API U CreateThread(void*,U,U(__stdcall*)(void*),void*,U,U*);API void Sleep(U);API U WaitForSingleObject(U,U);API U GetExitCodeThread(U,U*);API U CloseHandle(U);
static void check(U okay,U error){if(!okay)ExitProcess(error);}
static void put(unsigned char*p,U offset,U v){p[offset]=(unsigned char)v;p[offset+1]=(unsigned char)(v>>8);p[offset+2]=(unsigned char)(v>>16);p[offset+3]=(unsigned char)(v>>24);}
static U get(const unsigned char*p,U offset){return p[offset]|((U)p[offset+1]<<8)|((U)p[offset+2]<<16)|((U)p[offset+3]<<24);}
static U __stdcall worker(void*unused){
 (void)unused;check(CommDlgExtendedError()==0,27640);SetLastError(0x55);check(!PrintDlgA(0),27641);check(GetLastError()==0x55,27642);
 check(CommDlgExtendedError()==2&&GetLastError()==0,27643);Sleep(0);check(CommDlgExtendedError()==2,27644);return 98;
}
void run_print_dialog_test(void){
 unsigned char pd[66]={0};put(pd,0,66);put(pd,20,0x3400);put(pd,16,0x12345678);
 check(WriteProfileStringA("Windows","device",0),27600);
 check(!PrintDlgA(pd)&&CommDlgExtendedError()==0x1008,27601);
 check(get(pd,8)==0&&get(pd,12)==0&&get(pd,16)==0&&get(pd,20)==0x3400,27602);
 check(WriteProfileStringA("Windows","device",""),27603);
 U module=GetModuleHandleA("COMDLG32.DLL");typedef U(__stdcall*Print)(void*);Print ordinal=(Print)GetProcAddress(module,(const char*)117);check(ordinal!=0,27604);
 check(!ordinal(pd)&&CommDlgExtendedError()==0x1008,27605);
 put(pd,0,68);SetLastError(0x98);check(!PrintDlgA(pd)&&GetLastError()==0x98,27606);check(CommDlgExtendedError()==1&&GetLastError()==0,27607);
 U id=0,result=0,thread=CreateThread(0,0,worker,0,0,&id);check(thread&&id,27608);
 check(WaitForSingleObject(thread,0xffffffff)==0&&GetExitCodeThread(thread,&result)&&result==98,27609);
 check(CommDlgExtendedError()==1&&CloseHandle(thread),27610);
 check(!FindTextA(0)&&CommDlgExtendedError()==2,27611);
 char title[32];check(GetFileTitleA("C:\\WINDOWS\\FILE.TXT",title,32)==0&&title[0]=='F',27612);check(CommDlgExtendedError()==2,27613);
 SetLastError(99);check(!PrintDlgW((void*)0xdeadbeef)&&GetLastError()==120,27614);check(CommDlgExtendedError()==2,27615);
 put(pd,0,66);put(pd,4,0xdeadbeef);check(!PrintDlgA(pd)&&CommDlgExtendedError()==0xffff,27616);put(pd,4,0);
 U handle=GlobalAlloc(0x42,64);check(handle!=0,27617);put(pd,8,handle);
 check(!PrintDlgA(pd)&&CommDlgExtendedError()==0x1003&&get(pd,8)==handle,27618);check(!GlobalFree(handle),27619);put(pd,8,0);
}
#ifdef PRINT_DIALOG_STANDALONE
void mainCRTStartup(void){
 if(GetCommandLineA()[0]=='C'){
  unsigned char pd[66]={0};put(pd,0,66);put(pd,20,0x400);
  check(WriteProfileStringA("Windows","device","Fixture,winspool,LPT1:"),27690);
  PrintDlgA(pd);ExitProcess(27691); // Must stop at the actual unavailable driver.
 }
 run_print_dialog_test();ExitProcess(0);
}
#endif
