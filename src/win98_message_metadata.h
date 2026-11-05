// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98MessageMetadata {
struct Task {unsigned time=0,position=0,extra=0,last_node=0,flags=0,queue_flags=0,quit=0;
 unsigned quit_code=0,quit_observed=0;};
struct Stamp {unsigned time=0,position=0,extra=0;};
inline unsigned Get(const Task&t,unsigned op){return op==0?t.time:op==1?t.position:t.extra;}
inline unsigned SetExtra(Task&t,unsigned value){unsigned old=t.extra;t.extra=value;return old;}
inline unsigned Pack(int x,int y){return (unsigned(x)&65535)|((unsigned(y)&65535)<<16);}
inline unsigned Time(unsigned supplied,unsigned now){return supplied?supplied:now;}
// USER. Published for both PM_REMOVE and PM_NOREMOVE.
inline void Posted(Task&t,const Stamp&s,unsigned node){t.time=s.time;t.position=s.position;t.last_node=node;t.extra=s.extra;}
// USER. Wake/remove may enter a nested message loop.
struct PostedEnvironment {
 Task&task;explicit PostedEnvironment(Task&t):task(t){}
 virtual unsigned Quit(unsigned,unsigned)=0;virtual unsigned Find()=0;
 virtual Stamp Read(unsigned)=0;virtual void Copy(unsigned,unsigned)=0;
 virtual void Wake()=0;virtual void Remove(unsigned)=0;virtual unsigned Count()const=0;
};
// Complete USER decisions, with queue lookup/copy/unlink providers.
inline unsigned ReceivePosted(PostedEnvironment&e,unsigned remove,unsigned extended){
 if(e.Quit(remove,extended))return 1;
 unsigned node=e.Find();if(node){Posted(e.task,e.Read(node),node);e.Copy(node,extended);
  if(!remove)e.task.last_node=1;
  else {if(e.task.flags&0x2000)e.Wake();e.Remove(node);}
 }
 if(!e.Count()&&!e.task.quit)e.task.queue_flags&=~0x108u;
 return node!=0;
}
// USER. The accepted hardware path uses the processed cursor.
inline void Hardware(Task&t,const Stamp&s,unsigned processed_position,unsigned&queue_marker){queue_marker=1;t.position=processed_position;t.time=s.time;t.extra=s.extra;t.last_node=1;}
// USER. Accepted WM_MOUSEMOVE advances the processed cursor.
inline bool Move(unsigned message,unsigned packet_position,unsigned&processed){if(message!=0x200)return false;bool changed=processed!=packet_position;processed=packet_position;return changed;}
// USER supplies zero time and extra for ordinary PostMessage.
inline Stamp Post(unsigned now,unsigned physical_position){return {now,physical_position,0};}
}
