// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_menu_tracking.h"
namespace T=Win98MenuTracking;using T::U;using T::W;using T::S;
struct Event {U kind,a,b,c,d;};
struct TrackingWire {
 U flags,x,y,width,height,param_size,avoid,left,top,right,bottom;
 U thread,owner_thread,existing,left_down,right_down,menu_flags,valid_owner,valid_menu,valid_popup,create_fail,session_fail,action,loop_result,post_commands,animation_enabled;
 U pflags,animation,aux,signature,parent,sflags,sthread,command,extra,input_mode,capture,active,created;
 U count;Event events[128];
};
struct TrackingEnvironment:T::Environment {
 TrackingWire&w;T::Popup p;T::Session s;
 explicit TrackingEnvironment(TrackingWire&v):w(v){p={1,2,0x80,w.parent,W(w.pflags),W(w.animation),W(w.signature),W(w.aux),-1};s.current=(w.existing||w.active)?&p:nullptr;s.flags=W(w.sflags);s.thread=W(w.sthread);s.command=W(w.command);s.extra=W(w.extra);s.input_mode=S(w.input_mode);s.owner=1;}
 ~TrackingEnvironment(){w.pflags=p.flags;w.animation=p.animation;w.aux=p.auxiliary;w.signature=p.signature;w.parent=p.parent;w.sflags=s.flags;w.sthread=s.thread;w.command=s.command;w.extra=s.extra;w.input_mode=W(s.input_mode);w.active=s.current!=nullptr;}
 void event(U k,U a=0,U b=0,U c=0,U d=0){if(w.count<128)w.events[w.count++]={k,a,b,c,d};}
 W CurrentThread()override{return W(w.thread);}W WindowThread(U)override{return W(w.owner_thread);}
 T::Session*CurrentSession()override{return w.existing||w.active?&s:nullptr;}
 bool IsWindow(U h)override{return h==1?w.valid_owner:h==2?w.valid_popup:false;}bool IsMenu(U h)override{return h==0x80&&w.valid_menu;}W MenuFlags(U)override{return W(w.menu_flags);}
 bool ButtonDown(W b)override{event(1,b);return b==1?w.left_down:w.right_down;}
 T::Popup*CreatePopup(U menu,U owner,T::Point at,bool owned)override{event(2,menu,owner,U(W(at.x))|(U(W(at.y))<<16),owned);if(w.create_fail)return nullptr;w.created=1;return &p;}
 T::Session*BeginSession(T::Popup&,W thread)override{event(3,thread);if(w.session_fail)return nullptr;w.active=1;s.current=&p;s.thread=thread;return &s;}
 void FinishSession(T::Session*)override{event(4);w.active=0;s.current=nullptr;}
 U Send(U h,W m,W wp,U lp)override{event(5,h,m,wp,lp);if(m==w.action)w.valid_menu=0;return m==0x403?(w.width&65535)|(w.height<<16):1;}
 void Post(U h,W m,W wp,U lp)override{event(6,h,m,wp,lp);}bool PostCommands(U)override{return w.post_commands;}
 void Select(T::Popup&,S index)override{event(7,W(index));}void CloseSubmenus(T::Session&,T::Popup&)override{event(8);}
 void DestroyPopup(T::Popup&)override{event(9);w.valid_popup=0;}
 U CaptureOwner()override{return w.capture;}void SetCapture(T::Popup&)override{event(10);w.capture=1;}void ReleaseCapture()override{event(11);w.capture=0;}
 U WindowMenu(U)override{event(18);return 0x80;}bool MenuAnimationEnabled()override{event(19);return w.animation_enabled;}
 void StartMenuAnimation(T::Popup&)override{event(20);}
 void Accessibility(W kind,U window)override{event(12,kind,window);}
 T::Rect WorkArea(T::Point p)override{event(13,W(p.x),W(p.y));return {0,0,800,600};}S BorderX()override{return 2;}S BorderY()override{return 2;}
 void AnimationSetting(W a)override{event(14,a);}void PositionPopup(T::Popup&,T::Point p,S width,S height,bool noactivate)override{event(15,W(p.x),W(p.y),U(W(width))|(U(W(height))<<16),noactivate);}
 void UpdatePopup(T::Popup&)override{event(16);}W DispatchLoop(T::Session&,T::Popup&)override{event(17);return W(w.loop_result);}
};
extern "C" U menu_tracking(TrackingWire*w,U operation){TrackingEnvironment e(*w);T::Rect avoid{S(w->left),S(w->top),S(w->right),S(w->bottom)};
 if(operation==0)return T::Track(e,0x80,1,{S(w->x),S(w->y)},W(w->flags),w->avoid?&avoid:nullptr,w->param_size);
 if(operation==1)return T::InitializePopup(e,e.s,e.p);if(operation==2){T::Close(e,e.s,0x111,77,0x12345678);return 0;}return T::End(e);}
#ifdef MENU_TRACKING_REPLAY
#include <cstdio>
#include <cstring>
int main(int argc,char**argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;TrackingWire before,expected;U args[2],count=0;while(fread(&before,sizeof before,1,f)==1){if(fread(args,sizeof args,1,f)!=1||fread(&expected,sizeof expected,1,f)!=1)return 3;U result=menu_tracking(&before,args[0]);if((args[0]<2&&result!=args[1])||memcmp(&before,&expected,sizeof before)){fprintf(stderr,"popup replay mismatch %u\n",count);return 4;}++count;}fclose(f);printf("POPUP ORIGINAL REPLAY PASS: %u cases\n",count);}
#endif
