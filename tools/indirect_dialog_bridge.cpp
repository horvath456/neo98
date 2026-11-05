/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_indirect_dialog.h"
extern "C" void indirect_styles(unsigned style,unsigned extended,unsigned version,unsigned*out){Win98IndirectDialog::Styles(style,extended,version!=0);out[0]=style;out[1]=extended;}
struct E{unsigned style;unsigned Desktop(){return 0x9000;}unsigned Style(unsigned){return style;}unsigned Root(unsigned){return 0x8000;}};
extern "C" unsigned indirect_owner(unsigned owner,unsigned style){E e{style};return Win98IndirectDialog::ModalOwner(e,owner);}
