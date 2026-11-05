// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 and 0x273f: initialize 32-byte x86 call adapters.
// Generated instructions call NEO98's native gates; no 16-bit body is emitted.
#pragma once
namespace Win98ThunkPrime {
inline void Dword(unsigned char *p,unsigned value){for(unsigned i=0;i<4;++i)p[i]=value>>(8*i);}
inline void Template(bool quick,unsigned table,unsigned gate,unsigned char (&out)[32]){
 for(auto &b:out)b=0xcc;
 if(quick){
  out[0]=0x33;out[1]=0xc9; // xor ecx,ecx
  out[2]=0x8a;out[3]=0x4d;out[4]=0xfc; // mov cl,[ebp-4]
  out[5]=0x8b;out[6]=0x14;out[7]=0x8d; // mov edx,[ecx*4+table]
  Dword(out+8,table);out[12]=0xb8;Dword(out+13,gate);
  out[17]=0xff;out[18]=0xe0; // jmp eax
 }else{
  out[0]=0x0f;out[1]=0xb6;out[2]=0xd1; // movzx edx,cl
  out[3]=0x8b;out[4]=0x14;out[5]=0x95; // mov edx,[edx*4+table]
  Dword(out+6,table);out[10]=0x68;Dword(out+11,gate);out[15]=0xc3;
 }
}
}
