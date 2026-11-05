// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_number_format.h"
#include <cstring>
using namespace Win98NumberFormat;
extern "C" U nf_run(U kind,const U *fields,const char *text,const char *decimal,
                    const char *thousand,const char *symbol,const char *sign,
                    char *result,U *error) {
    Format f{fields[0],fields[1],fields[2],fields[3],fields[4],decimal,thousand,symbol};
    if(kind<2)return Valid(f,kind!=0);
    struct E {U &error;void Error(U v){error=v;}} e{*error};
    char storage[1024]={},*out=storage+64;
    U n=kind==2?Number(e,text,f,sign,out):Currency(e,text,f,sign,out);
    if(n)memcpy(result,out,n);
    return n;
}
extern "C" U nf_setting(U flags,U fallback,U maximum,U grouping,
                        const char *over,const char *locale) {
    struct E {
        const char *over,*locale;
        bool Override(U,char *out){if(!over)return false;strcpy(out,over);return true;}
        void Locale(U,char *out){strcpy(out,locale);}
    } e{over,locale};
    return Setting(e,flags,0,fallback,maximum,grouping!=0);
}
