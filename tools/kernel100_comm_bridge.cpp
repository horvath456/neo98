/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_comm_parse.h"
extern "C" unsigned kernel100_comm(const char *text,unsigned char *dcb,unsigned char *timeouts){return Win98CommParse::Build(text,dcb,timeouts);}
