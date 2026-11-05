// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98WindowText {
using U=unsigned;
// USER: direct children, original sibling order, full DWORD ID.
template<class E> U DialogItem(E&e,U parent,U id){for(U h=e.FirstChild(parent);h;h=e.NextSibling(h))if(e.ControlID(h)==id)return h;return 0;}
// USER: same task or process uses the actual window procedure;
// foreign processes use the default procedure, never a foreign code address.
template<class E> U Dispatch(E&e,U hwnd,U message,U wp,U lp){return e.SameProcess(hwnd)?e.Send(hwnd,message,wp,lp):e.Default(hwnd,message,wp,lp);}
// USER. A valid HWND's output is cleared even at capacity zero.
// Public invalid-HWND fallback4:1550 clears only when the count is nonzero.
template<class E> U Get(E&e,U hwnd,U output,U capacity){
 capacity&=65535;if(!e.Valid(hwnd)){if(capacity)e.Clear(output);return 0;}e.Clear(output);
 return U(int(short(Dispatch(e,hwnd,0xd,capacity,output))));
}
// USER32 joins DX:AX for length, unlike the CWDE copy APIs.
template<class E> U Length(E&e,U hwnd){return e.Valid(hwnd)?Dispatch(e,hwnd,0xe,0,0):0;}
// USER normalizes the high WORD's sign, including zero LRESULT.
template<class E> U Set(E&e,U hwnd,U text){if(!e.Valid(hwnd))return 0;U result=Dispatch(e,hwnd,0xc,0,text);return !(result&0x80000000u);}
// USER. String alias conversion2BBC is a provider
// boundary: the native application owns flat guest addresses already.
template<class E> U GetItem(E&e,U parent,U id,U output,U capacity){U h=e.Valid(parent)?DialogItem(e,parent,id):0;return Get(e,h,output,capacity);}
template<class E> U SetItem(E&e,U parent,U id,U text){U h=e.Valid(parent)?DialogItem(e,parent,id):0;return h?Set(e,h,text):0;}
} // namespace Win98WindowText
