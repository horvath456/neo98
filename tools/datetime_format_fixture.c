/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetLastError(void);API void SetLastError(U);
API U GetDateFormatA(U,U,const void*,const char*,char*,U);API U GetTimeFormatA(U,U,const void*,const char*,char*,U);
API U EnumDateFormatsA(void*,U,U);API U EnumTimeFormatsA(void*,U,U);API void GetLocalTime(void*);
static void check(int yes,U n){if(!yes)ExitProcess(n);}
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static U date_count,time_count,nested_count,stop_count;
static const char*times[]={"h:mm:ss tt","hh:mm:ss tt","H:mm:ss","HH:mm:ss"};
static const char*dates[]={"M/d/yy","M/d/yyyy","MM/dd/yy","MM/dd/yyyy","yy/MM/dd","yyyy-MM-dd","dd-MMM-yy"};
static U __stdcall nested(char*s){check(same(s,"MMMM, yyyy"),20730);++nested_count;return 1;}
static U __stdcall date_cb(char*s){check(date_count<7&&same(s,dates[date_count]),20731);if(!date_count){check(EnumDateFormatsA(nested,0x409,8),20732);check(same(s,dates[date_count]),20733);}++date_count;return 1;}
static U __stdcall time_cb(char*s){W t[8]={1998,6,0,25,13,4,5,6};char out[64];check(time_count<4&&same(s,times[time_count]),20734);check(GetTimeFormatA(0x409,0,t,s,out,64),20735);check(same(s,times[time_count]),20736);++time_count;return 1;}
static U __stdcall stop_cb(char*s){check(same(s,times[0]),20737);++stop_count;return 2;}
void run_datetime_format_test(void){
 W t[8]={1998,6,0,25,13,4,5,6};char out[128];
 SetLastError(0x12345678);check(GetDateFormatA(0x409,0,t,0,out,128)==8&&same(out,"6/25/98")&&GetLastError()==0x12345678,20700);
 check(GetTimeFormatA(0x400,0,t,0,out,128)==11&&same(out,"1:04:05 PM"),20701);
 check(GetDateFormatA(0x409,0,t,"dddd MMMM dd yyyy g",out,128)==23&&same(out,"Thursday June 25 1998 "),20702);
 check(GetTimeFormatA(0x409,7,t,"pre h:mm:ss tt tail",out,128)==6&&same(out,"pre 1"),20703);
 check(GetTimeFormatA(0x409,8,t,"hhh",out,128)==3&&same(out,"13"),20704);
 check(GetDateFormatA(0x409,0,t,"'' d 'unclosed",out,128)&&same(out," 25 unclosed"),20705);
 out[0]='!';check(!GetDateFormatA(0x409,0,t,0,out,7)&&GetLastError()==122&&out[0]=='!',20706);
 check(GetTimeFormatA(0x409,0,t,0,0,0)==11,20707);
 check(!GetTimeFormatA(0x409,0x80000000,t,"h",out,128)&&GetLastError()==1004,20708);
 check(!GetDateFormatA(0x409,3,t,0,out,128)&&GetLastError()==1004,20709);
 check(!GetDateFormatA(0x410,0,t,0,out,128)&&GetLastError()==87,20710);
 t[3]=0;check(GetDateFormatA(0x409,0,t,"dddd d",out,128)&&same(out,"Sunday 0"),20711);t[3]=25;
 t[4]=99;check(GetDateFormatA(0x409,0,t,0,out,128)==8,20712);check(!GetTimeFormatA(0x409,0,t,0,out,128)&&GetLastError()==87,20713);t[4]=13;
 t[1]=99;check(GetTimeFormatA(0x409,0,t,0,out,128)==11,20714);t[1]=6;
 check(EnumDateFormatsA(date_cb,0x409,1)&&date_count==7&&nested_count==1,20715);
 check(EnumTimeFormatsA(time_cb,0x409,0)&&time_count==4,20716);
 check(EnumTimeFormatsA(stop_cb,0x409,0)&&stop_count==1,20717);
 check(!EnumDateFormatsA(date_cb,0x409,0)&&GetLastError()==1004,20718);
 check(!EnumTimeFormatsA(0,0x409,0)&&GetLastError()==87,20719);
 W now[8];char actual[128];GetLocalTime(now);check(GetDateFormatA(0x409,0,0,0,out,128)&&GetDateFormatA(0x409,0,now,0,actual,128)&&same(out,actual),20720);
 check(GetTimeFormatA(0x409,0,0,"HH",out,128)&&GetTimeFormatA(0x409,0,now,"HH",actual,128)&&same(out,actual),20721);
}
#ifdef DATETIME_FORMAT_STANDALONE
void mainCRTStartup(void){run_datetime_format_test();ExitProcess(0);}
#endif
