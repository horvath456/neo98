// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
const char *LocaleOverrideName(unsigned field){
    switch(field){
    case 0x0c:return "sList";case 0x0d:return "iMeasure";case 0x0e:return "sDecimal";case 0x0f:return "sThousand";
    case 0x10:return "sGrouping";case 0x11:return "iDigits";case 0x12:return "iLZero";case 0x13:return "sNativeDigits";
    case 0x14:return "sCurrency";case 0x16:return "sMonDecimalSep";case 0x17:return "sMonThousandSep";case 0x18:return "sMonGrouping";
    case 0x19:return "iCurrDigits";case 0x1b:return "iCurrency";case 0x1c:return "iNegCurr";
    case 0x1d:return "sDate";case 0x1e:return "sTime";case 0x1f:return "sShortDate";case 0x20:return "sLongDate";
    case 0x23:return "iTime";case 0x28:return "s1159";case 0x29:return "s2359";
    case 0x50:return "sPositiveSign";case 0x51:return "sNegativeSign";case 0x1005:return "iNegNumber";
    case 0x1006:return "sYearMonth";case 0x1009:return "iCalendarType";case 0x100c:return "iFirstDayOfWeek";
    case 0x100d:return "iFirstWeekOfYear";case 0x1003:return "sTimeFormat";default:return nullptr;
    }
}
}
const char *Application::LocaleValue(unsigned locale,unsigned field){
    if(!Win98Locale::SystemInfo(locale,1))return nullptr;
    if(locale==UserDefaultLocale() && !(field&0x80000000u))if(const char *name=LocaleOverrideName(field&0xffff)){
        unsigned node;bool created=false;const Registry::Value *value=nullptr;
        if(!registry.store.Open(registry.store.roots[1],"Control Panel\\International",false,node,created)&&
           !registry.store.Query(node,name,value)&&value->type==1&&value->size&&value->data[value->size-1]==0)
            return reinterpret_cast<const char*>(value->data);
    }
    // LOCALE_SLANGUAGE: GetLocaleInfoA Windows 98 takes the name out of
    // KERNEL32's own string table, the LANGID as the string number,
    // the table VerLanguageNameA reads as well.
    if((field&0x1fffffffu)==2){
        for(const auto &entry:KernelLanguageNames)if(entry.id==(locale&0xffff))return entry.text;
        return nullptr;
    }
    return Win98Locale::SystemInfo(locale,field);
}
unsigned Application::NlsExtendedApi(ApiName name,const unsigned *a){
    if(!InitializeNls())return 0;
    const auto bad=[&](unsigned error){last_error=error;return 0u;};
    const auto valid_page=[](unsigned page){return page==1252||page==437||page==850||page==866||page==65000||page==65001;};
    const auto locale=[&](unsigned value){return Win98SystemLocale::ResolveLocale(value,system_locale.system,[this]{return UserDefaultLocale();});};
    const auto emit=[&](const char *value,unsigned out,unsigned capacity){
        unsigned size=strlen(value)+1;if(!capacity)return size;
        if(capacity>0x7fffffff||!virtual_memory.Access(out,capacity,true))return bad(87);
        if(capacity<size)return bad(122);return Store(out,value,size)?size:0u;
    };
    switch (name.id)
    {
    API_CASE("ConvertDefaultLocale")return locale(a[0]);
    API_CASE("IsValidCodePage")return valid_page(a[0]);
    API_CASE("IsValidLocale"){
        // KERNEL32: this API does not resolve neutral/default LCIDs.
        // LCID_SUPPORTED accepts an empty registered value; LCID_INSTALLED
        // requires the installed name. also rejects the default
        // value's alias. Cached original locale data precedes the registry.
        if((a[1]!=1&&a[1]!=2)||(a[0]&0xfff00000u))return bad(1004);
        if(a[0]&&a[0]!=9&&a[0]!=0x400&&a[0]!=0x800&&Win98Locale::SystemInfo(a[0],1))return 1;
        char name[16];snprintf(name,sizeof name,"%08x",a[0]);
        const char *paths[]={Win98SystemLocale::MachinePath(),"SYSTEM\\CurrentControlSet\\Control\\Nls\\AddLocales"};
        for(const char *path:paths){
            unsigned node;bool created=false;
            if(registry.store.Open(registry.store.roots[2],path,false,node,created))return bad(1009);
            const Registry::Value *value=nullptr,*fallback=nullptr;
            if(registry.store.Query(node,name,value)||value->type!=1||!value->size||value->data[value->size-1])continue;
            const char *text=reinterpret_cast<const char*>(value->data);
            if(a[1]==1&&!text[0])continue;
            if(!registry.store.Query(node,"",fallback)&&fallback->type==1&&fallback->size&&
               !fallback->data[fallback->size-1]&&!strcmp(text,reinterpret_cast<const char*>(fallback->data)))continue;
            return 1;
        }
        return bad(123);
    }
    API_CASE("GetCPInfoExA"){
        const unsigned page=a[0]==0?system_locale.ansi:a[0]==1?system_locale.oem:a[0];
        if(!valid_page(page)||!virtual_memory.Access(a[2],284,true))return bad(87);
        if(page<60000&&a[1])return bad(1004);
        const unsigned query[]={page,a[2]};if(!NlsApi(318,query))return 0;
        unsigned short replacement=0x3f;Store(a[2]+18,&replacement,2);Write32(a[2]+20,page);
        for(const auto &entry:KernelCodepageNames)if(entry.id==page){Store(a[2]+24,entry.text,strlen(entry.text)+1);return 1;}
        return bad(1814); // Original CODEPAGE resources have no UTF-7/UTF-8 name.
    }
    API_CASE("EnumSystemCodePagesA") API_CASE("EnumSystemLocalesA"){
        if(!a[0])return bad(87);if(a[1]!=1&&a[1]!=2)return bad(1004);
        const unsigned buffer=Allocate(32);if(!buffer)return bad(8);
        const bool pages=API_IS(name,"EnumSystemCodePagesA");const unsigned numbers[]={437,850,866,1252,65000,65001};
        for(unsigned i=0;i<(pages?6u:1u);++i){char value[32];if(pages)snprintf(value,sizeof value,"%u",numbers[i]);else strcpy(value,"00000409");Store(buffer,value,strlen(value)+1);if(!Call(a[0],&buffer,1)||state==Failed||state==Exited)break;}
        Free(buffer);return state!=Failed&&state!=Exited;
    }
    API_CASE("VerLanguageNameA"){
        const char *text="Language Neutral";
        for(const auto &entry:KernelLanguageNames)if(entry.id==(a[0]&65535)){text=entry.text;break;}
        if(!a[2])return 0;if(!virtual_memory.Access(a[1],a[2],true))return bad(87);
        unsigned n=strlen(text);if(n>=a[2])n=a[2]-1;Store(a[1],text,n);unsigned char zero=0;Store(a[1]+n,&zero,1);return n;
    }
    API_CASE("SetLocaleInfoA"){
        if(locale(a[0])!=0x409||!a[2])return bad(87);
        unsigned field=a[1]&~0x40000000u;const char *key=LocaleOverrideName(field);if(!key)return bad(1004);
        const char *value=ReadString(a[2]);const unsigned size=strlen(value);
        if(size>80)return bad(87);
        if(key[0]=='i'){
            unsigned number=0;if(!size||size>2)return bad(87);for(unsigned i=0;i<size;++i){if(value[i]<'0'||value[i]>'9')return bad(87);number=number*10+value[i]-'0';}
            const unsigned maximum=field==0x100c?6:field==0x100d?2:field==0x1c?15:field==0x1b?3:field==0x1005?4:field==0x11||field==0x19?9:1;
            if(number>maximum||(field==0x1009&&number!=1))return bad(87);
        }
        unsigned node;bool created=false;unsigned error=registry.store.Open(registry.store.roots[1],"Control Panel\\International",true,node,created);
        if(!error)error=registry.store.Set(node,key,1,value,size+1);return error?bad(error):1;
    }
    default:
        break;
    }
    const bool date=API_IS(name,"EnumDateFormatsExA"),calendar_enum=API_IS(name,"EnumCalendarInfoA")||API_IS(name,"EnumCalendarInfoExA");
    if(date||calendar_enum){
        if(!a[0]||locale(a[1])!=0x409)return bad(87);
        const bool extended=!API_IS(name,"EnumCalendarInfoA");
        unsigned type=calendar_enum?a[3]:a[2];const char *single=nullptr;const char *const *values=nullptr;unsigned count=0;
        if(date){unsigned error=Win98DateTime::EnumFlags(true,type);if(error)return bad(error);switch(type&11){case 1:values=Win98DateTime::ShortDates;count=7;break;case 2:values=Win98DateTime::LongDates;count=4;break;case 8:values=Win98DateTime::YearMonths;count=1;break;}}
        else{
            if(a[2]!=1&&a[2]!=~0u)return bad(87);
            type&=~0x40000000u;if(!type||type>=48)return bad(1004);
            if(type==1)single="1";else if(type==2)single="Gregorian Calendar";
            else if(type==5){values=Win98DateTime::ShortDates;count=7;}else if(type==6){values=Win98DateTime::LongDates;count=4;}
            else if(type>=7&&type<=13)single=LocaleValue(0x409,0x2a+type-7);
            else if(type>=14&&type<=20)single=LocaleValue(0x409,0x31+type-14);
            else if(type>=21&&type<=32)single=LocaleValue(0x409,0x38+type-21);
            else if(type>=34&&type<=45)single=LocaleValue(0x409,0x44+type-34);
            else if(type==47){values=Win98DateTime::YearMonths;count=1;}
        }
        if(single){values=&single;count=1;}
        unsigned buffer=Allocate(256);if(!buffer)return bad(8);
        for(unsigned i=0;i<count;++i){Store(buffer,values[i],strlen(values[i])+1);const unsigned args[]={buffer,1};if(!Call(a[0],args,extended?2:1)||state==Failed||state==Exited)break;}
        Free(buffer);return state!=Failed&&state!=Exited;
    }
    if(API_IS(name,"GetCalendarInfoA")||API_IS(name,"SetCalendarInfoA")){
        const bool set=name[0]=='S';
        const char *maximum=Win98Locale::CalendarYearMaximum(a[1]);
        if(!Win98Locale::LocaleCalendar(locale(a[0]),false)||!maximum)return bad(87);
        if((a[2]&0x1fffffff)!=0x30||(set&&(a[2]&~0x40000030u)))return bad(1004);
        unsigned node;bool created=false;const char *path="Control Panel\\International\\Calendars\\TwoDigitYearMax";
        unsigned error=registry.store.Open(registry.store.roots[1],path,set,node,created);
        char key[16];snprintf(key,sizeof key,"%u",a[1]);
        if(set){
            if(!a[3])return bad(87);const char *value=ReadString(a[3]);unsigned n=strlen(value);
            // Original Windows 98 validates length and the two-character 99 case.
            if(n<2||n>4||(n==2&&value[0]!='9'&&value[1]!='9'))return bad(87);
            if(!error)error=registry.store.Set(node,key,1,value,n+1);return error?bad(error):1;
        }
        const Registry::Value *entry=nullptr;const char *value=maximum;
        if(!(a[2]&0x80000000)&&!error&&!registry.store.Query(node,key,entry)&&entry->type==1&&entry->size&&entry->data[entry->size-1]==0)value=reinterpret_cast<const char*>(entry->data);
        if(a[2]&0x20000000){if(a[3]||a[4]||!virtual_memory.Access(a[5],4,true))return bad(87);Write32(a[5],Win98SystemLocale::ParseDecimal(value));return 4;}
        if(a[5])return bad(87);return emit(value,a[3],a[4]);
    }
    Fail(name);return 0;
}
