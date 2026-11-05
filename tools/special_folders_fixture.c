/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API U GetFileAttributesA(const char*);API U CreateDirectoryA(const char*,const void*);API U SHGetSpecialFolderPathA(U,char*,U,U);API U SHGetSpecialFolderPathW(U,W*,U,U);API U RegCreateKeyExA(U,const char*,U,void*,U,U,void*,U*,U*);API U RegSetValueExA(U,const char*,U,U,const void*,U);API U RegQueryValueExA(U,const char*,U,U*,void*,U*);API U RegDeleteValueA(U,const char*);API U RegCloseKey(U);
static void check(int yes,U code){if(!yes)ExitProcess(code);}
static U length(const char*s){U n=0;while(s[n])++n;return n;}
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static int attributes(const char*path,U mask){U a=GetFileAttributesA(path);return a!=~0u&&(a&mask)==mask;}
static int samew(const W*a,const char*b){while(*a&&*a==(unsigned char)*b){++a;++b;}return *a==(unsigned char)*b;}
static void value(U key,const char*name,U type,const char*text){check(!RegSetValueExA(key,name,0,type,text,length(text)+1),24090);}
void run_special_folders_test(void){
 U key=0,made=0;check(!RegCreateKeyExA(0x80000001,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",0,0,0,0x2001f,0,&key,&made),24000);
 char path[260];W wide[260];
 check(SHGetSpecialFolderPathA(0,path,16,1)&&same(path,"C:\\WINDOWS\\Desktop"),24001);check(attributes(path,16),24002);
 value(key,"Desktop",1,"C:\\WINDOWS\\TEMP\\LaterDesktop");check(SHGetSpecialFolderPathA(0,path,16,0)&&same(path,"C:\\WINDOWS\\Desktop"),24003); // Same original cached path after a registry write.
 check(SHGetSpecialFolderPathW(0,wide,16,1)&&samew(wide,"C:\\WINDOWS\\Desktop"),24004);
 value(key,"Programs",2,"%windir%\\PortedPrograms");check(SHGetSpecialFolderPathA(0,path,2,1)&&same(path,"C:\\WINDOWS\\PortedPrograms"),24005);check(attributes(path,16),24006);
 U shell=0;check(!RegCreateKeyExA(0x80000001,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",0,0,0,0x2001f,0,&shell,&made),24007);U type=0,n=sizeof path;check(!RegQueryValueExA(shell,"Programs",0,&type,path,&n)&&type==1&&same(path,"C:\\WINDOWS\\PortedPrograms"),24008);
 value(key,"Favorites",1,"C:\\WINDOWS\\TEMP\\FolderPortFavorites");check(!SHGetSpecialFolderPathA(0,path,6,0)&&!path[0],24009);check(SHGetSpecialFolderPathA(0,path,6,1)&&same(path,"C:\\WINDOWS\\TEMP\\FolderPortFavorites"),24010);
 /* The reference system installation had a Personal value under User Shell Folders;
    the ISO installation has none, so the deletion may answer 2. */
 {U gone=RegDeleteValueA(key,"Personal");check(!gone||gone==2,24011);}check(!SHGetSpecialFolderPathA(0,path,5,1)&&!path[0],24012);
 /* Without fCreate SHELL32 answers only a folder that exists; Win98 has no
    C:\Documents, so the value names one Setup creates. */
 value(key,"Personal",1,"C:\\WINDOWS\\TEMP");check(SHGetSpecialFolderPathA(0,path,5,0)&&same(path,"C:\\WINDOWS\\TEMP"),24013);
 check(!SHGetSpecialFolderPathA(0,path,22,0)&&!path[0],24014);check(SHGetSpecialFolderPathA(0,path,22,1)&&same(path,"C:\\WINDOWS\\All Users\\Start Menu"),24015);
 check(SHGetSpecialFolderPathA(0,path,8,1)&&same(path,"C:\\WINDOWS\\Recent")&&attributes(path,18),24016);
 path[0]='x';check(!SHGetSpecialFolderPathA(0,path,0x8010,1)&&!path[0],24017);wide[0]='x';check(!SHGetSpecialFolderPathW(0,wide,0xffff,1)&&!wide[0],24018);
 check(!SHGetSpecialFolderPathA(0,path,3,0)&&GetLastError()==50&&!path[0],24019);
 check(!RegCloseKey(key)&&!RegCloseKey(shell),24020);
}
#ifdef SPECIAL_FOLDERS_STANDALONE
void mainCRTStartup(void){run_special_folders_test();ExitProcess(0);}
#endif
