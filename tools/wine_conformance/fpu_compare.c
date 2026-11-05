/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static BOOL nan_bits(unsigned long long bits){return (bits&0x7ff0000000000000ULL)==0x7ff0000000000000ULL && (bits&0xfffffffffffffULL);}
START_TEST(fpu_compare)
{
    static const unsigned long long inputs[]={0,0x8000000000000000ULL,0x3ff0000000000000ULL,0xbff0000000000000ULL,
        0x4020000000000000ULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,
        0x7ff8000000000000ULL,0xfff8000000000000ULL,0x7ff8000000000001ULL,0xfff8000000000001ULL,
        0x7fffffffffffffffULL,0xffffffffffffffffULL,0x7ff0000000000001ULL,0xfff0000000000001ULL};
    static const unsigned long long refs[]={0,0x4020000000000000ULL,0x7ff8000000000000ULL};
    unsigned i,j,mode;unsigned short old,cw=0x037f;
    __asm__ volatile("fnstcw %0" : "=m"(old));
    for(i=0;i<sizeof inputs/sizeof inputs[0];++i)for(j=0;j<sizeof refs/sizeof refs[0];++j)for(mode=0;mode<2;++mode){
        unsigned short status;unsigned expected;unsigned long long roundtrip;
        if(nan_bits(inputs[i])||nan_bits(refs[j]))expected=0x4500;
        else {
            /* Integer ordering of the IEEE encodings avoids using the FPU
               under test to compute the expected condition codes. */
            unsigned long long x=inputs[i],y=refs[j];
            if(!(x<<1))x=0;if(!(y<<1))y=0;
            if(x==y)expected=0x4000;
            else if((x>>63)!=(y>>63))expected=x>>63?0x100:0;
            else expected=(x>>63?(x>y):(x<y))?0x100:0;
        }
        if(!mode)__asm__ volatile("fldcw %2; fnclex; fldl %3; fcompl %4; fnstsw %%ax; fnclex; fldcw %1"
            : "=a"(status) : "m"(old),"m"(cw),"m"(inputs[i]),"m"(refs[j]) : "st");
        else __asm__ volatile("fldcw %2; fnclex; fldl %4; fldl %3; fucompp; fnstsw %%ax; fnclex; fldcw %1"
            : "=a"(status) : "m"(old),"m"(cw),"m"(inputs[i]),"m"(refs[j]) : "st");
        ok((status&0x4500)==expected,"x87 compare input=%I64x reference=%I64x mode=%u flags=%04x expected=%04x\n",inputs[i],refs[j],mode,status,expected);
        __asm__ volatile("fldcw %2; fnclex; fldl %3; fstpl %0; fnclex; fldcw %1"
            : "=m"(roundtrip) : "m"(old),"m"(cw),"m"(inputs[i]) : "st");
        trace("FPU_COMPARE input=%I64x reference=%I64x mode=%u flags=%04x roundtrip=%I64x\n",inputs[i],refs[j],mode,status,roundtrip);
    }
}
