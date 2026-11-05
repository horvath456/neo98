// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
API void ExitProcess(U);API char*GetCommandLineA(void);API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
CRT int sprintf(char*,const char*,...);CRT int vsprintf(char*,const char*,const void*);CRT int _snprintf(char*,U,const char*,...);CRT int _vsnprintf(char*,U,const char*,const void*);CRT int swprintf(W*,const W*,...);CRT int vswprintf(W*,const W*,const void*);CRT int _snwprintf(W*,U,const W*,...);CRT int _vsnwprintf(W*,U,const W*,const void*);CRT char*setlocale(int,const char*);CRT int*_errno(void);
static void check(int yes,U code){if(!yes)ExitProcess(code);}static int eq(const char*a,const char*b){while(*a&&*a==*b)++a,++b;return*a==*b;}static int weq(const W*a,const W*b){while(*a&&*a==*b)++a,++b;return*a==*b;}
void run_crt_output_test(void){char out[128];W wide[128];U args[8];short small=-1;int n=-1;
 check(sprintf(out,"%ld",10)==2&&eq(out,"10"),20300);
 check(sprintf(out,"%+08d/%#x/%#.0o",-17,0xabc,0)==16&&eq(out,"-0000017/0xabc/0"),20301);
 check(sprintf(out,"%I64d",(long long)0x8000000000000000ULL)==20&&eq(out,"-9223372036854775808"),20302);
 check(sprintf(out,"%I64X/%hu",0x0123456789abcdefULL,0xffff8001)==21&&eq(out,"123456789ABCDEF/32769"),20303);
 check(sprintf(out,"%*.*d",-8,4,12)==8&&eq(out,"0012    "),20304);
 check(sprintf(out,"abc%n-%hn",&n,&small)==4&&n==3&&small==4&&eq(out,"abc-"),20305);
 check(sprintf(out,"%p/%#p",0,0x12)==19&&eq(out,"00000000/0X00000012"),20306);
 check(sprintf(out,"%q/%I32d/%")==7&&eq(out,"q/I32d/"),20307);
 args[0]=17;args[1]=(U)"text";check(vsprintf(out,"%u:%s",args)==7&&eq(out,"17:text"),20308);
 for(U i=0;i<128;++i)out[i]='Z';check(_snprintf(out,3,"abcd")==-1&&out[0]=='a'&&out[1]=='b'&&out[2]=='c'&&out[3]=='Z',20309);check(_snprintf(out,3,"abc")==3&&out[3]=='Z',20310);out[0]='Z';check(_snprintf(out,0,"")==0&&out[0]=='Z'&&_snprintf(out,0,"x")==-1&&out[0]=='Z',20311);
 args[0]=42;check(_vsnprintf(out,4,"%u",args)==2&&eq(out,"42"),20312);args[0]=(U)"bad";check(_vsnprintf(out,2,"%s",args)==-1&&out[0]=='b'&&out[1]=='a',20313);
 check(swprintf(wide,L"%s/%S/%hc",L"wide","ANSI",0xe9)==11&&weq(wide,L"wide/ANSI/\xe9"),20314);
 args[0]=0x123;args[1]=(U)L"wide";check(vswprintf(wide,L"%04X:%s",args)==9&&weq(wide,L"0123:wide"),20315);
 for(U i=0;i<128;++i)wide[i]='Z';check(_snwprintf(wide,3,L"abcd")==-1&&wide[0]=='a'&&wide[1]=='b'&&wide[2]=='c'&&wide[3]=='Z',20316);check(_snwprintf(wide,3,L"abc")==3&&wide[3]=='Z',20317);args[0]=10;check(_vsnwprintf(wide,4,L"%d",args)==2&&weq(wide,L"10"),20318);
 struct{W length,maximum;const void*text;} narrow={3,6,"abcdef"},unicode={6,12,L"abcdef"};check(sprintf(out,"%Z/%wZ",&narrow,&unicode)==7&&eq(out,"abc/abc"),20319);
 check(sprintf(out,"%s",0)==6&&eq(out,"(null)")&&swprintf(wide,L"%s",0)==6&&weq(wide,L"(null)"),20320);check(sprintf(out,"%.0s",(void*)1)==0&&out[0]==0,20321);
 *_errno()=0;check(sprintf(out,"%lc",0x1234)==0&&out[0]==0&&*_errno()==42,20322);
 check(setlocale(2,"")!=0,20323);check(sprintf(out,"%lc",0x20ac)==1&&(unsigned char)out[0]==0x80&&!out[1],20324);check(swprintf(wide,L"%hc",0x80)==1&&wide[0]==0x20ac&&!wide[1],20325);check(setlocale(2,"C")!=0,20326);
 typedef int(__cdecl*FN)(char*,const char*,...);FN ordinal=(FN)GetProcAddress(GetModuleHandleA("MSVCRT.DLL"),(const char*)691);check(ordinal&&ordinal(out,"%d",10)==2&&eq(out,"10"),20327);
}
#ifdef CRT_OUTPUT_STANDALONE
int _fltused;
void mainCRTStartup(void){if(GetCommandLineA()[0]=='F'){char out[32];sprintf(out,"%f",1.5);ExitProcess(20699);}run_crt_output_test();ExitProcess(0);}
#endif
