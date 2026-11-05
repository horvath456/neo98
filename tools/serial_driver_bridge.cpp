/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_serial_driver.h"
extern "C" {
void from_dcb(const unsigned char *in,unsigned char*out){Win98SerialDriver::FromDcb(in,out);}
void to_dcb(const unsigned char *in,unsigned char*out){Win98SerialDriver::ToDcb(in,out);}
unsigned divisor(unsigned rate){return Win98SerialDriver::Divisor(rate);}
unsigned line(unsigned bytes,unsigned parity,unsigned stop,unsigned*out){return Win98SerialDriver::Line(bytes,parity,stop,out[0],out[1]);}
void properties(unsigned char*out,unsigned rx,unsigned tx){Win98SerialDriver::Properties(out,rx,tx);}
void set_dcb(unsigned char*state,const unsigned char*input){Win98SerialDriver::SetDcb(state,input);}
}
