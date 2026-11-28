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
    case 0x50:return "sPositiveSign";case 0x51:return "sNegativeSign";case 0x1010:return "iNegNumber";
    case 0x1006:return "sYearMonth";case 0x1009:return "iCalendarType";case 0x100c:return "iFirstDayOfWeek";
    case 0x100d:return "iFirstWeekOfYear";case 0x1003:return "sTimeFormat";
    // Not set by a program: SetLocaleInfoA derives them from the short date
    // and from the time format and stores them beside those.
    case 0x21:return "iDate";case 0x25:return "iTLZero";case 0x1005:return "iTimePrefix";default:return nullptr;
    }
}
// The first character of `set` in a picture of a date or a time, outside
// text in single quotes; the end of the picture when there is none.
const char *PictureField(const char *picture,const char *set){
    for(bool quoted=false;*picture;++picture){
        if(*picture=='\''){quoted=!quoted;continue;}
        if(!quoted&&strchr(set,*picture))return picture;
    }
    return picture;
}
// KERNEL32's own search in a picture: the first character that is one of
// `set`, or null. It does not look at quotes.
const char *PictureAny(const char *text,const char *set){
    for(;*text;++text)if(strchr(set,*text))return text;
    return nullptr;
}
// KERNEL32, LOCALE_STIME: the time format with a new separator. What stands
// between two of the fields H, h, m and s becomes the separator; what leads
// to the marker t or to a quote stays.
void TimeWithSeparator(const char *picture,const char *separator,char *out,unsigned capacity){
    unsigned used=0;const auto put=[&](const char *from,const char *to){for(;from<to&&used+1<capacity;++from)out[used++]=*from;};
    for(const char *at=picture;;){
        const char *field=PictureAny(at,"Hhms");
        if(!field){put(at,at+strlen(at));break;}
        put(at,field+1);at=field+1;
        while(*at&&strchr("Hhms",*at))put(at,at+1),++at;
        if(!*at)break;
        const char *next=PictureAny(at,"Hhmst'");
        if(!next){put(at,at+strlen(at));break;}
        if(*next=='t'||*next=='\''){put(at,next+1);at=next+1;continue;}
        at=next;put(separator,separator+strlen(separator));
    }
    out[used]=0;
}
// KERNEL32, LOCALE_SDATE: the short date with a new separator. The first
// text between two of the fields d, M and y becomes the separator, and every
// later one that equals that first text; another one stays.
void DateWithSeparator(const char *picture,const char *separator,char *out,unsigned capacity){
    unsigned used=0;const auto put=[&](const char *from,const char *to){for(;from<to&&used+1<capacity;++from)out[used++]=*from;};
    const char *first=nullptr;unsigned first_size=0;
    for(const char *at=picture;;){
        const char *field=PictureAny(at,"dMy");
        if(!field){put(at,at+strlen(at));break;}
        put(at,field+1);at=field+1;
        while(*at&&strchr("dMy",*at))put(at,at+1),++at;
        if(!*at)break;
        const char *next=PictureAny(at,"dMy");
        if(!next){put(at,at+strlen(at));break;}
        const unsigned size=unsigned(next-at);
        if(!first){first=at;first_size=size;put(separator,separator+strlen(separator));}
        else if(size==first_size&&!memcmp(at,first,size))put(separator,separator+strlen(separator));
        else put(at,next);
        at=next;
    }
    out[used]=0;
}
}
// What the user has set for a value of the user's locale, or null: the
// string under HKCU\Control Panel\International.
const char *Application::LocaleOverride(unsigned locale,unsigned field){
    if(locale!=UserDefaultLocale()||(field&0x80000000u))return nullptr;
    const char *name=LocaleOverrideName(field&0xffff);if(!name)return nullptr;
    unsigned node;bool created=false;const Registry::Value *value=nullptr;
    if(!registry.store.Open(registry.store.roots[1],"Control Panel\\International",false,node,created)&&
       !registry.store.Query(node,name,value)&&value->type==1&&value->size&&value->data[value->size-1]==0)
        return reinterpret_cast<const char*>(value->data);
    return nullptr;
}
const char *Application::LocaleValue(unsigned locale,unsigned field){
    // LOCALE_SLANGUAGE and LOCALE_SCOUNTRY: KERNEL32 takes the names out of
    // its own resources, the language out of its string table (the LANGID as
    // the string number, the table VerLanguageNameA reads as well), the
    // country out of its RCDATA named by the LANGID. They come for every
    // locale LOCALE.NLS has, also when its code page has no table and every
    // other value is missing (Hebrew in Windows 98, Russian in Windows 95).
    const unsigned type=field&0x1fffffffu;
    if(type==2||type==6){
        const unsigned id=!locale||locale==9||locale==0x400||locale==0x800?0x409:locale;
        if(std::find(OriginalLocaleIds.begin(),OriginalLocaleIds.end(),id)==OriginalLocaleIds.end())return nullptr;
        for(const auto &entry:type==2?OriginalLanguageNames:OriginalCountryNames)if(entry.id==(id&0xffff))return entry.text;
        return nullptr;
    }
    if(!Win98Locale::SystemInfo(locale,1))return nullptr;
    if(const char *set=LocaleOverride(locale,field))return set;
    return Win98Locale::SystemInfo(locale,field);
}
unsigned Application::NlsExtendedApi(ApiName name,const unsigned *a){
    if(!InitializeNls())return 0;
    const auto bad=[&](unsigned error){last_error=error;return 0u;};
    const auto valid_page=[this](unsigned page){return ValidCodePage(page);};
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
        // KERNEL32: the flags are LCID_INSTALLED (1) or LCID_SUPPORTED (2),
        // and the high bits of the LCID are clear; neutral and default LCIDs
        // are not resolved. A locale LOCALE.NLS has is supported, and it is
        // installed when the table of its ANSI code page loads (Hebrew is
        // supported, not installed, in Windows 98). Any other locale is asked
        // of the registry, Nls\Locale and then Nls\AddLocales: the key needs
        // a default value, the locale's value must differ from it, and an
        // installed one has a name as its data.
        if((a[1]!=1&&a[1]!=2)||(a[0]&0xfff00000u))return bad(1004);
        if(std::find(OriginalLocaleIds.begin(),OriginalLocaleIds.end(),a[0])!=OriginalLocaleIds.end()){
            if(a[1]==2)return 1;
            const char *page=Win98Locale::SystemInfo(a[0],0x1004);
            return page&&ValidCodePage(Win98SystemLocale::ParseDecimal(page))?1u:0u;
        }
        char name[16];snprintf(name,sizeof name,"%08x",a[0]);
        const char *paths[]={Win98SystemLocale::MachinePath(),"SYSTEM\\CurrentControlSet\\Control\\Nls\\AddLocales"};
        for(const char *path:paths){
            unsigned node;bool created=false;
            if(registry.store.Open(registry.store.roots[2],path,false,node,created))return bad(1009);
            const Registry::Value *value=nullptr,*fallback=nullptr;
            const auto text=[](const Registry::Value *v){return v->type==1&&v->size&&!v->data[v->size-1]?reinterpret_cast<const char*>(v->data):nullptr;};
            if(registry.store.Query(node,"",fallback)||!text(fallback))continue;
            if(registry.store.Query(node,name,value)||!text(value))continue;
            if(!strcmp(text(value),text(fallback))||(a[1]==1&&!text(value)[0]))continue;
            return 1;
        }
        return 0;
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
    API_CASE("EnumSystemLocalesA"){
        // KERNEL32 reads the locales out of the registry, not out of
        // LOCALE.NLS: the values of Nls\Locale and then those of
        // Nls\AddLocales, in the order the registry keeps them. A value
        // counts when its name is eight hexadecimal digits; LCID_INSTALLED
        // (1) also wants a name of the locale as its data, LCID_SUPPORTED
        // (2) takes those without one too. The callback gets the name as
        // the registry spells it, and an answer other than TRUE ends the
        // values of that key, not those of the next. Setup writes the
        // locales of the installed code pages with their names and the
        // others empty, so Regional Settings, which lists the installed
        // ones, offers every locale of the installed language group.
        if(!a[0])return bad(87);if(a[1]!=1&&a[1]!=2)return bad(1004);
        const bool installed=a[1]==1;
        const char *paths[]={Win98SystemLocale::MachinePath(),"SYSTEM\\CurrentControlSet\\Control\\Nls\\AddLocales"};
        unsigned keys[2];
        for(unsigned k=0;k<2;++k){bool created=false;if(registry.store.Open(registry.store.roots[2],paths[k],false,keys[k],created))return bad(1009);}
        // RegEnumValueA into buffers of 256 bytes for the name and the data.
        const unsigned buffer=Allocate(256);if(!buffer)return bad(8);
        for(const unsigned key:keys)for(unsigned index=0;;++index){
            const Registry::Value *value=nullptr;
            if(registry.store.EnumValue(key,index,value))break;
            const size_t length=strlen(value->name);
            if(length>=256||value->size>256){Free(buffer);return bad(1009);}
            bool lcid=length==8;
            for(size_t i=0;lcid&&i<length;++i){const char c=char(value->name[i]|32);lcid=(c>='0'&&c<='9')||(c>='a'&&c<='f');}
            if(!lcid||(installed&&(!value->size||!value->data[0])))continue;
            Store(buffer,value->name,9);
            const unsigned answer=Call(a[0],&buffer,1);
            if(state==Failed||state==Exited){Free(buffer);return 0;}
            if(answer!=1)break;
        }
        Free(buffer);return 1;
    }
    API_CASE("EnumSystemCodePagesA"){
        // KERNEL32 reads the code pages out of the registry: the values of
        // Nls\CodePage in the order the registry keeps them, each whose name
        // has at most four characters and begins with a digit (ACP, OEMCP and
        // the pages of five digits stay out). CP_INSTALLED (1) wants the name
        // of a file as the data, CP_SUPPORTED (2) takes the empty ones too.
        // An answer other than TRUE from the callback ends the enumeration.
        if(!a[0])return bad(87);if(a[1]!=1&&a[1]!=2)return bad(1004);
        const bool installed=a[1]==1;
        unsigned key;bool created=false;
        if(registry.store.Open(registry.store.roots[2],"SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage",false,key,created))return bad(1009);
        // RegEnumValueA into buffers of 256 bytes for the name and the data.
        const unsigned buffer=Allocate(256);if(!buffer)return bad(8);
        for(unsigned index=0;;++index){
            const Registry::Value *value=nullptr;
            if(registry.store.EnumValue(key,index,value))break;
            const size_t length=strlen(value->name);
            if(length>=256||value->size>256){Free(buffer);return bad(1009);}
            if(length>=5||value->name[0]<'0'||value->name[0]>'9'||(installed&&(!value->size||!value->data[0])))continue;
            Store(buffer,value->name,length+1);
            const unsigned answer=Call(a[0],&buffer,1);
            if(state==Failed||state==Exited){Free(buffer);return 0;}
            if(answer!=1)break;
        }
        Free(buffer);return 1;
    }
    API_CASE("VerLanguageNameA"){
        const char *text="Language Neutral";
        for(const auto &entry:OriginalLanguageNames)if(entry.id==(a[0]&65535)){text=entry.text;break;}
        if(!a[2])return 0;if(!virtual_memory.Access(a[1],a[2],true))return bad(87);
        unsigned n=strlen(text);if(n>=a[2])n=a[2]-1;Store(a[1],text,n);unsigned char zero=0;Store(a[1]+n,&zero,1);return n;
    }
    API_CASE("SetLocaleInfoA"){
        if(locale(a[0])!=UserDefaultLocale()||!a[2])return bad(87);
        unsigned field=a[1]&~0x40000000u;const char *key=LocaleOverrideName(field);if(!key)return bad(1004);
        // KERNEL32 has no branch for the three derived values.
        if(field==0x21||field==0x25||field==0x1005)return bad(1004);
        const char *given=ReadString(a[2]);const unsigned size=strlen(given);
        if(size>80)return bad(87);
        // A copy: the registry and the locale are read again below.
        char value[81];memcpy(value,given,size+1);
        // KERNEL32: a separator of the date or the time has at most three
        // characters, no digit and none of the letters of its picture.
        if(field==0x1d||field==0x1e){
            if(size>3||PictureAny(value,"0123456789")||PictureAny(value,field==0x1d?"dMy":"Hhmst"))return bad(87);
        }
        if(key[0]=='i'){
            unsigned number=0;if(!size||size>2)return bad(87);for(unsigned i=0;i<size;++i){if(value[i]<'0'||value[i]>'9')return bad(87);number=number*10+value[i]-'0';}
            const unsigned maximum=field==0x100c?6:field==0x100d?2:field==0x1c?15:field==0x1b?3:field==0x1010?4:field==0x11||field==0x19?9:1;
            if(number>maximum||(field==0x1009&&number!=1))return bad(87);
        }
        unsigned node;bool created=false;unsigned error=registry.store.Open(registry.store.roots[1],"Control Panel\\International",true,node,created);
        // KERNEL32 compares every value it would write, the one given and
        // those that follow from it, with what the locale itself has for
        // that type (GetLocaleInfoA with LOCALE_NOUSEROVERRIDE, then
        // CompareStringA without flags), and removes the stored value when
        // they are equal instead of storing it.
        const unsigned lcid=UserDefaultLocale();
        const auto keep=[&](const char *name,unsigned type,const char *text){
            if(error)return;
            const unsigned length=unsigned(strlen(text));
            if(const char *own=Win98Locale::SystemInfo(lcid,type)){
                const unsigned mine=Allocate(unsigned(strlen(own))+length+2);
                if(mine){
                    Store(mine,own,unsigned(strlen(own))+1);Store(mine+unsigned(strlen(own))+1,text,length+1);
                    const unsigned compare[]={lcid,0,mine,~0u,mine+unsigned(strlen(own))+1,~0u};
                    const bool same=CompareStringApi(compare)==2;Free(mine);
                    if(same){registry.store.DeleteValue(node,name);return;}
                }
            }
            error=registry.store.Set(node,name,1,text,length+1);
        };
        // KERNEL32: LOCALE_ITIME rewrites the time format, every H to h for
        // 0 and every h to H for 1, and keeps the number itself.
        if(field==0x23&&!error){
            char picture[256]={};
            if(const char *now=LocaleValue(lcid,0x1003))snprintf(picture,sizeof picture,"%s",now);
            for(char *at=picture;*at;++at){if(value[0]=='0'&&*at=='H')*at='h';else if(value[0]=='1'&&*at=='h')*at='H';}
            keep(key,field,value);keep("sTimeFormat",0x1003,picture);
            return error?bad(error):1;
        }
        keep(key,field,value);
        // KERNEL32 keeps the values that follow from a picture beside it.
        // LOCALE_STIMEFORMAT: "iTime" (1 for the 24 hours of H), "iTLZero"
        // (1 for two hour digits) and "iTimePrefix" (1 when the AM/PM
        // marker comes first). LOCALE_SSHORTDATE: "iDate" (0 month, 1 day,
        // 2 year first) and "sDate", what stands between the first two
        // fields. The Date/Time page and 16-bit programs read these.
        const auto derive=[&](const char *name,unsigned type,const char *text){keep(name,type,text);};
        if(field==0x1003){
            const char *hour=PictureField(value,"Hh");
            derive("iTime",0x23,*hour=='H'?"1":"0");
            derive("iTLZero",0x25,*hour&&hour[1]==*hour?"1":"0");
            derive("iTimePrefix",0x1005,*PictureField(value,"Hhmst")=='t'?"1":"0");
            // "sTime": what stands between the first two of the fields H, h,
            // m and s that follow each other without the marker t between
            // them; empty when there is no such pair.
            char separator[81]={};
            for(const char *at=value;;){
                const char *first=PictureAny(at,"Hhms");if(!first)break;
                at=first+1;while(*at&&strchr("Hhms",*at))++at;
                const char *next=PictureAny(at,"Hhmst");if(!next)break;
                if(*next=='t'){at=next+1;continue;}
                memcpy(separator,at,size_t(next-at));break;
            }
            derive("sTime",0x1e,separator);
        }
        // KERNEL32: a new separator rewrites the picture it belongs to, the
        // time format for LOCALE_STIME and the short date for LOCALE_SDATE.
        // Regional Settings sets only the separator when only that changed.
        if(field==0x1e||field==0x1d){
            const unsigned of=field==0x1e?0x1003u:0x1fu;
            char picture[256]={},rewritten[256];
            if(const char *now=LocaleValue(lcid,of))snprintf(picture,sizeof picture,"%s",now);
            if(field==0x1e)TimeWithSeparator(picture,value,rewritten,sizeof rewritten);
            else DateWithSeparator(picture,value,rewritten,sizeof rewritten);
            if(strlen(rewritten)<=80)derive(field==0x1e?"sTimeFormat":"sShortDate",field==0x1e?0x1003u:0x1fu,rewritten);
        }
        if(field==0x1f){
            const char *first=PictureField(value,"dMy");
            derive("iDate",0x21,*first=='d'?"1":*first=='y'?"2":"0");
            const char *after=first;while(*after&&*after==*first)++after;
            const char *next=PictureField(after,"dMy");
            char separator[8]={};unsigned used=0;
            for(const char *p=after;p<next&&used+1<sizeof separator;++p)if(*p!='\'')separator[used++]=*p;
            if(*first&&*next&&used)derive("sDate",0x1d,separator);
        }
        return error?bad(error):1;
    }
    default:
        break;
    }
    const bool date=API_IS(name,"EnumDateFormatsExA"),calendar_enum=API_IS(name,"EnumCalendarInfoA")||API_IS(name,"EnumCalendarInfoExA");
    if(date||calendar_enum){
        // Names and format lists come from the record of the locale.
        const unsigned lcid=locale(a[1]);unsigned listed=0;
        if(!a[0]||!Win98Locale::Formats(lcid,0,listed))return bad(87);
        const bool extended=!API_IS(name,"EnumCalendarInfoA");
        unsigned type=calendar_enum?a[3]:a[2];const char *single=nullptr;const char *const *values=nullptr;unsigned count=0;
        const auto formats=[&](unsigned list){values=Win98Locale::Formats(lcid,list,count);};
        if(date){unsigned error=Win98DateTime::EnumFlags(true,type);if(error)return bad(error);switch(type&11){case 1:formats(1);break;case 2:formats(3);break;case 8:formats(2);break;}}
        else{
            if(a[2]!=1&&a[2]!=~0u)return bad(87);
            type&=~0x40000000u;if(!type||type>=48)return bad(1004);
            if(type==1)single="1";else if(type==2)single="Gregorian Calendar";
            else if(type==5)formats(1);else if(type==6)formats(3);
            else if(type>=7&&type<=13)single=LocaleValue(lcid,0x2a+type-7);
            else if(type>=14&&type<=20)single=LocaleValue(lcid,0x31+type-14);
            else if(type>=21&&type<=32)single=LocaleValue(lcid,0x38+type-21);
            else if(type>=34&&type<=45)single=LocaleValue(lcid,0x44+type-34);
            else if(type==47)formats(2);
        }
        if(single){values=&single;count=1;}
        // The date picture the user has set comes first and is left out of
        // the list of the locale, as in EnumDateFormatsA
        // (src/x86_datetime_format.inc).
        char own[81]={};
        if(date)if(const char *set=LocaleOverride(lcid,(type&11)==2?0x20:(type&11)==8?0x1006:0x1f))
            if(*set&&strlen(set)<sizeof own)strcpy(own,set);
        unsigned buffer=Allocate(256);if(!buffer)return bad(8);
        // The user's picture, then the list without it, in one loop with one
        // call into the program (src/state_rules/frames.calls counts it).
        for(unsigned i=*own?0:1;i<=count;++i){
            const char *value=i?values[i-1]:own;
            if(i&&*own&&!strcmp(own,value))continue;
            Store(buffer,value,strlen(value)+1);const unsigned args[]={buffer,1};
            if(!Call(a[0],args,extended?2:1)||state==Failed||state==Exited)break;
        }
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
