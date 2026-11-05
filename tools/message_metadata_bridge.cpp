// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_message_metadata.h"
#include <cstring>
using namespace Win98MessageMetadata;
struct State{Task task;Stamp stamp;unsigned candidate,count,quit_result,processed,now,mutation,queue_marker;unsigned char message[24],output[24];unsigned trace[8],trace_count;};
struct Bridge:PostedEnvironment{
 State&s;explicit Bridge(State&v):PostedEnvironment(v.task),s(v){}
 void Trace(unsigned n){s.trace[s.trace_count++]=n;}
 unsigned Quit(unsigned,unsigned)override{Trace(0);return s.quit_result;}
 unsigned Find()override{Trace(1);return s.candidate;}
 Stamp Read(unsigned)override{return s.stamp;}
 void Copy(unsigned,unsigned extended)override{std::memcpy(s.output,s.message,extended?20:18);}
 void Wake()override{Trace(2);if(s.mutation){task.time=0xabcd1234;task.position=0x9876fedc;task.extra=s.mutation;}}
 void Remove(unsigned)override{Trace(3);if(s.count)--s.count;}
 unsigned Count()const override{return s.count;}
};
extern "C" unsigned metadata_run(State*s,unsigned op,unsigned a,unsigned b){switch(op){case 0:case 1:case 2:return Get(s->task,op);case 3:return SetExtra(s->task,a);case 4:{Bridge e(*s);return ReceivePosted(e,a,b);}case 5:Hardware(s->task,s->stamp,s->processed,s->queue_marker);return 0;case 6:return Move(a,b,s->processed);case 7:return Time(a,s->now);case 8:{s->stamp=Post(s->now,s->processed);std::memcpy(s->message+10,&s->stamp.time,4);std::memcpy(s->message+14,&s->stamp.position,4);std::memcpy(s->message+20,&s->stamp.extra,4);return 0;}}return 0;}
