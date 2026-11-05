// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
/* What the generated declarations need before the first type. */
#include <stddef.h> /* wchar_t, which is WCHAR */
/* The system below is Windows 98: the version a program asks its headers
   for, unless it names another. */
#ifndef WINVER
#define WINVER 0x0410
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0400
#endif
#define _WIN32_WINNT_NT4 0x0400
#define _WIN32_WINNT_WIN2K 0x0500
#define _WIN32_WINNT_WINXP 0x0501
#define _WIN32_WINNT_VISTA 0x0600
#define _WIN32_WINNT_WIN7 0x0601
#define _WIN32_WINNT_WIN8 0x0602
#define _WIN32_WINNT_WIN10 0x0A00
/* winapifamily.h: a program here is a desktop program. */
#define WINAPI_PARTITION_DESKTOP 0x00000001
#define WINAPI_PARTITION_APP 0x00000002
#define WINAPI_FAMILY_PARTITION(partition) ((partition) & WINAPI_PARTITION_DESKTOP)
/* A keyword of the Microsoft compiler; code for Windows uses it everywhere. */
#if !defined(_MSC_VER) && !defined(__int64)
#define __int64 long long
#endif
#define WINAPI __stdcall
#define CALLBACK __stdcall
#define APIENTRY __stdcall
#define CONST const
#define VOID void
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif
#define TRUE 1
#define FALSE 0
