// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
#pragma once
#include "win98_localized_bytes.h"
// Storage populated from the mounted Windows installation before guest startup.







namespace Win98MessageTable
{
// The language and the length are those of the installation's table.
inline unsigned Language = 0;
inline unsigned Bytes = 0;
inline unsigned char Data[262144] = {};
}


inline unsigned char OriginalCodePage1252[9866] = {};
inline unsigned char OriginalCodePage437[8648] = {};
inline unsigned char OriginalCodePage850[8648] = {};
inline unsigned char OriginalCodePage866[7496] = {};


inline unsigned char OriginalUnicodeComposition[34676] = {};


inline unsigned char OriginalAtomUpper[128] = {};


inline unsigned char OriginalComputerAnsiHigh[128] = {};
inline unsigned char OriginalComputerOemBytes[160] = {};


inline unsigned short OriginalGdiAnsiCharset[256] = {};


inline unsigned OriginalGdiCharsetInfo[3][14] = {};


inline Win98LocalizedBytes<1024> OriginalSystemMenu1;
inline Win98LocalizedBytes<1024> OriginalSystemMenu2;




namespace Win98UserResources {


inline unsigned DefaultIcon10[10 * 10] = {};
static const int DefaultIcon10Width = 10, DefaultIcon10Height = 10;
inline unsigned DefaultIcon12[12 * 12] = {};
static const int DefaultIcon12Width = 12, DefaultIcon12Height = 12;
inline unsigned DefaultIcon14[14 * 14] = {};
static const int DefaultIcon14Width = 14, DefaultIcon14Height = 14;
inline unsigned DefaultIcon16[16 * 16] = {};
static const int DefaultIcon16Width = 16, DefaultIcon16Height = 16;
inline unsigned DefaultIcon22[22 * 22] = {};
static const int DefaultIcon22Width = 22, DefaultIcon22Height = 22;
inline unsigned DefaultIcon32[32 * 32] = {};
static const int DefaultIcon32Width = 32, DefaultIcon32Height = 32;



inline unsigned short StripCalls[91][2] = {};
static const unsigned StripCount = 91;
inline int StripIndex(unsigned type, unsigned state) {
    for (unsigned i = 0; i < StripCount; ++i)
        if (StripCalls[i][0] == type && StripCalls[i][1] == state)
            return (int)i;
    return -1;}
}



// `size` is the length of an image and the room of a dialog; a dialog's
// length differs by language and stands behind `length`.
struct OriginalImageResource {unsigned type,id;const char*name;const unsigned char*data;unsigned size;unsigned*length=nullptr;
    unsigned Size()const{return length?*length:size;}};
