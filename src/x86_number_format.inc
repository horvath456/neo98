// SPDX-License-Identifier: GPL-2.0-or-later
// Native locale/registry/memory providers for original number-format decisions.
#include "win98_number_format.h"
namespace X86 {
unsigned Application::NumberFormatApi(ApiName name,const unsigned *a) {
    using namespace Win98NumberFormat;
    const bool currency=API_IS(name,"GetCurrencyFormatA");
    // Both public original entries probe value[0] and optional format[0,31].
    auto span=[&](unsigned p,unsigned size,bool write=false) {
        if(!p||!virtual_memory.Access(p,size,write)){last_error=87;return false;}
        return true;
    };
    if(!span(a[2],1)||(a[3]&&(!span(a[3],1)||!span(a[3]+31,1))))return 0;
    if(!InitializeNls())return 0;
    U lcid=Win98SystemLocale::ResolveLocale(a[0],system_locale.system,[this]{return UserDefaultLocale();});
    struct Errors {U &value;void Error(U v){value=v;}} errors{last_error};
    if(!Request(errors,Win98Locale::SystemInfo(lcid,1)!=nullptr,a[1],a[2],a[3],a[4],a[5]))return 0;
    auto text=[&](U address,char *out,U capacity) {
        if(!address){last_error=87;return false;}
        for(U n=0;n<capacity;++n) {
            if(!span(address+n,1)||!Read(address+n,out+n,1))return false;
            if(!out[n])return true;
        }
        last_error=87;return false;
    };
    struct LocaleProvider {
        Application &app;U locale;
        void Error(U value){app.last_error=value;}
        bool Override(U field,char *out) {
            if(locale!=app.UserDefaultLocale())return false;
            const struct {U type;const char *name;} names[]={
                {0x11,"iDigits"},{0x12,"iLZero"},{0x1010,"iNegNumber"},
                {0x10,"sGrouping"},{0xe,"sDecimal"},{0xf,"sThousand"},
                {0x19,"iCurrDigits"},{0x1b,"iCurrency"},{0x1c,"iNegCurr"},
                {0x18,"sMonGrouping"},{0x16,"sMonDecimalSep"},{0x17,"sMonThousandSep"},
                {0x14,"sCurrency"},{0x51,"sNegativeSign"}};
            const char *name=nullptr;for(const auto &entry:names)if(entry.type==field)name=entry.name;
            if(!name)return false;
            U key=0,node=0;bool made=false;
            if(app.registry.Open(0x80000001,"Control Panel\\International",false,key,made))return false;
            const Registry::Value *value=nullptr;
            bool good=!app.registry.Key(key,node)&&!app.registry.store.Query(node,name,value)&&
                value->type==1&&value->size&&value->size<=128;
            if(good) {
                memcpy(out,value->data,value->size);
                good=out[value->size-1]==0;
            }
            app.registry.Close(key);return good;
        }
        void Locale(U field,char *out) {
            const char *s=Win98Locale::SystemInfo(locale,field);
            if(!s||strlen(s)>=128){out[0]=0;Error(87);return;}
            strcpy(out,s);
        }
    } e{*this,lcid};
    Format f;char decimal[128]{},thousand[128]{},symbol[128]{},sign[128]{};
    if(a[3]) {
        U fields[8]{};
        const U bytes=currency?32:24;
        if(!span(a[3],bytes)||!Read(a[3],fields,bytes))return 0;
        f.digits=fields[0];f.leading=fields[1];f.grouping=fields[2];f.negative=fields[5];
        f.positive=currency?fields[6]:0;
        if(!text(fields[3],decimal,4)||!text(fields[4],thousand,4)||
           (currency&&!text(fields[7],symbol,7)))return 0;
    } else {
        f.digits=Setting(e,a[1],currency?0x19:0x11,2,currency?99:9);
        f.leading=Setting(e,a[1],0x12,1,1);
        f.negative=Setting(e,a[1],currency?0x1c:0x1010,1,currency?15:4);
        f.grouping=Setting(e,a[1],currency?0x18:0x10,3,9,true);
        if(currency)f.positive=Setting(e,a[1],0x1b,0,3);
        Separator(e,a[1],currency?0x16:0xe,4,decimal);
        Separator(e,a[1],currency?0x17:0xf,4,thousand);
        if(currency)Separator(e,a[1],0x14,7,symbol);
    }
    f.decimal=decimal;f.thousand=thousand;f.currency=symbol;
    if(a[3]&&!Valid(f,currency)){last_error=87;return 0;}
    // Native registry and the existing original en-US table replace the NLS
    // descriptor/override cache. A changed override is observed on this call.
    Separator(e,a[1],0x51,5,sign);
    char value[32768];if(!text(a[2],value,sizeof value))return 0;
    // More room than the original fixed stack scratch; preserve its decimal
    // length guard while avoiding its possible oversized-locale stack writes.
    char storage[1024]{},*result=storage+64;
    U count=currency?Currency(e,value,f,sign,result):Number(e,value,f,sign,result);
    if(!count||!a[5])return count;
    if(count>a[5]){last_error=122;return 0;}
    return span(a[4],count,true)&&Store(a[4],result,count)?count:0;
}
}
