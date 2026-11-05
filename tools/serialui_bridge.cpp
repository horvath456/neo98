// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_serialui.h"
#include <cstring>
struct Wire { unsigned mode, name, config, size, resolve, query_error, bytes, set_error, result, queries, profiles; unsigned char memory[256], dcb[64]; };
struct Env {
    Wire &w;
    unsigned Read32(unsigned p) { unsigned v; memcpy(&v,w.memory+p,4);return v; }
    void Write32(unsigned p,unsigned v) { memcpy(w.memory+p,&v,4); }
    void Write16(unsigned p,unsigned v) { unsigned short s=v;memcpy(w.memory+p,&s,2); }
    bool Resolve(unsigned,unsigned &instance,unsigned &driver) {instance=11;driver=12;return w.resolve;}
    unsigned QueryDcb(unsigned,unsigned p,unsigned &n) {++w.queries;if(w.query_error)return w.query_error;n=w.bytes;if(p)memcpy(w.memory+p,w.dcb,n);return 0;}
    unsigned SetDcb(unsigned,unsigned p,unsigned n) {memcpy(w.dcb,w.memory+p,n);return w.set_error;}
    void Profile(unsigned,unsigned) {++w.profiles;}
};
extern "C" void run(Wire *w) {Env e{*w};w->result=w->mode?Win98SerialUi::Set(e,w->name,w->config,w->size):Win98SerialUi::Get(e,w->name,w->config,w->size);}
