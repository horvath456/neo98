// SPDX-License-Identifier: GPL-2.0-or-later
// x86 guest for the KERNEL32 16/32-bit bridge exports. The stubs below copy
// the shapes the thunk compiler generated into SHELL32, COMCTL32 and WINMM; the thunk data block imitates an
// "LS01" ThunkData32 with both code areas.
#include "kernel_thunks_imports.h"
#define P(x) ((U)(x))
#define CHECK(x) do{if(!(x))ExitProcess(__LINE__);}while(0)
typedef unsigned short W;
U block_entry0,block_entry1,block_table;
// ThunkData32: "LS01", checksum, target table, then the two code-area offsets
// at +0x1c and +0x20 the connector reads, and the areas themselves.
static U block[64]={0x3130534c,0x0002e50f,0,0,0,0,0,0x40,0x80};
static U commctrl_block[64]={0x3130534c,0x00001e9a,0,0,0,0,0,0x40,0x80};
static U commctrl_bad_block[64]={0x3130534c,0x00001e9b,0,0,0,0,0,0x40,0x80};
static U sum_args(U n,const U*a){U s=0;for(U i=0;i<n;++i)s=s*3+a[i];return s;}
#define PROC(n,...) static U __stdcall proc##n(__VA_ARGS__)
PROC(0,void){return 0x100;}
PROC(1,U a){U v[]={a};return sum_args(1,v);}
PROC(2,U a,U b){U v[]={a,b};return sum_args(2,v);}
PROC(3,U a,U b,U c){U v[]={a,b,c};return sum_args(3,v);}
PROC(4,U a,U b,U c,U d){U v[]={a,b,c,d};return sum_args(4,v);}
PROC(5,U a,U b,U c,U d,U e){U v[]={a,b,c,d,e};return sum_args(5,v);}
PROC(6,U a,U b,U c,U d,U e,U f){U v[]={a,b,c,d,e,f};return sum_args(6,v);}
PROC(7,U a,U b,U c,U d,U e,U f,U g){U v[]={a,b,c,d,e,f,g};return sum_args(7,v);}
PROC(8,U a,U b,U c,U d,U e,U f,U g,U h){U v[]={a,b,c,d,e,f,g,h};return sum_args(8,v);}
PROC(9,U a,U b,U c,U d,U e,U f,U g,U h,U i){U v[]={a,b,c,d,e,f,g,h,i};return sum_args(9,v);}
PROC(10,U a,U b,U c,U d,U e,U f,U g,U h,U i,U j){U v[]={a,b,c,d,e,f,g,h,i,j};return sum_args(10,v);}
PROC(11,U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k){U v[]={a,b,c,d,e,f,g,h,i,j,k};return sum_args(11,v);}
PROC(12,U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k,U l){U v[]={a,b,c,d,e,f,g,h,i,j,k,l};return sum_args(12,v);}
PROC(13,U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k,U l,U m){U v[]={a,b,c,d,e,f,g,h,i,j,k,l,m};return sum_args(13,v);}
PROC(14,U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k,U l,U m,U n){U v[]={a,b,c,d,e,f,g,h,i,j,k,l,m,n};return sum_args(14,v);}
PROC(15,U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k,U l,U m,U n,U o){U v[]={a,b,c,d,e,f,g,h,i,j,k,l,m,n,o};return sum_args(15,v);}
static U __stdcall inserted(U data,U a,U b){return data*1000+a*10+b;}
U __stdcall qt_stub(U a,U b);U __stdcall qt_import_stub(U a,U b);U __stdcall ft_stub(U a,U b);U __stdcall ft_mask_stub(U a,U b);
U __stdcall ft_import_stub(void);U __stdcall ft_fault_stub(U a,U b,U code);
U __stdcall ft_exit0_stub(void);U __stdcall ft_exit4_stub(U);U __stdcall ft_exit8_stub(U,U);U __stdcall ft_exit12_stub(U,U,U);U __stdcall ft_exit16_stub(U,U,U,U);U __stdcall ft_exit20_stub(U,U,U,U,U);U __stdcall ft_exit24_stub(U,U,U,U,U,U);U __stdcall ft_exit28_stub(U,U,U,U,U,U,U);U __stdcall ft_exit32_stub(U,U,U,U,U,U,U,U);U __stdcall ft_exit36_stub(U,U,U,U,U,U,U,U,U);U __stdcall ft_exit40_stub(U,U,U,U,U,U,U,U,U,U);U __stdcall ft_exit44_stub(U,U,U,U,U,U,U,U,U,U,U);U __stdcall ft_exit48_stub(U,U,U,U,U,U,U,U,U,U,U,U);U __stdcall ft_exit52_stub(U,U,U,U,U,U,U,U,U,U,U,U,U);U __stdcall ft_exit56_stub(U,U,U,U,U,U,U,U,U,U,U,U,U,U);
U register_map(U eax);U register_unmap(U eax);U register_fixarray(U eax,U count,U array);U register_instance(U which,U eax);U register_k32(U which,U eax);
// One stub per FT_Exitn: CH counts the argument dwords, index 11 needs no
// argument, the exit pops n bytes.
#define FT_EXIT_STUB(n) ".globl _ft_exit" #n "_stub@" #n "\n_ft_exit" #n "_stub@" #n ":\n" \
 " movw $((" #n "<<8)|11),%cx\n call *_block_entry1\n call *__imp__FT_Thunk@0\n movswl %ax,%ebx\n jmp *__imp__FT_Exit" #n "@" #n "\n"
