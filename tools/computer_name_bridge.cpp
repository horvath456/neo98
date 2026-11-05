// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_computer_name.h"
#include "../build/generated/computer_name_tables.h"
#include <cstring>
using U=unsigned;
struct E {
 const U*c;unsigned char*m;const unsigned char*upper;U*out,error,exists,regsize,events=0;
 unsigned char*at(U p){return m+p-0x200000;}void event(U op,U a=0,U b=0){out[8+events++]=op;out[8+events++]=a;out[8+events++]=b;}
 U Read8(U p){return *at(p);}U Read32(U p){U v;memcpy(&v,at(p),4);return v;}void Write8(U p,U v){*at(p)=v;}void Write32(U p,U v){memcpy(at(p),&v,4);}U Length(U p){return strlen((char*)at(p));}
 const char*Active(){return (char*)at(0x204000);}void Error(U value){error=value;event(8,value);}
 U Open(bool create,U&key){event(1,create);U result=create?c[5]:exists?c[4]:2;if(!result){key=0x88;exists=1;}return result;}
 void Close(U){event(4);}
 U Set(U,const char*text,U count){event(2,count);if(c[6])return c[6];memcpy(at(0x203000),text,count);regsize=count;return 0;}
 U Query(U,U output,U size){event(3,Read32(size));if(c[3])return c[3];U n=Read32(size);Write32(size,regsize);if(n<regsize)return 234;memcpy(at(output),at(0x203000),regsize);return 0;}
 unsigned char*Allocate(U n){event(5,n);return c[7]?nullptr:at(0x205000);}void Free(unsigned char*){event(6);}
 U AnsiHigh(U i){return OriginalComputerAnsiHigh[i];}U OemByte(U i){return OriginalComputerOemBytes[i];}
 void Upper(const unsigned char*text,U n,U output){event(7,n);if(!n){Error(87);return;}for(U i=0;i<n;++i)Write8(output+i,upper[text[i]]);}
};
extern "C" void computer_name(const U*c,const unsigned char*input,const unsigned char*upper,unsigned char*memory,U*out){memcpy(memory,input,0x6000);memset(out,0,128*4);E e{c,memory,upper,out,c[8],c[2],c[9]};out[0]=c[0]?Win98ComputerName::Set(e,0x202000):Win98ComputerName::Get(e,0x201000,0x201100);out[1]=e.error;out[2]=e.regsize;out[3]=e.exists;out[4]=e.events;}
