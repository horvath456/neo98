// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 Win16 module-loader gateway for ordinals 35, 36, and 37.
#pragma once
namespace Win98Win16Module {
using U=unsigned;
enum:U{
 LoadLibrary16=35,
 FreeLibrary16=36,
 GetProcAddress16=37,
 // Win16 LoadLibrary reports failure as an HINSTANCE below 32. Two is the
 // file-not-found value; 21 (ERROR_BAD_FORMAT) says the file exists but is
 // not a Win16 module. SHELL32's CPL loader at depends on the
 // difference: it calls LoadLibrary16 first and only on 21 loads the module
 // as a 32-bit DLL; every other failure ends the applet launch.
 FileNotFound=2,
 BadFormat=21,
};
}
