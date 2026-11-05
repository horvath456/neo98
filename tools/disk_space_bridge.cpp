// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_disk_space.h"
#include <cstring>
extern "C" unsigned disk_space_run(unsigned mode,const unsigned*a,const unsigned*c,unsigned char*memory,unsigned*error){
 struct E {
  const unsigned*c;unsigned char*m;unsigned*error;
  unsigned Byte(unsigned p){return m[p-0x200000];}void Byte(unsigned p,unsigned v){m[p-0x200000]=v;}
  unsigned Read(unsigned p){unsigned v;memcpy(&v,m+p-0x200000,4);return v;}void Write(unsigned p,unsigned v){memcpy(m+p-0x200000,&v,4);}void Error(unsigned v){*error=v;}
  void Current(unsigned p){strcpy((char*)m+p-0x200000,"C:\\Documents");}
  bool FullPath(unsigned p,unsigned out){memmove(m+out-0x200000,m+p-0x200000,strlen((char*)m+p-0x200000)+1);return true;}
  unsigned Attributes(unsigned){if(c[4]==~0u)Error(3);return c[4];}
  bool Basic(unsigned drive,Win98DiskSpace::Geometry&g){g={c[0],c[1],c[2],c[3]};return drive==3&&!c[5];}
  bool Extended(unsigned,Win98DiskSpace::Geometry&g,unsigned&size){g={c[0],c[1],c[2],c[3]};size=c[6];return !c[7];}
 }e{c,memory,error};*error=777;
 return mode==0?Win98DiskSpace::Legacy(e,a,0x201000):mode==1?Win98DiskSpace::Extended(e,a,0x201000):Win98DiskSpace::QueryEx(e,a,0x201000);
}
