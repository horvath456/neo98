/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_shell_control.h"
#include <initializer_list>
using U=unsigned;static U(*readbyte)(U);static void(*writebyte)(U,U);static U(*provider)(U,const U*,U);
struct E{U sp=0x1000f000;struct Scratch{E&e;U address,old;Scratch(E&v,U n):e(v),old(v.sp){address=e.sp=(e.sp-n-32)&~15u;}~Scratch(){e.sp=old;}};
 U R8(U p){return readbyte(p);}U R16(U p){return R8(p)|(R8(p+1)<<8);}U R32(U p){return R16(p)|(R16(p+2)<<16);}void W8(U p,U v){writebyte(p,v);}void W16(U p,U v){W8(p,v&255);W8(p+1,v>>8);}void W32(U p,U v){W16(p,v&65535);W16(p+2,v>>16);}
 U P(U op,std::initializer_list<U>a){return provider(op,a.begin(),a.size());}
 U SpanDelimiter(U p){return P(1,{p});}U FindComma(U p){return P(2,{p});}void CopyString(U a,U b,U c){P(3,{a,b,c});}U Integer(U p){return P(4,{p});}U Lead(U c){return P(5,{c});}U Next(U p){return P(6,{p});}U Previous(U a,U b){return P(7,{a,b});}
 U Applet(U a,U b,U c,U d,U e,U f,U g){return P(8,{a,b,c,d,e,f,g});}U LoadIcon(U a,U b){return P(9,{a,b});}void DestroyIcon(U a){P(10,{a});}void LoadString(U a,U b,U c,U d){P(11,{a,b,c,d});}U SetString(U a,U b){return P(12,{a,b});}void WideInquiry(U a){P(13,{a});}U Procedure(U a,U b){return P(14,{a,b});}
 U ArrayCreate(U a,U b){return P(15,{a,b});}U ArrayGet(U a,U b){return P(16,{a,b});}U ArrayInsert(U a,U b,U c){return P(17,{a,b,c});}void ArrayClear(U a){P(18,{a});}void ArrayDestroy(U a){P(19,{a});}void Unload(U a,U b){P(20,{a,b});}void Close(U a){P(21,{a});}void ArrayDelete(U a,U b){P(22,{a,b});}
 U LoadLegacy(U a){return P(23,{a});}U ProcessId(){return P(24,{});}U OpenSelf(U a){return P(25,{a});}U LoadLibrary(U a){return P(26,{a});}void ModuleName(U a,U b,U c,U d){P(27,{a,b,c,d});}int Compare(U a,U b){return int(P(28,{a,b}));}U SetupText(){return 0x108000;}
 U AddAtom(U a){return P(29,{a});}void DeleteAtom(U a){P(30,{a});}void ClassName(U a,U b,U c){P(31,{a,b,c});}int CompareExact(U a,U b){return int(P(32,{a,b}));}U GetProp(U a,U b){return P(33,{a,b});}U NameText(){return 0x108010;}U FlagsText(){return 0x108020;}
 void OpenPanel(U a,U b,U c){P(34,{a,b,c});}U Resolve(U a){return P(35,{a});}U FindExisting(Win98ShellControl::State&s,U a,U b){return P(36,{s.name_atom,s.flags_atom,a,b});}U LastPopup(U a){return P(37,{a});}U IsWindow(U a){return P(38,{a});}void Foreground(U a){P(39,{a});}U SetProp(U a,U b,U c){return P(40,{a,b,c});}void NotifyOwner(U a,U b,U c){P(41,{a,b,c});}void RemoveProp(U a,U b){P(42,{a,b});}
};
extern "C" void callbacks(U(*r)(U),void(*w)(U,U),U(*p)(U,const U*,U)){readbyte=r;writebyte=w;provider=p;}
extern "C" U run(U op,const U*a,Win98ShellControl::State*s){E e;using namespace Win98ShellControl;switch(op){case 0:return Token(e,a[0],a[1],a[2],a[3]);case 1:Parse(e,a[0],a[1],a[2]);return 0;case 2:Normalize(e,a[0]);return 0;case 3:return Inquire(e,a[0],a[1]);case 4:return Initialize(e,a[0]);case 5:Destroy(e,a[0]);return 0;case 6:return Load(e,*s,a[0]);case 7:return Select(e,*s,a[0],a[1],a[2]);case 8:return PrepareAtoms(e,a[0],a[1],a[2]);case 9:FreeAtoms(e,a[0]);return 0;case 10:return Run(e,*s,a[0],a[1],a[2],a[3],a[4]);case 11:return ReleaseIndex(e,*s,a[0]);case 12:return Match(e,*s,a[0],a[1],a[2]);}return 0;}
