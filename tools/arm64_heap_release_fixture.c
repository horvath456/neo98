/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#define COUNT 196608
static void *blocks[COUNT];
static uintptr_t samples[240];
static int failures;
static void Check(int ok,const char *name){if(!ok){printf("FAIL %s\n",name);++failures;}}
static void Round(unsigned count,int scattered)
{
    unsigned i, made=0, released=0;
    for(i=0;i<count;++i){
        unsigned char *p=malloc(1000);
        if(!p)break;
        blocks[i]=p;p[0]=(unsigned char)i;p[999]=(unsigned char)(i>>8);++made;
    }
    if(made!=count)printf("HEAP_RELEASE allocated=%u/%u errno=%d\n",made,count,errno);
    Check(made==count,"allocate small blocks");
    if(made!=count){while(made)free(blocks[--made]);return;}
    for(i=0;i<240;++i)samples[i]=(uintptr_t)blocks[i*count/240];
    for(i=0;i<count;++i){
        /* 8191 is coprime to both counts, so this visits every block once. */
        unsigned at=scattered?(i*8191u)%count:i;
        unsigned char *p=blocks[at];
        Check(p[0]==(unsigned char)at&&p[999]==(unsigned char)(at>>8),"contents");
        free(p);blocks[at]=NULL;
    }
    for(i=0;i<240;++i){
        MEMORY_BASIC_INFORMATION info;
        if(VirtualQuery((void *)samples[i],&info,sizeof info)==sizeof info&&info.State==MEM_FREE)++released;
    }
    printf("HEAP_RELEASE blocks=%u order=%s released=%u/240\n",count,scattered?"scattered":"sequential",released);
    Check(released>=235,"small groups return address space");
    unsigned char *large=malloc(180u*1024*1024);
    Check(large!=NULL,"large allocation after release");
    if(large){for(i=0;i<180u*1024*1024;i+=4096)large[i]=(unsigned char)(i>>12);free(large);}
}
int main(void)
{
    Round(24576,0);Round(24576,1);Round(COUNT,1);
    printf("HEAP_RELEASE failures=%d\n",failures);
    return failures;
}
