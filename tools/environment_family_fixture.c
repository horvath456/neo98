/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetEnvironmentVariableA(const char*,char*,U);
API U SetEnvironmentVariableA(const char*,const char*);
API U ExpandEnvironmentStringsA(const char*,char*,U);
API char *GetEnvironmentStrings(void);
API char *GetEnvironmentStringsA(void);
API U FreeEnvironmentStringsA(char*);
API U GetEnvironmentVariableW(const void*,void*,U);
API U SetEnvironmentVariableW(const void*,const void*);
API U ExpandEnvironmentStringsW(const void*,void*,U);
API U GetEnvironmentStringsW(void);
API U FreeEnvironmentStringsW(void*);
API U GetTempPathA(U,char*);
API void SetLastError(U);
API U GetLastError(void);
API U CreateThread(void*,U,U(__stdcall*)(void*),void*,U,U*);
API U WaitForSingleObject(U,U);
API U GetExitCodeThread(U,U*);
API U CloseHandle(U);
API void ExitProcess(U);
static U length(const char*s){U n=0;while(s[n])++n;return n;}
static int equal(const char*a,const char*b){while(*a && *a==*b)++a,++b;return *a==*b;}
static char large[20001],saved_temp[512];
static U __stdcall environment_worker(void *context)
{
    char name[110],out[40];U i,index=(U)context;
    for(i=0;i<105;++i)name[i]='A'+index;name[105]=0;
    if(!SetEnvironmentVariableA(name,"thread-value"))return 8260;
    {
        char source[110];source[0]='%';for(i=0;i<105;++i)source[i+1]=name[i];source[106]='%';source[107]=0;
        for(i=0;i<20;++i)if(ExpandEnvironmentStringsA(source,out,sizeof out)!=13 || !equal(out,"thread-value"))return 8261;
    }
    if(!SetEnvironmentVariableA(name,0))return 8262;
    return 0;
}
int run_environment_family_test(void)
{
    char out[512];char *block=GetEnvironmentStrings();U i,handles[3],code=0;
    if(!block || GetEnvironmentStringsA()!=block)return 8200;
    SetLastError(0x76543210);
    if(!FreeEnvironmentStringsA(block)||GetLastError()!=0x76543210||GetEnvironmentStrings()!=block)return 8201;
    if(FreeEnvironmentStringsA(block+1)||GetLastError()!=87)return 8202;
    if(!SetEnvironmentVariableA("neo98_env","abcd"))return 8203;
    for(i=0;i<sizeof out;++i)out[i]='!';
    SetLastError(0x76543210);
    if(GetEnvironmentVariableA("NeO98_EnV",out,4)!=5||out[0]!='!'||GetLastError()!=0x76543210)return 8204;
    if(GetEnvironmentVariableA("neo98_env",0,0)!=5)return 8205;
    if(GetEnvironmentVariableA("neo98_env",out,5)!=4||!equal(out,"abcd"))return 8206;
    if(ExpandEnvironmentStringsA("x%NEO98_ENV%y",out,sizeof out)!=7||!equal(out,"xabcdy"))return 8207;
    if(ExpandEnvironmentStringsA("%NEO98_ENV%",0,0)!=5)return 8208;
    SetLastError(0);
    if(ExpandEnvironmentStringsA("%NEO98_MISSING%",out,sizeof out)!=16||!equal(out,"%NEO98_MISSING%")||GetLastError()!=203)return 8209;
    if(!SetEnvironmentVariableA("neo98_env",""))return 8210;
    if(GetEnvironmentVariableA("NEO98_ENV",out,sizeof out)||GetLastError()!=203)return 8211;
    if(!SetEnvironmentVariableA("NEO98_ENV",0))return 8212;
    if(!SetEnvironmentVariableA("=c:","C:\\env-drive")||GetEnvironmentVariableA("=C:",out,sizeof out)!=12||!equal(out,"C:\\env-drive"))return 8213;
    if(!SetEnvironmentVariableA("=C:",0))return 8214;
    if(!GetEnvironmentVariableA("TMP",saved_temp,sizeof saved_temp))return 8215;
    if(!SetEnvironmentVariableA("TMP","C:\\WINDOWS")||GetTempPathA(sizeof out,out)!=11||!equal(out,"C:\\WINDOWS\\"))return 8216;
    if(!SetEnvironmentVariableA("TMP",saved_temp))return 8217;
    for(i=0;i<20000;++i)large[i]='X';large[5000]=0;
    if(!SetEnvironmentVariableA("NEO98_LARGE",large)||GetEnvironmentStrings()!=block)return 8220;
    if(GetEnvironmentVariableA("NEO98_LARGE",0,0)!=5001)return 8221;
    large[5000]='X';large[20000]=0;
    if(SetEnvironmentVariableA("NEO98_LARGE",large)||GetLastError()!=8)return 8222;
    if(GetEnvironmentVariableA("NEO98_LARGE",0,0)!=5001)return 8223;
    if(!SetEnvironmentVariableA("NEO98_LARGE",0))return 8224;
    for(i=0;i<3;++i){handles[i]=CreateThread(0,0,environment_worker,(void*)i,0,0);if(!handles[i])return 8263;}
    for(i=0;i<3;++i){if(WaitForSingleObject(handles[i],10000)!=0||!GetExitCodeThread(handles[i],&code)||code)return code?code:8264;if(!CloseHandle(handles[i]))return 8265;}
    SetLastError(0);if(GetEnvironmentStringsW()||GetLastError()!=120)return 8270;
    SetLastError(0);if(FreeEnvironmentStringsW((void*)1)||GetLastError()!=120)return 8271;
    SetLastError(0);if(GetEnvironmentVariableW((void*)1,(void*)1,~0u)||GetLastError()!=120)return 8272;
    SetLastError(0);if(SetEnvironmentVariableW((void*)1,(void*)1)||GetLastError()!=120)return 8273;
    SetLastError(0);if(ExpandEnvironmentStringsW((void*)1,(void*)1,~0u)||GetLastError()!=120)return 8274;
    if(GetEnvironmentStrings()!=block||!FreeEnvironmentStringsA(block))return 8275;
    return 0;
}
#ifdef ENVIRONMENT_STANDALONE
void mainCRTStartup(void){ExitProcess(run_environment_family_test());}
#endif
