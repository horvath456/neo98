/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

/* Integer operands keep the compiler from quieting a NaN before the probe. */
typedef struct { DWORD lo, hi; } bits64;
typedef struct __attribute__((packed)) { DWORD lo, hi; WORD exponent; } bits80;
typedef int (__cdecl *class_bits)(DWORD, DWORD);

START_TEST(fpu_nan)
{
    static const bits64 values[] = {
        {1,0x7ff00000}, {1,0xfff00000},
        {0xffffffff,0x7ff7ffff}, {0xffffffff,0xfff7ffff},
        {1,0x7ff80000}, {1,0xfff80000}, {0,0x7ff00000}, {0,0xfff00000}
    };
    static const DWORD singles[] = {
        0x7f800001,0xff800001,0x7fbfffff,0xffbfffff,
        0x7fc00001,0xffc00001,0x7f800000,0xff800000
    };
    HMODULE crt=GetModuleHandleA("msvcrt.dll");
    class_bits classify=(class_bits)GetProcAddress(crt,"_fpclass");
    unsigned precision,i,operation;
    unsigned char saved[108];
    ok(classify!=NULL,"original _fpclass available\n");
    if(!classify)return;
    __asm__ volatile("fnsave %0" : "=m"(saved));
    for(precision=0;precision<4;++precision) {
        WORD control=(precision<<8)|0x7f;
        if(precision==1)continue; /* Reserved precision setting. */
        for(i=0;i<sizeof(values)/sizeof(values[0]);++i) {
            for(operation=0;operation<3;++operation) {
                bits64 result={0,0};bits80 extended={0,0,0};
                WORD loaded,stored;
                __asm__ volatile("fninit; fldcw %0" : : "m"(control));
                if(operation==0)
                    __asm__ volatile("fldl %2; fnstsw %0; fstpl %3; fnstsw %1"
                        : "=m"(loaded),"=m"(stored) : "m"(values[i]),"m"(result) : "st","memory");
                else if(operation==1)
                    __asm__ volatile("fldl %2; fnstsw %0; fstpt %3; fnstsw %1"
                        : "=m"(loaded),"=m"(stored) : "m"(values[i]),"m"(extended) : "st","memory");
                else
                    __asm__ volatile("fldl %2; fnstsw %0; fldz; faddp; fstpl %3; fnstsw %1"
                        : "=m"(loaded),"=m"(stored) : "m"(values[i]),"m"(result) : "st","memory");
                trace("FPUNAN pc=%u bits=64 i=%u op=%u result=%08lx%08lx ext=%04x%08lx%08lx load=%04x store=%04x\n",
                    precision,i,operation,result.hi,result.lo,extended.exponent,extended.hi,extended.lo,loaded,stored);
            }
            __asm__ volatile("fninit; fldcw %0" : : "m"(control));
            {
                int result=classify(values[i].lo,values[i].hi);WORD status;
                __asm__ volatile("fnstsw %0" : "=m"(status));
                trace("FPUNAN pc=%u bits=64 i=%u class=%d status=%04x\n",precision,i,result,status);
            }
        }
        for(i=0;i<sizeof(singles)/sizeof(singles[0]);++i) {
            DWORD result;WORD loaded,stored;
            __asm__ volatile("fninit; fldcw %4; flds %2; fnstsw %0; fstps %3; fnstsw %1"
                : "=m"(loaded),"=m"(stored) : "m"(singles[i]),"m"(result),"m"(control) : "st","memory");
            trace("FPUNAN pc=%u bits=32 i=%u result=%08lx load=%04x store=%04x\n",precision,i,result,loaded,stored);
        }
    }
    __asm__ volatile("frstor %0" : : "m"(saved) : "st","memory");
}
