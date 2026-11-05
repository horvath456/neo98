// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_profile_buffer.h"
namespace Win98Profile {
struct StructEnvironment {
 virtual unsigned char*Allocate(U)=0;virtual void Free(unsigned char*)=0;
 virtual U Read(unsigned char*,W)=0;virtual U Write(const unsigned char*)=0;
};
// KRNL386. String routing and global memory are native providers.
inline U WriteStruct(StructEnvironment&e,const unsigned char*data,W size){
 if(!data)return e.Write(nullptr);U bytes=U(size)*2+3;unsigned char local[256],*out=bytes>256?e.Allocate(bytes):local;if(!out)return 0;
 const char*hex="0123456789ABCDEF";unsigned char sum=0;U at=0;for(U n=0;n<size;++n){sum+=data[n];out[at++]=hex[data[n]>>4];out[at++]=hex[data[n]&15];}out[at++]=hex[sum>>4];out[at++]=hex[sum&15];out[at]=0;U result=e.Write(out);if(bytes>256)e.Free(out);return result;
}
inline unsigned char Nibble(unsigned char c){return c>='0'&&c<='9'?c-'0':(c-7)&15;}
// KRNL386. Output is already written when checksum validation fails;
// bytes outside the hex alphabet use the original (c-7)&15 fallback.
inline U ReadStruct(StructEnvironment&e,unsigned char*data,W size){
 if(W(size*2)<size||W((size+5)&0x7fff)<W(size&0x7fff))return 0;
 W bytes=W((size+5)*2);unsigned char local[256],*in=bytes>256?e.Allocate(bytes):local;if(!in)return 0;U got=e.Read(in,bytes),result=0;
 if(got==W((size+1)*2)){unsigned char sum=0;for(U n=0;n<=size;++n){unsigned char c=(Nibble(in[n*2])<<4)|Nibble(in[n*2+1]);if(n<size){sum+=c;data[n]=c;}else result=sum==c;}}
 if(bytes>256)e.Free(in);return result;
}
}
