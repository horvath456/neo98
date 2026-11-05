// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_volume_information.h"
namespace Win98DiskSpace {
using U=unsigned;using Q=unsigned long long;
struct Geometry{U sectors,bytes,free,total;};
// KERNEL32. Native Root/Basic replace path and INT21 providers.
template<class E> U Legacy(E&e,const U*a,U scratch){
 U root=Win98Volume::Root(e,a[0],scratch);
 if(root<1){e.Error(123);return 0;}if(root==1){e.Error(50);return 0;}
 Geometry g{};if(!e.Basic(root&255,g)){e.Error(15);return 0;}
 const U values[]={g.sectors,g.bytes,g.free,g.total};for(U i=0;i<4;++i)if(a[i+1])e.Write(a[i+1],values[i]);
 return g.sectors!=0;
}
// Extended DOS7303 stores high halves first. Its first
// sectors*bytes product deliberately retains only EAX before cluster multiply.
template<class E> U Extended(E&e,const U*a,U scratch){
 if(a[0]){if(!e.FullPath(a[0],scratch)){e.Error(123);return 0;}}
 else e.Current(scratch);
 U n=0;while(e.Byte(scratch+n))++n;if(!n){e.Error(123);return 0;}
 if(n>3&&(e.Byte(scratch+n-1)=='\\'||e.Byte(scratch+n-1)=='/'))e.Byte(scratch+n-1,0);
 U attributes=e.Attributes(scratch);if(attributes==~0u)return 0;
 if(!(attributes&16)){e.Error(123);return 0;}
 Geometry g{};U size=0;if(!e.Extended(scratch,g,size)){e.Error(123);return 0;}
 if(size!=0x2c){e.Error(50);return 0;}
 U unit=g.sectors*g.bytes;Q free=Q(unit)*g.free,total=Q(unit)*g.total;
 const U order[]={3,1,2};for(U index:order)if(a[index]){Q value=index==2?total:free;e.Write(a[index]+4,U(value>>32));e.Write(a[index],U(value));}
 return 1;
}
// public Ex wrapper retries the older query after any
// extended-query failure. Original fallback requires all three result pointers.
template<class E> U QueryEx(E&e,const U*a,U scratch){
 if(Extended(e,a,scratch))return 1;
 const U old[]={a[0],scratch+512,scratch+516,scratch+520,scratch+524};
 if(!Legacy(e,old,scratch))return 0;
 if(!a[1]||!a[2]||!a[3]){e.Error(87);return 0;} // Native guard instead of original null fault.
 Q unit=Q(e.Read(scratch+512))*e.Read(scratch+516),free=unit*e.Read(scratch+520),total=unit*e.Read(scratch+524);
 e.Write(a[1],U(free));e.Write(a[1]+4,U(free>>32));e.Write(a[2],U(total));e.Write(a[2]+4,U(total>>32));
 U low=e.Read(a[1]),high=e.Read(a[1]+4);e.Write(a[3],low);e.Write(a[3]+4,high);return 1;
}
}
