// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport) __stdcall
typedef unsigned U;typedef unsigned short W;
typedef struct {W year,month,weekday,day,hour,minute,second,milliseconds;}Time;
typedef struct {U bias;W standardName[32];Time standard;U standardBias;W daylightName[32];Time daylight;U daylightBias;}Zone;
API U GetDaylightFlag(void);API U SetDaylightFlag(U);
API U GetTimeZoneInformation(Zone*);API U SetTimeZoneInformation(const Zone*);
API void GetLocalTime(Time*);API U SetLocalTime(const Time*);
API void SetLastError(U);API U GetLastError(void);API void ExitProcess(U);
API void* GetModuleHandleA(const char*);API void* GetProcAddress(void*,const char*);
static void check(int good,U code){if(!good)ExitProcess(code);}
static int same(const void*a,const void*b,U n){const unsigned char*x=a,*y=b;for(U i=0;i<n;++i)if(x[i]!=y[i])return 0;return 1;}
U run_daylight_flags(void){
 Zone old,zone={0},got;Time prior;GetLocalTime(&prior);check(GetTimeZoneInformation(&old)!=~0u,32400);
 zone.bias=60;zone.standard.month=10;zone.standard.day=5;zone.standard.hour=3;
 zone.daylight.month=3;zone.daylight.day=5;zone.daylight.hour=2;zone.daylightBias=(U)-60;
 Time summer={1998,6,0,25,12,0,0,0},winter={1998,12,0,25,12,0,0,0};
 check(SetLocalTime(&summer),32401);check(SetTimeZoneInformation(&zone),32402);
 check(GetTimeZoneInformation(&got)==2&&same(&got,&zone,sizeof zone),32403);
 SetLastError(0x13572468);check(GetDaylightFlag()==1&&GetLastError()==0x13572468,32404);
 check(SetDaylightFlag(0)==1&&GetDaylightFlag()==1&&GetLastError()==0x13572468,32405);
 check(SetDaylightFlag(~0u)==1&&GetDaylightFlag()==1,32406);
 check(SetLocalTime(&winter),32407);check(GetTimeZoneInformation(&got)==1,32408);
 SetLastError(0x13572468);check(!GetDaylightFlag()&&GetLastError()==0x13572468,32409);
 check(SetDaylightFlag(2)==1&&!GetDaylightFlag()&&GetLastError()==0x13572468,32410);
 typedef U(__stdcall*Get)(void);typedef U(__stdcall*Set)(U);
 void*kernel=GetModuleHandleA("KERNEL32.DLL");Get get=(Get)GetProcAddress(kernel,(const char*)352);Set set=(Set)GetProcAddress(kernel,(const char*)707);
 check(get&&set&&set(1)==1&&!get(),32411);
 // Restore the guest date/time and zone structure. Original SetTimeZoneInformation
 // classifies it; an initial unknown cache flag0 cannot be restored by SetDaylightFlag.
 check(SetTimeZoneInformation(&old),32412);check(SetLocalTime(&prior),32413);
 check(GetTimeZoneInformation(&got)!=~0u&&same(&got,&old,sizeof old),32414);
 return 0;
}
#ifdef DAYLIGHT_FLAGS_STANDALONE
void mainCRTStartup(void){ExitProcess(run_daylight_flags());}
#endif
