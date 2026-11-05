// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_pen_line.h"
extern "C" unsigned pen_line_query(const unsigned*a,unsigned char*out,unsigned*error){struct Memory{unsigned char*out;unsigned*error;bool Write8(unsigned p,unsigned v){out[p-0x601000]=v;return true;}void Error(unsigned e){*error=e;}}memory{out,error};return Win98PenLine::Query(memory,{a[0],a[1],a[2],a[3],0},a[4],a[5]);}
