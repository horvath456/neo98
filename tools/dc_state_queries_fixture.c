// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
typedef unsigned U;
API U CreateCompatibleDC(U);API U DeleteDC(U);API U SaveDC(U);API U RestoreDC(U,int);
API U GetBkColor(U);API U GetTextColor(U);API U GetBkMode(U);
API U SetBkColor(U,U);API U SetTextColor(U,U);API U SetBkMode(U,U);
API U CreateSolidBrush(U);API U DeleteObject(U);
API void SetLastError(U);API U GetLastError(void);API void ExitProcess(U);
API void* GetModuleHandleA(const char*);API void* GetProcAddress(void*,const char*);
static void check(int good,U code){if(!good)ExitProcess(code);}
U run_dc_state_queries(void){
 U a=CreateCompatibleDC(0),b=CreateCompatibleDC(0);check(a&&b,33200);
 check(GetBkColor(a)==0xffffff&&GetTextColor(a)==0&&GetBkMode(a)==2,33201);
 check(SetBkColor(a,0x123456)==0xffffff&&SetTextColor(a,0xabcdef)==0&&SetBkMode(a,1)==2,33202);
 SetLastError(0x87654321);check(GetBkColor(a)==0x123456&&GetTextColor(a)==0xabcdef&&GetBkMode(a)==1&&GetLastError()==0x87654321,33203);
 check(GetBkColor(b)==0xffffff&&GetTextColor(b)==0&&GetBkMode(b)==2,33204);
 U saved=SaveDC(a);check(saved,33205);SetBkColor(a,0x102030);SetTextColor(a,0x405060);SetBkMode(a,2);
 check(GetBkColor(a)==0x102030&&GetTextColor(a)==0x405060&&GetBkMode(a)==2,33206);
 check(RestoreDC(a,(int)saved)&&GetBkColor(a)==0x123456&&GetTextColor(a)==0xabcdef&&GetBkMode(a)==1,33207);
 U brush=CreateSolidBrush(0x13579b);check(brush,33208);
 SetLastError(0x87654321);check(GetBkColor(brush)==~0u&&GetTextColor(0)==~0u&&!GetBkMode(brush)&&GetLastError()==0x87654321,33209);
 typedef U(__stdcall*Query)(U);void*gdi=GetModuleHandleA("GDI32.DLL");Query bg=(Query)GetProcAddress(gdi,(const char*)225),mode=(Query)GetProcAddress(gdi,(const char*)226),fg=(Query)GetProcAddress(gdi,(const char*)305);
 check(bg&&mode&&fg&&bg(a)==0x123456&&mode(a)==1&&fg(a)==0xabcdef,33210);
 check(DeleteObject(brush)&&DeleteDC(a)&&DeleteDC(b),33211);
 SetLastError(0x87654321);check(GetBkColor(a)==~0u&&GetTextColor(a)==~0u&&!GetBkMode(a)&&GetLastError()==0x87654321,33212);
 return 0;
}
#ifdef DC_STATE_QUERIES_STANDALONE
void mainCRTStartup(void){ExitProcess(run_dc_state_queries());}
#endif