inline unsigned char OriginalImageBytes0[32] = {};
inline unsigned char OriginalImageBytes1[32] = {};
inline unsigned char OriginalImageBytes2[32] = {};
inline unsigned char OriginalImageBytes3[32] = {};
inline unsigned char OriginalImageBytes4[32] = {};
inline unsigned char OriginalImageBytes5[32] = {};
inline unsigned char OriginalImageBytes6[32] = {};
inline unsigned char OriginalImageBytes7[32] = {};
inline unsigned char OriginalImageBytes8[32] = {};
inline unsigned char OriginalImageBytes9[32] = {};
inline unsigned char OriginalImageBytes10[32] = {};
inline unsigned char OriginalImageBytes11[32] = {};
inline unsigned char OriginalImageBytes12[32] = {};
inline unsigned char OriginalImageBytes13[32] = {};
inline unsigned char OriginalImageBytes14[32] = {};
inline unsigned char OriginalImageBytes15[32] = {};
inline unsigned char OriginalImageBytes16[32] = {};
inline unsigned char OriginalImageBytes17[32] = {};
inline unsigned char OriginalImageBytes18[32] = {};
inline unsigned char OriginalImageBytes19[32] = {};
inline unsigned char OriginalImageBytes20[32] = {};
inline unsigned char OriginalImageBytes21[32] = {};
inline unsigned char OriginalImageBytes22[32] = {};
inline unsigned char OriginalImageBytes23[32] = {};
inline unsigned char OriginalImageBytes24[32] = {};
inline unsigned char OriginalImageBytes25[32] = {};
inline unsigned char OriginalImageBytes26[32] = {};
inline unsigned char OriginalImageBytes27[96] = {};
inline unsigned char OriginalImageBytes28[64] = {};
inline unsigned char OriginalImageBytes29[64] = {};
inline unsigned char OriginalImageBytes30[64] = {};
inline unsigned char OriginalImageBytes31[64] = {};
inline unsigned char OriginalImageBytes32[64] = {};
inline unsigned char OriginalImageBytes33[64] = {};
inline unsigned char OriginalImageBytes34[192] = {};
inline unsigned char OriginalImageBytes35[352] = {};
inline unsigned char OriginalImageBytes36[384] = {};
inline unsigned char OriginalImageBytes37[416] = {};
inline Win98LocalizedBytes<4096> OriginalImageBytes38;
inline Win98LocalizedBytes<4096> OriginalImageBytes39;
inline Win98LocalizedBytes<4096> OriginalImageBytes40;
inline Win98LocalizedBytes<4096> OriginalImageBytes41;
inline Win98LocalizedBytes<4096> OriginalImageBytes42;
inline Win98LocalizedBytes<4096> OriginalImageBytes43;
inline Win98LocalizedBytes<4096> OriginalImageBytes44;
inline Win98LocalizedBytes<4096> OriginalImageBytes45;
inline unsigned char OriginalImageBytes46[768] = {};
inline unsigned char OriginalImageBytes47[224] = {};
inline unsigned char OriginalImageBytes48[288] = {};
inline unsigned char OriginalImageBytes49[256] = {};
inline unsigned char OriginalImageBytes50[320] = {};
inline unsigned char OriginalImageBytes51[480] = {};
inline unsigned char OriginalImageBytes52[768] = {};
inline unsigned char OriginalImageBytes53[320] = {};
inline unsigned char OriginalImageBytes54[768] = {};
inline unsigned char OriginalImageBytes55[320] = {};
inline unsigned char OriginalImageBytes56[768] = {};
inline unsigned char OriginalImageBytes57[320] = {};
inline unsigned char OriginalImageBytes58[768] = {};
inline unsigned char OriginalImageBytes59[320] = {};
inline unsigned char OriginalImageBytes60[768] = {};
inline unsigned char OriginalImageBytes61[320] = {};
inline unsigned char OriginalImageBytes62[320] = {};
inline unsigned char OriginalImageBytes63[768] = {};
inline unsigned char OriginalImageBytes64[320] = {};
inline unsigned char OriginalImageBytes65[320] = {};
inline unsigned char OriginalImageBytes66[320] = {};
inline unsigned char OriginalImageBytes67[320] = {};
inline unsigned char OriginalImageBytes68[320] = {};
inline unsigned char OriginalImageBytes69[320] = {};
inline unsigned char OriginalImageBytes70[320] = {};
inline unsigned char OriginalImageBytes71[320] = {};
inline unsigned char OriginalImageBytes72[320] = {};
inline unsigned char OriginalImageBytes73[320] = {};
inline unsigned char OriginalImageBytes74[320] = {};
inline unsigned char OriginalImageBytes75[320] = {};
inline unsigned char OriginalImageBytes76[320] = {};
inline unsigned char OriginalImageBytes77[320] = {};
inline unsigned char OriginalImageBytes78[320] = {};
inline unsigned char OriginalImageBytes79[320] = {};
inline unsigned char OriginalImageBytes80[320] = {};
inline unsigned char OriginalImageBytes81[320] = {};
inline unsigned char OriginalImageBytes82[320] = {};
inline unsigned char OriginalImageBytes83[320] = {};
inline unsigned char OriginalImageBytes84[320] = {};
inline unsigned char OriginalImageBytes85[320] = {};
inline unsigned char OriginalImageBytes86[320] = {};
inline unsigned char OriginalImageBytes87[320] = {};
inline unsigned char OriginalImageBytes88[320] = {};
inline unsigned char OriginalImageBytes89[320] = {};
inline unsigned char OriginalImageBytes90[320] = {};
inline unsigned char OriginalImageBytes91[320] = {};
inline const OriginalImageResource OriginalImageResources[]={
{12,100,"",OriginalImageBytes0,sizeof OriginalImageBytes0},
{12,101,"",OriginalImageBytes1,sizeof OriginalImageBytes1},
{12,102,"",OriginalImageBytes2,sizeof OriginalImageBytes2},
{12,103,"",OriginalImageBytes3,sizeof OriginalImageBytes3},
{12,104,"",OriginalImageBytes4,sizeof OriginalImageBytes4},
{12,105,"",OriginalImageBytes5,sizeof OriginalImageBytes5},
{12,106,"",OriginalImageBytes6,sizeof OriginalImageBytes6},
{12,107,"",OriginalImageBytes7,sizeof OriginalImageBytes7},
{12,108,"",OriginalImageBytes8,sizeof OriginalImageBytes8},
{12,109,"",OriginalImageBytes9,sizeof OriginalImageBytes9},
{12,110,"",OriginalImageBytes10,sizeof OriginalImageBytes10},
{12,111,"",OriginalImageBytes11,sizeof OriginalImageBytes11},
{12,112,"",OriginalImageBytes12,sizeof OriginalImageBytes12},
{12,113,"",OriginalImageBytes13,sizeof OriginalImageBytes13},
{12,114,"",OriginalImageBytes14,sizeof OriginalImageBytes14},
{12,115,"",OriginalImageBytes15,sizeof OriginalImageBytes15},
{12,32652,"",OriginalImageBytes16,sizeof OriginalImageBytes16},
{12,32653,"",OriginalImageBytes17,sizeof OriginalImageBytes17},
{12,32654,"",OriginalImageBytes18,sizeof OriginalImageBytes18},
{12,32655,"",OriginalImageBytes19,sizeof OriginalImageBytes19},
{12,32656,"",OriginalImageBytes20,sizeof OriginalImageBytes20},
{12,32657,"",OriginalImageBytes21,sizeof OriginalImageBytes21},
{12,32658,"",OriginalImageBytes22,sizeof OriginalImageBytes22},
{12,32659,"",OriginalImageBytes23,sizeof OriginalImageBytes23},
{12,32660,"",OriginalImageBytes24,sizeof OriginalImageBytes24},
{12,32661,"",OriginalImageBytes25,sizeof OriginalImageBytes25},
{12,32662,"",OriginalImageBytes26,sizeof OriginalImageBytes26},
{14,105,"",OriginalImageBytes27,sizeof OriginalImageBytes27},
{14,101,"",OriginalImageBytes28,sizeof OriginalImageBytes28},
{14,102,"",OriginalImageBytes29,sizeof OriginalImageBytes29},
{14,103,"",OriginalImageBytes30,sizeof OriginalImageBytes30},
{14,104,"",OriginalImageBytes31,sizeof OriginalImageBytes31},
{14,100,"",OriginalImageBytes32,sizeof OriginalImageBytes32},
{14,1000,"",OriginalImageBytes33,sizeof OriginalImageBytes33},
{2,0,"OBM_HELP",OriginalImageBytes34,sizeof OriginalImageBytes34},
{2,32559,"",OriginalImageBytes35,sizeof OriginalImageBytes35},
{2,32660,"",OriginalImageBytes36,sizeof OriginalImageBytes36},
{2,32661,"",OriginalImageBytes37,sizeof OriginalImageBytes37},
{5,7,"",OriginalImageBytes38.bytes,sizeof OriginalImageBytes38.bytes,&OriginalImageBytes38.size},
{5,8,"",OriginalImageBytes39.bytes,sizeof OriginalImageBytes39.bytes,&OriginalImageBytes39.size},
{5,9,"",OriginalImageBytes40.bytes,sizeof OriginalImageBytes40.bytes,&OriginalImageBytes40.size},
{5,10,"",OriginalImageBytes41.bytes,sizeof OriginalImageBytes41.bytes,&OriginalImageBytes41.size},
{5,200,"",OriginalImageBytes42.bytes,sizeof OriginalImageBytes42.bytes,&OriginalImageBytes42.size},
{5,11,"",OriginalImageBytes43.bytes,sizeof OriginalImageBytes43.bytes,&OriginalImageBytes43.size},
{5,12,"",OriginalImageBytes44.bytes,sizeof OriginalImageBytes44.bytes,&OriginalImageBytes44.size},
{5,13,"",OriginalImageBytes45.bytes,sizeof OriginalImageBytes45.bytes,&OriginalImageBytes45.size},
{3,1,"",OriginalImageBytes46,sizeof OriginalImageBytes46},
{3,2,"",OriginalImageBytes47,sizeof OriginalImageBytes47},
{3,3,"",OriginalImageBytes48,sizeof OriginalImageBytes48},
{3,4,"",OriginalImageBytes49,sizeof OriginalImageBytes49},
{3,5,"",OriginalImageBytes50,sizeof OriginalImageBytes50},
{3,6,"",OriginalImageBytes51,sizeof OriginalImageBytes51},
{3,7,"",OriginalImageBytes52,sizeof OriginalImageBytes52},
{3,8,"",OriginalImageBytes53,sizeof OriginalImageBytes53},
{3,9,"",OriginalImageBytes54,sizeof OriginalImageBytes54},
{3,10,"",OriginalImageBytes55,sizeof OriginalImageBytes55},
{3,11,"",OriginalImageBytes56,sizeof OriginalImageBytes56},
{3,12,"",OriginalImageBytes57,sizeof OriginalImageBytes57},
{3,13,"",OriginalImageBytes58,sizeof OriginalImageBytes58},
{3,14,"",OriginalImageBytes59,sizeof OriginalImageBytes59},
{3,15,"",OriginalImageBytes60,sizeof OriginalImageBytes60},
{3,16,"",OriginalImageBytes61,sizeof OriginalImageBytes61},
{3,17,"",OriginalImageBytes62,sizeof OriginalImageBytes62},
{3,18,"",OriginalImageBytes63,sizeof OriginalImageBytes63},
{3,19,"",OriginalImageBytes64,sizeof OriginalImageBytes64},
{1,20,"",OriginalImageBytes65,sizeof OriginalImageBytes65},
{1,21,"",OriginalImageBytes66,sizeof OriginalImageBytes66},
{1,22,"",OriginalImageBytes67,sizeof OriginalImageBytes67},
{1,23,"",OriginalImageBytes68,sizeof OriginalImageBytes68},
{1,24,"",OriginalImageBytes69,sizeof OriginalImageBytes69},
{1,25,"",OriginalImageBytes70,sizeof OriginalImageBytes70},
{1,26,"",OriginalImageBytes71,sizeof OriginalImageBytes71},
{1,27,"",OriginalImageBytes72,sizeof OriginalImageBytes72},
{1,28,"",OriginalImageBytes73,sizeof OriginalImageBytes73},
{1,29,"",OriginalImageBytes74,sizeof OriginalImageBytes74},
{1,30,"",OriginalImageBytes75,sizeof OriginalImageBytes75},
{1,31,"",OriginalImageBytes76,sizeof OriginalImageBytes76},
{1,32,"",OriginalImageBytes77,sizeof OriginalImageBytes77},
{1,33,"",OriginalImageBytes78,sizeof OriginalImageBytes78},
{1,34,"",OriginalImageBytes79,sizeof OriginalImageBytes79},
{1,35,"",OriginalImageBytes80,sizeof OriginalImageBytes80},
{1,36,"",OriginalImageBytes81,sizeof OriginalImageBytes81},
{1,37,"",OriginalImageBytes82,sizeof OriginalImageBytes82},
{1,38,"",OriginalImageBytes83,sizeof OriginalImageBytes83},
{1,39,"",OriginalImageBytes84,sizeof OriginalImageBytes84},
{1,40,"",OriginalImageBytes85,sizeof OriginalImageBytes85},
{1,41,"",OriginalImageBytes86,sizeof OriginalImageBytes86},
{1,42,"",OriginalImageBytes87,sizeof OriginalImageBytes87},
{1,43,"",OriginalImageBytes88,sizeof OriginalImageBytes88},
{1,44,"",OriginalImageBytes89,sizeof OriginalImageBytes89},
{1,45,"",OriginalImageBytes90,sizeof OriginalImageBytes90},
{1,46,"",OriginalImageBytes91,sizeof OriginalImageBytes91},
};



