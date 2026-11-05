// SPDX-License-Identifier: GPL-2.0-or-later
#include "lz_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
static int same(const char*a,const char*b,U n){while(n--)if(*a++!=*b++)return 0;return 1;}
static void seed(const char*path,const void*data,U n){U h=CreateFileA(P(path),0xc0000000,3,0,2,0,0),done=0;CHECK(h!=~0u);CHECK(WriteFile(h,P(data),n,P(&done),0)&&done==n);CHECK(CloseHandle(h));}
static unsigned char large_packed[45014],large_out[40000];
void mainCRTStartup(void){
 unsigned char packed[]={'S','Z','D','D',0x88,0xf0,0x27,0x33,'A','t',9,0,0,0,7,'A','B','C',0xf0,0xf3};
 seed("C:\\packed.tx_",packed,sizeof packed);seed("C:\\plain.txt","plain data",10);
 CHECK(LZStart());char of[136]={0},buf[64]={0};
 CHECK(GetExpandedNameA(P("C:\\packed.tx_"),P(buf))==1&&same(buf,"C:\\packed.txt",14));
 U h=LZOpenFileA(P("C:\\packed.tx_"),P(of),0);CHECK(h!=~0u);
 CHECK(LZRead(h,P(buf),4)==4&&same(buf,"ABCA",4));CHECK(LZSeek(h,-2,1)==2);
 CHECK(LZRead(h,P(buf),20)==7&&same(buf,"CABCABC",7));CHECK(LZRead(h,P(buf),1)==0);
 CHECK(LZSeek(h,-3,2)==6&&LZRead(h,P(buf),3)==3&&same(buf,"ABC",3));CHECK(LZSeek(h,-20,0)==~0u);
 CHECK(LZSeek(h,0,0)==0);U dest=LZOpenFileA(P("C:\\expanded.txt"),P(of),0x1001);CHECK(dest!=~0u);
 CHECK(LZCopy(h,dest)==9);LZClose(dest);LZClose(h);CHECK(LZRead(h,P(buf),1)==~0u);
 h=LZOpenFileA(P("C:\\expanded.txt"),P(of),0);CHECK(h!=~0u);CHECK(LZRead(h,P(buf),64)==9&&same(buf,"ABCABCABC",9));LZClose(h);
 h=LZOpenFileA(P("C:\\plain.txt"),P(of),0);CHECK(h!=~0u);CHECK(LZInit(h)==h);CHECK(LZRead(h,P(buf),5)==5&&same(buf,"plain",5));LZClose(h);
 for(U i=0;i<160;++i){h=LZOpenFileA(P("C:\\packed.tx_"),P(of),0);CHECK(h!=~0u);CHECK(LZRead(h,P(buf),9)==9&&same(buf,"ABCABCABC",9));LZClose(h);}
 for(U i=0;i<14;++i)large_packed[i]=packed[i];large_packed[10]=40000&255;large_packed[11]=40000>>8;
 for(U i=0;i<5000;++i){large_packed[14+i*9]=255;for(U j=0;j<8;++j)large_packed[15+i*9+j]=(i*8+j)%251;}
 seed("C:\\large.bin",large_packed,sizeof large_packed);h=LZOpenFileA(P("C:\\large.bin"),P(of),0);CHECK(h!=~0u);
 CHECK(LZRead(h,P(large_out),sizeof large_out)==sizeof large_out);for(U i=0;i<sizeof large_out;++i)CHECK(large_out[i]==i%251);LZClose(h);
 packed[8]='B';seed("C:\\bad.bin",packed,sizeof packed);h=CreateFileA(P("C:\\bad.bin"),0x80000000,3,0,3,0,0);CHECK(h!=~0u);CHECK(LZInit(h)==(U)-8);CHECK(CloseHandle(h));
 packed[8]='A';seed("C:\\short.bin",packed,14);h=CreateFileA(P("C:\\short.bin"),0x80000000,3,0,3,0,0);CHECK(h!=~0u);CHECK(LZInit(h)==(U)-3);CHECK(CloseHandle(h));
 LZDone();ExitProcess(0);
}
