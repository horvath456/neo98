/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef struct{int sec,min,hour,mday,mon,year,wday,yday,isdst;}TM;
#define CRT __declspec(dllimport) __cdecl
#define API __declspec(dllimport) __stdcall
CRT int mktime(TM*);CRT TM*gmtime(const int*);CRT TM*localtime(const int*);CRT int time(int*);CRT char*asctime(const TM*);CRT char*ctime(const int*);CRT unsigned short*_wasctime(const TM*);CRT unsigned short*_wctime(const int*);CRT void _tzset(void);CRT void _ftime(void*);CRT int*__p__timezone(void);CRT int*__p__daylight(void);CRT int*__p__dstbias(void);CRT char**__p__tzname(void);CRT char***__p__environ(void);CRT int*_errno(void);
__declspec(dllimport) extern int _timezone,_daylight,_dstbias;__declspec(dllimport) extern char*_tzname[2];
API void ExitProcess(U);API U SetTimeZoneInformation(const void*);API U GetTimeZoneInformation(void*);API U SetLocalTime(const void*);API void GetSystemTime(void*);API U CreateThread(void*,U,void*,void*,U,U*);API U WaitForSingleObject(U,U);API U CloseHandle(U);
static void check(int b,U e){if(!b)ExitProcess(e);}static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}static U parent_tm,parent_text,worker_tm,worker_text;
static U __stdcall worker(U v){(void)v;int t=86400;TM*p=gmtime(&t);worker_tm=(U)p;worker_text=(U)asctime(p);check(p&&p->mday==2&&equal((char*)worker_text,"Fri Jan 02 00:00:00 1970\n"),36800);return 0;}
void mainCRTStartup(void){
 check(_timezone==28800&&_daylight==1&&equal(_tzname[0],"PST")&&equal(_tzname[1],"PDT"),36801);check(__p__timezone()==&_timezone&&__p__daylight()==&_daylight&&__p__tzname()==_tzname&&__p__dstbias()==&_dstbias&&_dstbias==-3600,36802);
 char**previous=*__p__environ();char tz[]="TZ=UTC0";char*env[]={tz,0};*__p__environ()=env;_tzset();check(_timezone==0&&_daylight==0&&equal(_tzname[0],"UTC")&&!_tzname[1][0],36803);
 TM m={0,0,0,1,0,100,99,99,-1};check(mktime(&m)==946684800&&m.wday==6&&m.yday==0&&m.isdst==0,36804);int stamp=946684800;TM*p=gmtime(&stamp);check(p&&p->year==100&&p->mon==0&&p->mday==1,36805);parent_tm=(U)p;
 check(localtime(&stamp)==p&&equal(ctime(&stamp),"Sat Jan 01 00:00:00 2000\n"),36806);char*text=asctime(p);parent_text=(U)text;check(equal(text,"Sat Jan 01 00:00:00 2000\n"),36807);unsigned short*w=_wasctime(p),*w2=_wctime(&stamp);check(w&&w==w2,36808);for(U i=0;i<26;++i)check(w[i]==(unsigned char)text[i],36809);
 int negative=-1;*_errno()=123;check(!gmtime(&negative)&&!localtime(&negative)&&!ctime(&negative)&&!_wctime(&negative)&&*_errno()==123,36810);
 TM bad={0,0,0,1,24,139,7,8,-1};check(mktime(&bad)==-1&&bad.mon==0&&bad.year==139,36811);
 TM normalized={70,61,25,0,14,98,0,0,-1};int value=mktime(&normalized);check(value!=-1&&normalized.sec==10&&normalized.min==2&&normalized.hour==2&&normalized.mday==1&&normalized.mon==2&&normalized.year==99,36812);
 stamp=946684800;p=gmtime(&stamp);text=asctime(p);U thread=CreateThread(0,0,(void*)worker,0,0,0);check(thread&&WaitForSingleObject(thread,2000)==0&&CloseHandle(thread),36813);check(worker_tm!=parent_tm&&worker_text!=parent_text&&p->year==100&&equal(text,"Sat Jan 01 00:00:00 2000\n"),36814);
 char est[]="TZ=EST5EDT";env[0]=est;_tzset();check(_timezone==18000&&_daylight=='E'&&equal(_tzname[1],"EDT"),36815);int summer=899251200;TM*d=localtime(&summer);check(d&&d->isdst==1,36816);TM back=*d;check(mktime(&back)==summer,36817);
 * __p__environ()=previous;U zone[43]={0};zone[1]='U'|('T'<<16);zone[2]='C';check(SetTimeZoneInformation(zone),36818);_tzset();check(_timezone==0&&_daylight==0,36819);
 unsigned short clock[8]={1998,6,4,25,12,0,0,0};check(SetLocalTime(clock),36820);int now=0,result=time(&now);check(now==result&&now>=898776000&&now<898776005,36821);
 struct{U first;unsigned char bytes[12];U last;} tb;tb.first=0x12345678;tb.last=0x87654321;for(U i=0;i<12;++i)tb.bytes[i]=0xa5;_ftime(tb.bytes);int seconds=*(int*)tb.bytes;check(seconds>=now&&seconds<=now+1&&*(unsigned short*)(tb.bytes+4)<1000&&!*(short*)(tb.bytes+6)&&!*(short*)(tb.bytes+8),36822);check(tb.first==0x12345678&&tb.last==0x87654321&&tb.bytes[10]==0xa5&&tb.bytes[11]==0xa5,36823);ExitProcess(0);
}
