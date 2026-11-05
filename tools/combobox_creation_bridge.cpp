/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_combobox.h"
extern "C" void combo_creation(unsigned style,unsigned extended,unsigned*out){out[0]=Win98ComboBox::WindowStyle(style);out[1]=Win98ComboBox::WindowExtendedStyle(extended);out[2]=Win98ComboBox::ListScrollStyle(style);}
