// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_volume_information.h"
namespace Win98DriveType {
using U=unsigned;
// KERNEL32: UNC directory validation and error selection.
template<class E> bool Directory(E&e,U root){U a=e.Attributes(root);if(a==~0u)return false;if(a&0x10)return true;e.Error(267);return false;}
template<class E> bool Prefix(E&e,U p,const char*s,U n){for(U i=0;i<n;++i)if(e.Byte(p+i)!=U(static_cast<unsigned char>(s[i])))return false;return true;}
// DOS IOCTL, CD and volume-label services are providers. The volume-label
// provider supplies a normalized label, not a filesystem name.
template<class E> U Classify(E&e,U drive,U root,U label){
 if(!drive)return 1;if(drive==1)return Directory(e,root)?4:1;
 U index=(drive&255)-1;if(!e.Exists(index))return 1;if(e.Cd(index))return 5;
 U flags=0;if(!e.Remote(drive&255,flags))return 0;if(flags&0x1000)return 4;
 if(!e.FixedMedia(drive&255))return 2;
 e.Label(root,label); // The original ignores the label helper's return value.
 if(Prefix(e,label,"MS-RAMDRIVE",11)||Prefix(e,label,"RDV",3)||Prefix(e,label,"VDISK",5))return 6;
 return 3;
}
// Uses the already implemented root parser unchanged.
template<class E> U Query(E&e,U path,U scratch){U drive=Win98Volume::Root(e,path,scratch);return Classify(e,drive,scratch,scratch+264);}
}
