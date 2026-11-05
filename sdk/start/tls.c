/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* The TLS directory of an ARM64 application: __declspec(thread) and
   thread_local. The compiler puts such variables into.tls$ and reaches them
   through the TEB (x18+58h) and _tls_index; the linker takes _tls_used as
   the directory of the image, and the system gives every thread its copy of
   what lies between _tls_start and _tls_end. This file is linked only into a
   program that has such a variable. docs/ARM64-APPS.md. */
#include <windows.h>

unsigned long _tls_index = 0;

#pragma section(".tls", read, write)
#pragma section(".tls$ZZZ", read, write)
#pragma section(".CRT$XLA", read)
#pragma section(".CRT$XLZ", read)
#pragma section(".rdata$T", read)
__declspec(allocate(".tls")) char _tls_start = 0;
__declspec(allocate(".tls$ZZZ")) char _tls_end = 0;
/* The callbacks of the directory: none. The list is here because the format
   has it. */
typedef void (__stdcall *TlsCallback)(void *, unsigned long, void *);
__declspec(allocate(".CRT$XLA")) const TlsCallback __xl_a = 0;
__declspec(allocate(".CRT$XLZ")) const TlsCallback __xl_z = 0;

/* IMAGE_TLS_DIRECTORY64. */
struct TlsDirectory
{
    unsigned long long StartAddressOfRawData, EndAddressOfRawData, AddressOfIndex, AddressOfCallBacks;
    unsigned long SizeOfZeroFill, Characteristics;
};
__declspec(allocate(".rdata$T")) const struct TlsDirectory _tls_used = {
    (unsigned long long)&_tls_start, (unsigned long long)&_tls_end, (unsigned long long)&_tls_index,
    (unsigned long long)(&__xl_a + 1), 0, 0};
