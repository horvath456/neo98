/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_combobox.h"
extern "C" unsigned combo_dialog_code(unsigned flags,unsigned key,unsigned has,unsigned message){return Win98ComboBox::DialogCode(flags,key,has,message);}
