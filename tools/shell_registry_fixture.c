/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U SHRegOpenUSKeyA(const char*,U,U,U*,U);
API U SHRegCreateUSKeyA(const char*,U,U,U*,U);
API U SHRegQueryUSValueA(U,const char*,U*,void*,U*,U,const void*,U);
API U SHRegGetUSValueA(const char*,const char*,U*,void*,U*,U,const void*,U);
API U SHRegSetUSValueA(const char*,const char*,U,const void*,U,U);
API U SHRegWriteUSValueA(U,const char*,U,const void*,U,U);
API U SHRegDeleteUSValueA(U,const char*,U);
API U SHRegDeleteEmptyUSKeyA(U,const char*,U);
API U SHRegCloseUSKey(U);
API U SHRegEnumUSKeyA(U,U,char*,U*,U);
API U SHRegEnumUSValueA(U,U,char*,U*,U*,void*,U*,U);
API U SHRegQueryInfoUSKeyA(U,U*,U*,U*,U*,U);
API U SHGetValueA(U,const char*,const char*,U*,void*,U*);
API U SHQueryValueExA(U,const char*,U,U*,void*,U*);
API U SHSetValueA(U,const char*,const char*,U,const void*,U);
API U SHDeleteValueA(U,const char*,const char*);
API U SHDeleteEmptyKeyA(U,const char*);
API U SHDeleteOrphanKeyA(U,const char*);
API U SHRegDuplicateHKey(U);
API U SHEnumKeyExA(U,U,char*,U*);
API U SHEnumValueA(U,U,char*,U*,U*,void*,U*);
API U SHQueryInfoKeyA(U,U*,U*,U*,U*);
API U RegOpenKeyExA(U,const char*,U,U,U*);
API U RegCloseKey(U);
API U RegDeleteKeyA(U,const char*);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);
static int equal(const char*a,const char*b){while(*a && *a==*b)++a,++b;return *a==*b;}
static const char path[]="Software\\NEO98\\ShellRegistryFixture";
int run_shell_registry_test(void)
{
    U key=0,child=0,raw=0,duplicate=0,kind=0,size=0,value=0,cu=11,lm=22,other=33;
    U subkeys=0,maxkey=0,values=0,maxvalue=0;char text[256],name[256];
    /* These are explicit fixture values in a private test subtree. */
    if(SHRegSetUSValueA(path,"Number",4,&cu,4,2))return 8600;
    if(SHRegSetUSValueA(path,"Number",4,&lm,4,8))return 8601;
    SetLastError(0x12345678);size=4;
    if(SHRegGetUSValueA(path,"Number",&kind,&value,&size,0,0,0)||value!=cu||kind!=4||size!=4||GetLastError()!=0x12345678)return 8602;
    size=4;if(SHRegGetUSValueA(path,"Number",&kind,&value,&size,1,0,0)||value!=lm)return 8603;
    if(SHRegSetUSValueA(path,"Number",4,&other,4,1))return 8604;
    size=4;if(SHRegGetUSValueA(path,"Number",0,&value,&size,0,0,0)||value!=cu)return 8605;
    size=4;kind=0x1234;
    if(SHRegGetUSValueA("Software\\NEO98\\DefinitelyMissingShellKey","Missing",&kind,&value,&size,0,&other,4)||value!=other||kind!=0x1234||size!=4)return 8606;
    size=1;if(SHRegGetUSValueA(path,"Number",&kind,&value,&size,0,0,0)!=234||size!=4)return 8607;
    if(SHRegOpenUSKeyA(path,3,0,&key,0)||!key)return 8608;
    if(SHRegWriteUSValueA(key,"Number",4,&other,4,2))return 8609;
    size=4;if(SHRegQueryUSValueA(key,"Number",&kind,&value,&size,0,0,0)||value!=other)return 8610;
    if(SHRegCreateUSKeyA("Child",3,key,&child,15)||!child)return 8611;
    if(SHRegCloseUSKey(child))return 8612;
    if(SHRegQueryInfoUSKeyA(key,&subkeys,&maxkey,&values,&maxvalue,0)||subkeys!=1||values!=1||maxkey!=5||maxvalue!=6)return 8613;
    size=sizeof name;if(SHRegEnumUSKeyA(key,0,name,&size,0)||size!=5||!equal(name,"Child"))return 8614;
    size=sizeof name;if(SHRegEnumUSKeyA(key,1,name,&size,0)!=259)return 8615;
    size=sizeof name;if(SHRegEnumUSKeyA(key,0,name,&size,17)!=120)return 8616;
    size=sizeof name;{U bytes=4;if(SHRegEnumUSValueA(key,0,name,&size,&kind,&value,&bytes,0)||!equal(name,"Number")||value!=other||kind!=4||bytes!=4)return 8617;}
    if(SHRegQueryInfoUSKeyA(key,0,0,0,0,17)!=120)return 8618;
    if(SHRegDeleteEmptyUSKeyA(key,"Child",17))return 8619;
    if(SHRegDeleteUSValueA(key,"Number",1))return 8620;
    size=4;if(SHRegQueryUSValueA(key,"Number",&kind,&value,&size,0,0,0)||value!=lm)return 8621;
    if(SHRegDeleteUSValueA(key,"Number",16)||SHRegCloseUSKey(key))return 8622;
    if(SHRegCloseUSKey(0)!=87||SHRegOpenUSKeyA(path,1,0,0,0)!=87||SHRegCreateUSKeyA(path,3,0,&key,0)!=87)return 8623;
    if(SHSetValueA(0x80000001,path,"Expanded",2,"%TMP%\\shell",12))return 8630;
    size=sizeof text;if(SHGetValueA(0x80000001,path,"Expanded",&kind,text,&size)||kind!=1||size!=22||!equal(text,"C:\\WINDOWS\\TEMP\\shell"))return 8631;
    size=0;if(SHGetValueA(0x80000001,path,"Expanded",&kind,0,&size)||size!=22||kind!=1)return 8632;
    if(RegOpenKeyExA(0x80000001,path,0,3,&raw))return 8633;
    duplicate=SHRegDuplicateHKey(raw);if(!duplicate||duplicate==raw||RegCloseKey(duplicate))return 8634;
    if(SHSetValueA(raw,0,"Plain",1,"raw",3))return 8635; /* original helper adds spare-capacity NUL */
    size=sizeof text;text[3]='!';if(SHQueryValueExA(raw,"Plain",0,&kind,text,&size)||kind!=1||size!=3||!equal(text,"raw"))return 8636;
    if(SHQueryInfoKeyA(raw,&subkeys,&maxkey,&values,&maxvalue)||subkeys||values!=2)return 8637;
    size=sizeof name;{U bytes=sizeof text;if(SHEnumValueA(raw,0,name,&size,&kind,text,&bytes))return 8638;}
    size=sizeof name;if(SHEnumKeyExA(raw,0,name,&size)!=259)return 8639;
    if(SHDeleteValueA(raw,0,"Plain")||SHDeleteValueA(raw,0,"Expanded")||RegCloseKey(raw))return 8640;
    if(SHDeleteEmptyKeyA(0x80000001,path)||SHDeleteOrphanKeyA(0x80000002,path))return 8641;
    if(RegOpenKeyExA(0x80000001,path,0,1,&raw)!=2||RegOpenKeyExA(0x80000002,path,0,1,&raw)!=2)return 8642;
    return 0;
}
#ifdef SHELL_REGISTRY_STANDALONE
void mainCRTStartup(void){ExitProcess(run_shell_registry_test());}
#endif
