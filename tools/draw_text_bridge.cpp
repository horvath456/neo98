// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_draw_text.h"
#include <cstring>
#include <cstdio>
using namespace Win98DrawText;
struct E{const char*start;int height,average,external,overhang;U trace[1024]{},used=0;bool failed=false;
 Metrics MetricsFor(U){return {height,external,average,overhang,1,1};}
 int Extent(const char*s,U n){int width=0;for(U i=0;i<n;++i)width+=s[i]==' '?3:s[i]=='&'?4:s[i]=='\r'||s[i]=='\n'?0:5;return n?width+overhang:0;}
 U Source(const char*s,U n){if(s>=start&&s<=start+strlen(start))return U(s-start);U sum=0;for(U i=0;i<n;++i)sum+=(i+1)*(unsigned char)s[i];return 0x80000000u|(sum&0x7fffffffu);}
 void Draw(int x,int y,const char*s,U n,U flags){trace[used++]=U(x);trace[used++]=U(y);trace[used++]=Source(s,n);trace[used++]=n;trace[used++]=!(flags&0x800);}
 void NoProgress(){failed=true;}
};
extern "C" void draw_text(const U*row,const char*text,U*out){Rect r{int(row[3]),int(row[4]),int(row[5]),int(row[6])};Params p{row[8],int(row[9]),int(row[10]),int(row[11]),row[12]};E e{text,int(row[13]),int(row[14]),int(row[15]),int(row[16])};U n=row[1]==~0u?strlen(text):row[1];out[0]=U(Layout(e,text,n,row[1]==0,r,row[0],row[7]?&p:nullptr));out[1]=r.left;out[2]=r.top;out[3]=r.right;out[4]=r.bottom;out[5]=p.drawn;out[6]=e.used;out[7]=e.failed;memcpy(out+8,e.trace,sizeof e.trace);}
#ifdef DRAW_TEXT_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;U row[17],expected[1032],got[1032],count=0;char text[256];while(fread(row,sizeof row,1,f)==1){if(fread(text,sizeof text,1,f)!=1||fread(expected,sizeof expected,1,f)!=1)return 2;draw_text(row,text,got);if(memcmp(expected,got,sizeof got)){fprintf(stderr,"DrawText case%u failed\n",count);return 1;}++count;}fclose(f);printf("DRAWTEXT CORPUS %u PASS\n",count);}
#endif
