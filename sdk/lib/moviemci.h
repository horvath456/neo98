/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Films through MCI command strings, decoded in the program itself. Windows
   plays an AVI through MCIAVI.DRV and an installed codec, x86 code an ARM64
   process cannot load. MovieMciSendString takes the strings a program sends
   to mciSendString for digital video and serves them with the decoders of
   this library (movie.h: Cinepak in AVI, MPEG-1), a child window for the
   picture and waveOut for the sound; every other command, and a film these
   decoders do not read, goes on to mciSendStringA of WINMM.

       open "C:\FILM.AVI" alias film parent 1234 style child
       set film time format milliseconds (or frames, the default)
       status film length | position | mode | ready
       where film source | destination | window
       put film window at X Y W H put film destination at X Y W H
       window film state show | hide
       play film [from N] [to N] pause, resume, stop
       seek film to N | start | end
       setaudio film volume to 0..1000 close film

   The window procedure of the film runs in the thread that sent `open`; that
   thread must run a message loop while the film plays. `wait` and `notify`
   are not served. docs/ARM64-APPS.md. */
#ifndef NEO98_MOVIEMCI_H
#define NEO98_MOVIEMCI_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
MCIERROR MovieMciSendString(LPCSTR command, LPSTR result, UINT result_size, HWND callback);
BOOL MovieMciGetErrorString(MCIERROR error, LPSTR text, UINT size);
#ifdef __cplusplus
}
#endif
#endif
