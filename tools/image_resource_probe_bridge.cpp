// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_image_probe.h"
#include "win98_image_file.h"
using ProbeFn=unsigned(*)(unsigned,unsigned);
extern "C" unsigned image_probe(const unsigned char*p,unsigned size,unsigned icon,unsigned version,ProbeFn f){struct E{const unsigned char*p;ProbeFn f;const unsigned char*Data(){return p;}bool Readable(unsigned a,unsigned b){return f(a,b)!=0;}}e{p,f};return Win98ImageProbe::Icon(e,size,icon,version);}
extern "C" void image_file_entry(unsigned char*out,const unsigned char*file,unsigned index,unsigned icon,unsigned planes,unsigned depth){Win98ImageFile::MakeEntry(out,file,index,icon,planes,depth);}
