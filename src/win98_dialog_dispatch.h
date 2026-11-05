// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98DialogDispatch {
using U=unsigned;
// USER,3153..315C,372A..3736. The message number is the
// original logical Win16 number; native WM_CTLCOLORxxx maps to19h.
template<class E> U Procedure(E&e,U message){
 e.Result(0);
 U result=0;
 if(e.HasProcedure()){result=e.Call();if(!e.Alive())return 0;}
 if(message==0x2e||message==0x2f||message==0x37||message==0x39||message==0x110||(message==0x19&&result))return result;
 if(result)return e.Result();
 return e.Default();
}
}
