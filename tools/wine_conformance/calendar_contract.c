/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(calendar_contract)
{
    const LCID locales[]={0x409,0xc07,0x411,0x404,0x412,0x41e,0xffff};
    const WORD dates[][3]={{1868,9,7},{1868,9,8},{1912,7,29},{1912,7,30},
        {1926,12,24},{1926,12,25},{1989,1,7},{1989,1,8},{2019,4,4},{2019,5,1}};
    unsigned l,c,d,f;char text[128];DWORD value,error;int n;
    for(l=0;l<sizeof locales/sizeof locales[0];++l){
        for(c=0;c<=13;++c){
            value=0xdeadbeef;SetLastError(0x12345678);
            n=GetCalendarInfoA(locales[l],c,CAL_ITWODIGITYEARMAX|CAL_RETURN_NUMBER|CAL_NOUSEROVERRIDE,NULL,0,&value);
            error=GetLastError();
            trace("CALENDAR_NUMBER locale=%04lx calendar=%u n=%d error=%08lx value=%lu\n",locales[l],c,n,error,value);
            if(n)ok(n==4,"numeric calendar result size %d\n",n);
            memset(text,0x7f,sizeof text);SetLastError(0x12345678);
            n=GetCalendarInfoA(locales[l],c,CAL_ITWODIGITYEARMAX|CAL_NOUSEROVERRIDE,text,sizeof text,NULL);
            error=GetLastError();text[sizeof text-1]=0;
            trace("CALENDAR_STRING locale=%04lx calendar=%u n=%d error=%08lx text=%s\n",locales[l],c,n,error,n?text:"untouched");
            if(n)ok((DWORD)atoi(text)==value,"string and numeric limits differ\n");
        }
        for(d=0;d<sizeof dates/sizeof dates[0];++d)for(f=0;f<2;++f){
            SYSTEMTIME t={0};t.wYear=dates[d][0];t.wMonth=dates[d][1];t.wDay=dates[d][2];
            memset(text,0,sizeof text);SetLastError(0x12345678);
            n=GetDateFormatA(locales[l],f?DATE_USE_ALT_CALENDAR:0,&t,"g y yy yyyy M d",text,sizeof text);
            error=GetLastError();
            trace("CALENDAR_DATE locale=%04lx date=%u/%u/%u alt=%u n=%d error=%08lx text=%s\n",
                locales[l],t.wYear,t.wMonth,t.wDay,f,n,error,text);
            if(n)ok(n==(int)strlen(text)+1,"date result length %d\n",n);
        }
    }
}
