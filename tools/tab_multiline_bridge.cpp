// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_tab_multiline.h"
#include <cstring>
using U=unsigned;static constexpr U BASE=0x20000000;
struct E{
 unsigned char*m;
 U Read32(U p){U v;std::memcpy(&v,m+p-BASE,4);return v;}void Write32(U p,U v){std::memcpy(m+p-BASE,&v,4);}
 U Array(U,U array,U index,U){return index<Read32(array)?Read32(Read32(array+4)+index*4):0;}
 int TextWidth(U,U item){U p=Read32(item+0x2c);return p?Read32(p+4):0;}
};
extern "C" U tab_multiline(U* row,unsigned char* memory){E e{memory};U t=BASE+0x100;Win98Tab::Metrics metrics{int(row[5]),int(row[6]),int(row[7]),int(row[8])};
 if(row[0]==1){Win98Tab::MoveRow(e,t,row[9]);return 1;}
 if(row[0]==3)return Win98Tab::RowKey(e,t,row[3],row[9]);
 if(row[0]==2)return Win98Tab::StretchRows(e,t,row[7]);
 return Win98Tab::MultilineLayout(e,t,metrics,row[2]);
}
