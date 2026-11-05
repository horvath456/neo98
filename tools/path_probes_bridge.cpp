// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_path_probes.h"
#include <cstring>
#include <cstdio>
using namespace Win98PathProbes;
template<class C>U Hash(const C*p){if(!p)return 0;U h=2166136261u;while(*p)h=(h^U(*p++&255))*16777619;return h;}
struct E{const U*r;U mode,used=0,trace[240]={},position=0;void Event(U a,U b=0,U c=0,U d=0){trace[used++]=a;trace[used++]=b;trace[used++]=c;trace[used++]=d;}
 const char*Next(const char*p){return p+((r[8]&&(unsigned char)*p==0x81&&p[1])?2:1);}const unsigned short*Next(const unsigned short*p){return p+1;}
 U SetMode(U v){Event(1,v);U old=mode;mode=v;return old;}
 template<class C>U Attributes(const C*p){Event(2,Hash(p),r[1]);return r[1];}
 template<class C>U Network(const C*p,Resource&info,bool shell){Event(3,Hash(p),shell);info={r[5],r[4]};return r[3];}
 template<class C>bool Pattern(const C*p,C*out){U n=0;while(p[n]){out[n]=p[n];++n;}if(n&&out[n-1]!='\\')out[n++]='\\';out[n++]='*';out[n++]='.';out[n++]='*';out[n]=0;return true;}
 const char*Name(U index){static const char*dots[]={".","..",nullptr};static const char*file[]={"file",nullptr};static const char*after[]={".","..","file",nullptr};static const char*three[]={"...",nullptr};return r[6]==1?dots[index]:r[6]==2?file[index]:r[6]==3?after[index]:three[index];}
 template<class C>void Copy(C*out,const char*in){while((*out++=(unsigned char)*in++));}
 template<class C>U First(const C*p,C*out){Event(4,Hash(p));position=0;if(!r[6])return ~0u;Copy(out,Name(position++));return 7;}
 template<class C>U More(U h,C*out){Event(5,h,position);const char*n=Name(position++);if(!n)return 0;Copy(out,n);return 1;}
 void Close(U h){Event(6,h);}
};
template<class C>U Call(E&e,U op,const C*p,U&attr){if(!op)return ShellExists(e,p,attr);if(op<=2)return Exists(e,p);if(op<=4)return Directory(e,p);if(op<=6)return Empty(e,p);return Unc(e,p,op>=9?3:2);}
extern "C" void path_probes(const U*r,const char*text,U*out){memset(out,0,1024);E e{r,r[2]};U attr=~0u;unsigned short wide[260]={};for(U i=0;text[i];++i)wide[i]=(unsigned char)text[i];out[0]=(r[0]&&!(r[0]&1))?Call(e,r[0],r[7]?nullptr:wide,attr):Call(e,r[0],r[7]?nullptr:text,attr);out[1]=attr;out[2]=e.mode;out[3]=e.used;memcpy(out+4,e.trace,e.used*4);}
#ifdef PATH_PROBES_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;U r[10],exp[256],got[256],n=0;char text[260];while(fread(r,sizeof r,1,f)==1){if(fread(text,sizeof text,1,f)!=1||fread(exp,sizeof exp,1,f)!=1)return 2;path_probes(r,text,got);if(memcmp(exp,got,sizeof got)){fprintf(stderr,"Path probe case%u failed\n",n);return 1;}++n;}fclose(f);printf("PATH PROBES CORPUS %u PASS\n",n);}
#endif
