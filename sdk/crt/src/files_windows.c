/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: directories, what a file is, and
   threads under the names of the Windows runtime and of mingw-w64
   (<direct.h>, <sys/stat.h>, <dirent.h>, <io.h>, <process.h>),
   over KERNEL32. Own code. docs/ARM64-APPS.md. */
#include <windows.h>
#include <direct.h>
#include <dirent.h>
#include <errno.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* 100 ns units between 1601 and 1970. */
#define EPOCH_DIFFERENCE 116444736000000000ull

static int Fail(void)
{
    switch (GetLastError())
    {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_NAME: case ERROR_BAD_PATHNAME:
    case ERROR_INVALID_DRIVE: case ERROR_NO_MORE_FILES:
        errno = ENOENT; break;
    case ERROR_ALREADY_EXISTS: case ERROR_FILE_EXISTS: errno = EEXIST; break;
    case ERROR_DIR_NOT_EMPTY: errno = ENOTEMPTY; break;
    case ERROR_NOT_ENOUGH_MEMORY: case ERROR_OUTOFMEMORY: errno = ENOMEM; break;
    case ERROR_DISK_FULL: errno = ENOSPC; break;
    default: errno = EACCES; break;
    }
    return -1;
}

int _mkdir(const char *name) { return CreateDirectoryA(name, 0) ? 0 : Fail(); }
int mkdir(const char *name, ...) { return _mkdir(name); }
int _rmdir(const char *name) { return RemoveDirectoryA(name) ? 0 : Fail(); }
int _chdir(const char *name) { return SetCurrentDirectoryA(name) ? 0 : Fail(); }
char *_getcwd(char *buffer, int size)
{
    char path[MAX_PATH];
    const DWORD length = GetCurrentDirectoryA(sizeof path, path);
    if (!length || length >= sizeof path) { Fail(); return 0; }
    if (!buffer)
    {
        if (size < (int)length + 1) size = (int)length + 1;
        buffer = malloc((size_t)size);
        if (!buffer) { errno = ENOMEM; return 0; }
    }
    else if (size < (int)length + 1) { errno = ERANGE; return 0; }
    memcpy(buffer, path, length + 1);
    return buffer;
}
int _unlink(const char *name) { return DeleteFileA(name) ? 0 : Fail(); }
int _access(const char *name, int mode)
{
    const DWORD attributes = GetFileAttributesA(name);
    if (attributes == INVALID_FILE_ATTRIBUTES) return Fail();
    if ((mode & 2) && (attributes & FILE_ATTRIBUTE_READONLY) && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        errno = EACCES;
        return -1;
    }
    return 0;
}

