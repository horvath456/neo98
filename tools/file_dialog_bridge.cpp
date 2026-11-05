/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_file_dialog.h"
#include <cstring>
#include <cassert>
using U=unsigned;
struct E{
 unsigned char*m;static constexpr U D=0x100000;const U*args=nullptr;U options=0,error=777;U event[32]{},events=0;
 U Byte(U p){assert(p>=D&&p<D+65536);return m[p-D];}void Byte(U p,U v){assert(p>=D&&p<D+65536);m[p-D]=v;}
 U Read(U p){U v;memcpy(&v,m+p-D,4);return v;}void Write(U p,U v){memcpy(m+p-D,&v,4);}
 void Word(U p,U v){Byte(p,v);Byte(p+1,v>>8);}
 bool CompareExtension(const char*value,U p){U i=0;for(;value[i]&&Byte(p+i);++i)if(Lower((unsigned char)value[i])!=Lower(Byte(p+i)))return true;return value[i]||Byte(p+i);}
 void ChangeDirectory(U p){event[events++]=1;event[events++]=Length(p);}
 bool ExcludedRecent(U){return true;}void Recent(U){}void Truncate(U p,U n){Byte(p+n,0);}
 bool ReadOnly(){return args[0];}
 U Pattern(){return D+0x900;}bool HasHook(){return args[0];}
 U Notify(U){event[events++]=2;return args[1];}U LegacyFileOK(){event[events++]=3;return args[2];}
 void ReleaseView(){}void Cleanup(){event[events++]=4;}bool Cancelled(){return args[3];}
 U InitialDirectory(){return D+0xa00;}void End(U result){event[events++]=5;event[events++]=result;}
 U Next(U p){return p+1;}U Lower(U c){return c>='A'&&c<='Z'?c+32:c;}
 U Length(U p){return strlen((char*)m+p-D);}void Copy(U p,U q){memmove(m+p-D,m+q-D,Length(q)+1);}
 void Error(U e){error=e;event[events++]=e;}U Compatibility(){return options;}
 U Explorer(U,bool save){event[events++]=save?101:100;return 1;}U Legacy(U,bool save){event[events++]=save?201:200;return 1;}
};
extern "C" U file_core(U mode,unsigned char*memory,const U*a,U*out){E e{memory};e.args=a;U result=0;
 if(mode==0)result=Win98FileDialog::Parse(e,E::D+0x100,a[0],a[1],a[2]);
 if(mode==1){e.options=a[2];result=Win98FileDialog::Run(e,a[0]?E::D+0x100:0,a[1]);out[0]=e.error;out[1]=e.events;for(U i=0;i<e.events;++i)out[2+i]=e.event[i];}
 if(mode==2){Win98FileDialog::AppendExtension(e,E::D+0x100,a[0]?E::D+0x500:0,a[1]);}
 if(mode==3)Win98FileDialog::Output(e,E::D+0x100,E::D+0x500);
 if(mode==4)Win98FileDialog::Close(e,E::D+0x100,a[4]);
 if(mode>=3){out[0]=e.events;for(U i=0;i<e.events;++i)out[1+i]=e.event[i];}
 return result;
}
#ifdef FILE_DIALOG_SANITIZER
#include <cstdio>
int main(){unsigned char memory[65536];U a[8],mode,wanted,out[64],expected[64];unsigned char after[65536];unsigned cases=0;while(fread(&mode,4,1,stdin)==1){if(fread(a,4,8,stdin)!=8||fread(memory,1,65536,stdin)!=65536||fread(&wanted,4,1,stdin)!=1||fread(expected,4,64,stdin)!=64||fread(after,1,65536,stdin)!=65536)return 2;memset(out,0,sizeof out);if(file_core(mode,memory,a,out)!=wanted||memcmp(expected,out,sizeof out)||memcmp(memory,after,65536))return 3;++cases;}printf("PASS %u original cases ASan/UBSan\n",cases);}
#endif
