// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original DIBENG clipping and sample-phase helpers.
// These fields replace the original SS-relative stack records.
namespace Win98DibStretch {
using U = unsigned;
inline int Signed(U v) { return int(v & 0x7fffffffu) - int(v & 0x80000000u ? 0x7fffffff : 0) - int(v >> 31); }
inline int Add(int a,int b){return Signed(U(a)+U(b));}
inline int Sub(int a,int b){return Signed(U(a)-U(b));}
struct Axis {int origin,extent,bound_origin,bound_extent,start,count;};
struct Phase {int error,maximum,minimum;U kind;};
struct Quotient {int value,remainder;};
inline Quotient Divide(U value,int divisor){const int signed_value=Signed(value);return {signed_value/divisor,signed_value%divisor};}
// Original15:0976, including different exact-boundary rules on the two axes.
inline Quotient Forward(const Phase &p,int value){
 auto q=Divide(U(value)*U(p.minimum)-U(p.error),p.maximum);
 if(q.remainder<0)q.remainder=-q.remainder;
 else if(q.remainder){++q.value;q.remainder=p.maximum-q.remainder;}
 else if(p.kind!=2){++q.value;q.remainder=p.maximum;}
 return q;
}
// Original15:093D. The clamp branch calls Forward to recover its remainder.
inline Quotient Inverse(const Phase &p,int value,int limit){
 auto q=Divide(U(value)*U(p.maximum)+U(p.error),p.minimum);
 if(q.value>limit){auto bounded=Forward(p,limit);return {limit,bounded.remainder};}
 if(!q.remainder&&p.kind!=2){--q.value;q.remainder=p.minimum;}
 return q;
}
// Original15:09AD. Empty intersections retain the original partial writes.
inline bool Clip(Axis &a){
 a.start=a.origin;a.count=a.extent;
 const int end=Add(a.origin,a.extent),bound_end=Add(a.bound_origin,a.bound_extent);
 if(end<=a.bound_origin||a.start>=bound_end)return false;
 const int before=Sub(a.start,a.bound_origin);
 if(before<0){a.start=Sub(a.start,before);a.count=Add(a.count,before);}
 const int after=Sub(end,bound_end);if(after>0)a.count=Sub(a.count,after);
 return true;
}
// Original15:0863. Caller orders a as the longer extent and b as the shorter.
inline bool ClipPair(Axis &a,Axis &b,Phase &p){
 int value=Sub(a.start,a.origin),remainder=0;
 if(value){
  const auto q=Forward(p,value);value=Add(q.value,b.origin);remainder=q.remainder;
  if(value>=Add(b.start,b.count))return false;
  if(value>=b.start){const int delta=Sub(value,b.start);b.start=value;b.count=Sub(b.count,delta);p.error=remainder;goto tail;}
 }
 value=Sub(b.start,b.origin);
 if(value){
  const auto q=Inverse(p,value,Sub(Add(a.start,a.count),a.origin));value=Add(q.value,a.origin);remainder=q.remainder;
  const int delta=Sub(value,a.start);a.start=value;a.count=Sub(a.count,delta);
  if(a.count<=0)return false;p.error=remainder;
 }
 tail:
 value=a.count;
 if(value!=a.extent){
  value=Sub(value,1);
  if(value)value=Forward(p,value).value;
  value=Add(value,1);
  if(value<=b.count){b.count=value;return true;}
 }
 value=b.count;
 if(value!=b.extent){value=Sub(value,1);value=Inverse(p,value,a.count).value;a.count=Add(value,1);}
 return true;
}
struct Plan {Axis source,destination;Phase phase;bool growing;};
inline bool Prepare(Plan &p,int source,int source_extent,int source_bound,int destination,int destination_extent,int clip,int clip_extent,U kind){
 p.source={source,source_extent,0,source_bound,0,0};p.destination={destination,destination_extent,clip,clip_extent,0,0};
 p.growing=source_extent<destination_extent;
 const int high=p.growing?destination_extent:source_extent,low=p.growing?source_extent:destination_extent;
 p.phase={high-(low>>1),high,low,kind};
 if(!Clip(p.source)||!Clip(p.destination))return false;
 return p.growing?ClipPair(p.destination,p.source,p.phase):ClipPair(p.source,p.destination,p.phase);
}
}

namespace Win98DibStretch {
// Original15:0585..0603 builds the horizontal step array. The generated
// vertical kernel uses <= on growth and > on shrink, rather than < and >=.
// Keeping those comparisons separate preserves the original tie behavior.
inline void Samples(Plan p,int *indices){
 int source=p.source.start,remaining=p.source.count,error=p.phase.error;
 const int high=p.phase.maximum,low=p.phase.minimum;const bool horizontal=p.phase.kind==2;
 if(!p.growing){while(remaining>1&&error>low){++source;--remaining;error-=low;}}
 for(int i=0;i<p.destination.count;++i){
  indices[i]=source;
  if(p.growing){error-=low;if(horizontal?error<0:error<=0){error+=high;++source;}}
  else{
   ++source;--remaining;error+=high-low;
   while(remaining>1&&(horizontal?error>=low:error>low)){++source;--remaining;error-=low;}
  }
 }
}
}

namespace Win98DibStretch {
struct Rect {int left,top,right,bottom;};
struct Request {int source_width,source_height;Rect source,destination,clip;};
// Original setup15:03A6..056C plus the sample decisions emitted by segment5.
// Storage, format translation and ROP composition stay in the pixel provider.
template<class E> bool Raster(E &e,const Request &r){
 Plan x{},y{};
 if(!Prepare(x,r.source.left,Sub(r.source.right,r.source.left),r.source_width,r.destination.left,Sub(r.destination.right,r.destination.left),r.clip.left,Sub(r.clip.right,r.clip.left),2))return true;
 if(!Prepare(y,r.source.top,Sub(r.source.bottom,r.source.top),r.source_height,r.destination.top,Sub(r.destination.bottom,r.destination.top),r.clip.top,Sub(r.clip.bottom,r.clip.top),1))return true;
 if(x.destination.count<=0||y.destination.count<=0)return false;
 int *indices=e.AllocateSamples(U(x.destination.count+y.destination.count));if(!indices)return false;
 int *rows=indices+x.destination.count;Samples(x,indices);Samples(y,rows);
 for(int row=0;row<y.destination.count;++row)
  for(int column=0;column<x.destination.count;++column)
   e.Pixel(x.destination.start+column,y.destination.start+row,indices[column],rows[row]);
 e.FreeSamples(indices);return true;
}
}
