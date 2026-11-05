// SPDX-License-Identifier: GPL-2.0-or-later
// Private libc++ Windows adapter. These names are not Windows exports.
#pragma once
#include <windows.h>
#include <stddef.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace neo98_filesystem {
struct Narrow {
    char text[MAX_PATH]; bool valid;
    explicit Narrow(const wchar_t *path) {
        BOOL replaced=FALSE;
        valid=WideCharToMultiByte(CP_ACP,0,path,-1,text,sizeof text,NULL,&replaced)!=0 && !replaced;
        if(replaced) SetLastError(1113); // ERROR_NO_UNICODE_TRANSLATION
    }
    operator const char *() const { return text; }
    explicit operator bool() const { return valid; }
};
struct FindData {
    DWORD dwFileAttributes; FILETIME ftCreationTime,ftLastAccessTime,ftLastWriteTime;
    DWORD nFileSizeHigh,nFileSizeLow,dwReserved0,dwReserved1;
    wchar_t cFileName[MAX_PATH],cAlternateFileName[14];
};
inline bool FindConvert(const WIN32_FIND_DATAA &a,FindData *w) {
    memcpy(w,&a,offsetof(WIN32_FIND_DATAA,cFileName));
    return MultiByteToWideChar(CP_ACP,0,a.cFileName,-1,w->cFileName,MAX_PATH) &&
           MultiByteToWideChar(CP_ACP,0,a.cAlternateFileName,-1,w->cAlternateFileName,14);
}
inline HANDLE FindFirst(const wchar_t *path,FindData *out) {
    Narrow p(path); if(!p)return INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA data; HANDLE h=FindFirstFileA(p,&data);
    if(h!=INVALID_HANDLE_VALUE && !FindConvert(data,out)){DWORD e=GetLastError();FindClose(h);SetLastError(e);return INVALID_HANDLE_VALUE;}
    return h;
}
inline BOOL FindNext(HANDLE h,FindData *out) {
    WIN32_FIND_DATAA data;
    return FindNextFileA(h,&data) && FindConvert(data,out);
}
inline DWORD TempPath(DWORD size,wchar_t *out) {
    char path[MAX_PATH];DWORD n=GetTempPathA(sizeof path,path);
    if(!n)return 0;
    if(n>=sizeof path){SetLastError(206);return 0;}
    int needed=MultiByteToWideChar(CP_ACP,0,path,-1,NULL,0);
    if(needed>(int)size)return needed;
    return MultiByteToWideChar(CP_ACP,0,path,-1,out,size)?needed-1:0;
}
inline char descriptor_paths[64][MAX_PATH];
inline int Open(const wchar_t *path,int flags,int = 0) {
    Narrow p(path);if(!p){errno=EINVAL;return -1;}
    DWORD access=(flags&O_RDWR)?GENERIC_READ|GENERIC_WRITE:(flags&O_WRONLY)?GENERIC_WRITE:GENERIC_READ;
    DWORD create=(flags&O_CREAT)?((flags&O_EXCL)?CREATE_NEW:(flags&O_TRUNC)?CREATE_ALWAYS:OPEN_ALWAYS):
                 (flags&O_TRUNC)?TRUNCATE_EXISTING:OPEN_EXISTING;
    HANDLE h=CreateFileA(p,access,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,create,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE){DWORD e=GetLastError();errno=e==2||e==3?ENOENT:e==5?EACCES:e==80||e==183?EEXIST:EIO;return -1;}
    int fd=_open_osfhandle((intptr_t)h,flags);
    if(fd<0)CloseHandle(h);
    else {
        DWORD n=GetFullPathNameA(p,MAX_PATH,descriptor_paths[fd],NULL);
        if(!n||n>=MAX_PATH){_close(fd);errno=n?ENAMETOOLONG:EIO;return -1;}
    }
    return fd;
}
}
#define WIN32_FIND_DATAW neo98_filesystem::FindData
#define FindFirstFileW neo98_filesystem::FindFirst
#define FindNextFileW neo98_filesystem::FindNext
#define GetTempPathW neo98_filesystem::TempPath
#define _wopen neo98_filesystem::Open
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400
#define IO_REPARSE_TAG_SYMLINK 0xa000000c
#define FILE_READ_ATTRIBUTES 0x80
#define FILE_WRITE_ATTRIBUTES 0x100
