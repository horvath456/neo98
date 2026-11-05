// SPDX-License-Identifier: GPL-2.0-or-later
#define API __declspec(dllimport)
#define CALL __stdcall
API unsigned CALL GetModuleHandleA(const char*);API void* CALL GetProcAddress(unsigned,const char*);
API void CALL ExitProcess(unsigned);API unsigned CALL GetLastError(void);API void CALL SetLastError(unsigned);
API unsigned CALL GetProfileIntA(const char*,const char*,unsigned);API unsigned CALL GetPrivateProfileIntA(const char*,const char*,unsigned,const char*);
API unsigned CALL GetProfileStringA(const char*,const char*,const char*,char*,unsigned);API unsigned CALL GetPrivateProfileStringA(const char*,const char*,const char*,char*,unsigned,const char*);
API unsigned CALL GetProfileSectionA(const char*,char*,unsigned);API unsigned CALL GetPrivateProfileSectionA(const char*,char*,unsigned,const char*);API unsigned CALL GetPrivateProfileSectionNamesA(char*,unsigned,const char*);
API unsigned CALL WriteProfileStringA(const char*,const char*,const char*);API unsigned CALL WritePrivateProfileStringA(const char*,const char*,const char*,const char*);
API unsigned CALL WriteProfileSectionA(const char*,const char*);API unsigned CALL WritePrivateProfileSectionA(const char*,const char*,const char*);
API unsigned CALL GetPrivateProfileStructA(const char*,const char*,void*,unsigned,const char*);API unsigned CALL WritePrivateProfileStructA(const char*,const char*,const void*,unsigned,const char*);
API unsigned CALL GetPrivateProfileStringW(const void*,const void*,const void*,void*,unsigned,const void*);
API unsigned CALL CreateFileA(const char*,unsigned,unsigned,void*,unsigned,unsigned,unsigned);API unsigned CALL ReadFile(unsigned,void*,unsigned,unsigned*,void*);API unsigned CALL CloseHandle(unsigned);
static void check(int okay,unsigned code){if(!okay)ExitProcess(code);}static unsigned length(const char*p){unsigned n=0;while(p[n])++n;return n;}static int equal(const char*a,const char*b){while(*a&&*a==*b)++a,++b;return *a==*b;}static int contains(const char*a,const char*b){for(;*a;++a){const char*p=a,*q=b;while(*q&&*p==*q)++p,++q;if(!*q)return 1;}return 0;}
static char too_large[65511];
void mainCRTStartup(void){char text[512];for(unsigned i=0;i<sizeof text;++i)text[i]=(char)0xa5;
 if(GetPrivateProfileIntA("Lifetime","Marker",0,"life.ini")==0x12345678){check(GetPrivateProfileIntA("Application","Integer",0,"profile-test.ini")==0xffffffffu,9733);ExitProcess(0);}
 check(GetPrivateProfileStringA("Missing","Key","  'fallback'   ",text,sizeof text,"profile-test.ini")==12&&equal(text,"  'fallback'"),9700);
 check(WritePrivateProfileStringA("Application","Key","'quoted value'","profile-test.ini"),9701);check(GetPrivateProfileStringA("application","KEY","",text,sizeof text,"profile-test.ini")==12&&equal(text,"quoted value"),9702);
 check(WritePrivateProfileStringA("Application","Integer","--4294967295","profile-test.ini"),9703);check(GetPrivateProfileIntA("Application","Integer",7,"profile-test.ini")==0xffffffffu,9704);check(GetPrivateProfileIntA("Application","Absent",0x12345678,"profile-test.ini")==0x12345678,9705);
 check(GetPrivateProfileStringA("Application","Key","",text,5,"profile-test.ini")==4&&equal(text,"quot"),9706);text[0]='Q';check(!GetPrivateProfileStringA("Application","Key","",text,0,"profile-test.ini")&&text[0]=='Q',9707);
 check(GetPrivateProfileStringA("Application",0,"",text,sizeof text,"profile-test.ini")>0&&equal(text,"Key")&&equal(text+4,"Integer"),9708);
 check(GetPrivateProfileSectionNamesA(text,sizeof text,"profile-test.ini")==13&&equal(text,"Application")&&!text[12],9709);
 check(GetPrivateProfileSectionA("Application",text,sizeof text,"profile-test.ini")>0&&equal(text,"Key='quoted value'")&&contains(text+length(text)+1,"Integer="),9710);
 const unsigned char data[]={0,1,127,128,255};unsigned char decoded[5]={9,9,9,9,9};check(WritePrivateProfileStructA("Application","Binary",data,5,"profile-test.ini"),9711);check(GetPrivateProfileStructA("Application","Binary",decoded,5,"profile-test.ini"),9712);for(unsigned i=0;i<5;++i)check(decoded[i]==data[i],9713);
 check(WritePrivateProfileStringA("Application","BadChecksum","01020300","profile-test.ini"),9714);check(!GetPrivateProfileStructA("Application","BadChecksum",decoded,3,"profile-test.ini")&&decoded[0]==1&&decoded[1]==2&&decoded[2]==3,9715);
 check(WritePrivateProfileStringA("Application","Key",0,"profile-test.ini"),9716);check(GetPrivateProfileStringA("Application","Key","deleted",text,sizeof text,"profile-test.ini")==7&&equal(text,"deleted"),9717);
 check(WritePrivateProfileSectionA("Replace","A=one\0;comment\0B=two\0","profile-test.ini"),9718);check(GetPrivateProfileStringA("Replace","A","",text,sizeof text,"profile-test.ini")==3&&equal(text,"one"),9719);
 check(WritePrivateProfileStringA("Replace",0,0,"profile-test.ini"),9720);check(!GetPrivateProfileSectionA("Replace",text,sizeof text,"profile-test.ini")&&!text[0]&&!text[1],9721);
 check(!WritePrivateProfileStringA(0,0,0,"profile-test.ini"),9722);unsigned file=CreateFileA("C:\\WINDOWS\\profile-test.ini",0x80000000,3,0,3,0,0),got=0;check(file!=0xffffffffu&&ReadFile(file,text,sizeof text-1,&got,0),9723);text[got]=0;check(CloseHandle(file)&&contains(text,"[Application]")&&contains(text,"Integer=--4294967295")&&contains(text,";comment"),9724);
 check(WriteProfileStringA("Defaults","Number","0x12345678"),9725);check(GetProfileIntA("Defaults","Number",0)==0x12345678,9726);check(GetProfileStringA("Defaults","Number","",text,sizeof text)==10&&equal(text,"0x12345678"),9727);
 check(WriteProfileSectionA("Bulk","X=first\0Y=second\0"),9728);check(GetProfileSectionA("Bulk",text,sizeof text)>0&&equal(text,"X=first"),9729);
 SetLastError(123);check(!GetPrivateProfileStringW(0,0,0,0,0,0)&&GetLastError()==120,9730);
 const char*w_names[]={"GetPrivateProfileIntW","GetPrivateProfileSectionNamesW","GetPrivateProfileSectionW","GetPrivateProfileStringW","GetPrivateProfileStructW","GetProfileIntW","GetProfileSectionW","GetProfileStringW","WritePrivateProfileSectionW","WritePrivateProfileStringW","WritePrivateProfileStructW","WriteProfileSectionW","WriteProfileStringW","OpenProfileUserMapping","CloseProfileUserMapping"};
 const unsigned w_args[]={4,3,4,6,5,3,3,5,3,4,5,2,3,0,0};unsigned kernel=GetModuleHandleA("KERNEL32.DLL");
 for(unsigned i=0;i<15;++i){void*p=GetProcAddress(kernel,w_names[i]);check(p!=0,9735);SetLastError(123);unsigned r=9;switch(w_args[i]){case 0:r=((unsigned(CALL*)(void))p)();break;case 2:r=((unsigned(CALL*)(unsigned,unsigned))p)(0,0);break;case 3:r=((unsigned(CALL*)(unsigned,unsigned,unsigned))p)(0,0,0);break;case 4:r=((unsigned(CALL*)(unsigned,unsigned,unsigned,unsigned))p)(0,0,0,0);break;case 5:r=((unsigned(CALL*)(unsigned,unsigned,unsigned,unsigned,unsigned))p)(0,0,0,0,0);break;case 6:r=((unsigned(CALL*)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned))p)(0,0,0,0,0,0);break;}check(!r&&GetLastError()==120,9736);}
 for(unsigned i=0;i<65510;++i)too_large[i]='A';too_large[65510]=0;check(!WritePrivateProfileStringA("Large","Value",too_large,"large.ini"),9737);check(GetPrivateProfileStringA("Large","Value","unchanged",text,sizeof text,"large.ini")==9&&equal(text,"unchanged"),9738);
 check(!GetPrivateProfileStringA("Application","Integer","",text,65536,"profile-test.ini"),9731);check(GetLastError()==87,9732);check(WritePrivateProfileStringA("Lifetime","Marker","0x12345678","life.ini"),9734);ExitProcess(0);}
