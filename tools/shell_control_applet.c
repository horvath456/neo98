/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U SendMessageA(U,U,U,U);
static U calls[201],attached,failed,bad,active;
static int same(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static void copy(char*out,const char*in){while((*out++=*in++));}
__declspec(dllexport) U __stdcall FixtureQuery(U op,U a){if(op==0){for(U i=0;i<201;++i)calls[i]=0;failed=a;return 0;}if(op==1)return a<201?calls[a]:0;if(op==2)return attached;if(op==3)return bad;return active;}
__declspec(dllexport) U __stdcall CPlApplet(U h,U m,U a,U b){if(m<201)++calls[m];
 if(m==1){if(active)bad=1;if(!failed)active=1;return !failed;}
 if(m==2)return 2;
 if(m==3){U*p=(U*)b;p[0]=p[1]=p[2]=0;p[3]=0xa000+a;return 0;}
 if(m==8){U*p=(U*)b;p[0]=244;p[1]=p[2]=0;p[3]=0xa000+a;p[4]=0;copy((char*)b+20,a?"Clock(&C)":"&Date");copy((char*)b+52,"CPL fixture");*((char*)b+116)=0;return 0;}
 if(m==9){SendMessageA(h,0x8001,a,b);return same((char*)b,"handled");}
 if(m==5){SendMessageA(h,0x8002,a,b);return 0;}
 if(m==6){if(b!=0xa000+a||!active)bad=2;return 0;}
 if(m==7){if(!active)bad=3;active=0;return 0;}return 0;
}
U __stdcall DllMain(U h,U why,U reserved){if(why==1)++attached;return 1;}
