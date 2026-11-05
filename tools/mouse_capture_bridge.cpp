// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_mouse_capture.h"
using namespace Win98Capture;
struct State {Globals globals;Queue queues[4];Window windows[8];unsigned current;};
using Callback=unsigned(*)(unsigned,unsigned,unsigned,unsigned);
struct Bridge:Environment{State&s;Callback callback;Bridge(State&v,Callback f):Environment(v.globals),s(v),callback(f){}Queue ReadQueue(unsigned q)const override{return q<4?s.queues[q]:Queue{};}void WriteQueue(unsigned q,const Queue&v)override{if(q<4)s.queues[q]=v;}Window ReadWindow(unsigned h)const override{return h<8?s.windows[h]:Window{};}unsigned CurrentQueue()const override{return s.current;}unsigned Service(unsigned op,unsigned h,unsigned a,unsigned b)override{return callback(op,h,a,b);}};
extern "C" unsigned capture_run(State*s,Callback cb,unsigned op,unsigned h,unsigned mode){Bridge e(*s,cb);switch(op){case 0:return Get(e);case 1:return Set(e,h);case 2:return Release(e);case 3:Change(e,s->current,h,mode);break;case 4:MenuSet(e,h);break;case 5:return MenuRelease(e);case 6:Destroy(e,h);break;case 7:Cancel(e,h);break;}return 0;}
