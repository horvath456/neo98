/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Deletes D:\VICTIM.TXT into the Recycle Bin with SHFileOperationA and writes
   the answer of SHQueryRecycleBinA to D:\BBRESULT.TXT. */
typedef unsigned U; typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U); API U CreateFileA(const char*,U,U,void*,U,U,U); API int WriteFile(U,const void*,U,U*,void*);
API int CloseHandle(U);
typedef struct { U hwnd; U wFunc; const char *pFrom; const char *pTo; W fFlags; int fAnyAborted; void *hNameMappings; const char *lpszProgressTitle; } SHFILEOPSTRUCTA;
API int SHFileOperationA(SHFILEOPSTRUCTA*);
typedef struct { U cbSize; U sizeLo, sizeHi, itemsLo, itemsHi; } SHQUERYRBINFO;
API U SHQueryRecycleBinA(const char*, SHQUERYRBINFO*);
int __cdecl wsprintfA(char*, const char*, ...);
void mainCRTStartup(void){
  char buf[128]; U n=0, h;
  h=CreateFileA("D:\\VICTIM.TXT",0x40000000,0,0,2,0x80,0); WriteFile(h,"hello",5,&n,0); CloseHandle(h);
  /* FO_DELETE with FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT */
  SHFILEOPSTRUCTA op={0,3,"D:\\VICTIM.TXT\0\0",0,0x0040|0x0010|0x0004,0,0,0};
  int r=SHFileOperationA(&op);
  SHQUERYRBINFO q={20,0,0,0,0}; U qr=SHQueryRecycleBinA("D:\\",&q);
  int len=wsprintfA(buf,"op=%d qr=%08x items=%u\r\n",r,qr,q.itemsLo);
  h=CreateFileA("D:\\BBRESULT.TXT",0x40000000,0,0,2,0x80,0); WriteFile(h,buf,len,&n,0); CloseHandle(h);
  ExitProcess(0);
}
