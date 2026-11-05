// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);API U AllocateLocallyUniqueId(void*);API U OpenProcessToken(U,U,U*);API U AdjustTokenPrivileges(U,U,void*,U,void*,U*);API U InitializeSecurityDescriptor(void*,U);API U IsValidSecurityDescriptor(void*);API U SetSecurityDescriptorDacl(void*,U,void*,U);API U GetSecurityInfo(U,U,U,void*,void*,void*,void*,void*);API U RevertToSelf(void);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
static void check(int yes,U code){if(!yes)ExitProcess(code);}
void run_security_identity_test(void){U data[8];for(U i=0;i<8;++i)data[i]=0xaaaaaaaa;SetLastError(777);check(!AllocateLocallyUniqueId(data)&&GetLastError()==120,38000);for(U i=0;i<8;++i)check(data[i]==0xaaaaaaaa,38001);check(!AllocateLocallyUniqueId(0)&&GetLastError()==120,38002);check(!AllocateLocallyUniqueId((void*)0xdeadbeef)&&GetLastError()==120,38003);
 U module=GetModuleHandleA("ADVAPI32.DLL");U(__stdcall*ordinal)(void*)=(void*)GetProcAddress(module,(const char*)13);check(ordinal&&!ordinal(data)&&GetLastError()==120,38004);check(!OpenProcessToken((U)-1,8,data)&&data[0]==0xaaaaaaaa&&GetLastError()==120,38005);check(!AdjustTokenPrivileges(0,0,0,0,0,0)&&GetLastError()==120,38006);
 check(InitializeSecurityDescriptor(data,1)==1&&GetLastError()==120,38007);check(IsValidSecurityDescriptor((void*)0xdeadbeef)==1&&GetLastError()==120,38008);check(SetSecurityDescriptorDacl(data,1,0,0)==1&&GetLastError()==120,38009);for(U i=0;i<8;++i)check(data[i]==0xaaaaaaaa,38010);check(GetSecurityInfo(0,0,0,0,0,0,0,0)==120&&GetLastError()==120,38011);check(!RevertToSelf()&&GetLastError()==120,38012);
}
#ifdef SECURITY_IDENTITY_STANDALONE
void mainCRTStartup(void){run_security_identity_test();ExitProcess(0);}
#endif