namespace OriginalSerialUiAssets {
inline Win98LocalizedBytes<4096> Template119;
inline Win98LocalizedBytes<8192> Template101;
inline Win98LocalizedBytes<256> String462;
inline Win98LocalizedBytes<256> String342;
inline Win98LocalizedBytes<256> String208;
inline Win98LocalizedBytes<256> String209;
inline Win98LocalizedBytes<256> String210;
inline Win98LocalizedBytes<256> String211;
inline Win98LocalizedBytes<256> String213;
inline Win98LocalizedBytes<256> String214;
inline Win98LocalizedBytes<256> String215;
inline Win98LocalizedBytes<256> String216;
inline Win98LocalizedBytes<256> String217;
inline Win98LocalizedBytes<256> String218;
inline Win98LocalizedBytes<256> String219;
inline Win98LocalizedBytes<256> String220;
inline Win98LocalizedBytes<256> String221;
inline Win98LocalizedBytes<256> String222;
inline Win98LocalizedBytes<256> String200;
inline Win98LocalizedBytes<256> String201;
inline Win98LocalizedBytes<256> String202;
inline Win98LocalizedBytes<256> String203;
inline Win98LocalizedBytes<256> String204;
inline Win98LocalizedBytes<256> String205;
inline Win98LocalizedBytes<256> String206;
inline Win98LocalizedBytes<256> String207;
inline Win98LocalizedBytes<256> String240;
inline Win98LocalizedBytes<256> String241;
inline Win98LocalizedBytes<256> String242;
inline Win98LocalizedBytes<256> String243;
inline Win98LocalizedBytes<256> String244;
inline Win98LocalizedBytes<256> String250;
inline Win98LocalizedBytes<256> String251;
inline Win98LocalizedBytes<256> String252;
inline Win98LocalizedBytes<256> String253;
inline Win98LocalizedBytes<256> String254;
inline Win98LocalizedBytes<256> String255;
inline Win98LocalizedBytes<256> String256;
inline Win98LocalizedBytes<256> String257;
inline Win98LocalizedBytes<256> String260;
inline Win98LocalizedBytes<256> String261;
inline Win98LocalizedBytes<256> String262;
inline const char *String(unsigned id){switch(id){
case 462:return reinterpret_cast<const char *>(String462.bytes);
case 342:return reinterpret_cast<const char *>(String342.bytes);
case 208:return reinterpret_cast<const char *>(String208.bytes);
case 209:return reinterpret_cast<const char *>(String209.bytes);
case 210:return reinterpret_cast<const char *>(String210.bytes);
case 211:return reinterpret_cast<const char *>(String211.bytes);
case 213:return reinterpret_cast<const char *>(String213.bytes);
case 214:return reinterpret_cast<const char *>(String214.bytes);
case 215:return reinterpret_cast<const char *>(String215.bytes);
case 216:return reinterpret_cast<const char *>(String216.bytes);
case 217:return reinterpret_cast<const char *>(String217.bytes);
case 218:return reinterpret_cast<const char *>(String218.bytes);
case 219:return reinterpret_cast<const char *>(String219.bytes);
case 220:return reinterpret_cast<const char *>(String220.bytes);
case 221:return reinterpret_cast<const char *>(String221.bytes);
case 222:return reinterpret_cast<const char *>(String222.bytes);
case 200:return reinterpret_cast<const char *>(String200.bytes);
case 201:return reinterpret_cast<const char *>(String201.bytes);
case 202:return reinterpret_cast<const char *>(String202.bytes);
case 203:return reinterpret_cast<const char *>(String203.bytes);
case 204:return reinterpret_cast<const char *>(String204.bytes);
case 205:return reinterpret_cast<const char *>(String205.bytes);
case 206:return reinterpret_cast<const char *>(String206.bytes);
case 207:return reinterpret_cast<const char *>(String207.bytes);
case 240:return reinterpret_cast<const char *>(String240.bytes);
case 241:return reinterpret_cast<const char *>(String241.bytes);
case 242:return reinterpret_cast<const char *>(String242.bytes);
case 243:return reinterpret_cast<const char *>(String243.bytes);
case 244:return reinterpret_cast<const char *>(String244.bytes);
case 250:return reinterpret_cast<const char *>(String250.bytes);
case 251:return reinterpret_cast<const char *>(String251.bytes);
case 252:return reinterpret_cast<const char *>(String252.bytes);
case 253:return reinterpret_cast<const char *>(String253.bytes);
case 254:return reinterpret_cast<const char *>(String254.bytes);
case 255:return reinterpret_cast<const char *>(String255.bytes);
case 256:return reinterpret_cast<const char *>(String256.bytes);
case 257:return reinterpret_cast<const char *>(String257.bytes);
case 260:return reinterpret_cast<const char *>(String260.bytes);
case 261:return reinterpret_cast<const char *>(String261.bytes);
case 262:return reinterpret_cast<const char *>(String262.bytes);
default:return "";}}
}




