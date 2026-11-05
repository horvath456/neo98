// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_multiwait.h"
using U=unsigned;
struct E{
 U*a,*out,n=0,retries=0;
 void event(U code,U a=0,U b=0,U c=0,U d=0){out[1+n++]=code;out[1+n++]=a;out[1+n++]=b;out[1+n++]=c;out[1+n++]=d;out[0]=n;}
 void Error(U v){event(5,v);}void Lock(){event(1);}void Unlock(){event(2);}
 U Resolve(U h){event(3,h);if(h==99){Error(6);return 0;}return 0x30001000+(h==3?1:h)*64;}
 void Release(U o){event(4,o);}
 U Wait(U count,const U*objects,U timeout,U all,U alert){event(6,count,timeout,all,alert);for(U i=0;i<count;++i)event(7,objects[i]);return retries++<a[6]?0x40000001:a[5];}
 void DeliverAPC(){event(8);}
 U Status(){return a[7]|a[8]<<16;}
 bool Allocate(U count){event(10,count*4);return a[9]!=0;}
 void SetMask(U mask){event(11,mask);}void ResetQueue(){event(12);}void PrepareQueue(){event(13);}void ProcessWake(){event(14);}void Free(){event(15);}
 U KernelWait(U count,const U*,U timeout,U all,U alert){event(16,count+1,timeout,all,alert);return a[5];}
 U WaitQueue(U mask,U timeout,U flags){event(17,mask,timeout,flags);return a[5];}
};
extern "C" U multiwait_run(U*a,U*out){E e{a,out};U handles[]={a[10],a[11],a[12]};return a[0]==0?Win98Multiwait::Kernel(e,a[1],handles,a[2],a[3],a[4]):a[0]==1?Win98Multiwait::Message(e,a[1],handles,a[3],a[2],a[4]):Win98Multiwait::MessageOnly(e);}
