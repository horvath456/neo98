// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Volume {
using U=unsigned;
// KERNEL32: locate the end of a UNC share component.
template<class E> U ShareEnd(E&e,U p){U at=p+2;while(e.Byte(at)&&e.Byte(at)!='\\')++at;if(!e.Byte(at))return 0;++at;while(e.Byte(at)&&e.Byte(at)!='\\'&&e.Byte(at)!='/')++at;if(e.Byte(at))return at;return e.Byte(at-1)=='\\'?0:at;}
// Return BL drive+1/BH uppercase letter, 1 for UNC,
// or zero for a non-root path. FullPath/current-directory are providers.
template<class E> U Root(E&e,U path,U out){
 U drive=0;if(!path){e.Current(out);drive=1;if(e.Byte(out+1)==':'){e.Byte(out+3,0);goto local;}}
 else{if(!e.FullPath(path,out)||!e.Byte(out))return 0;if(e.Byte(out+1)==':')goto local;}
 {U end=ShareEnd(e,out);if(!end)return 0;if(!drive&&(!e.Byte(end)||e.Byte(end+1)))return 0;e.Byte(end+1,0);return 1;}
local:
 if(e.Byte(out+3))return 0;U letter=e.Byte(out)&0xdf;return(letter<<8)|((letter-'A'+1)&255);
}
// Overflow leaves destination unchanged and clears the
// private source's first byte. The return length excludes the terminator.
template<class E> bool Copy(E&e,U out,U source,U capacity,U&length){U n=0;while(e.Byte(source+n))++n;length=n;if(n+1>capacity){e.Byte(source,0);e.Error(111);return false;}for(U i=0;i<=n;++i)e.Byte(out+i,e.Byte(source+i));return true;}
// Metadata queries preserve the original output order,
// including serial publication before a later filesystem query fails.
// Native providers replace OEM thunk allocation and INT21/IFSMGR services.
template<class E> U Information(E&e,const U*a,U scratch){
 U root=scratch,fs=scratch+264,label=scratch+280,drive=Root(e,a[0],root);
 if(drive<1){e.Error(161);return 0;}U serial=0;
 if(drive>1){if(!e.Exists((drive&255)-1)){e.Error(13);return 0;}if(!e.Serial(drive&255,serial))serial=0;}
 if(a[3])e.Write(a[3],serial);
 U maximum=0,flags=0,error=e.Query(root,fs,maximum,flags);
 if(error){if((error&65535)!=15){e.Error(error&65535);return 0;}maximum=12;flags=0;e.Byte(fs,0);}
 if(a[4])e.Write(a[4],maximum);if(a[5])e.Write(a[5],flags);
 U length=0;if(a[6]&&!Copy(e,a[6],fs,a[7]>8?8:a[7],length))return 0;
 if(a[1]){if(!e.Label(root,label))e.Byte(label,0);if(!Copy(e,a[1],label,a[2],length))return 0;}
 return 1;
}
// original 26-drive IOCTL success mask.
template<class E> U Drives(E&e){U result=0;for(U i=0;i<26;++i)if(e.IoctlDrive(i+1))result|=1u<<i;return result;}
// strict greater-than capacity before each four-byte entry.
// Partial entries already copied remain when the final size is insufficient.
template<class E> U Strings(E&e,U capacity,U out){U mask=Drives(e);if(!mask)return 0;U count=0,cursor=out;for(U i=0;i<26;++i)if(mask&(1u<<i)){count+=4;if(count<capacity){e.Byte(cursor++,'a'+i);e.Byte(cursor++,':');e.Byte(cursor++,'\\');e.Byte(cursor++,0);}}if(count<capacity)e.Byte(cursor,0);else ++count;return count;}
}
