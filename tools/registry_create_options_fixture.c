/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Win98 local RegCreateKeyExA ignores unused arguments and probes outputs before creation. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegCreateKeyExA(U,const char *,U,const char *,U,U,void *,U *,U *);
API U RegSetValueExA(U,const char *,U,U,const void *,U);
API U RegQueryValueExA(U,const char *,U,U *,void *,U *);
API U RegCloseKey(U);
API U RegOpenKeyA(U,const char *,U *);
API U RegQueryInfoKeyA(U,char *,U *,U,U *,U *,U *,U *,U *,U *,U *,void *);
static void check(U ok,U code){if(!ok)ExitProcess(code);}
void mainCRTStartup(void){
 static const U options[]={0,1,2,4,8,0xffffffffu};
 char path[]="Software\\NEO98CreateOptions\\0";
 for(U j=0;j<4;++j) for(U i=0;i<6;++i){
  U key=0,disposition=0,value=0x12345600+i,result=0,size=4,type=0;
  path[sizeof(path)-2]=(char)('A'+6*j+i);
  U reserved=j?0xffffffffu:0;
  const char *klass=j==1?"Contract":j>1?(const char *)1:0;
  check(!RegCreateKeyExA(0x80000001u,path,reserved,klass,options[i],j==3?0:0xf003f,(void *)(j>1),&key,&disposition),20+i);
  check(key&&disposition==1,30+i);
  char className[16]="!";U classSize=sizeof className;
  check(!RegQueryInfoKeyA(key,className,&classSize,0,0,0,0,0,0,0,0,0)&&classSize==0&&!className[0],100+i);
  check(!RegSetValueExA(key,"Value",0,4,&value,4)&&!RegCloseKey(key),40+i);
  key=0;disposition=0;
  check(!RegCreateKeyExA(0x80000001u,path,reserved,klass,options[i],j==3?0:0xf003f,(void *)(j>1),&key,&disposition),50+i);
  check(key&&disposition==2,60+i);
  check(!RegQueryValueExA(key,"Value",0,&type,&result,&size)&&type==4&&size==4&&result==value,70+i);
  check(!RegCloseKey(key),80+i);
 }
 check(RegCreateKeyExA(0x80000001u,path,0,0,1,0,0,0,0)==87,90);
 U key=0,disposition=0xdeadbeef;
 check(RegCreateKeyExA(0x80000001u,"Software\\NEO98InvalidOutput",0,0,0,0,0,(U *)1,&disposition)==87&&disposition==0xdeadbeef,91);
 check(RegCreateKeyExA(0x80000001u,"Software\\NEO98InvalidOutput",0,0,0,0,0,&key,(U *)1)==87&&!key,92);
 check(RegOpenKeyA(0x80000001u,"Software\\NEO98InvalidOutput",&key)==2,93);
 check(RegCreateKeyExA(0x1234,"Software",~0u,(const char *)1,~0u,0,(void *)1,&key,&disposition)==6&&disposition==1,94);
 ExitProcess(0);
}
