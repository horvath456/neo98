/* SPDX-License-Identifier: GPL-2.0-or-later AND MIT
 * Copyright (C) 2026 Andreas Horvath
 * Project adaptations; musl portions retain their upstream notices in LICENSES/musl-COPYRIGHT.txt.
 */
/* Windows wchar_t is one 16-bit code unit. A C multibyte conversion emits
   one wchar_t per character, so supplementary Unicode scalars cannot be
   represented. Limit musl's UTF-8 lead-byte bound to three-byte sequences;
   its unchanged decoder then reports EILSEQ, including in counting mode.
   UTF-16 surrogate pairs belong to MultiByteToWideChar, not mbrtowc. */
#undef SB
#define SB 0xefu