static time_t UnixTime(FILETIME time)
{
    const unsigned long long ticks = (unsigned long long)time.dwHighDateTime << 32 | time.dwLowDateTime;
    return ticks < EPOCH_DIFFERENCE ? 0 : (time_t)((ticks - EPOCH_DIFFERENCE) / 10000000ull);
}
static unsigned short Mode(DWORD attributes)
{
    unsigned short mode = attributes & FILE_ATTRIBUTE_DIRECTORY ? _S_IFDIR | _S_IEXEC : _S_IFREG;
    mode |= attributes & FILE_ATTRIBUTE_READONLY ? _S_IREAD : _S_IREAD | _S_IWRITE;
    /* The bits of the owner, for group and others too, as the Windows runtime sets them. */
    return (unsigned short)(mode | (mode & 0700) >> 3 | (mode & 0700) >> 6);
}
int stat(const char *name, struct stat *out)
{
    WIN32_FIND_DATAA found;
    HANDLE search;
    DWORD attributes;
    if (!name || !out) { errno = EINVAL; return -1; }
    memset(out, 0, sizeof *out);
    out->st_nlink = 1;
    /* A pattern is no file. */
    if (strpbrk(name, "*?")) { errno = ENOENT; return -1; }
    search = FindFirstFileA(name, &found);
    if (search != INVALID_HANDLE_VALUE)
    {
        FindClose(search);
        out->st_mode = Mode(found.dwFileAttributes);
        out->st_size = (long long)found.nFileSizeHigh << 32 | found.nFileSizeLow;
        out->st_mtime = UnixTime(found.ftLastWriteTime);
        out->st_atime = UnixTime(found.ftLastAccessTime);
        out->st_ctime = UnixTime(found.ftCreationTime);
        if (!out->st_atime) out->st_atime = out->st_mtime;
        if (!out->st_ctime) out->st_ctime = out->st_mtime;
        return 0;
    }
    /* The root of a drive and a name with a separator at its end are found
       by no search. */
    attributes = GetFileAttributesA(name);
    if (attributes == INVALID_FILE_ATTRIBUTES) return Fail();
    out->st_mode = Mode(attributes);
    return 0;
}
int fstat(int number, struct stat *out)
{
    const HANDLE handle = (HANDLE)_get_osfhandle(number);
    DWORD high = 0, low;
    FILETIME created, accessed, written;
    if (!out) { errno = EINVAL; return -1; }
    if (handle == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    memset(out, 0, sizeof *out);
    out->st_nlink = 1;
    if (_isatty(number)) { out->st_mode = _S_IFCHR | _S_IREAD | _S_IWRITE; return 0; }
    low = GetFileSize(handle, &high);
    if (low == 0xFFFFFFFFu && GetLastError() != ERROR_SUCCESS) return Fail();
    out->st_mode = Mode(0);
    out->st_size = (long long)high << 32 | low;
    if (GetFileTime(handle, &created, &accessed, &written))
    {
        out->st_mtime = UnixTime(written);
        out->st_atime = UnixTime(accessed);
        out->st_ctime = UnixTime(created);
    }
    return 0;
}

struct __neo98_dir
{
    HANDLE search;
    int first;      /* `found` holds an entry readdir has not handed out */
    WIN32_FIND_DATAA found;
    struct dirent entry;
    char pattern[MAX_PATH + 4];
};
static int Start(DIR *directory)
{
    directory->search = FindFirstFileA(directory->pattern, &directory->found);
    directory->first = directory->search != INVALID_HANDLE_VALUE;
    return directory->first;
}
DIR *opendir(const char *name)
{
    DIR *directory;
    size_t length;
    DWORD attributes;
    if (!name || !*name) { errno = ENOENT; return 0; }
    length = strlen(name);
    if (length >= MAX_PATH) { errno = ENAMETOOLONG; return 0; }
    attributes = GetFileAttributesA(name);
    if (attributes == INVALID_FILE_ATTRIBUTES) { Fail(); return 0; }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) { errno = ENOTDIR; return 0; }
    directory = calloc(1, sizeof *directory);
    if (!directory) { errno = ENOMEM; return 0; }
    memcpy(directory->pattern, name, length);
    if (name[length - 1] != '\\' && name[length - 1] != '/' && name[length - 1] != ':') directory->pattern[length++] = '\\';
    memcpy(directory->pattern + length, "*", 2);
    /* An empty root has no entry at all; that is a directory at its end. */
    if (!Start(directory) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_NO_MORE_FILES)
    {
        Fail();
        free(directory);
        return 0;
    }
    return directory;
}
struct dirent *readdir(DIR *directory)
{
    size_t length;
    if (!directory) { errno = EBADF; return 0; }
    if (directory->search == INVALID_HANDLE_VALUE) return 0;
    if (!directory->first && !FindNextFileA(directory->search, &directory->found)) return 0;
    directory->first = 0;
    length = strlen(directory->found.cFileName);
    if (length >= sizeof directory->entry.d_name) length = sizeof directory->entry.d_name - 1;
    memcpy(directory->entry.d_name, directory->found.cFileName, length);
    directory->entry.d_name[length] = 0;
    directory->entry.d_namlen = (unsigned short)length;
    directory->entry.d_reclen = sizeof directory->entry;
    return &directory->entry;
}
void rewinddir(DIR *directory)
{
    if (!directory) return;
    if (directory->search != INVALID_HANDLE_VALUE) FindClose(directory->search);
    Start(directory);
}
int closedir(DIR *directory)
{
    if (!directory) { errno = EBADF; return -1; }
    if (directory->search != INVALID_HANDLE_VALUE) FindClose(directory->search);
    free(directory);
    return 0;
}

uintptr_t _beginthreadex(void *security, unsigned stack_size, unsigned (__stdcall *start)(void *), void *argument,
                         unsigned flags, unsigned *thread_id)
{
    DWORD id = 0;
    const HANDLE thread = CreateThread(security, stack_size, (LPTHREAD_START_ROUTINE)start, argument, flags, &id);
    if (thread_id) *thread_id = id;
    if (!thread) errno = EAGAIN;
    return (uintptr_t)thread;
}
void _endthreadex(unsigned code) { ExitThread(code); }
struct Begin { void (__cdecl *start)(void *); void *argument; };
static DWORD WINAPI Run(void *context)
{
    const struct Begin begin = *(struct Begin *)context;
    free(context);
    begin.start(begin.argument);
    return 0;
}
uintptr_t _beginthread(void (__cdecl *start)(void *), unsigned stack_size, void *argument)
{
    struct Begin *begin = malloc(sizeof *begin);
    HANDLE thread;
    DWORD id;
    if (!begin) { errno = EAGAIN; return (uintptr_t)-1; }
    begin->start = start;
    begin->argument = argument;
    thread = CreateThread(0, stack_size, Run, begin, 0, &id);
    if (!thread) { free(begin); errno = EAGAIN; return (uintptr_t)-1; }
    /* The Windows runtime closes this handle when the thread ends; closed
       here, the thread lives on and the number says no more than there. */
    CloseHandle(thread);
    return (uintptr_t)thread;
}
void _endthread(void) { ExitThread(0); }
int _getpid(void) { return (int)GetCurrentProcessId(); }
