// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// USER, checked by executing the unchanged initializer.
// Procedure tokens are native dispatch providers, not original x86 addresses.
namespace Win98ClassDefaults {
struct Descriptor {const char *name;unsigned style,extra,background,procedure;};
inline constexpr Descriptor Records[]={
 {"#32768",0x08000808,2,5,0x71000020},
 // The class procedure of BUTTON has a token of its own. A program that
 // superclasses BUTTON (Delphi's TButton, VB command buttons) reads it with
 // GetClassInfo and forwards to it through CallWindowProc; with the token of
 // the default window procedure nothing drew such a button.
 {"Button",0x0800008b,8,0,0x71000030},
 // STATIC as well: Delphi's TStaticText superclasses it and forwards every
 // message; with the token of the dialog procedure its text never appeared
 // (MahJong 2000's counter).
 {"Static",0x08000088,6,0,0x71000031},
 {"#32770",0x08002808,30,0,0x71000020},
 {"Edit",0x08000088,6,0,0x71000021},
 {"ListBox",0x08000088,2,0,0x71000004},
 // The next three registrations are part of the same USER initializer.
 {"ScrollBar",0x0800008b,20,0,0x7100000a},
 {"ComboLBox",0x08000808,2,0,0x71000004},
 {"ComboBox",0x0800008b,2,0,0x71000006},
 // USER registers MDIClient: no CS_ bit of its own, 22 window extra
 // bytes, the arrow cursor and COLOR_APPWORKSPACE+1 as the background.
 {"MDIClient",0x08000000,22,13,0x71000005}
};
}
