/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_serialui_dialog.h"
extern "C" void selection(const unsigned char*d,unsigned*v){Win98SerialUi::DialogSelection(d,v);}
extern "C" void apply(unsigned char*d,const unsigned*v){Win98SerialUi::DialogApply(d,v);}
extern "C" void fifo(unsigned char*s,unsigned mode,unsigned*v){if(mode)Win98SerialUi::FifoWrite(s,v[0]!=0,v[1],v[2]);else{bool enabled;Win98SerialUi::FifoRead(s,enabled,v[1],v[2]);v[0]=enabled;}}
