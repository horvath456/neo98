// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_user_characters.h"
using U = unsigned;
using Provider = U (*)(U, U, U);
extern "C" U user_characters(Provider callback, U operation, const U *a) {
    struct E {
        Provider call;
        bool Type(U byte,U &type) { type=call(0,byte,0);return call(1,0,0); }
        bool BadRead(U p) { return call(2,p,0); }
        U Read8(U p) { return call(3,p,0); }
        bool Lead(U page,U byte) { return call(4,page,byte); }
        void Error(U error) { call(5,error,0); }
    } e{callback};
    if(operation<4)return Win98UserCharacters::Predicate(e,operation,a[0]);
    if(operation==4)return Win98UserCharacters::Next(e,a[0],false,0);
    if(operation==5)return Win98UserCharacters::Previous(e,a[0],a[1],false,0);
    if(operation==6)return Win98UserCharacters::Next(e,a[1],true,a[0]);
    return Win98UserCharacters::Previous(e,a[1],a[2],true,a[0]);
}
