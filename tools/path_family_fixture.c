/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own guest contract for the original KERNEL32 path family + RAM filesystem. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void SetLastError(U);
API U GetLastError(void);
API U GetFullPathNameA(const char *, U, char *, char **);
API U GetShortPathNameA(const char *, char *, U);
API U GetLongPathNameA(const char *, char *, U);
API U GetCurrentDirectoryA(U, char *);
API U SetCurrentDirectoryA(const char *);
API U GetTempPathA(U, char *);
API U GetTempFileNameA(const char *, const char *, U, char *);
API U GetFullPathNameW(const W *, U, W *, W **);
API U GetShortPathNameW(const W *, W *, U);
API U GetLongPathNameW(const W *, W *, U);
API U GetCurrentDirectoryW(U, W *);
API U SetCurrentDirectoryW(const W *);
API U GetTempPathW(U, W *);
API U GetTempFileNameW(const W *, const W *, U, W *);
API U CreateDirectoryA(const char *, U);
API U RemoveDirectoryA(const char *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U CloseHandle(U);
API U DeleteFileA(const char *);
API U GetFileAttributesA(const char *);
static void check(int ok,U code){if(!ok)ExitProcess(code);}
static U length(const char*s){U n=0;while(s[n])++n;return n;}
static int equal(const char*a,const char*b){while(*a&&*a==*b)++a,++b;return *a==*b;}
static void copy(char*a,const char*b){while((*a++=*b++)){} }
static void fill(char*p,U n,char c){while(n--)*p++=c;}
void run_kernel_path_family_test(void)
{
    char saved[260],path[300],other[300],first[300],second[300];char *part=0;
    /* Scratch below the TEMP folder Win98 Setup creates. */
    const char *folder="C:\\WINDOWS\\TEMP\\PathFamily";
    check(GetCurrentDirectoryA(260,saved)>0,5500);
    check(CreateDirectoryA(folder,0),5501);
    check(SetCurrentDirectoryA("C:/WINDOWS/TEMP/PathFamily"),5502);
    check(GetCurrentDirectoryA(260,path)==length(folder)&&equal(path,folder),5503);
    SetLastError(999);
    check(GetCurrentDirectoryA(0,0)==length(folder)+1&&GetLastError()==111,5504);
    check(GetFullPathNameA("../PathFamily/./first.txt",260,path,&part)>0&&
          equal(path,"C:\\WINDOWS\\TEMP\\PathFamily\\first.txt")&&equal(part,"first.txt"),5505);
    SetLastError(999);
    check(GetFullPathNameA("../PathFamily/./first.txt",0,0,0)==length(path)+1&&GetLastError()==111,5506);
    copy(path,"..\\PathFamily\\one\\..\\file...");
    check(GetFullPathNameA(path,260,path,&part)>0&&equal(path,"C:\\WINDOWS\\TEMP\\PathFamily\\file")&&equal(part,"file"),5507);
    check(GetFullPathNameA("D:relative.txt",260,path,&part)>0&&equal(path,"D:\\relative.txt"),5508);
    check(!GetFullPathNameA("1:bad",260,path,0)&&GetLastError()==161,5509);
    check(GetFullPathNameA("C:\\",260,path,&part)==3&&!part,5510);
    fill(path,sizeof path,'?');
    check(!GetShortPathNameA("C:\\WINDOWS\\TEMP\\PathFamily\\missing.txt",path,260)&&GetLastError()==2&&path[0]=='?',5511);
    U file=CreateFileA("Long Name.txt",0xc0000000,3,0,1,0x80,0);
    check(file!=~0u&&CloseHandle(file),5512);
    /* The RAM volume has no DOS aliases, so the original resolver falls back to
     * the long name. A volume provider that keeps them answers with the alias.
     * Both are checked here. */
    const char *long_name="C:\\WINDOWS\\TEMP\\PathFamily\\Long Name.txt";
    const char *alias_name="C:\\WINDOWS\\TEMP\\PATHFA~1\\LONGNA~1.TXT";
    U short_length=GetShortPathNameA(long_name,path,260);
    if(equal(path,alias_name)){
        check(short_length==length(alias_name),5513);
        check(GetLongPathNameA(alias_name,other,260)==length(long_name)&&equal(other,long_name),5514);
        check(GetShortPathNameA(long_name,0,0)==length(alias_name)+1,5515);
        copy(path,long_name);
        check(GetShortPathNameA(path,path,260)==length(alias_name)&&equal(path,alias_name),5516);
    } else {
        check(short_length==length(long_name)&&equal(path,long_name),5513);
        check(GetLongPathNameA(long_name,other,260)==length(long_name)&&equal(other,long_name),5514);
        check(GetShortPathNameA(long_name,0,0)==length(long_name)+1,5515);
        copy(path,long_name);
        check(GetShortPathNameA(path,path,260)==length(long_name)&&equal(path,long_name),5516);
    }
    check(!SetCurrentDirectoryA(long_name)&&GetLastError()==267,5517);
    check(GetCurrentDirectoryA(260,path)==length(folder)&&equal(path,folder),5518);
    check(GetTempPathA(260,path)==16&&equal(path,"C:\\WINDOWS\\TEMP\\"),5519);
    fill(path,sizeof path,'?');
    check(GetTempPathA(16,path)==17&&path[15]=='\\'&&path[16]=='?',5520);
    check(GetTempFileNameA(folder,"abcDEF",0x1234,path)==0x1234&&
          equal(path,"C:\\WINDOWS\\TEMP\\PathFamily\\abc1234.TMP"),5521);
    check(GetFileAttributesA(path)==~0u,5522);
    check(GetTempFileNameA(folder,"abc",0x10000,path)==0x10000&&
          equal(path,"C:\\WINDOWS\\TEMP\\PathFamily\\abc.TMP"),5523);
    check(GetTempFileNameA(folder,"tmp",0,first)!=0&&GetFileAttributesA(first)!=~0u,5524);
    check(GetTempFileNameA(folder,"tmp",0,second)!=0&&!equal(first,second)&&GetFileAttributesA(second)!=~0u,5525);
    check(!GetTempFileNameA("C:\\NoSuchPathFamily","abc",1,path)&&GetLastError()==3,5526);
    check(!GetFullPathNameW(0,0,0,0)&&GetLastError()==120,5527);
    check(!GetShortPathNameW(0,0,0)&&GetLastError()==120,5528);
    check(!GetLongPathNameW(0,0,0)&&GetLastError()==120,5529);
    check(!GetCurrentDirectoryW(0,0)&&GetLastError()==120,5530);
    check(!SetCurrentDirectoryW(0)&&GetLastError()==120,5531);
    check(!GetTempPathW(0,0)&&GetLastError()==120,5532);
    check(!GetTempFileNameW(0,0,0,0)&&GetLastError()==120,5533);
    const char *accent="C:\\WINDOWS\\TEMP\\PathFamily\\caf\xe9";
    check(CreateDirectoryA(accent,0)&&SetCurrentDirectoryA(accent),5534);
    check(GetCurrentDirectoryA(260,path)==length(accent)&&equal(path,accent),5535);
    check(SetCurrentDirectoryA(folder)&&RemoveDirectoryA(accent),5536);
    check(DeleteFileA(first)&&DeleteFileA(second)&&DeleteFileA(long_name),5537);
    check(SetCurrentDirectoryA(saved)&&RemoveDirectoryA(folder),5538);
}
