// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_image_directory.h"
extern "C" unsigned image_directory(const unsigned char*bytes,const unsigned short*display,unsigned icon,unsigned cx,unsigned cy,unsigned flags){
    const Win98ImageDirectory::Display d{display[0],display[1],display[2],display[3],display[4],display[5]};
    return Win98ImageDirectory::Lookup(d,bytes,icon,cx,cy,flags);
}
