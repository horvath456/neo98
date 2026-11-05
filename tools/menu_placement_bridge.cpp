// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_menu_placement.h"
using namespace Win98MenuPlacement;
extern "C" void menu_place(const int*in,int*out){Rect work{S(in[0]),S(in[1]),S(in[2]),S(in[3])},avoid{S(in[8]),S(in[9]),S(in[10]),S(in[11])};W animation=W(in[14]);auto p=Place(work,{S(in[4]),S(in[5])},S(in[6]),S(in[7]),in[12]?&avoid:nullptr,W(in[13]),animation);out[0]=p.x;out[1]=p.y;out[2]=animation;}
