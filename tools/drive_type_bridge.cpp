/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_drive_type.h"
#include <cstring>
struct E{
 const unsigned*c;const char*label;unsigned error=777,events[32]{},count=0;
 void Event(unsigned x){events[count++]=x;}unsigned Byte(unsigned p){return static_cast<unsigned char>(label[p]);}
 unsigned Attributes(unsigned){Event(1);if(c[0]==~0u)error=3;return c[0];}void Error(unsigned e){error=e;}
 bool Exists(unsigned){Event(2);return c[1];}bool Cd(unsigned){Event(3);return c[2];}
 bool Remote(unsigned,unsigned&flags){Event(4);flags=c[4];return c[3];}
 unsigned FixedMedia(unsigned){Event(5);return c[5];}bool Label(unsigned,unsigned){Event(6);return c[6];}
};
extern "C" unsigned drive_type_run(unsigned drive,const unsigned*c,const char*label,unsigned*out){E e{c,label};unsigned r=Win98DriveType::Classify(e,drive,0,0);out[0]=e.error;out[1]=e.count;memcpy(out+2,e.events,e.count*4);return r;}
