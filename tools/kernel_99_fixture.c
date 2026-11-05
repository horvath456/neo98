/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
struct ST{W year,month,weekday,day,hour,minute,second,milliseconds;};
struct Zone{U bias;W standard_name[32];struct ST standard;U standard_bias;W daylight_name[32];struct ST daylight;U daylight_bias;};
API void ExitProcess(U);API void Sleep(U);API char*GetCommandLineA(void);API U GetLastError(void);API void SetLastError(U);
API U O99(U);API U GetTimeZoneInformation(struct Zone*);API U SetTimeZoneInformation(const struct Zone*);
API void GetLocalTime(struct ST*);API void GetSystemTime(struct ST*);API U SetLocalTime(const struct ST*);API U SetSystemTime(const struct ST*);
API U RegOpenKeyExA(U,const char*,U,U,U*);API U RegQueryValueExA(U,const char*,U*,U*,void*,U*);API U RegSetValueExA(U,const char*,U,U,const void*,U);API U RegCloseKey(U);
API U SystemTimeToFileTime(const struct ST*,U*);API U FileTimeToLocalFileTime(const U*,U*);
static void check(U condition,U code){if(!condition)ExitProcess(code);}
static void settime(W month,W day,W hour,W minute,W second,W ms){struct ST t={2026,month,0,day,hour,minute,second,ms};check(SetLocalTime(&t),13100);}
static U query(U key,const char*name,U expected_type,U expected_size){U data[2]={0,0},type=0,size=8;check(!RegQueryValueExA(key,name,0,&type,data,&size),13101);check(type==expected_type&&size==expected_size,13102);return data[0];}
void mainCRTStartup(void){
 char*cmd=GetCommandLineA();U second=0;for(U i=0;cmd[i];++i)if(cmd[i]=='!')second=1;
 struct Zone z={0},out={0};struct ST local={0},utc={0};
 if(second){check(GetTimeZoneInformation(&out)==1&&out.bias==(U)-60&&out.standard_name[0]=='S',13103);GetLocalTime(&local);check(local.year==2026&&local.month==10&&local.day==25&&local.hour==2,13104);ExitProcess(0);}
 settime(1,15,12,0,0,0);
 z.bias=(U)-60;z.daylight_bias=(U)-60;z.standard_name[0]='S';z.daylight_name[0]='D';
 z.standard=(struct ST){0,10,0,5,3,0,0,0};z.daylight=(struct ST){0,3,0,5,2,0,0,0};
 check(SetTimeZoneInformation(&z),13105);check(GetTimeZoneInformation(&out)==1&&out.bias==z.bias&&out.daylight.day==5,13106);
 GetSystemTime(&utc);check(utc.hour==11&&utc.month==1,13107);
 U key=0;check(!RegOpenKeyExA(0x80000002,"System\\CurrentControlSet\\Control\\TimeZoneInformation",0,0,&key),13108);
 check(query(key,"ActiveTimeBias",3,4)==(U)-60,13109);
 U oldflag=GetTimeZoneInformation(&out);z.daylight.day=6;SetLastError(0);check(!SetTimeZoneInformation(&z)&&GetLastError()==87,13110);check(GetTimeZoneInformation(&out)==oldflag&&out.daylight.day==5,13111);z.daylight.day=5;
 settime(7,15,12,0,0,0);check(GetTimeZoneInformation(&out)==2,13112);GetSystemTime(&utc);check(utc.hour==10,13113);check(query(key,"ActiveTimeBias",3,4)==(U)-120,13114);
 utc=(struct ST){2026,7,0,15,8,9,10,0};check(SetSystemTime(&utc),13115);GetLocalTime(&local);check(local.hour==10&&local.minute==9,13116);
 U ft[2],lf[2];GetSystemTime(&utc);check(SystemTimeToFileTime(&utc,ft)&&FileTimeToLocalFileTime(ft,lf),13117);check((unsigned long long)lf[0]+((unsigned long long)lf[1]<<32)-((unsigned long long)ft[0]+((unsigned long long)ft[1]<<32))==72000000000ull,13118);
 settime(3,29,1,59,59,900);Sleep(150);check(O99(1)==2,13119);GetLocalTime(&local);check(local.hour==3&&local.minute==0,13120);check(GetTimeZoneInformation(&out)==2,13121);
 U disable=1;check(!RegSetValueExA(key,"DisableAutoDaylightTimeSet",0,4,&disable,4),13122);
 settime(10,25,2,59,59,900);Sleep(150);check(O99(1)==1,13123);GetLocalTime(&local);check(local.hour==3,13124);
 disable=0;check(!RegSetValueExA(key,"DisableAutoDaylightTimeSet",0,4,&disable,4),13125);
 settime(10,25,2,59,59,900);Sleep(150);check(O99(1)==2,13126);GetLocalTime(&local);check(local.hour==2&&local.minute==0,13127);check(O99(1)==0,13128);check(GetTimeZoneInformation(&out)==1,13129);query(key,"NoChangeStart",3,8);query(key,"NoChangeEnd",3,8);
 check(RegCloseKey(key)==0,13130);ExitProcess(0);
}
