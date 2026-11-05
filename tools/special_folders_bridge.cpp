// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_special_folders.h"
#include <cstdio>
#include <cstring>
using namespace Win98SpecialFolders;
static U Hash(const char*s){U h=2166136261u;while(*s)h=(h^U((unsigned char)*s++))*16777619;return h;}
struct E {
 const U*r;U trace[900]={},used=0;char item[260]={};bool redirected_created=false,default_created=false,moved=false;U personal=99;
 void Event(U a,U b=0,U c=0,U d=0){trace[used++]=a;trace[used++]=b;trace[used++]=c;trace[used++]=d;}
 U Epoch(){return r[9];}void Lock(){Event(1);}void Unlock(){Event(2);}
 void ReleaseShared(U p){Event(3,p);}void Limit(){Event(99);}
 U Open(U root,const char*sub){Event(4,root,Hash(sub));return root?0x500000+(root&15):0;}
 U Query(U key,const char*name,U*type,char*out){Event(5,key,Hash(name));*type=r[3];strcpy(out,"C:\\Redirect");return r[2];}
 void Close(U key){Event(6,key);}void Windows(char*out){Event(7);strcpy(out,"C:\\WINDOWS");}
 bool LongNames(const char*p){Event(8,Hash(p));return r[12]!=0;}
 void DefaultName(U i,bool lfn,char*out){Event(9,i,lfn);strcpy(out,lfn?Table[i].long_name:Table[i].short_name);}
 void CombineDefault(char*p,const char*leaf){Event(10,Hash(p),Hash(leaf));strcat(p,"\\");strcat(p,leaf);}
 bool Canonicalize(char*p){Event(11,Hash(p));return true;}
 U Attributes(const char*p){bool redirect=!strcmp(p,"C:\\Redirect");U a=(redirect?redirected_created:default_created)?(redirect?r[5]:(r[6]?~0u:16u)):moved?~0u:r[4];Event(12,Hash(p),a);return a;}
 bool MoveCollision(const char*p,U i){Event(13,Hash(p),i);bool success=i>=r[7];if(success)moved=true;return success;}
 U OwnerMedia(U owner,const char*p,U a){Event(14,owner,Hash(p));return a;}
 void CreateDirectories(const char*p){Event(15,Hash(p));if(!strcmp(p,"C:\\Redirect"))redirected_created=true;else default_created=true;}
 void SetHidden(const char*p){Event(16,Hash(p));}
 void Set(U key,const char*name,const char*p){Event(17,key,Hash(name),Hash(p));}
 void PersonalChanged(){personal=0;}
 U Virtual(U id){Event(18,id);return 0x20002000;}
 U PathItem(const char*p){Event(19,Hash(p));strcpy(item,p);return 0x20002000;}
 U Share(U p){Event(20,p);return p?0x20003000:0;}void FreeTemporary(U p){Event(21,p);}
 U PathFromItem(U p,char*out){Event(22,p);strcpy(out,item[0]?item:"C:\\Cached");return 1;}
};
extern "C" void special_folders(const U*row,U*out){memset(out,0,4096);State s;s.epoch=row[10];s.long_names=int(row[11]);int i=Index(row[0]);if(row[8]&&i>=0)s.cached[i]=0x20003000;E e{row};char path[260]={};out[0]=Path(e,s,row[13],path,row[0],row[1]);out[1]=s.epoch;out[2]=U(s.long_names);memcpy(out+3,s.cached,sizeof s.cached);memcpy(out+33,path,260);out[98]=e.personal;out[99]=e.used;memcpy(out+100,e.trace,e.used*4);}
#ifdef SPECIAL_FOLDERS_CORPUS_MAIN
int main(int argc,char**argv){if(argc!=2)return 2;FILE*f=fopen(argv[1],"rb");if(!f)return 2;U row[14],expected[1024],got[1024],count=0;while(fread(row,sizeof row,1,f)==1){if(fread(expected,sizeof expected,1,f)!=1)return 2;special_folders(row,got);if(memcmp(expected,got,sizeof got)){fprintf(stderr,"Special folder case%u failed\n",count);return 1;}++count;}fclose(f);printf("SPECIAL FOLDERS CORPUS %u PASS\n",count);}
#endif
