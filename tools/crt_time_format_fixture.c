/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;typedef struct{int sec,min,hour,mday,mon,year,wday,yday,isdst;}TM;
#define CRT __declspec(dllimport) __cdecl
#define API __declspec(dllimport) __stdcall
CRT U strftime(char*,U,const char*,const TM*);CRT U _Strftime(char*,U,const char*,const TM*,const void*);CRT U wcsftime(unsigned short*,U,const unsigned short*,const TM*);CRT char*_strdate(char*);CRT char*_strtime(char*);CRT unsigned short*_wstrdate(unsigned short*);CRT unsigned short*_wstrtime(unsigned short*);CRT char*setlocale(int,const char*);CRT char***__p__environ(void);CRT void _tzset(void);CRT int*_errno(void);
API void ExitProcess(U);API U SetLocalTime(const void*);
static void check(int b,U e){if(!b)ExitProcess(e);}static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
void mainCRTStartup(void){
 check(setlocale(0,"C")!=0,37200);TM t={7,5,13,9,1,98,1,39,0};char out[256];U n=strftime(out,256,"%a %A %b %B",&t);check(n==23&&equal(out,"Mon Monday Feb February"),37201);
 n=strftime(out,256,"%c",&t);check(n&&equal(out,"02/09/98 13:05:07"),37202);check(strftime(out,256,"%#c",&t)&&equal(out,"Monday, February 09, 1998 13:05:07"),37203);
 check(strftime(out,256,"%d %H %I %j %m %M %p %S %U %w %W %y %Y",&t)&&equal(out,"09 13 01 040 02 05 PM 07 06 1 06 98 1998"),37204);
 check(strftime(out,256,"%#d %#I %#j %#m %#M %#S %#Y",&t)&&equal(out,"9 1 40 2 5 7 1998"),37205);
 check(strftime(out,256,"[%Q] %%",&t)==4&&equal(out,"[] %"),37206);check(!strftime((void*)0,0,(void*)0,(void*)0),37207);check(strftime(out,8,"literal",(void*)0)==7&&equal(out,"literal"),37208);
 for(U i=0;i<256;++i)out[i]=0x6d;check(!strftime(out,4,"%Y",&t)&&out[0]==0x6d&&out[4]==0x6d,37209);check(!strftime(out,2,"AB",&t)&&out[0]=='A'&&out[1]=='B'&&out[2]==0x6d,37210);check(strftime(out,3,"%#Y",&t)==2&&equal(out,"98"),37211);
 struct{char before;char bytes[8];}edge;edge.before='A';edge.bytes[0]='B';check(!strftime(edge.bytes,1,"%#d",&t)&&edge.before=='B'&&!edge.bytes[0],37212);
 const char*labels[43];for(U i=0;i<43;++i)labels[i]="";labels[27]="Custom February";labels[39]="evening";labels[40]="'custom 'yyyy";labels[42]="HH:mm";check(_Strftime(out,256,"%B %p %x %X",&t,labels)&&equal(out,"Custom February evening custom 1998 13:05"),37213);
 unsigned short wide[64];for(U i=0;i<64;++i)wide[i]=0x6d6d;check(wcsftime(wide,2,L"AB",&t)==2&&wide[0]=='A'&&wide[1]=='B'&&wide[2]==0x6d6d,37214);wide[0]=0x6d6d;check(!wcsftime(wide,32,L"",&t)&&wide[0]==0x6d6d,37215);
 check(wcsftime(wide,64,L"%Y-%m-%d",&t)==10,37216);const char*expected="1998-02-09";for(U i=0;i<11;++i)check(wide[i]==(unsigned char)expected[i],37217);
 unsigned short unmapped[]={0x100,0};wide[0]=0x6d6d;*_errno()=0;check(!wcsftime(wide,64,unmapped,&t)&&*_errno()==42&&wide[0]==0x6d6d,37218);check(setlocale(2,"")!=0,37219);unsigned short euro[]={0x20ac,0};check(wcsftime(wide,64,euro,&t)==1&&wide[0]==0x20ac&&!wide[1],37220);check(setlocale(2,"C")!=0,37221);
 char**previous=*__p__environ();char tz[]="TZ=PST8PDT";char*env[]={tz,0};*__p__environ()=env;_tzset();check(strftime(out,256,"%z/%Z",&t)&&equal(out,"PST/PST"),37222);t.isdst=-1;check(strftime(out,256,"%z",&t)&&equal(out,"PDT"),37223);*__p__environ()=previous;_tzset();
 unsigned short clock[8]={1998,6,4,25,12,3,4,0};check(SetLocalTime(clock),37224);for(U i=0;i<256;++i)out[i]=0x6d;check(_strdate(out)==out&&equal(out,"06/25/98")&&out[9]==0x6d,37225);check(_strtime(out)==out&&equal(out,"12:03:04")&&out[9]==0x6d,37226);
 for(U i=0;i<64;++i)wide[i]=0x6d6d;check(_wstrdate(wide)==wide&&wide[9]==0x6d6d,37227);expected="06/25/98";for(U i=0;i<9;++i)check(wide[i]==(unsigned char)expected[i],37228);check(_wstrtime(wide)==wide&&wide[9]==0x6d6d,37229);expected="12:03:04";for(U i=0;i<9;++i)check(wide[i]==(unsigned char)expected[i],37230);ExitProcess(0);
}
