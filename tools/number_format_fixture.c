// SPDX-License-Identifier: GPL-2.0-or-later
// Frozen original public vectors, generated from build/nls-format/public-vectors.json.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void SetLastError(U);
API U GetLastError(void);
API U GetNumberFormatA(U,U,const char*,const void*,char*,U);
API U GetCurrencyFormatA(U,U,const char*,const void*,char*,U);
API U RegCreateKeyExA(U,const char*,U,char*,U,U,void*,U*,U*);
API U RegSetValueExA(U,const char*,U,U,const void*,U);
API U RegCloseKey(U);
static void check(int okay,U code){if(!okay)ExitProcess(code);}
static U length(const char*s){U n=0;while(s[n])++n;return n;}
struct Vector{U currency;const char*value;U flags,capacity,locale,custom,fields[5],result,error;const char*output;U unchanged;};
static const struct Vector vectors[]={
    {0,"-1234.567",0u,256u,1033,0,{0,0,0,0,0},10,1985229328u,"-1,234.57",0},
    {0,"999999.995",2147483648u,256u,1033,0,{0,0,0,0,0},13,1985229328u,"1,000,000.00",0},
    {0,"1",0u,0u,0,0,{0,0,0,0,0},5,1985229328u,0,1},
    {0,"1",0u,1u,2048,0,{0,0,0,0,0},0,122u,0,1},
    {0,"-0.004",0u,256u,9,0,{0,0,0,0,0},5,1985229328u,"0.00",0},
    {0,"12.3.4",0u,256u,1033,0,{0,0,0,0,0},0,87u,0,1},
    {0,"1",1u,256u,1033,0,{0,0,0,0,0},0,1004u,0,1},
    {0,"1",0u,256u,2457,0,{0,0,0,0,0},0,87u,0,1},
    {0,"1",0u,4294967295u,1033,0,{0,0,0,0,0},0,87u,0,1},
    {0,"-123456.9996",0u,256u,1033,1,{3,0,2,4,3},16,1985229328u,"12_34_57::000 -",0},
    {0,"1",2147483648u,256u,1033,1,{2,1,3,1,0},0,1004u,0,1},
    {1,"-1234.567",0u,256u,1033,0,{0,0,0,0,0},12,1985229328u,"($1,234.57)",0},
    {1,"999999.995",2147483648u,256u,1033,0,{0,0,0,0,0},14,1985229328u,"$1,000,000.00",0},
    {1,"1",0u,0u,0,0,{0,0,0,0,0},6,1985229328u,0,1},
    {1,"1",0u,1u,2048,0,{0,0,0,0,0},0,122u,0,1},
    {1,"-0.004",0u,256u,9,0,{0,0,0,0,0},6,1985229328u,"$0.00",0},
    {1,"12.3.4",0u,256u,1033,0,{0,0,0,0,0},0,87u,0,1},
    {1,"1",1u,256u,1033,0,{0,0,0,0,0},0,1004u,0,1},
    {1,"1",0u,256u,2457,0,{0,0,0,0,0},0,87u,0,1},
    {1,"1",0u,4294967295u,1033,0,{0,0,0,0,0},0,87u,0,1},
    {1,"-123456.9996",0u,256u,1033,1,{3,0,2,4,3},19,1985229328u,"(12_34_57::000USD)",0},
    {1,"1",2147483648u,256u,1033,1,{2,1,3,1,0},0,1004u,0,1},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,0,0},15,1985229328u,"(USD1_000::00)",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,1,1},14,1985229328u,"-USD1_000::00",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,2,2},14,1985229328u,"USD-1_000::00",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,3,3},14,1985229328u,"USD1_000::00-",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,4,0},15,1985229328u,"(1_000::00USD)",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,5,1},14,1985229328u,"-1_000::00USD",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,6,2},14,1985229328u,"1_000::00-USD",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,7,3},14,1985229328u,"1_000::00USD-",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,8,0},15,1985229328u,"-1_000::00 USD",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,9,1},15,1985229328u,"-USD 1_000::00",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,10,2},15,1985229328u,"1_000::00 USD-",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,11,3},15,1985229328u,"USD 1_000::00-",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,12,0},15,1985229328u,"USD -1_000::00",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,13,1},15,1985229328u,"1_000::00- USD",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,14,2},16,1985229328u,"(USD 1_000::00)",0},
    {1,"-999.995",0u,256u,1033,1,{2,1,3,15,3},16,1985229328u,"(1_000::00 USD)",0},
    {0,"-12.345",0u,256u,1033,0,{0,0,0,0,0},11,1985229328u,"12::3450 ~",0},
    {0,"-12.345",2147483648u,256u,1033,0,{0,0,0,0,0},7,1985229328u,"-12.35",0},
};
static void overrides(void){
    static const char*names[]={"iDigits","sDecimal","sThousand","sGrouping","iNegNumber","sNegativeSign"};
    static const char*values[]={"4","::","_","2;0","4","~"};
    U key=0,disposition=0;
    check(!RegCreateKeyExA(0x80000001u,"Control Panel\\International",0,0,0,0xf003f,0,&key,&disposition),21400);
    for(U i=0;i<6;++i)check(!RegSetValueExA(key,names[i],0,1,values[i],length(values[i])+1),21401+i);
    check(!RegCloseKey(key),21407);
}
void run_number_format_test(void){
    for(U i=0;i<sizeof vectors/sizeof vectors[0];++i){
        const struct Vector*v=&vectors[i];
        if(i==38)overrides();
        U fields[8]={v->fields[0],v->fields[1],v->fields[2],(U)"::",(U)"_",v->fields[3],v->fields[4],(U)"USD"};
        unsigned char guarded[544];for(U j=0;j<sizeof guarded;++j)guarded[j]=0xcc;
        char*out=(char*)guarded+16;
        SetLastError(0x76543210);
        U result=v->currency?GetCurrencyFormatA(v->locale,v->flags,v->value,v->custom?fields:0,out,v->capacity):GetNumberFormatA(v->locale,v->flags,v->value,v->custom?fields:0,out,v->capacity);
        U error=GetLastError(),code=21100+i*6;
        check(result==v->result,code);
        check(error==v->error,code+1);
        U changed=0;
        if(!v->unchanged){
            check(v->output!=0,code+2);changed=length(v->output)+1;
            for(U j=0;j<changed;++j)check((unsigned char)out[j]==(unsigned char)v->output[j],code+2);
        }
        for(U j=changed;j<512;++j)check((unsigned char)out[j]==0xcc,code+3);
        for(U j=0;j<16;++j)check(guarded[j]==0xcc&&guarded[528+j]==0xcc,code+4);
    }
}
#ifdef NUMBER_FORMAT_STANDALONE
void mainCRTStartup(void){run_number_format_test();ExitProcess(0);}
#endif
