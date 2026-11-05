// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_cbt_hooks.h"
extern "C" unsigned cbt_veto(unsigned c,unsigned r){return Win98CbtHooks::Veto(c,r);}
extern "C" unsigned cbt_minmax(unsigned s,unsigned c){return Win98CbtHooks::MinMaxRequest(s,c);}
