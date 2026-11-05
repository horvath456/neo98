/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
START_TEST(global_contract)
{
    static const unsigned sizes[]={0,1,4,8,9,16,17,24,25,32,33,64,65,100,1024,4096};
    static const unsigned flags[]={0,GMEM_MOVEABLE,GMEM_MOVEABLE|GMEM_DDESHARE};
    unsigned api,f,i;
    for(api=0;api<2;++api)for(f=0;f<3;++f)for(i=0;i<sizeof sizes/sizeof sizes[0];++i){
        HGLOBAL h=api?LocalAlloc(flags[f],sizes[i]):GlobalAlloc(flags[f],sizes[i]);
        SIZE_T size=api?LocalSize(h):GlobalSize(h);
        ok(h!=NULL,"allocation api=%u flags=%x size=%u\n",api,flags[f],sizes[i]);
        ok(size>=sizes[i],"capacity api=%u flags=%x requested=%u actual=%lu\n",api,flags[f],sizes[i],(unsigned long)size);
        trace("GLOBAL_CAPACITY api=%u flags=%x requested=%u actual=%lu\n",api,flags[f],sizes[i],(unsigned long)size);
        if(api)LocalFree(h);else GlobalFree(h);
    }
}
