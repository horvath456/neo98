// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_datetime_format.h"
namespace X86 {
unsigned Application::DateTimeFormatApi(ApiName name,const unsigned*a)
{
    using namespace Win98DateTime;
    const bool enumeration=!strncmp(name,"Enum",4);
    const bool date=strstr(name,"Date")!=nullptr;
    if(!InitializeNls())return 0;
    U locale=Win98SystemLocale::ResolveLocale(a[enumeration?1:0],system_locale.system,[this]{return UserDefaultLocale();});
    const U flags=a[enumeration?2:1];
    const U calendar=date?Win98Locale::LocaleCalendar(locale,(flags&4)!=0):1;
    const bool convertible=Win98Locale::SystemInfo(locale,1)!=nullptr;
    // An explicit alternate calendar is selected from the binary locale
    // record before ANSI conversion. This permits numeric dates
    // even when the locale's code page is absent, as on western Win98.
    const bool numeric_alternate=date&&(flags&4)&&calendar&&a[3];
    // The lists of an enumeration come from the record of the locale.
    U count=0;const char*const*list=nullptr;
    if(enumeration)list=Win98Locale::Formats(locale,!date?0:(flags&11)==2?3:(flags&11)==8?2:1,count);
    if((enumeration?!list:(!convertible&&!numeric_alternate))||
       (enumeration?!a[0]:(a[5]>0x7fffffff||(a[5]&&!a[4])))){last_error=87;return 0;}
    U error=enumeration?EnumFlags(date,flags):FormatFlags(date,flags,a[3]!=0);
    if(error){last_error=error;return 0;}
    if(enumeration){
        // Guest callbacks must execute on the owning app fiber, as normal API
        // dispatch does. A direct host-side callback invocation is unsupported.
        if(!threads[current_thread].fiber||!threads[current_thread].fiber->Active()){last_error=50;return 0;}
        U lease=Allocate(256);if(!lease){last_error=8;return 0;}
        struct E {Application&app;U callback,lease;
            bool Callback(const char*s,U&result){if(!app.Store(lease,s,strlen(s)+1))return false;result=app.Call(callback,&lease,1);return app.state!=Failed&&app.state!=Exited;}
        }e{*this,a[0],lease};
        U result=Enumerate(e,list,count);Free(lease);return result;
    }
    Time time{};
    if(a[2]){if(date){if(!Read(a[2],&time,8))return 0;}else if(!Read(a[2]+8,&time.hour,8))return 0;}
    else{U lease=Allocate(sizeof time);if(!lease){last_error=8;return 0;}TimezoneApi("GetLocalTime",&lease);bool ok=Read(lease,&time,sizeof time);Free(lease);if(!ok)return 0;}
    if(!(date?ValidDate(time):ValidTime(time))){last_error=87;return 0;}
    if(date&&calendar!=1&&calendar!=2&&calendar!=3&&calendar!=4&&calendar!=5&&calendar!=7){last_error=50;return 0;}
    const char *era=nullptr;
    if(date)time.year=Win98Locale::CalendarYear(calendar,time.year,time.month,time.day,era);
    struct E {
        Application &app;U locale;const char *era;
        const char*Info(U field){return app.LocaleValue(locale,field);}
        const char*Month(U month,bool abbreviated,bool){return Info((abbreviated?0x44:0x38)+month-1);}
        void Era(Output&o,const Time&){o.Text(era);}
    }e{*this,locale,convertible?era:nullptr};
    char custom[4096],buffer[4096];const char*format;
    if(a[3]){const char*s=ReadString(a[3]);if(state==Failed)return 0;U n=strlen(s);if(n>=sizeof custom){last_error=50;return 0;}memcpy(custom,s,n+1);format=custom;}
    else {unsigned field=!date?0x1003:(flags&11)==2?0x20:(flags&11)==8?0x1006:0x1f;format=LocaleValue(locale,field|(flags&0x80000000));}
    if(!format){last_error=87;return 0;}
    Output output{buffer,sizeof buffer};U length=date?DateFormat(e,time,format,output):TimeFormat(e,time,flags,format,output);
    // Original uses a fixed stack scratch buffer. Native storage is bounded;
    // original stack corruption on overlong patterns is not reproduced.
    if(output.overflow){last_error=50;return 0;}
    if(!a[5])return length;
    if(a[5]<length){last_error=122;return 0;}
    return Store(a[4],buffer,length)?length:0;
}
}
