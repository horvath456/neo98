/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_trackbar.h"
extern "C" unsigned track(unsigned op,int value,int low,int high,int origin,int extent){return op==0?Win98Trackbar::Clamp(value,low,high):op==1?Win98Trackbar::Pixel(value,low,high,origin,extent):Win98Trackbar::Page(low,high,value);}
