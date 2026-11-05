/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static int rounded(int quarters,unsigned mode)
{
    int value=quarters/4,remainder=quarters%4;
    if(mode==0){
        int magnitude=remainder<0?-remainder:remainder;
        if(magnitude>2 || (magnitude==2 && (value&1)))value+=quarters<0?-1:1;
    }else if(mode==1 && remainder<0)--value;
    else if(mode==2 && remainder>0)++value;
    return value;
}
START_TEST(fpu_integer)
{
    static const int inputs[]={-131077,-131075,-131074,-131073,-131072,-131071,-131070,-131069,
        -6,-2,0,2,6,131065,131066,131067,131068,131069,131070,131071,131072,131073};
    unsigned precision,mode,i;const int four=4;
    for(precision=2;precision<=3;++precision)for(mode=0;mode<4;++mode)for(i=0;i<sizeof(inputs)/sizeof(inputs[0]);++i){
        unsigned short old,cw=(precision<<8)|(mode<<10)|0x7f,status;
        short result;int expected=rounded(inputs[i],mode),invalid=expected < -32768 || expected > 32767;
        __asm__ volatile("fnstcw %0" : "=m"(old));
        __asm__ volatile("fldcw %3; fnclex; fildl %4; fidivl %5; fistps %0; fnstsw %%ax; fnclex; fldcw %2"
            : "=m"(result),"=a"(status) : "m"(old),"m"(cw),"m"(inputs[i]),"m"(four) : "st");
        if(invalid){
            ok(result==-32768,"FIST16 overflow pc=%u rc=%u quarters=%d result=%d\n",precision,mode,inputs[i],result);
            ok(status&1,"FIST16 overflow pc=%u rc=%u quarters=%d status=%04x\n",precision,mode,inputs[i],status);
        }else{
            ok(result==expected,"FIST16 valid pc=%u rc=%u quarters=%d result=%d expected=%d\n",precision,mode,inputs[i],result,expected);
            ok(!(status&1),"FIST16 valid pc=%u rc=%u quarters=%d status=%04x\n",precision,mode,inputs[i],status);
        }
    }
}
