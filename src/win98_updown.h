// SPDX-License-Identifier: GPL-2.0-or-later
// COMCTL32 5.80 family, recovered from the original Win98 image.
// E supplies addressable memory, buddy text, notifications and elapsed time.
#pragma once
namespace Win98UpDown {
using U=unsigned;
template<class E> int Compare(E&e,U t,U a,U b,U mode=0){bool uns=mode==2||((e.Read32(t+0x28)&4)&&mode!=1);return uns?(a>b?1:a<b?-1:0):(int(a)>int(b)?1:int(a)<int(b)?-1:0);}
// wrap selects the opposite endpoint; it is not modulo arithmetic.
template<class E> U Clamp(E&e,U t){U lo=e.Read32(t+0x30),hi=e.Read32(t+0x34),p=e.Read32(t+0x38);if(Compare(e,t,hi,lo)<0){U x=lo;lo=hi;hi=x;}if(Compare(e,t,p,lo)<0){e.Write32(t+0x38,e.Read32(t+8)&1?hi:lo);return 1;}if(Compare(e,t,p,hi)>0){e.Write32(t+0x38,e.Read32(t+8)&1?lo:hi);return 1;}return 0;}
// +..7CA. Text provider truncates at 17 UTF-16 units.
template<class E> U GetPosition(E&e,U t,U error){U bad=1;if(e.ValidBuddy(t)&&(e.Read32(t+8)&2)){U p=0;bool valid=true;if(e.Read32(t+0x3c)==2)p=e.ListPosition(t);else{unsigned short text[18]{};e.BuddyText(t,text,18);unsigned i=0;if(e.Read32(t+0x2c)==16){if(text[0]=='x'||text[0]=='X')i=1;else if(text[0]=='0'&&(text[1]=='x'||text[1]=='X'))i=2;for(;text[i];++i){U c=text[i],v=c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:c>='0'&&c<='9'?c-'0':16;if(v==16){valid=false;break;}p=p*16+v;}}else{unsigned short separator=e.Thousands();bool negative=text[0]=='-';i=negative?1:0;for(;text[i];++i){if(text[i]==separator)continue;U v=U(text[i]-'0');if(v>9){valid=false;break;}p=p*10+v;}if(negative)p=0-p;}}if(valid){e.Write32(t+0x38,p);bad=Clamp(e,t);}}if(error)e.Write32(error,bad);return e.Read32(t+0x38);}
// setter clamps even when UDS_WRAP is set; returns full old DWORD.
template<class E> U SetPosition(E&e,U t,U p){U lo=e.Read32(t+0x30),hi=e.Read32(t+0x34);if(Compare(e,t,hi,lo)<0){U x=lo;lo=hi;hi=x;}if(Compare(e,t,p,lo)<0)p=lo;if(Compare(e,t,p,hi)>0)p=hi;U old=e.Read32(t+0x38);e.Write32(t+0x38,p);e.WriteBuddy(t);return old;}
template<class E> U SetBase(E&e,U t,U base){if(base!=10&&base!=16)return 0;U old=e.Read32(t+0x2c);e.Write32(t+0x28,(e.Read32(t+0x28)&~4u)|(base==16?4:0));e.Write32(t+0x2c,base);e.WriteBuddy(t);return old;}
// /0225: GETACCEL returns the stored count, including a short copy.
template<class E> U SetAccel(E&e,U t,U count,U source){if(!count)return 0;if(count>=3){t=e.Reallocate(t,0x54+count*8);if(!t)return 0;e.Relocated(t);}e.Write32(t+0x48,count);for(U i=0;i<count;++i){e.Write32(t+0x4c+i*8,e.Read32(source+i*8));e.Write32(t+0x50+i*8,e.Read32(source+4+i*8));}return 1;}
template<class E> U GetAccel(E&e,U t,U count,U out){U n=e.Read32(t+0x48),copy=count<n?count:n;for(U i=0;i<copy;++i){e.Write32(out+i*8,e.Read32(t+0x4c+i*8));e.Write32(out+i*8+4,e.Read32(t+0x50+i*8));}return n;}
// pressed bits change only for enabled, non-degenerate ranges.
template<class E> void Press(E&e,U t,bool first,bool second){if(e.Read32(t+0x30)==e.Read32(t+0x34)||!e.Enabled(t))first=second=false;U old=e.Read32(t+0x28),bits=U(first)|(U(second)<<1);if((old&3)!=bits){e.Write32(t+0x28,(old&~3u)|bits);e.Write32(t+0x44,e.Now());e.Dirty(t);}}
// acceleration uses milliseconds >>10, backwards table search,
// mutable UDN_DELTAPOS, then signed divisibility alignment and endpoint clamp.
template<class E> void Step(E&e,U t){U elapsed=(e.Now()-e.Read32(t+0x44))>>10,n=e.Read32(t+0x48),increment=e.Read32(t+0x50);for(U i=n;i;--i)if(e.Read32(t+0x4c+(i-1)*8)<=elapsed){increment=e.Read32(t+0x50+(i-1)*8);break;}if(!increment)return;int direction=Compare(e,t,e.Read32(t+0x30),e.Read32(t+0x34))>=0?1:-1;U flags=e.Read32(t+0x28);if(flags&2)direction=-direction;if(!(flags&3))return;U delta=U(direction)*increment;if(e.Notify(t,e.Read32(t+0x38),delta)||!e.Alive(t))return;U p=e.Read32(t+0x38)+delta;
 // The original increments to the next multiple using IDIV. Widening only
 // avoids C++ overflow; INT_MIN/-1 is an explicit original fault boundary.
 if(int(p)==int(0x80000000u)&&increment==~0u){e.DivideFault();return;}long long divisor=int(increment),remainder=static_cast<long long>(int(p))%divisor;
 if(remainder){long long magnitude=divisor<0?-divisor:divisor;long long adjustment=direction>0?(remainder>0?magnitude-remainder:-remainder):(remainder>0?-remainder:-magnitude-remainder);if(direction>0&&int(p)>=0&&adjustment>0x7fffffffLL-int(p)){long long rem=(-0x80000000LL)%divisor;adjustment=0x80000000LL-int(p)-rem;}else if(direction<0&&int(p)<=0&&-adjustment>static_cast<long long>(int(p))+0x80000000LL){long long rem=0x7fffffffLL%divisor;adjustment=-(static_cast<long long>(int(p))+0x80000001LL+rem);}p+=U(adjustment);}
 e.Write32(t+0x38,p);Clamp(e,t);e.WriteBuddy(t);if(e.Alive(t))e.Scroll(t,4);
}
// the dividing pixel is deliberately neither half.
inline U Hit(U style,int width,int height,int x,int y){if(style&0x40){int middle=width/2;return x<middle?1:x>middle?2:0;}int middle=height/2;return y<middle?2:y>middle?1:0;}
}
