/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "kernel_exceptions_fixture.c"
U unwind_expected_sp;extern U unwind_call(void*,struct Record*,U);extern char unwind_return;
static U invalid,raised,calls,collision,mutate,expected_code,expected_flags,frames[3],seen[3];
static int cleanup(struct Record*r,struct Registration*f,U*c,U*d){
 if(r->previous){check(invalid&&r->code==0xc0000026&&r->flags==1,22810);++raised;r->flags=0;return 0;}
 check(calls<3,22801);seen[calls++]=(U)f;
 check(r->code==expected_code&&r->flags==expected_flags,22802);
 check(c[0]==0x10007&&c[44]==(mutate&&calls>1?0xabcdef01:0x12345678)&&c[46]==(U)&unwind_return&&c[49]==unwind_expected_sp,22803);
 if(mutate)c[44]=0xabcdef01;
 if(collision){*d=frames[1];return 3;}return invalid&&(U)f==frames[0]?0:1;
}
static void unwind_test(void){
 struct Registration f[3];U previous=head();for(U i=0;i<3;++i){frames[i]=(U)&f[i];f[i].next=i==2?(void*)~0u:&f[i+1];f[i].handler=cleanup;}
 for(U mode=0;mode<6;++mode){
  __asm__ volatile("movl %0,%%fs:0"::"r"(&f[0]):"memory");calls=0;collision=mode==2;mutate=mode==3;invalid=mode==5;raised=0;
  struct Record r={0xe123abcd,16};expected_code=mode==4?0xc0000026:r.code;expected_flags=mode==4?2:18;
  U target=mode==0?frames[0]:mode==1?~0u:frames[2];
  U result=unwind_call((void*)target,mode==4?0:&r,0x12345678);
  check(result==(mutate?0xabcdef01:0x12345678),22804);
  check(head()==target,22805);check(calls==(mode==0?0:mode==1?3:mode==2?1:2),22806);
  if(calls)check(seen[0]==frames[0],22807);
  if(calls>1)check(seen[1]==frames[1],22808);
  if(mode!=4)check(r.flags==18,22809);check(raised==(mode==5),22811);
 }
 __asm__ volatile("movl %0,%%fs:0"::"r"(previous):"memory");
}
void mainCRTStartup(void){run_kernel_exceptions_test();unwind_test();ExitProcess(0);}
