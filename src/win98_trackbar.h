// SPDX-License-Identifier: GPL-2.0-or-later
// Native trackbar arithmetic matching COMCTL32.
#pragma once
namespace Win98Trackbar {
inline int Clamp(int value,int low,int high){int limited=value<high?value:high;return limited>low?limited:low;}
inline int Pixel(int value,int low,int high,int origin,int extent){
 if(low==high)return origin;
 long long product=static_cast<long long>(int(unsigned(value)-unsigned(low)))*int(unsigned(extent)-1);
 long long divisor=int(unsigned(high)-unsigned(low));bool negative=(product<0)!=(divisor<0);
 unsigned long long a=product<0?-product:product,b=divisor<0?-divisor:divisor;
 long long rounded=(a+b/2)/b;if(negative)rounded=-rounded;
 return int(unsigned(origin)+unsigned(rounded>0x7fffffffLL||rounded< -0x80000000LL?-1:rounded));
}
inline int Page(int low,int high,int page){return page==-1?int(unsigned(high)-unsigned(low))/5:page;}
}
