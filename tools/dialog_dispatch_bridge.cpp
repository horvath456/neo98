/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_dialog_dispatch.h"
struct E{unsigned has,callback,written,alive,result=0,*out,count=0;void Event(unsigned v){out[2+count++]=v;}void Result(unsigned v){Event(1);result=v;}unsigned Result(){Event(5);return result;}bool HasProcedure(){return has;}unsigned Call(){Event(2);result=written;return callback;}bool Alive(){Event(3);return alive;}unsigned Default(){Event(4);return 0x44445555;}};
extern "C" unsigned dialog_dispatch(unsigned msg,unsigned has,unsigned callback,unsigned written,unsigned alive,unsigned*out){E e{has,callback,written,alive,0,out};unsigned r=Win98DialogDispatch::Procedure(e,msg);out[0]=e.result;out[1]=e.count;return r;}
