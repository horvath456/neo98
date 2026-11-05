/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The generated include contains only the unchanged guard from the user's
 * local original. This oracle supplies its time and fatal-error providers. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void *VirtualAlloc(void*,U,U,U);
API void ExitProcess(U);
#include "heretic-date-original.h"
static U stamp,calls,errors,bad;
static U clock_value(U *out) { ++calls;*out=stamp;return stamp; }
static void fatal(U kind,const char *text) {
 static const char expected[]="Unauthorized access not permitted!\n";
 ++errors;if(kind)bad=1;
 for(U i=0;i<sizeof expected;i++)if(text[i]!=expected[i])bad=1;
}
static void copy(void *target,const void *source,U n) {
 unsigned char *d=target;const unsigned char *s=source;for(U i=0;i<n;i++)d[i]=s[i];
}
static void jump(U where,void *target) {
 *(unsigned char*)where=0xe9;*(U*)(where+1)=(U)target-where-5;
}
void mainCRTStartup(void) {
 if(VirtualAlloc((void*)0x10000000,0x70000,0x3000,0x40)!=(void*)0x10000000)ExitProcess(10);
 copy((void*)0x1002df50,original_guard,sizeof original_guard);
 copy((void*)0x1004d378,"Unauthorized access not permitted!\n",sizeof "Unauthorized access not permitted!\n");
 jump(0x1003a963,clock_value);jump(0x10013f10,fatal);
 const U cases[]={0x35649ff7,0x35649ff8,0x35649ff9,0x363c8dea,0x363c8deb,0x363c8dec,1793318400};
 for(U i=0;i<sizeof cases/sizeof cases[0];i++) {
  stamp=cases[i];calls=errors=bad=0;((void(*)(void))0x1002df50)();
  U rejected=stamp<0x35649ff8||stamp>0x363c8deb;
  if(calls!=1||errors!=rejected||bad)ExitProcess(20+i);
 }
 ExitProcess(0);
}