__asm__(
 ".text\n"
 FT_EXIT_STUB(0)
 FT_EXIT_STUB(4)
 FT_EXIT_STUB(8)
 FT_EXIT_STUB(12)
 FT_EXIT_STUB(16)
 FT_EXIT_STUB(20)
 FT_EXIT_STUB(24)
 FT_EXIT_STUB(28)
 FT_EXIT_STUB(32)
 FT_EXIT_STUB(36)
 FT_EXIT_STUB(40)
 FT_EXIT_STUB(44)
 FT_EXIT_STUB(48)
 FT_EXIT_STUB(52)
 FT_EXIT_STUB(56)
 // SHELL32 : index in CL, 0x40-byte frame, word arguments, call
 // through the block's frame-indexed code area, CWDE.
 ".globl _qt_stub@8\n_qt_stub@8:\n"
 " movb $12,%cl\n pushl %ebp\n movl %esp,%ebp\n pushl %ecx\n subl $0x3c,%esp\n"
 " pushw 8(%ebp)\n pushw 12(%ebp)\n call *_block_entry0\n cwtl\n leave\n ret $8\n"
 // SHELL32 : the stub reads ThunkData32+8 itself, indexes it with
 // [ebp-4] and calls QT_Thunk; the result is DX:AX.
 ".globl _qt_import_stub@8\n_qt_import_stub@8:\n"
 " movb $12,%cl\n pushl %ebp\n movl %esp,%ebp\n pushl %ecx\n subl $0x3c,%esp\n"
 " pushw 8(%ebp)\n pushw 12(%ebp)\n movl _block_table,%edx\n movzbl -4(%ebp),%ecx\n movl (%edx,%ecx,4),%edx\n"
 " call *__imp__QT_Thunk@0\n shll $16,%eax\n shrdl $16,%edx,%eax\n leave\n ret $8\n"
 // COMCTL32 and WINMM : CX = argument dwords << 10 |
 // index, the CL-indexed code area returns into FT_Prolog, then the word
 // arguments, FT_Thunk, the widening and the FT_Exit jump.
 ".globl _ft_stub@8\n_ft_stub@8:\n"
 " movw $0x080c,%cx\n call *_block_entry1\n pushw 8(%ebp)\n pushw 12(%ebp)\n"
 " call *__imp__FT_Thunk@0\n movswl %ax,%ebx\n jmp *__imp__FT_Exit8@8\n"
 ".globl _ft_mask_stub@8\n_ft_mask_stub@8:\n"
 " movw $0x080c,%cx\n call *_block_entry1\n pushw 8(%ebp)\n pushw 12(%ebp)\n orl $1,-0x14(%ebp)\n"
 " call *__imp__FT_Thunk@0\n movswl %ax,%ebx\n jmp *__imp__FT_Exit8@8\n"
 // FT_Prolog through its import with EDX loaded from the table (index 11).
 ".globl _ft_import_stub@0\n_ft_import_stub@0:\n"
 " movw $0x000b,%cx\n movl _block_table,%edx\n movl 44(%edx),%edx\n call *__imp__FT_Prolog@0\n"
 " call *__imp__FT_Thunk@0\n movswl %ax,%ebx\n jmp *__imp__FT_Exit0@0\n"
 // A fault inside the FT frame: CH bits 0-1 choose the error result, bits
 // 2-7 the argument dwords the handler removes; EAX at the prolog is the
 // "saved EAX" choice.
 ".globl _ft_fault_stub@12\n_ft_fault_stub@12:\n"
 " movl 12(%esp),%ecx\n movl $0x5555,%eax\n call *_block_entry1\n"
 " pushl $0\n pushl $0\n pushl $0\n pushl $0xe0000001\n call *__imp__RaiseException@16\n"
 " movl $0xdead,%ebx\n jmp *__imp__FT_Exit12@12\n"
 // Register entries: SMapLS, SUnMapLS, UnMapSLFixArray, MapHInst*, K32Thk*.
 ".globl _register_map\n_register_map:\n movl 4(%esp),%eax\n call *__imp__SMapLS@0\n ret\n"
 ".globl _register_unmap\n_register_unmap:\n movl 4(%esp),%eax\n xorl %ecx,%ecx\n call *__imp__SUnMapLS@0\n ret\n"
 ".globl _register_fixarray\n_register_fixarray:\n pushl 12(%esp)\n pushl 12(%esp)\n movl 12(%esp),%eax\n call *__imp__UnMapSLFixArray@8\n ret\n"
 ".globl _register_instance\n_register_instance:\n movl 8(%esp),%eax\n movl 4(%esp),%ecx\n"
 " cmpl $0,%ecx\n je 1f\n cmpl $1,%ecx\n je 2f\n cmpl $2,%ecx\n je 3f\n jmp *__imp__MapHInstSL_PN@0\n"
 "1: jmp *__imp__MapHInstLS@0\n2: jmp *__imp__MapHInstLS_PN@0\n3: jmp *__imp__MapHInstSL@0\n"
 ".globl _register_k32\n_register_k32:\n movl 8(%esp),%eax\n cmpl $0,4(%esp)\n jne 1f\n call *__imp__K32Thk1632Prolog@0\n ret\n"
 "1: call *__imp__K32Thk1632Epilog@0\n ret\n"
);
U frame_map(U,U*);
__asm__(".globl _frame_map\n_frame_map:\n pushl %ebp\n movl %esp,%ebp\n"
 " call *__imp__SMapLS_IP_EBP_8@0\n pushl %eax\n call *__imp__MapSL@4\n"
 " movl 12(%ebp),%ecx\n movl %eax,(%ecx)\n"
 " movl 8(%ebp),%eax\n movl %eax,4(%ecx)\n"
 " call *__imp__SUnMapLS_IP_EBP_8@0\n leave\n ret\n");
