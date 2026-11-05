/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: the lower end of a stream. The
   stream layer above (buffers, printf, scanf) is musl's; a stream of musl
   reads, writes, seeks and closes through four functions, which are here
   over ReadFile, WriteFile, SetFilePointer and CloseHandle. Opening a file
   is here too, because the mode of a Windows program says more than musl
   reads from it: "b" and "t". A stream without "b" is a text stream, CR LF
   in the file and a newline in the program. Own code. docs/ARM64-APPS.md. */
#include <windows.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "stdio_impl.h"
#include "lock.h"

/* A stream of musl names its file by a number. */
#define FILES 64
static struct Descriptor
{
    HANDLE handle;
    char used, text, append;
    /* A text stream read a CR at the end of a block and the byte after it,
       which was no LF: that byte waits here. */
    char has_pending, pending;
    /* A temporary file: its name, to delete it when it is closed. */
    char *temporary;
} files[FILES];
static int standard_ready;
static volatile int table_lock;

static struct Descriptor *File(int number)
{
    if (!standard_ready)
    {
        static const DWORD ids[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        standard_ready = 1;
        for (int i = 0; i < 3; ++i)
        {
            files[i].handle = GetStdHandle(ids[i]);
            files[i].used = files[i].text = 1;
        }
    }
    return number >= 0 && number < FILES && files[number].used ? &files[number] : NULL;
}
static int WriteAll(HANDLE handle, const unsigned char *data, size_t size)
{
    while (size)
    {
        DWORD done = 0;
        if (!WriteFile(handle, data, size > 0x40000000 ? 0x40000000 : (DWORD)size, &done, NULL) || !done) return 0;
        data += done;
        size -= done;
    }
    return 1;
}
/* A newline of the program becomes CR LF in a text stream. */
static int WriteBytes(struct Descriptor *file, const unsigned char *data, size_t size)
{
    if (file->append) SetFilePointer(file->handle, 0, NULL, FILE_END);
    if (!file->text) return WriteAll(file->handle, data, size);
    while (size)
    {
        unsigned char block[1024];
        size_t in = 0, out = 0;
        while (in < size && out + 2 <= sizeof block)
        {
            if (data[in] == '\n') block[out++] = '\r';
            block[out++] = data[in++];
        }
        if (!WriteAll(file->handle, block, out)) return 0;
        data += in;
        size -= in;
    }
    return 1;
}
size_t __stdio_write(FILE *f, const unsigned char *buffer, size_t length)
{
    struct Descriptor *file = File(f->fd);
    if (!file || !WriteBytes(file, f->wbase, (size_t)(f->wpos - f->wbase)) || !WriteBytes(file, buffer, length))
    {
        f->wpos = f->wbase = f->wend = 0;
        f->flags |= F_ERR;
        return 0;
    }
    f->wend = f->buf + f->buf_size;
    f->wpos = f->wbase = f->buf;
    return length;
}
/* Standard output stays line buffered wherever it leads: what a program
   printed before it stopped is there. */
size_t __stdout_write(FILE *f, const unsigned char *buffer, size_t length)
{
    f->write = __stdio_write;
    return __stdio_write(f, buffer, length);
}
/* Bytes from the file; in a text stream without the CR of a CR LF. Zero at
   the end of the file, -1 for an error. */
static long long ReadBytes(struct Descriptor *file, unsigned char *out, size_t size)
{
    for (;;)
    {
        DWORD done = 0;
        size_t have = 0, kept = 0;
        if (!size) return 0;
        if (file->has_pending)
        {
            out[have++] = (unsigned char)file->pending;
            file->has_pending = 0;
        }
        if (have < size)
        {
            if (!ReadFile(file->handle, out + have, size - have > 0x40000000 ? 0x40000000 : (DWORD)(size - have), &done, NULL))
            {
                /* The other end of a pipe went away: the end of the input. */
                if (GetLastError() != 109 /* ERROR_BROKEN_PIPE */) return have ? (long long)have : -1;
            }
            have += done;
        }
        if (!have || !file->text) return (long long)have;
        for (size_t i = 0; i < have; ++i)
        {
            if (out[i] == '\r')
            {
                if (i + 1 < have)
                {
                    if (out[i + 1] == '\n') continue;
                }
                else
                {
                    /* The CR is the last byte: the next one decides. */
                    unsigned char next;
                    DWORD one = 0;
                    if (ReadFile(file->handle, &next, 1, &one, NULL) && one)
                    {
                        if (next == '\n')
                        {
                            out[kept++] = '\n';
                            continue;
                        }
                        file->pending = (char)next;
                        file->has_pending = 1;
                    }
                }
            }
            out[kept++] = out[i];
        }
        /* A block that was a single CR before an LF leaves nothing: on. */
        if (kept) return (long long)kept;
    }
}
size_t __stdio_read(FILE *f, unsigned char *buffer, size_t length)
{
    struct Descriptor *file = File(f->fd);
    long long count;
    /* As musl reads: into the caller's memory and, behind it, into the
       buffer of the stream, whose first byte completes the caller's. */
    if (!file)
        count = -1;
    else if (f->buf_size && length <= f->buf_size)
        count = ReadBytes(file, f->buf, f->buf_size);
    else
        count = ReadBytes(file, buffer, length);
    if (count <= 0)
    {
        f->flags |= count ? F_ERR : F_EOF;
        return 0;
    }
    if (!(f->buf_size && length <= f->buf_size)) return (size_t)count;
    f->rpos = f->buf;
    f->rend = f->buf + count;
    if ((size_t)count < length) length = (size_t)count;
    memcpy(buffer, f->rpos, length);
    f->rpos += length;
    return length;
}
off_t __stdio_seek(FILE *f, off_t offset, int origin)
{
    struct Descriptor *file = File(f->fd);
    LONG high = (LONG)(offset >> 32);
    DWORD low;
    if (!file)
    {
        errno = EBADF;
        return -1;
    }
    file->has_pending = 0;
    low = SetFilePointer(file->handle, (LONG)offset, &high, origin == SEEK_SET ? FILE_BEGIN : origin == SEEK_CUR ? FILE_CURRENT : FILE_END);
    if (low == 0xffffffffu && GetLastError())
    {
        errno = ESPIPE;
        return -1;
    }
    return (off_t)((unsigned long long)(unsigned)high << 32 | low);
}
int __stdio_close(FILE *f)
{
    struct Descriptor *file = File(f->fd);
    int result = 0;
    if (!file) return -1;
    /* The standard handles belong to the process. */
    if (f->fd > 2 && !CloseHandle(file->handle)) result = -1;
    if (file->temporary)
    {
        DeleteFileA(file->temporary);
        free(file->temporary);
    }
    if (f->fd > 2)
    {
        __lock(&table_lock);
        memset(file, 0, sizeof *file);
        __unlock(&table_lock);
    }
    return result;
}

/* Adopt a Windows handle into the same descriptor table used by musl's
   fdopen. Ownership transfers only after a slot was allocated. */
int _open_osfhandle(intptr_t value, int flags)
{
    int number=-1;
    if ((HANDLE)value==INVALID_HANDLE_VALUE) { errno=EBADF;return -1; }
    File(0);
    __lock(&table_lock);
    for(int i=3;i<FILES;++i) if(!files[i].used) {
        number=i; memset(&files[i],0,sizeof files[i]);
        files[i].handle=(HANDLE)value;
        files[i].text=(flags&O_BINARY)==0;
        files[i].append=(flags&O_APPEND)!=0;
        files[i].used=1;break;
    }
    __unlock(&table_lock);
    if(number<0)errno=EMFILE;
    return number;
}
int _close(int number)
{
    FILE f={0};f.fd=number;
    if(!File(number)){errno=EBADF;return -1;}
    return __stdio_close(&f);
}

/* CreateThread does not enter the CRT. Enable musl's stream locks before
   constructors can start threads; the locking algorithm is imported from
   musl's __lockfile.c and waits through our Windows futex adapter. */
void __neo98_stdio_init(void)
{
    File(0);
    stdin->lock = stdout->lock = stderr->lock = 0;
}

/* --- opening ----------------------------------------------------------------- */
static int Open(const char *name, const char *mode)
{
    DWORD access = 0, creation = 0;
    const int plus = strchr(mode, '+') != NULL;
    int number = -1;
    HANDLE handle;
    File(0);
    switch (mode[0])
    {
    case 'r': access = GENERIC_READ | (plus ? GENERIC_WRITE : 0); creation = OPEN_EXISTING; break;
    case 'w': access = GENERIC_WRITE | (plus ? GENERIC_READ : 0); creation = strchr(mode, 'x') ? CREATE_NEW : CREATE_ALWAYS; break;
    case 'a': access = GENERIC_WRITE | (plus ? GENERIC_READ : 0); creation = OPEN_ALWAYS; break;
    default: errno = EINVAL; return -1;
    }
    handle = CreateFileA(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT
              : error == ERROR_ACCESS_DENIED ? EACCES : error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ? EEXIST : EIO;
        return -1;
    }
    __lock(&table_lock);
    for (int i = 3; i < FILES; ++i)
        if (!files[i].used)
        {
            number = i;
            memset(&files[i], 0, sizeof files[i]);
            files[i].used = 1;
            break;
        }
    __unlock(&table_lock);
    if (number < 0)
    {
        CloseHandle(handle);
        errno = EMFILE;
        return -1;
    }
    files[number].handle = handle;
    files[number].append = mode[0] == 'a';
    files[number].text = strchr(mode, 'b') == NULL;
    if (files[number].append) SetFilePointer(handle, 0, NULL, FILE_END);
    return number;
}
/* What musl's __fdopen does for a file descriptor: the record of the stream
   with its buffer behind it. */
static void Prepare(FILE *f, int number, const char *mode)
{
    f->flags = (f->flags & F_PERM) | (strchr(mode, '+') ? 0 : mode[0] == 'r' ? F_NOWR : F_NORD) | (mode[0] == 'a' ? F_APP : 0);
    f->fd = number;
    f->lbf = EOF;
    f->read = __stdio_read;
    f->write = __stdio_write;
    f->seek = __stdio_seek;
    f->close = __stdio_close;
    f->lock = 0;
}
static FILE *Make(int number, const char *mode)
{
    FILE *f = (FILE *)malloc(sizeof *f + UNGET + BUFSIZ);
    if (!f) return NULL;
    memset(f, 0, sizeof *f);
    f->buf = (unsigned char *)f + sizeof *f + UNGET;
    f->buf_size = BUFSIZ;
    Prepare(f, number, mode);
    return __ofl_add(f);
}
FILE *fopen(const char *name, const char *mode)
{
    const int number = Open(name, mode);
    FILE *f;
    if (number < 0) return NULL;
    f = Make(number, mode);
    if (!f)
    {
        CloseHandle(files[number].handle);
        files[number].used = 0;
    }
    return f;
}
/* A stream for a number that is open already: one of the standard three, or
   the number of another stream. */
FILE *fdopen(int number, const char *mode)
{
    if (!File(number) || !strchr("rwa", mode[0]))
    {
        errno = EBADF;
        return NULL;
    }
    return Make(number, mode);
}
FILE *freopen(const char *name, const char *mode, FILE *f)
{
    int number;
    fflush(f);
    if (!name) return f;
    number = Open(name, mode);
    if (number < 0)
    {
        fclose(f);
        return NULL;
    }
    f->close(f);
    f->rpos = f->rend = f->wpos = f->wbase = f->wend = 0;
    f->off = 0;
    f->mode = 0;
    Prepare(f, number, mode);
    return f;
}
FILE *_wfopen(const wchar_t *name, const wchar_t *mode)
{
    char narrow_name[MAX_PATH], narrow_mode[16];
    if (!WideCharToMultiByte(CP_ACP, 0, name, -1, narrow_name, sizeof narrow_name, NULL, NULL) ||
        !WideCharToMultiByte(CP_ACP, 0, mode, -1, narrow_mode, sizeof narrow_mode, NULL, NULL))
    {
        errno = EINVAL;
        return NULL;
    }
    return fopen(narrow_name, narrow_mode);
}
/* A name for a temporary file in the directory the system has for them. The
   system makes the file with it, so no other caller gets the same name. */
char *tmpnam(char *buffer)
{
    static char own[L_tmpnam];
    char directory[MAX_PATH];
    if (!buffer) buffer = own;
    if (!GetTempPathA(sizeof directory, directory) || !GetTempFileNameA(directory, "tmp", 0, buffer)) return NULL;
    return buffer;
}
/* A binary stream on a file that goes away when the stream is closed. */
FILE *tmpfile(void)
{
    char name[L_tmpnam];
    FILE *f;
    if (!tmpnam(name)) return NULL;
    f = fopen(name, "w+b");
    if (!f)
    {
        DeleteFileA(name);
        return NULL;
    }
    files[f->fd].temporary = strdup(name);
    return f;
}
int remove(const char *name) { return DeleteFileA(name) ? 0 : -1; }
int rename(const char *from, const char *to) { return MoveFileA(from, to) ? 0 : -1; }

/* --- names of the Windows runtime ---------------------------------------------- */
int _fseeki64(FILE *f, long long offset, int origin) { return __fseeko(f, offset, origin); }
long long _ftelli64(FILE *f) { return __ftello(f); }
intptr_t _get_osfhandle(int number)
{
    struct Descriptor *file = File(number);
    return file ? (intptr_t)file->handle : -1;
}
int _isatty(int number)
{
    struct Descriptor *file = File(number);
    return file && GetFileType(file->handle) == FILE_TYPE_CHAR;
}
int _sscanf_l(const char *text, const char *format, _locale_t locale, ...)
{
    va_list arguments;
    (void)locale;
    va_start(arguments, locale);
    const int n = vsscanf(text, format, arguments);
    va_end(arguments);
    return n;
}
