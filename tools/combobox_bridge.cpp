/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "win98_combobox.h"
#include <cstring>
using U=unsigned;
struct E{U*events;U used=0;U result;U call(U op,U m=0,U w=0,U l=0){events[used++]=op;events[used++]=m;events[used++]=w;events[used++]=l;return result;}U Edit(U m,U w,U l){return call(1,m,w,l);}U List(U m,U w,U l){return call(2,m,w,l);}void Sync(){call(3);}void Drop(bool show){call(4,show);}void Layout(){call(5);}U SelectionHeight(U h){return call(6,h);}U Unsupported(U,U,U){return ~0u;}};
extern "C" U combo_run(Win98ComboBox::State*s,U m,U w,U l,U result,U*trace){E e{trace+1,0,result};U r=Win98ComboBox::Message(e,*s,m,w,l);trace[0]=e.used;return r;}
extern "C" U combo_flags(U s,U f){return Win98ComboBox::Flags(s,f);}
extern "C" U combo_selection_state(U flags,U enabled,U win40){return Win98ComboBox::SelectionState(flags,enabled,win40);}

extern "C" U combo_list_style(U s,U f){return Win98ComboBox::ListStyle(s,f);}
extern "C" U combo_edit_style(U s,U f){return Win98ComboBox::EditStyle(s,f);}