namespace Win98GdiPalette {
inline unsigned DefaultColors[20] = {};
inline unsigned HalftoneColors[256] = {};
}


inline unsigned char OriginalGdiObjectTypes[13] = {};
inline unsigned char OriginalGdiCurrentOffsets[8] = {};

namespace OriginalPropertySheetAssets {
inline Win98LocalizedBytes<2048> Template1006;
inline Win98LocalizedBytes<2048> Template1020;
inline Win98LocalizedBytes<256> CaptionFormat;
}

// Enumerate imported storage so states retain the loading installation.
template<class Visit> void VisitWindowsRuntimeData(Visit visit)
{
    visit("Win98MessageTable::Data", Win98MessageTable::Data, Win98MessageTable::Bytes);
    visit("OriginalCodePage1252", &OriginalCodePage1252, sizeof OriginalCodePage1252);
    visit("OriginalCodePage437", &OriginalCodePage437, sizeof OriginalCodePage437);
    visit("OriginalCodePage850", &OriginalCodePage850, sizeof OriginalCodePage850);
    visit("OriginalCodePage866", &OriginalCodePage866, sizeof OriginalCodePage866);
    visit("OriginalUnicodeComposition", &OriginalUnicodeComposition, sizeof OriginalUnicodeComposition);
    visit("OriginalAtomUpper", &OriginalAtomUpper, sizeof OriginalAtomUpper);
    visit("OriginalComputerAnsiHigh", &OriginalComputerAnsiHigh, sizeof OriginalComputerAnsiHigh);
    visit("OriginalComputerOemBytes", &OriginalComputerOemBytes, sizeof OriginalComputerOemBytes);
    visit("OriginalGdiAnsiCharset", &OriginalGdiAnsiCharset, sizeof OriginalGdiAnsiCharset);
    visit("OriginalGdiCharsetInfo", &OriginalGdiCharsetInfo, sizeof OriginalGdiCharsetInfo);
    visit("OriginalSystemMenu1", OriginalSystemMenu1.bytes, OriginalSystemMenu1.size);
    visit("OriginalSystemMenu2", OriginalSystemMenu2.bytes, OriginalSystemMenu2.size);
    visit("Win98UserResources::DefaultIcon10", &Win98UserResources::DefaultIcon10, sizeof Win98UserResources::DefaultIcon10);
    visit("Win98UserResources::DefaultIcon12", &Win98UserResources::DefaultIcon12, sizeof Win98UserResources::DefaultIcon12);
    visit("Win98UserResources::DefaultIcon14", &Win98UserResources::DefaultIcon14, sizeof Win98UserResources::DefaultIcon14);
    visit("Win98UserResources::DefaultIcon16", &Win98UserResources::DefaultIcon16, sizeof Win98UserResources::DefaultIcon16);
    visit("Win98UserResources::DefaultIcon22", &Win98UserResources::DefaultIcon22, sizeof Win98UserResources::DefaultIcon22);
    visit("Win98UserResources::DefaultIcon32", &Win98UserResources::DefaultIcon32, sizeof Win98UserResources::DefaultIcon32);
    visit("Win98UserResources::StripCalls", &Win98UserResources::StripCalls, sizeof Win98UserResources::StripCalls);
    visit("OriginalImageBytes0", &OriginalImageBytes0, sizeof OriginalImageBytes0);
    visit("OriginalImageBytes1", &OriginalImageBytes1, sizeof OriginalImageBytes1);
    visit("OriginalImageBytes2", &OriginalImageBytes2, sizeof OriginalImageBytes2);
    visit("OriginalImageBytes3", &OriginalImageBytes3, sizeof OriginalImageBytes3);
    visit("OriginalImageBytes4", &OriginalImageBytes4, sizeof OriginalImageBytes4);
    visit("OriginalImageBytes5", &OriginalImageBytes5, sizeof OriginalImageBytes5);
    visit("OriginalImageBytes6", &OriginalImageBytes6, sizeof OriginalImageBytes6);
    visit("OriginalImageBytes7", &OriginalImageBytes7, sizeof OriginalImageBytes7);
    visit("OriginalImageBytes8", &OriginalImageBytes8, sizeof OriginalImageBytes8);
    visit("OriginalImageBytes9", &OriginalImageBytes9, sizeof OriginalImageBytes9);
    visit("OriginalImageBytes10", &OriginalImageBytes10, sizeof OriginalImageBytes10);
    visit("OriginalImageBytes11", &OriginalImageBytes11, sizeof OriginalImageBytes11);
    visit("OriginalImageBytes12", &OriginalImageBytes12, sizeof OriginalImageBytes12);
    visit("OriginalImageBytes13", &OriginalImageBytes13, sizeof OriginalImageBytes13);
    visit("OriginalImageBytes14", &OriginalImageBytes14, sizeof OriginalImageBytes14);
    visit("OriginalImageBytes15", &OriginalImageBytes15, sizeof OriginalImageBytes15);
    visit("OriginalImageBytes16", &OriginalImageBytes16, sizeof OriginalImageBytes16);
    visit("OriginalImageBytes17", &OriginalImageBytes17, sizeof OriginalImageBytes17);
    visit("OriginalImageBytes18", &OriginalImageBytes18, sizeof OriginalImageBytes18);
    visit("OriginalImageBytes19", &OriginalImageBytes19, sizeof OriginalImageBytes19);
    visit("OriginalImageBytes20", &OriginalImageBytes20, sizeof OriginalImageBytes20);
    visit("OriginalImageBytes21", &OriginalImageBytes21, sizeof OriginalImageBytes21);
    visit("OriginalImageBytes22", &OriginalImageBytes22, sizeof OriginalImageBytes22);
    visit("OriginalImageBytes23", &OriginalImageBytes23, sizeof OriginalImageBytes23);
    visit("OriginalImageBytes24", &OriginalImageBytes24, sizeof OriginalImageBytes24);
    visit("OriginalImageBytes25", &OriginalImageBytes25, sizeof OriginalImageBytes25);
    visit("OriginalImageBytes26", &OriginalImageBytes26, sizeof OriginalImageBytes26);
    visit("OriginalImageBytes27", &OriginalImageBytes27, sizeof OriginalImageBytes27);
    visit("OriginalImageBytes28", &OriginalImageBytes28, sizeof OriginalImageBytes28);
    visit("OriginalImageBytes29", &OriginalImageBytes29, sizeof OriginalImageBytes29);
    visit("OriginalImageBytes30", &OriginalImageBytes30, sizeof OriginalImageBytes30);
    visit("OriginalImageBytes31", &OriginalImageBytes31, sizeof OriginalImageBytes31);
    visit("OriginalImageBytes32", &OriginalImageBytes32, sizeof OriginalImageBytes32);
    visit("OriginalImageBytes33", &OriginalImageBytes33, sizeof OriginalImageBytes33);
    visit("OriginalImageBytes34", &OriginalImageBytes34, sizeof OriginalImageBytes34);
    visit("OriginalImageBytes35", &OriginalImageBytes35, sizeof OriginalImageBytes35);
    visit("OriginalImageBytes36", &OriginalImageBytes36, sizeof OriginalImageBytes36);
    visit("OriginalImageBytes37", &OriginalImageBytes37, sizeof OriginalImageBytes37);
    visit("OriginalImageBytes38", OriginalImageBytes38.bytes, OriginalImageBytes38.size);
    visit("OriginalImageBytes39", OriginalImageBytes39.bytes, OriginalImageBytes39.size);
    visit("OriginalImageBytes40", OriginalImageBytes40.bytes, OriginalImageBytes40.size);
    visit("OriginalImageBytes41", OriginalImageBytes41.bytes, OriginalImageBytes41.size);
    visit("OriginalImageBytes42", OriginalImageBytes42.bytes, OriginalImageBytes42.size);
    visit("OriginalImageBytes43", OriginalImageBytes43.bytes, OriginalImageBytes43.size);
    visit("OriginalImageBytes44", OriginalImageBytes44.bytes, OriginalImageBytes44.size);
    visit("OriginalImageBytes45", OriginalImageBytes45.bytes, OriginalImageBytes45.size);
    visit("OriginalImageBytes46", &OriginalImageBytes46, sizeof OriginalImageBytes46);
    visit("OriginalImageBytes47", &OriginalImageBytes47, sizeof OriginalImageBytes47);
    visit("OriginalImageBytes48", &OriginalImageBytes48, sizeof OriginalImageBytes48);
    visit("OriginalImageBytes49", &OriginalImageBytes49, sizeof OriginalImageBytes49);
    visit("OriginalImageBytes50", &OriginalImageBytes50, sizeof OriginalImageBytes50);
    visit("OriginalImageBytes51", &OriginalImageBytes51, sizeof OriginalImageBytes51);
    visit("OriginalImageBytes52", &OriginalImageBytes52, sizeof OriginalImageBytes52);
    visit("OriginalImageBytes53", &OriginalImageBytes53, sizeof OriginalImageBytes53);
    visit("OriginalImageBytes54", &OriginalImageBytes54, sizeof OriginalImageBytes54);
    visit("OriginalImageBytes55", &OriginalImageBytes55, sizeof OriginalImageBytes55);
    visit("OriginalImageBytes56", &OriginalImageBytes56, sizeof OriginalImageBytes56);
    visit("OriginalImageBytes57", &OriginalImageBytes57, sizeof OriginalImageBytes57);
    visit("OriginalImageBytes58", &OriginalImageBytes58, sizeof OriginalImageBytes58);
    visit("OriginalImageBytes59", &OriginalImageBytes59, sizeof OriginalImageBytes59);
    visit("OriginalImageBytes60", &OriginalImageBytes60, sizeof OriginalImageBytes60);
    visit("OriginalImageBytes61", &OriginalImageBytes61, sizeof OriginalImageBytes61);
    visit("OriginalImageBytes62", &OriginalImageBytes62, sizeof OriginalImageBytes62);
    visit("OriginalImageBytes63", &OriginalImageBytes63, sizeof OriginalImageBytes63);
    visit("OriginalImageBytes64", &OriginalImageBytes64, sizeof OriginalImageBytes64);
    visit("OriginalImageBytes65", &OriginalImageBytes65, sizeof OriginalImageBytes65);
    visit("OriginalImageBytes66", &OriginalImageBytes66, sizeof OriginalImageBytes66);
    visit("OriginalImageBytes67", &OriginalImageBytes67, sizeof OriginalImageBytes67);
    visit("OriginalImageBytes68", &OriginalImageBytes68, sizeof OriginalImageBytes68);
    visit("OriginalImageBytes69", &OriginalImageBytes69, sizeof OriginalImageBytes69);
    visit("OriginalImageBytes70", &OriginalImageBytes70, sizeof OriginalImageBytes70);
    visit("OriginalImageBytes71", &OriginalImageBytes71, sizeof OriginalImageBytes71);
    visit("OriginalImageBytes72", &OriginalImageBytes72, sizeof OriginalImageBytes72);
    visit("OriginalImageBytes73", &OriginalImageBytes73, sizeof OriginalImageBytes73);
    visit("OriginalImageBytes74", &OriginalImageBytes74, sizeof OriginalImageBytes74);
    visit("OriginalImageBytes75", &OriginalImageBytes75, sizeof OriginalImageBytes75);
    visit("OriginalImageBytes76", &OriginalImageBytes76, sizeof OriginalImageBytes76);
    visit("OriginalImageBytes77", &OriginalImageBytes77, sizeof OriginalImageBytes77);
    visit("OriginalImageBytes78", &OriginalImageBytes78, sizeof OriginalImageBytes78);
    visit("OriginalImageBytes79", &OriginalImageBytes79, sizeof OriginalImageBytes79);
    visit("OriginalImageBytes80", &OriginalImageBytes80, sizeof OriginalImageBytes80);
    visit("OriginalImageBytes81", &OriginalImageBytes81, sizeof OriginalImageBytes81);
    visit("OriginalImageBytes82", &OriginalImageBytes82, sizeof OriginalImageBytes82);
    visit("OriginalImageBytes83", &OriginalImageBytes83, sizeof OriginalImageBytes83);
    visit("OriginalImageBytes84", &OriginalImageBytes84, sizeof OriginalImageBytes84);
    visit("OriginalImageBytes85", &OriginalImageBytes85, sizeof OriginalImageBytes85);
    visit("OriginalImageBytes86", &OriginalImageBytes86, sizeof OriginalImageBytes86);
    visit("OriginalImageBytes87", &OriginalImageBytes87, sizeof OriginalImageBytes87);
    visit("OriginalImageBytes88", &OriginalImageBytes88, sizeof OriginalImageBytes88);
    visit("OriginalImageBytes89", &OriginalImageBytes89, sizeof OriginalImageBytes89);
    visit("OriginalImageBytes90", &OriginalImageBytes90, sizeof OriginalImageBytes90);
    visit("OriginalImageBytes91", &OriginalImageBytes91, sizeof OriginalImageBytes91);
    visit("OriginalSerialUiAssets::Template119", OriginalSerialUiAssets::Template119.bytes, OriginalSerialUiAssets::Template119.size);
    visit("OriginalSerialUiAssets::Template101", OriginalSerialUiAssets::Template101.bytes, OriginalSerialUiAssets::Template101.size);
    visit("OriginalSerialUiAssets::String462", OriginalSerialUiAssets::String462.bytes, OriginalSerialUiAssets::String462.size);
    visit("OriginalSerialUiAssets::String342", OriginalSerialUiAssets::String342.bytes, OriginalSerialUiAssets::String342.size);
    visit("OriginalSerialUiAssets::String208", OriginalSerialUiAssets::String208.bytes, OriginalSerialUiAssets::String208.size);
    visit("OriginalSerialUiAssets::String209", OriginalSerialUiAssets::String209.bytes, OriginalSerialUiAssets::String209.size);
    visit("OriginalSerialUiAssets::String210", OriginalSerialUiAssets::String210.bytes, OriginalSerialUiAssets::String210.size);
    visit("OriginalSerialUiAssets::String211", OriginalSerialUiAssets::String211.bytes, OriginalSerialUiAssets::String211.size);
    visit("OriginalSerialUiAssets::String213", OriginalSerialUiAssets::String213.bytes, OriginalSerialUiAssets::String213.size);
    visit("OriginalSerialUiAssets::String214", OriginalSerialUiAssets::String214.bytes, OriginalSerialUiAssets::String214.size);
    visit("OriginalSerialUiAssets::String215", OriginalSerialUiAssets::String215.bytes, OriginalSerialUiAssets::String215.size);
    visit("OriginalSerialUiAssets::String216", OriginalSerialUiAssets::String216.bytes, OriginalSerialUiAssets::String216.size);
    visit("OriginalSerialUiAssets::String217", OriginalSerialUiAssets::String217.bytes, OriginalSerialUiAssets::String217.size);
    visit("OriginalSerialUiAssets::String218", OriginalSerialUiAssets::String218.bytes, OriginalSerialUiAssets::String218.size);
    visit("OriginalSerialUiAssets::String219", OriginalSerialUiAssets::String219.bytes, OriginalSerialUiAssets::String219.size);
    visit("OriginalSerialUiAssets::String220", OriginalSerialUiAssets::String220.bytes, OriginalSerialUiAssets::String220.size);
    visit("OriginalSerialUiAssets::String221", OriginalSerialUiAssets::String221.bytes, OriginalSerialUiAssets::String221.size);
    visit("OriginalSerialUiAssets::String222", OriginalSerialUiAssets::String222.bytes, OriginalSerialUiAssets::String222.size);
    visit("OriginalSerialUiAssets::String200", OriginalSerialUiAssets::String200.bytes, OriginalSerialUiAssets::String200.size);
    visit("OriginalSerialUiAssets::String201", OriginalSerialUiAssets::String201.bytes, OriginalSerialUiAssets::String201.size);
    visit("OriginalSerialUiAssets::String202", OriginalSerialUiAssets::String202.bytes, OriginalSerialUiAssets::String202.size);
    visit("OriginalSerialUiAssets::String203", OriginalSerialUiAssets::String203.bytes, OriginalSerialUiAssets::String203.size);
    visit("OriginalSerialUiAssets::String204", OriginalSerialUiAssets::String204.bytes, OriginalSerialUiAssets::String204.size);
    visit("OriginalSerialUiAssets::String205", OriginalSerialUiAssets::String205.bytes, OriginalSerialUiAssets::String205.size);
    visit("OriginalSerialUiAssets::String206", OriginalSerialUiAssets::String206.bytes, OriginalSerialUiAssets::String206.size);
    visit("OriginalSerialUiAssets::String207", OriginalSerialUiAssets::String207.bytes, OriginalSerialUiAssets::String207.size);
    visit("OriginalSerialUiAssets::String240", OriginalSerialUiAssets::String240.bytes, OriginalSerialUiAssets::String240.size);
    visit("OriginalSerialUiAssets::String241", OriginalSerialUiAssets::String241.bytes, OriginalSerialUiAssets::String241.size);
    visit("OriginalSerialUiAssets::String242", OriginalSerialUiAssets::String242.bytes, OriginalSerialUiAssets::String242.size);
    visit("OriginalSerialUiAssets::String243", OriginalSerialUiAssets::String243.bytes, OriginalSerialUiAssets::String243.size);
    visit("OriginalSerialUiAssets::String244", OriginalSerialUiAssets::String244.bytes, OriginalSerialUiAssets::String244.size);
    visit("OriginalSerialUiAssets::String250", OriginalSerialUiAssets::String250.bytes, OriginalSerialUiAssets::String250.size);
    visit("OriginalSerialUiAssets::String251", OriginalSerialUiAssets::String251.bytes, OriginalSerialUiAssets::String251.size);
    visit("OriginalSerialUiAssets::String252", OriginalSerialUiAssets::String252.bytes, OriginalSerialUiAssets::String252.size);
    visit("OriginalSerialUiAssets::String253", OriginalSerialUiAssets::String253.bytes, OriginalSerialUiAssets::String253.size);
    visit("OriginalSerialUiAssets::String254", OriginalSerialUiAssets::String254.bytes, OriginalSerialUiAssets::String254.size);
    visit("OriginalSerialUiAssets::String255", OriginalSerialUiAssets::String255.bytes, OriginalSerialUiAssets::String255.size);
    visit("OriginalSerialUiAssets::String256", OriginalSerialUiAssets::String256.bytes, OriginalSerialUiAssets::String256.size);
    visit("OriginalSerialUiAssets::String257", OriginalSerialUiAssets::String257.bytes, OriginalSerialUiAssets::String257.size);
    visit("OriginalSerialUiAssets::String260", OriginalSerialUiAssets::String260.bytes, OriginalSerialUiAssets::String260.size);
    visit("OriginalSerialUiAssets::String261", OriginalSerialUiAssets::String261.bytes, OriginalSerialUiAssets::String261.size);
    visit("OriginalSerialUiAssets::String262", OriginalSerialUiAssets::String262.bytes, OriginalSerialUiAssets::String262.size);
    visit("Win98GdiPalette::DefaultColors", &Win98GdiPalette::DefaultColors, sizeof Win98GdiPalette::DefaultColors);
    visit("Win98GdiPalette::HalftoneColors", &Win98GdiPalette::HalftoneColors, sizeof Win98GdiPalette::HalftoneColors);
    visit("OriginalGdiObjectTypes", &OriginalGdiObjectTypes, sizeof OriginalGdiObjectTypes);
    visit("OriginalGdiCurrentOffsets", &OriginalGdiCurrentOffsets, sizeof OriginalGdiCurrentOffsets);
    visit("OriginalPropertySheetAssets::Template1006", OriginalPropertySheetAssets::Template1006.bytes, OriginalPropertySheetAssets::Template1006.size);
    visit("OriginalPropertySheetAssets::Template1020", OriginalPropertySheetAssets::Template1020.bytes, OriginalPropertySheetAssets::Template1020.size);
    visit("OriginalPropertySheetAssets::CaptionFormat", OriginalPropertySheetAssets::CaptionFormat.bytes, OriginalPropertySheetAssets::CaptionFormat.size);
}
