// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Derived from LLVM libc++ 21.1.8 src/filesystem/posix_compat.h.
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// Windows 98 adaptation for libc++ 21.1.8 filesystem's POSIX-shaped calls.
// Path traversal, copying, iteration and time algorithms remain in libc++.
#ifndef POSIX_COMPAT_H
#define POSIX_COMPAT_H
#include "filesystem_win98_api.h"
#include "error.h"
#include "time_utils.h"
#include <filesystem>
#include <memory>
#define _S_IFMT 0xf000
#define _S_IFDIR 0x4000
#define _S_IFREG 0x8000
#define S_ISDIR(m) (((m)&_S_IFMT)==_S_IFDIR)
#define S_ISREG(m) (((m)&_S_IFMT)==_S_IFREG)
#define S_ISCHR(m) (((m)&_S_IFMT)==0x2000)
#define S_ISFIFO(m) (((m)&_S_IFMT)==0x1000)
#define S_ISBLK(m) (((m)&_S_IFMT)==0x6000)
#define S_ISLNK(m) (((m)&_S_IFMT)==0xa000)
#define S_ISSOCK(m) (((m)&_S_IFMT)==0xc000)
#define AT_FDCWD -1
#define AT_SYMLINK_NOFOLLOW 1
#define MAX_SYMLINK_SIZE MAX_PATH

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM
namespace detail {
using ModeT=int;
using SSizeT=int64_t;
using neo98_filesystem::Narrow;
class WinHandle {
    HANDLE h;
public:
    WinHandle(const wchar_t *p,DWORD access,DWORD) : h(INVALID_HANDLE_VALUE) {
        Narrow path(p);if(!path)return;
        if(access&FILE_READ_ATTRIBUTES)access=(access&~FILE_READ_ATTRIBUTES)|GENERIC_READ;
        if(access&FILE_WRITE_ATTRIBUTES)access=(access&~FILE_WRITE_ATTRIBUTES)|GENERIC_WRITE;
        h=CreateFileA(path,access,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    }
    ~WinHandle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    operator HANDLE()const{return h;}
    explicit operator bool()const{return h!=INVALID_HANDLE_VALUE;}
};
inline void attributes(StatT *out,DWORD attrs,FILETIME atime,FILETIME mtime,DWORD high,DWORD low) {
    out->st_mode=0555|((attrs&FILE_ATTRIBUTE_READONLY)?0:0222)|((attrs&FILE_ATTRIBUTE_DIRECTORY)?_S_IFDIR:_S_IFREG);
    out->st_atim=filetime_to_timespec(atime);out->st_mtim=filetime_to_timespec(mtime);
    out->st_size=(uint64_t(high)<<32)|low;out->st_nlink=1;
}
inline int stat_handle(HANDLE h,StatT *out) {
    BY_HANDLE_FILE_INFORMATION info;
    if(!GetFileInformationByHandle(h,&info))return -1;
    memset(out,0,sizeof *out);
    attributes(out,info.dwFileAttributes,info.ftLastAccessTime,info.ftLastWriteTime,info.nFileSizeHigh,info.nFileSizeLow);
    out->st_dev=info.dwVolumeSerialNumber;out->st_nlink=info.nNumberOfLinks;
    memcpy(out->st_ino.id,&info.nFileIndexHigh,4);memcpy(out->st_ino.id+4,&info.nFileIndexLow,4);
    return 0;
}
inline int stat(const wchar_t *p,StatT *out) {
    Narrow path(p);if(!path)return -1;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!GetFileAttributesExA(path,0,&data))return -1;
    if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        WinHandle h(p,FILE_READ_ATTRIBUTES,0);return h?stat_handle(h,out):-1;
    }
    // Win98 has no directory handles. Its full/long-name services resolve
    // directory identity, including drive-relative paths and 8.3 aliases.
    memset(out,0,sizeof *out);
    attributes(out,data.dwFileAttributes,data.ftLastAccessTime,data.ftLastWriteTime,data.nFileSizeHigh,data.nFileSizeLow);
    char full[MAX_PATH];DWORD n=GetFullPathNameA(path,MAX_PATH,full,NULL);
    if(!n||n>=MAX_PATH)return -1;
    n=GetLongPathNameA(full,out->st_ino.directory,MAX_PATH);
    if(!n||n>=MAX_PATH)return -1;
    // GetLongPathName retains an optional final separator.
    if(n>3 && out->st_ino.directory[n-1]=='\\')out->st_ino.directory[n-1]=0;
    return 0;
}
inline int lstat(const wchar_t *p,StatT *out){return detail::stat(p,out);}
inline int fstat(int fd,StatT *out){return stat_handle((HANDLE)_get_osfhandle(fd),out);}
inline int mkdir(const wchar_t *p,int){Narrow path(p);return path&&CreateDirectoryA(path,NULL)?0:-1;}
inline int unsupported(){SetLastError(50);return -1;}
inline int symlink_file(const wchar_t *,const wchar_t *){return unsupported();}
inline int symlink_dir(const wchar_t *,const wchar_t *){return unsupported();}
inline int link(const wchar_t *,const wchar_t *){return unsupported();}
inline SSizeT readlink(const wchar_t *,wchar_t *,size_t){return unsupported();}
inline int remove(const wchar_t *p) {
    Narrow path(p);if(!path)return -1;
    DWORD a=GetFileAttributesA(path);if(a==INVALID_FILE_ATTRIBUTES)return -1;
    return (a&FILE_ATTRIBUTE_DIRECTORY?RemoveDirectoryA(path):DeleteFileA(path))?0:-1;
}
inline int truncate_handle(HANDLE h,off_t size) {
    if(size<0){SetLastError(87);return -1;}
    LONG high=(LONG)(size>>32);SetLastError(0);
    DWORD low=SetFilePointer(h,(LONG)size,&high,FILE_BEGIN);
    if(low==0xffffffffu&&GetLastError())return -1;
    return SetEndOfFile(h)?0:-1;
}
inline int ftruncate(int fd,off_t size){return truncate_handle((HANDLE)_get_osfhandle(fd),size);}
inline int truncate(const wchar_t *p,off_t size){WinHandle h(p,GENERIC_WRITE,0);return h?truncate_handle(h,size):-1;}
inline int rename(const wchar_t *from,const wchar_t *to) {
    Narrow a(from),b(to);return a&&b&&MoveFileA(a,b)?0:-1;
}
inline int chdir(const wchar_t *p){Narrow path(p);return path&&SetCurrentDirectoryA(path)?0:-1;}
inline wchar_t *wide_result(const char *s) {
    int n=MultiByteToWideChar(CP_ACP,0,s,-1,NULL,0);if(!n)return nullptr;
    wchar_t *out=(wchar_t *)malloc(n*sizeof(wchar_t));
    if(!out){SetLastError(8);return nullptr;}
    if(!MultiByteToWideChar(CP_ACP,0,s,-1,out,n)){free(out);return nullptr;}
    return out;
}
inline wchar_t *getcwd(wchar_t *,size_t) {
    char path[MAX_PATH];DWORD n=GetCurrentDirectoryA(MAX_PATH,path);
    if(!n||n>=MAX_PATH)return nullptr;
    return wide_result(path);
}
inline wchar_t *realpath(const wchar_t *p,wchar_t *) {
    Narrow path(p);if(!path)return nullptr;
    if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES)return nullptr;
    char full[MAX_PATH],long_name[MAX_PATH];DWORD n=GetFullPathNameA(path,MAX_PATH,full,NULL);
    if(!n||n>=MAX_PATH)return nullptr;
    n=GetLongPathNameA(full,long_name,MAX_PATH);if(!n||n>=MAX_PATH)return nullptr;
    return wide_result(long_name);
}
inline int chmod_path(const char *path,int perms) {
    DWORD attrs=GetFileAttributesA(path);if(attrs==INVALID_FILE_ATTRIBUTES)return -1;
    DWORD next=attrs&~FILE_ATTRIBUTE_READONLY;if(!(perms&0222))next|=FILE_ATTRIBUTE_READONLY;
    return next==attrs||SetFileAttributesA(path,next)?0:-1;
}
inline int fchmodat(int,const wchar_t *p,int perms,int){Narrow path(p);return path?chmod_path(path,perms):-1;}
inline int fchmod(int fd,int perms) {
    if(fd<0||fd>=64||_get_osfhandle(fd)==-1){SetLastError(6);return -1;}
    return chmod_path(neo98_filesystem::descriptor_paths[fd],perms);
}
struct StatVFS {uint64_t f_frsize,f_blocks,f_bfree,f_bavail;};
inline int statvfs(const wchar_t *p,StatVFS *out) {
    // libc++ supplies a file as well as a directory to space. Keep its
    // Windows adapter's parent selection; the volume call is Win98's A API.
    path dir=p;
    while(true) {
        error_code ec;auto st=status(dir,ec);
        if(!exists(st)||is_directory(st))break;
        path parent=dir.parent_path();
        if(parent==dir){SetLastError(3);return -1;}
        dir=parent;
    }
    Narrow path(dir.c_str());if(!path)return -1;
    ULONGLONG available,total,free;
    if(!GetDiskFreeSpaceExA(path,&available,&total,&free))return -1;
    out->f_frsize=1;out->f_blocks=total;out->f_bfree=free;out->f_bavail=available;return 0;
}
}
_LIBCPP_END_NAMESPACE_FILESYSTEM
#endif
