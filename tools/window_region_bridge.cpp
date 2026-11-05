// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_window_region.h"
using namespace Win98WindowRegion;
struct State{
 Window w[8];U valid,desktop,task,queue[8],types[8];
 U defer,defer_ok,defer_region,end_result,combine_result,offset_result;
 U class_record,borrowed,desktop_region,instance,undefined;
 U error_object,error_code;U trace[192],count;
};
struct Provider{
 State&s;
 void Event(U a,U b=0,U c=0,U d=0){if(s.count+4<=192){s.trace[s.count++]=a;s.trace[s.count++]=b;s.trace[s.count++]=c;s.trace[s.count++]=d;}}
 Window Read(U h){return h<8?s.w[h]:Window{};}
 void Write(U h,const Window&v){if(h<8)s.w[h]=v;}
 U Desktop(){return s.desktop;}
 U Resolve(U hwnd){Event(1,hwnd);return hwnd<8&&(s.valid&(1u<<hwnd))?hwnd:0;}
 U ObjectType(U h){Event(2,h);return h<8?s.types[h]:0;}
 void Error(U object,U code){Event(3,object,code);s.error_object=object;s.error_code=code;}
 U CurrentTask(){Event(4);return s.task;}
 U Queue(U task){return task<8?s.queue[task]:0;}
 U BeginDefer(U n){Event(6,n);return s.defer;}
 U Defer(U d,U w,U after,U x,U y,U cx,U cy,U flags){(void)after;(void)x;(void)y;(void)cx;(void)cy;Event(7,d,w,flags);return s.defer_ok;}
 void DeferRegion(U d,U value){s.defer_region=value;(void)d;}
 U EndDefer(U d,U v){Event(9,d,v);return s.end_result;}
 void Delete(U h){Event(10,h);}
 U ClassRegion(U w){Event(11,w);return s.class_record;}
 U BorrowedRegion(U record){return record?s.borrowed:0;}
 U Offset(U h,U dx,U dy){Event(13,h,dx,dy);return s.offset_result;}
 void SetOwner(U h,U owner){Event(14,h,owner);}
 void MakePrivate(U h,U flag){Event(15,h,flag);}
 U Combine(U dst,U a,U b,U mode){Event(16,dst,a,(b<<8)|mode);return s.combine_result;}
 U DesktopRegion(){return s.desktop_region;}
 U UserInstance(){return s.instance;}
 U Undefined(){return s.undefined;}
};
extern "C" U window_region_run(State*state,U op,U a,U b,U c){
 Provider e{*state};
 if(op==0)return Set(e,a,b,c);
 if(op==1)return Get(e,a,b);
 if(op==2){Apply(e,a,b);return 0;}
 return U(Result(a));
}