static void segment_map(void){
 U buffer[4]={1,2,3,4},hmod=GetModuleHandleA(0);CHECK(hmod);
 CHECK(MapLS(0x1234)==0x1234&&MapLS(0)==0);
 U mapped=MapLS(P(buffer));CHECK(mapped&&mapped!=P(buffer)&&!(mapped&65535));
 CHECK(MapSL(mapped)==P(buffer)&&MapSL(mapped+4)==P(buffer)+4&&MapSLFix(mapped)==P(buffer));
 CHECK(MapSL(0x1234)==0x1234&&MapSL(0x00080000)==0&&MapSLFix(0)==0);
 SetLastError(0x1357);UnMapLS(mapped);CHECK(MapSL(mapped)==0);UnMapLS(0x1234);UnMapLS(0);CHECK(GetLastError()==0x1357);
 mapped=register_map(P(buffer));CHECK(MapSL(mapped)==P(buffer));UnMapLS(mapped);
 CHECK(register_map(0x42)==0x42&&register_unmap(0xabc)==0xabc);
 CHECK(register_fixarray(0x1234567,2,P(buffer))==0x1234567&&buffer[0]==1&&buffer[1]==2);
 U mapped_frame[2]={};frame_map(P(buffer),mapped_frame);
 CHECK(mapped_frame[0]==P(buffer)&&mapped_frame[1]);
 frame_map(42,mapped_frame);CHECK(mapped_frame[0]==42&&mapped_frame[1]==0);
 U alias=MapHModuleLS(hmod);CHECK(alias>=32&&alias<0x10000&&alias!=hmod);
 CHECK(register_instance(0,0)==alias&&register_instance(0,hmod)==alias&&register_instance(0,0x1234)==0x1234);
 CHECK(register_instance(0,0x12345678)==0&&register_instance(0,0x12340000)==0);
 CHECK(register_instance(1,0)==0&&register_instance(1,hmod)==alias&&register_instance(1,0x12345678)==0);
 CHECK(register_instance(2,0)==hmod&&register_instance(2,alias)==hmod&&register_instance(2,0x1234)==0);
 CHECK(register_instance(3,0)==0&&register_instance(3,alias)==hmod&&register_instance(3,0x1234)==0);
 CHECK(MapHModuleLS(0)==alias&&MapHModuleLS(hmod)==alias&&MapHModuleLS(0x77)==0x77);
 CHECK(MapHModuleSL(0)==hmod&&MapHModuleSL(alias)==hmod&&MapHModuleSL(0x77)==0);
 CHECK(MapHModuleSL(0x76540000|alias)==hmod); // SL consumes the low WORD.
 U library=LoadLibraryA(P("GDI32.DLL"));CHECK(library);
 U other=MapHModuleLS(library);CHECK(other&&other!=alias&&MapHModuleSL(other)==library);
 CHECK(FreeLibrary(library)&&MapHModuleSL(other)==0);
 CHECK(MapHModuleLS(library)==0); // A base the process no longer holds has no alias.
 library=LoadLibraryA(P("GDI32.DLL"));CHECK(library);
 U reloaded=MapHModuleLS(library);CHECK(reloaded&&reloaded!=other&&MapHModuleSL(other)==0);
 CHECK(MapHModuleSL(reloaded)==library&&FreeLibrary(library));
 U count=0;CHECK(RestoreThunkLock(0));
 CHECK(register_k32(0,0x77)==GetCurrentThreadId()&&register_k32(1,0x99)==0x99);
 CHECK(ReleaseThunkLock(P(&count))==P(&count)&&count==0);
}
static void callbacks(void){
 CHECK(Callback4(P(proc0))==0x100);
 CHECK(Callback8(1,P(proc1))==1);
 CHECK(Callback12(1,2,P(proc2))==5);
 CHECK(Callback16(1,2,3,P(proc3))==18);
 CHECK(Callback20(1,2,3,4,P(proc4))==58);
 CHECK(Callback24(1,2,3,4,5,P(proc5))==179);
 CHECK(Callback28(1,2,3,4,5,6,P(proc6))==543);
 CHECK(Callback32(1,2,3,4,5,6,7,P(proc7))==1636);
 CHECK(Callback36(1,2,3,4,5,6,7,8,P(proc8))==4916);
 CHECK(Callback40(1,2,3,4,5,6,7,8,9,P(proc9))==14757);
 CHECK(Callback44(1,2,3,4,5,6,7,8,9,10,P(proc10))==44281);
 CHECK(Callback48(1,2,3,4,5,6,7,8,9,10,11,P(proc11))==132854);
 CHECK(Callback52(1,2,3,4,5,6,7,8,9,10,11,12,P(proc12))==398574);
 CHECK(Callback56(1,2,3,4,5,6,7,8,9,10,11,12,13,P(proc13))==1195735);
 CHECK(Callback60(1,2,3,4,5,6,7,8,9,10,11,12,13,14,P(proc14))==3587219);
 CHECK(Callback64(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,P(proc15))==10761672);
}
static void allocated_callbacks(void){
 typedef U (__stdcall *F)(U,U);
 U p=AllocLSCallback(P(inserted),0x00c0ffee);CHECK(p);
 const unsigned char*c=(const unsigned char*)p;
 CHECK(c[0]==0x5a&&c[1]==0x68&&c[2]==0xee&&c[3]==0xff&&c[4]==0xc0&&c[5]==0x00&&c[6]==0x52&&c[7]==0xe9);
 CHECK(*(U*)(p+8)==P(inserted)-(p+12));
 CHECK(((F)p)(3,4)==0x00c0ffee*1000u+34);
 CHECK(IsLSCallback(p)==(p|1)&&IsLSCallback(p+1)==0&&IsLSCallback(0)==0&&IsSLCallback(p)==0);
 CHECK(GetLSCallbackTemplate(p)==P(inserted)&&GetLSCallbackTarget(p)==*(U*)(p+4)&&GetLSCallbackTarget(p+1)==0);
 CHECK(GetSLCallbackTarget(p)==0&&GetSLCallbackTemplate(p)==0);
 U s=AllocSLCallback(0x12345678,0xabcdef01);CHECK(s);
 const unsigned char*d=(const unsigned char*)MapSL(s);CHECK(P(d)!=s);
 CHECK(d[0]==0x66&&d[1]==0x5a&&d[2]==0x66&&d[3]==0x68&&d[8]==0x66&&d[9]==0x52&&d[10]==0xea&&*(U*)(d+4)==0xabcdef01&&*(U*)(d+11)==0x12345678);
 CHECK(((s>>16)&7)==7&&(s&0xffff)>=0xc);
 CHECK(IsSLCallback(s)==(P(d)|1)&&IsLSCallback(s)==0&&IsSLCallback(s+1)==0&&IsSLCallback(0)==0);
 CHECK(GetSLCallbackTarget(s)==0x12345678&&GetSLCallbackTemplate(s)==0x12345678&&GetLSCallbackTarget(s)==0);
 CHECK(FreeLSCallback(s)==0&&IsSLCallback(s)==(P(d)|1)); // Windows 98 validates the entry, not its family
 CHECK(FreeSLCallback(s)==P(d)&&IsSLCallback(s)==0); // a free entry is answered, not released again
 CHECK(FreeLSCallback(p)==p&&IsLSCallback(p)==0&&FreeLSCallback(p+1)==0&&FreeLSCallback(0)==0&&FreeSLCallback(0)==0);
 // Page layout: 100 entries per page, LIFO free list, newest page first.
 U e[201];for(U i=0;i<201;++i){e[i]=AllocLSCallback(P(proc0),i);CHECK(e[i]);}
 for(U i=1;i<100;++i)CHECK(e[i]==e[i-1]+0x20);
 for(U i=101;i<200;++i)CHECK(e[i]==e[i-1]+0x20);
 CHECK((e[100]>>16)!=(e[0]>>16)&&(e[100]&0xffff)==(e[0]&0xffff)&&(e[200]>>16)!=(e[100]>>16)&&(e[200]&0xffff)==(e[0]&0xffff));
 CHECK(FreeLSCallback(e[50])==e[50]&&FreeLSCallback(e[150])==e[150]);
 U a1=AllocLSCallback(P(proc0),0);CHECK(a1==e[200]+0x20); // the newest page is searched first
 CHECK(FreeLSCallback(a1)==a1&&AllocLSCallback(P(proc0),0)==a1&&FreeLSCallback(a1)==a1);
 CHECK(IsLSCallback(e[50])==0&&IsLSCallback(e[49])==(e[49]|1));
 for(U i=0;i<201;++i)if(i!=50&&i!=150)CHECK(FreeLSCallback(e[i])==e[i]);
 CHECK(IsLSCallback(e[0])==0&&AllocLSCallback(P(proc0),0)==e[200]&&FreeLSCallback(e[200])==e[200]);
}
static void flat_thunks(void){
 U hmod=GetModuleHandleA(0);
 CHECK(ThunkConnect32(P(block),P("Shl3216_ThunkData16"),P("shell.dll"),P("KTHUNKS.EXE"),hmod,1)==1);
 block_table=block[2];CHECK(block_table);
 block_entry0=P(block)+block[7];block_entry1=P(block)+block[8];
 CHECK(((U*)block_table)[12]!=((U*)block_table)[11]&&((U*)block_table)[0]);
 CHECK(ThunkConnect32(P(block),P("Shl3216_ThunkData16"),P("shell.dll"),P("KTHUNKS.EXE"),hmod,2)==1&&block[2]==block_table);
 CHECK(qt_stub(0,4)==1);
 CHECK(qt_import_stub(0,4)==1);
 CHECK(ft_stub(0,4)==1);
 CHECK(ft_mask_stub(0,4)==1);
 CHECK(ft_import_stub()==1);
 CHECK(ft_exit0_stub()==1);
 CHECK(ft_exit4_stub(0)==1);
 CHECK(ft_exit8_stub(0,1)==1);
 CHECK(ft_exit12_stub(0,1,2)==1);
 CHECK(ft_exit16_stub(0,1,2,3)==1);
 CHECK(ft_exit20_stub(0,1,2,3,4)==1);
 CHECK(ft_exit24_stub(0,1,2,3,4,5)==1);
 CHECK(ft_exit28_stub(0,1,2,3,4,5,6)==1);
 CHECK(ft_exit32_stub(0,1,2,3,4,5,6,7)==1);
 CHECK(ft_exit36_stub(0,1,2,3,4,5,6,7,8)==1);
 CHECK(ft_exit40_stub(0,1,2,3,4,5,6,7,8,9)==1);
 CHECK(ft_exit44_stub(0,1,2,3,4,5,6,7,8,9,10)==1);
 CHECK(ft_exit48_stub(0,1,2,3,4,5,6,7,8,9,10,11)==1);
 CHECK(ft_exit52_stub(0,1,2,3,4,5,6,7,8,9,10,11,12)==1);
 CHECK(ft_exit56_stub(0,1,2,3,4,5,6,7,8,9,10,11,12,13)==1);
 CHECK(ft_fault_stub(7,8,0x0c00)==0);          // CH = 3 dwords << 2 | 0
 CHECK(ft_fault_stub(7,8,0x0d00)==1);          // | 1
 CHECK(ft_fault_stub(7,8,0x0e00)==0x5555);     // | 2: the EAX saved by FT_Prolog
 CHECK(ft_fault_stub(7,8,0x0f00)==0xffffffffu);// | 3
 CHECK(ThunkConnect32(P(block),P("Shl3216_ThunkData16"),P("shell.dll"),P("KTHUNKS.EXE"),hmod,0)==1&&block[2]==0);
 CHECK(ThunkConnect32(P(commctrl_block),P("Cctl1632_ThunkData16"),P("COMMCTRL.DLL"),P("KTHUNKS.EXE"),hmod,1)==1);
 CHECK(commctrl_block[2]!=0);
 CHECK(ThunkConnect32(P(commctrl_bad_block),P("Cctl1632_ThunkData16"),P("COMMCTRL.DLL"),P("KTHUNKS.EXE"),hmod,1)==0&&commctrl_bad_block[2]==0);
}
static void universal_thunks(void){
 U pfn=0x77;SetLastError(0x2468);
 CHECK(UTRegister(GetModuleHandleA(0),P("KTHUNK16.DLL"),0,P("UTProc"),P(&pfn),0,0)==0&&pfn==0x77&&GetLastError()==0x2468);
 CHECK(UTUnRegister(GetModuleHandleA(0))==0);
}
void mainCRTStartup(void){
 segment_map();callbacks();allocated_callbacks();flat_thunks();universal_thunks();
 ExitProcess(0);
}
