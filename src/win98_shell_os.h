// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ShellOs {
using U=unsigned;
struct State{U initialized=0,enterprise=0,datacenter=0;unsigned char version[156]{};};
inline U Read32(const unsigned char*p){return p[0]|U(p[1])<<8|U(p[2])<<16|U(p[3])<<24;}
inline void Write32(unsigned char*p,U v){for(U i=0;i<4;++i)p[i]=v>>(i*8);}
// SHLWAPI ordinal 437 caches OS feature queries.
// Cache publication precedes provider calls.
template<class E> U Query(E&e,State&s,U selector){
 if(!s.initialized){s.initialized=1;Write32(s.version,156);if(!e.Version(s.version)){Write32(s.version,148);e.Version(s.version);}
  U key=0;if(!e.Open(key)){char product[260];if(!e.Product(key,product,sizeof product)){if(e.Contains(product,"Enterprise"))s.enterprise=1;else if(e.Contains(product,"DataCenter"))s.datacenter=1;}e.Close(key);}
 }
 const U major=Read32(s.version+4),minor=Read32(s.version+8),build=Read32(s.version+12),platform=Read32(s.version+16),type=s.version[154];
 switch(selector){
 case 0:return platform==1;
 case 1:return platform==2;
 case 2:return platform==1&&major>=4;
 case 3:return platform==2&&major>=4;
 case 4:return platform==2&&major>=5;
 case 5:return platform==1&&(major>4||(major==4&&minor>=10));
 case 6:return platform==1&&major==4&&minor==10&&(build&65535)==0x7ce;
 case 7:return (type==1||type==2||type==3)&&major>=5;
 case 8:return type==1&&major==5;
 case 9:return (type==2||type==3)&&major==5&&!s.enterprise&&!s.datacenter;
 case 10:return (type==2||type==3)&&major==5&&s.enterprise;
 case 11:return (type==2||type==3)&&major==5&&s.datacenter;
 case 12:return e.Metric(0x1000);
 default:return 0;
 }
}
// ASCII case folding for ProductOptions. The native
// non-ASCII substring comparison remains a separate collation provider.
inline unsigned LowerAscii(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
}
