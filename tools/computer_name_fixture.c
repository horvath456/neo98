/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetComputerNameA(char*,U*);API U SetComputerNameA(const char*);
API U GetComputerNameW(unsigned short*,U*);API U SetComputerNameW(const unsigned short*);
API U GetLastError(void);API void SetLastError(U);API void ExitProcess(U);API char*GetCommandLineA(void);
API U RegOpenKeyA(U,const char*,U*);API U RegQueryValueExA(U,const char*,U,U*,void*,U*);API U RegSetValueExA(U,const char*,U,U,const void*,U);API U RegDeleteValueA(U,const char*);API U RegCloseKey(U);
static const char path[]="System\\CurrentControlSet\\Control\\ComputerName\\ComputerName";
static void check(int good,U code){if(!good)ExitProcess(code);}
static int equal(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void name(const char*expected,U code){char b[32];for(U i=0;i<32;++i)b[i]=(char)0xa5;U size=16;SetLastError(0x33445566);check(GetComputerNameA(b,&size)==1,code);check(equal(b,expected),code+1);U n=0;while(expected[n])++n;check(size==n&&b[n+1]==(char)0xa5,code+2);check(GetLastError()==0x33445566,code+3);}
void mainCRTStartup(void){
 char b[32];U size,key=0,type=0;for(U i=0;i<32;++i)b[i]=(char)0xa5;
 if(GetCommandLineA()[0]=='r'){
  name("SHARED-PI",33700);check(!RegOpenKeyA(0x80000002,path,&key),33704);check(!RegSetValueExA(key,"ComputerName",0,1,"other",6),33705);name("OTHER",33706);check(!RegCloseKey(key),33710);ExitProcess(0);
 }
 name("DEFAULT",33600);
 for(U capacity=0;capacity<16;capacity+=15){size=capacity;SetLastError(0x5555);check(!GetComputerNameA(b,&size),33604);check(size==capacity&&b[0]==(char)0xa5&&GetLastError()==111,33605);}
 SetLastError(0x44556677);check(SetComputerNameA("pi test!"),33606);check(GetLastError()==0x44556677,33607);name("PI TEST!",33608);
 check(SetComputerNameA("123456789012345"),33612);name("123456789012345",33613);check(!SetComputerNameA("1234567890123456")&&GetLastError()==111,33617);name("123456789012345",33618);
 check(SetComputerNameA("\xe4\xf6\xfc"),33622);name("\xc4\xd6\xdc",33623);check(!RegOpenKeyA(0x80000002,path,&key),33627);size=32;check(!RegQueryValueExA(key,"ComputerName",0,&type,b,&size),33628);check(type==1&&size==4&&(unsigned char)b[0]==0x84&&(unsigned char)b[1]==0x94&&(unsigned char)b[2]==0x81&&!b[3],33629);
 check(SetComputerNameA(""),33630);size=16;SetLastError(0x778899);check(GetComputerNameA(b,&size)&&size==0&&!b[0]&&GetLastError()==87,33631);
 check(!RegDeleteValueA(key,"ComputerName"),33632);size=16;b[0]=(char)0xa5;check(!GetComputerNameA(b,&size)&&GetLastError()==2&&size==16&&b[0]==(char)0xa5,33633);
 unsigned short wide[]={0x1111,0x2222,0};size=16;check(!GetComputerNameW(wide,&size)&&GetLastError()==120&&size==16&&wide[0]==0x1111,33634);check(!SetComputerNameW(wide)&&GetLastError()==120,33635);
 check(SetComputerNameA("shared-pi"),33636);check(!RegCloseKey(key),33637);ExitProcess(0);
}
