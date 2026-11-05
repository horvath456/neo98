// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Local COMDLG32 original parameter/default-printer/handle/DC decisions.
namespace Win98PrintDialog {
using U=unsigned;
struct State {U print=0,selection=0,printer=0,info=0,info_owned=0,version=0;};
template<class E> void Error(E&e,U value){e.ErrorLock();e.ErrorWrite(value);e.ErrorUnlock();}
template<class E> U ExtendedError(E&e){e.ErrorLock();U v=e.ErrorRead();e.ErrorUnlock();return v;}
template<class E> U Wide(E&e,U){e.LastError(120);return 0;}
// Stack buffers and strings are explicit address-space providers.
template<class E> void Names(E&e,State&s)
{
 U p=s.print,device=0,port=0;
 if(s.selection){device=e.Read(s.selection+4);port=e.Read(s.selection+12);}
 else {
  if(!(e.Byte(p+21)&4))return;
  U buffer=e.Buffer();if(!e.Profile("Windows","device",buffer,260)||!e.Alive())return;
  U at=buffer;while(e.Byte(at)==32)++at;device=at;
  while(e.Byte(at)&&e.Byte(at)!=44)++at;e.Byte(at,0);++at;
  while(e.Byte(at)){U ch=e.Byte(at++);if(ch==44)break;}
  while(e.Byte(at)==32)++at;port=at;
 }
 U driver=e.DriverName();U bytes=e.Length(port)+e.Length(device)+e.Length(driver)+27;
 U old=e.Read(p+12),handle=old?e.Reallocate(old,bytes,0x42):e.Allocate(bytes,0x42);
 e.Write(p+12,handle);if(!handle)return;U names=e.Lock(handle);if(!names)return;
 e.Word(names,8);e.CopyString(names+8,driver);
 U offset=(8+e.Length(driver)+1)&65535;e.Word(names+2,offset);e.CopyString(names+offset,device);
 offset=(offset+e.Length(device)+1)&65535;e.Word(names+4,offset);e.CopyString(names+offset,port);
 e.Word(names+6,((e.Byte(p+21)&4)||e.SameName(device,e.DefaultName()))?1:0);
 e.Unlock(handle);
}
// Native DocumentProperties/driver execution is a provider.
template<class E> U DeviceMode(E&e,U owner,U printer,U device,U previous)
{
 int bytes=int(e.DocumentProperties(owner,printer,device,0,0,0));U handle=0;
 if(bytes<=0){U error=e.LastError();if(owner&&(error==0x705||error==0x7e))e.DriverError(owner,device);return 0;}
 handle=previous?e.Reallocate(previous,U(bytes),0x42):e.Allocate(U(bytes),0x42);
 if(!handle){Error(e,9);return 0;}U memory=e.Lock(handle);
 if(!memory){Error(e,10);e.Free(handle);return 0;}
 U result=e.DocumentProperties(owner,printer,device,memory,0,2);
 if(result!=1){Error(e,0x1008);e.Unlock(handle);e.Free(handle);return 0;}
 e.Unlock(handle);return handle;
}
// RETURNIC falls back to CreateDC when CreateIC fails.
template<class E> void DeviceContext(E&e,U p,U names,U mode)
{
 if(e.Byte(p+20)&0x20)e.CopyString(names+e.Word(names+4),e.FilePort());
 U flags=e.Read(p+20)&0x300;
 if(flags==0x200){U dc=e.CreateDC(true,names+e.Word(names),names+e.Word(names+2),names+e.Word(names+4),mode);e.Write(p+16,dc);if(dc)return;}
 else if(flags!=0x100&&flags!=0x300)return;
 e.Write(p+16,e.CreateDC(false,names+e.Word(names),names+e.Word(names+2),names+e.Word(names+4),mode));
}
// RETURNDEFAULT never opens the interactive print dialog.
template<class E> U Defaults(E&e,State&s)
{
 U p=s.print;Error(e,0);
 if(e.Read(p+12)||e.Read(p+8)){Error(e,0x1003);return 0;}
 Names(e,s);if(!e.Alive())return 0;U handle=e.Read(p+12);
 if(handle){U names=e.Lock(handle);if(names){
   U mode=DeviceMode(e,0,0,names+e.Word(names+2),0);e.Write(p+8,mode);
   if(!e.Alive()){e.Unlock(handle);e.Free(handle);e.Write(p+12,0);return 0;}
   if(mode){U memory=e.Lock(mode);if(memory){DeviceContext(e,p,names,memory);e.Unlock(mode);e.Unlock(handle);return e.Alive()?1:0;}}
   e.Unlock(handle);e.Free(handle);e.Write(p+12,0);
 }}
 Error(e,0x1008);return 0;
}
// and7FE25B7E..25B9A, selected-printer/list ownership cleanup.
template<class E> void Cleanup(E&e,State&s)
{if(s.printer){e.ClosePrinter(s.printer);s.printer=0;}s.selection=0;if(s.info){e.Free(s.info);s.info=0;s.info_owned=0;}}
// Warning construction is a UI provider boundary.
template<class E> U ErrorReturn(E&e,State&s,U error)
{
 Error(e,error);
 if(!(e.Byte(s.print+20)&0x80)){
  U value=ExtendedError(e);
  if(s.version>=0x40000||value==0x1008||value==0x100b)e.Warning(s.print,value);
 }
 return 0;
}
// full public validation and the RETURNDEFAULT dispatch branch.
// The interactive dialog is a separate unavailable provider.
template<class E> U Run(E&e,U p)
{
 if(!p){Error(e,2);return 0;}if(e.Read(p)!=66){Error(e,1);return 0;}
 U owner=e.Read(p+4);if(owner&&!e.IsWindow(owner)){Error(e,0xffff);return 0;}
 State s;s.print=p;s.version=e.ProcessVersion();
 U mode=0,names=0,device=0,handle=e.Read(p+8);
 if(handle){mode=e.Lock(handle);if(!mode)return ErrorReturn(e,s,10);}
 handle=e.Read(p+12);if(handle){names=e.Lock(handle);if(!names){if(mode)e.Unlock(e.Read(p+8));return ErrorReturn(e,s,10);}U offset=e.Word(names+2);if(offset)device=names+offset;}
 if(mode&&e.Byte(mode)){
  e.Byte(mode+31,0);if(!e.DeviceExists(mode)){e.Unlock(e.Read(p+8));if(names)e.Unlock(e.Read(p+12));return ErrorReturn(e,s,0x100b);}
 }
 if(device&&e.Byte(device)&&!e.DeviceExists(device)){if(mode)e.Unlock(e.Read(p+8));e.Unlock(e.Read(p+12));return ErrorReturn(e,s,0x100b);}
 if(mode)e.Unlock(e.Read(p+8));if(names)e.Unlock(e.Read(p+12));
 e.Write(p+16,0);if(!e.Services())return ErrorReturn(e,s,0x1004);Error(e,0);
 if(e.Byte(p+21)&4){U result=Defaults(e,s);Cleanup(e,s);return result;}
 return e.Interactive(p,s);
}
}
