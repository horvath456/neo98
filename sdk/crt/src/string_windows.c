/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* The static C runtime of the NEO98 SDK: what <string.h> and <wchar.h> have
   under Windows names, and the texts of the error numbers of the Windows
   runtime. The functions of the standard are musl's. Own code.
   docs/ARM64-APPS.md. */
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

int _stricmp(const char *a, const char *b)
{
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); ++a, ++b) {}
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int _strnicmp(const char *a, const char *b, size_t count)
{
    for (; count; --count, ++a, ++b)
    {
        const int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y) return x - y;
        if (!x) break;
    }
    return 0;
}
char *_strdup(const char *text) { return strdup(text); }
int _wcsicmp(const wchar_t *a, const wchar_t *b)
{
    for (; *a && towlower(*a) == towlower(*b); ++a, ++b) {}
    return (int)towlower(*a) - (int)towlower(*b);
}
int _wcsnicmp(const wchar_t *a, const wchar_t *b, size_t count)
{
    for (; count; --count, ++a, ++b)
    {
        const wint_t x = towlower(*a), y = towlower(*b);
        if (x != y) return (int)x - (int)y;
        if (!x) break;
    }
    return 0;
}
wchar_t *_wcsdup(const wchar_t *text) { return wcsdup(text); }

/* The texts of the Windows runtime for its error numbers. */
char *strerror(int code)
{
    static const struct { int code; const char *text; } texts[] = {
        {0, "No error"}, {EPERM, "Operation not permitted"}, {ENOENT, "No such file or directory"},
        {ESRCH, "No such process"}, {EINTR, "Interrupted function call"}, {EIO, "Input/output error"},
        {ENXIO, "No such device or address"}, {E2BIG, "Arg list too long"}, {ENOEXEC, "Exec format error"},
        {EBADF, "Bad file descriptor"}, {ECHILD, "No child processes"}, {EAGAIN, "Resource temporarily unavailable"},
        {ENOMEM, "Not enough space"}, {EACCES, "Permission denied"}, {EFAULT, "Bad address"},
        {EBUSY, "Resource device"}, {EEXIST, "File exists"}, {EXDEV, "Improper link"}, {ENODEV, "No such device"},
        {ENOTDIR, "Not a directory"}, {EISDIR, "Is a directory"}, {EINVAL, "Invalid argument"},
        {ENFILE, "Too many open files in system"}, {EMFILE, "Too many open files"},
        {ENOTTY, "Inappropriate I/O control operation"}, {EFBIG, "File too large"},
        {ENOSPC, "No space left on device"}, {ESPIPE, "Invalid seek"}, {EROFS, "Read-only file system"},
        {EMLINK, "Too many links"}, {EPIPE, "Broken pipe"}, {EDOM, "Domain error"}, {ERANGE, "Result too large"},
        {EDEADLK, "Resource deadlock avoided"}, {ENAMETOOLONG, "Filename too long"}, {ENOLCK, "No locks available"},
        {ENOSYS, "Function not implemented"}, {ENOTEMPTY, "Directory not empty"}, {EILSEQ, "Illegal byte sequence"}};
    for (unsigned i = 0; i < sizeof texts / sizeof texts[0]; ++i)
        if (texts[i].code == code) return (char *)texts[i].text;
    return "Unknown error";
}
int *__sys_nerr(void)
{
    static int count = EILSEQ + 1;
    return &count;
}
int strerror_s(char *buffer, size_t size, int code)
{
    const char *text = strerror(code);
    const size_t length = strlen(text);
    if (!buffer || !size) return EINVAL;
    memcpy(buffer, text, length < size ? length : size - 1);
    buffer[length < size ? length : size - 1] = 0;
    return 0;
}
