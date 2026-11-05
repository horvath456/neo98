// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ComputerName {
using U=unsigned;
inline const char*Path(){return "System\\CurrentControlSet\\Control\\ComputerName\\ComputerName";}
inline const char*Value(){return "ComputerName";}
// an existing key is left untouched. Only the create fallback
// installs the original eight-byte default; failure closes the newly opened key.
template<class E> U Open(E&e){
 U key=0;if(!e.Open(false,key))return key;
 if(e.Open(true,key))return 0;
 const char initial[]="Default";
 if(e.Set(key,initial,sizeof initial)){e.Close(key);return 0;}
 return key;
}
// The original installed byte tables.
// Their low ASCII ranges are copied directly; OEM controls have a separate table.
template<class E> unsigned char ToOem(E&e,unsigned c){return c<128?c:e.AnsiHigh(c-128);}
template<class E> unsigned char ToAnsi(E&e,unsigned c){return c<32?e.OemByte(c):c<128?c:e.OemByte(c-96);}
// The case-map result is deliberately not the public result.
template<class E> U ReadRegistry(E&e,U output,U size){
 U key=Open(e);if(!key){e.Error(1011);return 0;}
 U error=e.Query(key,output,size);e.Close(key);
 if(error){e.Error(error);return 0;}
 U length=e.Length(output);auto*temporary=e.Allocate(length+1);if(!temporary)return 0;
 for(U i=0;i<length;++i)temporary[i]=ToAnsi(e,e.Read8(output+i));temporary[length]=0;
 e.Upper(temporary,length,output);e.Free(temporary);return 1;
}
// This Win98 implementation always requires capacity >=16,
// even for a short cached name. The failure leaves both buffer and size intact.
template<class E> U Get(E&e,U output,U size){
 const U capacity=e.Read32(size);const char*active=e.Active();U length=0;while(active[length])++length;
 if(capacity<16){e.Error(111);return 0;}
 if(length){for(U i=0;i<=length;++i)e.Write8(output+i,active[i]);e.Write32(size,length);return 1;}
 if(!ReadRegistry(e,output,size))return 0;e.Write32(size,e.Read32(size)-1);return 1;
}
// There is no punctuation/empty-name rejection here.
// The registry receives OEM bytes; the boot-active cache is not changed.
template<class E> U Set(E&e,U source){
 U count=e.Length(source)+1;if(int(count)>16){e.Error(111);return 0;}
 U key=Open(e);if(!key){e.Error(1011);return 0;}
 char text[16];for(U i=0;i<count;++i)text[i]=ToOem(e,e.Read8(source+i));
 U error=e.Set(key,text,count);e.Close(key);if(error){e.Error(error);return 0;}return 1;
}
}
