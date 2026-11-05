/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The PE TLS directory that a Windows CRT would supply. There is no CRT here,
   so the fixtures build IMAGE_TLS_DIRECTORY32 themselves. lld-link finds it
   through the x86 symbol __tls_used, which is _tls_used in C source. The
   compiler emits every __declspec(thread) object into.tls$ and reads the
   vector as fs:__tls_array; that offset is an absolute symbol, exactly as the
   Windows CRT ships it..tls sorts before.tls$ and.tls$ZZZ, so the two
   markers delimit the template..CRT$XLA and.CRT$XLZ delimit the callback
   list; callbacks live in.CRT$XLB between them. */
#pragma once
typedef unsigned U;
__asm__(".globl __tls_array\n.set __tls_array, 0x2c\n");
typedef void(__stdcall *TlsCallback)(void *, U, void *);
__attribute__((section(".tls"))) char tls_template_start = 0;
__attribute__((section(".tls$ZZZ"))) char tls_template_end = 0;
__attribute__((section(".CRT$XLA"))) TlsCallback tls_callback_first = 0;
__attribute__((section(".CRT$XLZ"))) TlsCallback tls_callback_last = 0;
U _tls_index = 0;
__attribute__((section(".rdata$T"))) const U _tls_used[6] = {
    (U)&tls_template_start, (U)&tls_template_end, (U)&_tls_index,
    (U)(&tls_callback_first + 1), 0, 0};
