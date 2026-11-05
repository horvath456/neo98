// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_stretch.h"
#include <cstring>
extern "C" unsigned dib_stretch_axis(unsigned operation,int *values){
 using namespace Win98DibStretch;Axis a{},b{};Phase p{};
 memcpy(&a,values,sizeof a);memcpy(&b,values+6,sizeof b);memcpy(&p,values+12,sizeof p);
 unsigned result=0;
 if(operation==0)result=Clip(a);
 if(operation==1)result=ClipPair(a,b,p);
 if(operation==2||operation==3){auto q=operation==2?Forward(p,values[16]):Inverse(p,values[16],values[17]);values[18]=q.value;values[19]=q.remainder;}
 memcpy(values,&a,sizeof a);memcpy(values+6,&b,sizeof b);memcpy(values+12,&p,sizeof p);return result;
}
#ifdef DIB_STRETCH_AXIS_STANDALONE
#include <cstdio>
int main(int argc,char **argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;unsigned op,result,count=0;int before[20],after[20];while(fread(&op,4,1,f)==1){if(fread(&result,4,1,f)!=1||fread(before,sizeof before,1,f)!=1||fread(after,sizeof after,1,f)!=1)return 3;if(dib_stretch_axis(op,before)!=result||memcmp(before,after,sizeof before))return 4;++count;}fclose(f);printf("Stretch axes ASan: %u\n",count);return 0;}
#endif
