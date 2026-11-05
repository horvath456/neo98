/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API void SetLastError(U);API U GetLastError(void);API U GetLocaleInfoA(U,U,void*,U);
API U GetModuleHandleA(const char*);API U GetProcAddress(U,const char*);
API U CreateCompatibleDC(U);API U DeleteDC(U);API U SaveDC(U);API U RestoreDC(U,int);API U SelectObject(U,U);API U GetStockObject(U);API U GetTextMetricsA(U,void*);API U GetTextFaceA(U,U,char*);API U GetCharWidthW(U,U,U,void*);
API U GetTextCharset(U);API U GetTextCharsetInfo(U,void*,U);API U GetFontLanguageInfo(U);API U TranslateCharsetInfo(U,void*,U);API U EnumFontFamiliesA(U,const char*,void*,U);API U CreateFontIndirectA(const void*);API U DeleteObject(U);
static void check(int ok,U code){if(!ok)ExitProcess(code);}static U dc,seen;
static void selected(void){
 unsigned char metrics[56];char name[32];U width=0;check(GetTextMetricsA(dc,metrics)&&GetTextFaceA(dc,32,name)&&GetCharWidthW(dc,0x20ac,0x20ac,&width)&&width,12300);
 check(!metrics[52]&&GetTextCharset(dc)==metrics[52]&&!GetFontLanguageInfo(dc),12301);
 U signature[8];for(U i=0;i<8;++i)signature[i]=0x12345678;SetLastError(99);check(GetTextCharsetInfo(dc,signature,0xffffffff)==metrics[52]&&GetLastError()==99,12302);
 for(U i=0;i<6;++i)check(!signature[i],12303);check(signature[6]==0x12345678&&signature[7]==0x12345678,12304);
 check(GetTextCharsetInfo(dc,0,123)==GetTextCharset(dc),12305);
}
static U __stdcall font(const unsigned char*lf,const void*tm,U type,U param){
 (void)tm;check(type==1&&param==0x9876,12306);unsigned char local[60];for(U i=0;i<60;++i)local[i]=lf[i];for(U i=4;i<16;++i)local[i]=0;
 U f=CreateFontIndirectA(local),old=SelectObject(dc,f);check(f&&old,12307);selected();check(SelectObject(dc,old)==f&&DeleteObject(f),12308);++seen;return 0x12345678;
}
void run_font_charset_test(void){
 for(U i=0;i<260;++i){U recycled=CreateCompatibleDC(0);check(recycled&&DeleteDC(recycled),12333);}
 dc=CreateCompatibleDC(0);check(dc>65535,12309);selected();check(SaveDC(dc)&&SelectObject(dc,GetStockObject(16)),12310);selected();check(RestoreDC(dc,-1),12311);selected();
 check(EnumFontFamiliesA(dc,"MS Sans Serif",(void*)font,0x9876)==0x12345678&&seen==6,12312);
 const U charset[]={0,128,129,130,134,136,177,178,161,162,186,238,204,222};
 const U page[]={1252,932,949,1361,936,950,1255,1256,1253,1254,1257,1250,1251,874};
 const U mask[]={1,0x20000,0x80000,0x200000,0x40000,0x100000,32,64,8,16,128,2,4,0x10000};
 U result[10];
 for(U i=0;i<14;++i)for(U mode=1;mode<=3;++mode){
  for(U j=0;j<10;++j)result[j]=0xabcdef01;U signature[2]={mask[i],0};U input=mode==1?0x10000|charset[i]:mode==2?0x10000|page[i]:(U)signature;
  SetLastError(99);check(TranslateCharsetInfo(input,result,mode)&&GetLastError()==99,12313);
  check(result[0]==charset[i]&&result[1]==page[i]&&result[6]==mask[i]&&!result[7]&&result[8]==0xabcdef01&&result[9]==0xabcdef01,12314);for(U j=2;j<6;++j)check(!result[j],12315);
 }
 for(U mode=0;mode<=4;mode+=4){for(U i=0;i<8;++i)result[i]=0xabcdef01;SetLastError(99);check(!TranslateCharsetInfo(0,result,mode)&&GetLastError()==99,12316);check(result[0]==0xabcdef01&&result[1]==0xabcdef01&&result[6]==0xabcdef01&&!result[7],12317);for(U i=2;i<6;++i)check(!result[i],12318);}
 U invalid[2]={3,0};check(!TranslateCharsetInfo((U)invalid,result,3),12319);invalid[0]=1;invalid[1]=1;check(!TranslateCharsetInfo((U)invalid,result,3),12320);
 // The original clears fsCsb[1] before reading an overlapping signature.
 result[6]=1;result[7]=0xffffffff;check(TranslateCharsetInfo((U)(result+6),result,3)&&result[0]==0&&result[1]==1252&&result[6]==1&&!result[7],12321);
 SetLastError(99);check(GetTextCharsetInfo(dc,(void*)1,0)==1&&GetLastError()==99,12322);check(!TranslateCharsetInfo(0,(void*)1,1)&&GetLastError()==99,12323);
 check(!TranslateCharsetInfo(1,result,3),12324); // invalid input pointer is probed.
 unsigned char locale[36];SetLastError(99);U locale_result=GetLocaleInfoA(0x409,0x58,locale,34),locale_error=GetLastError();check(!locale_result&&locale_error==87,12325);
 for(U i=0;i<8;++i)result[i]=0xabcdef01;SetLastError(99);check(!TranslateCharsetInfo(0x10409,result,0x1000)&&GetLastError()==locale_error,12326);check(result[0]==0xabcdef01&&result[1]==0xabcdef01&&result[6]==0xabcdef01&&!result[7],12327);
 U mod=GetModuleHandleA("GDI32.DLL");typedef U(__stdcall*Info)(U,void*,U);Info info=(Info)GetProcAddress(mod,(const char*)304);check(info&&!info(dc,result,0),12328);Info translate=(Info)GetProcAddress(mod,(const char*)430);check(translate&&translate(1252,result,2)&&result[1]==1252,12329);
 check(DeleteDC(dc)&&GetTextCharset(dc)==1&&!GetFontLanguageInfo(dc),12330);for(U i=0;i<6;++i)result[i]=0x12345678;check(GetTextCharsetInfo(dc,result,0)==1,12331);for(U i=0;i<6;++i)check(!result[i],12332);
}
#ifdef FONT_CHARSET_STANDALONE
void mainCRTStartup(void){run_font_charset_test();ExitProcess(0);}
#endif
