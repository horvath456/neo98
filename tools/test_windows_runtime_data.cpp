// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_windows_data.h"
#include "../src/win98_format_message.h"
#include "../src/win98_nls.h"
#include "../src/dialog_template.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cassert>
namespace Expected {
#include "../build/crt-locale/crt_locale_data.h"
#include "../build/property-sheet-assets/templates.h"
#include "../build/winmm-message/winmm_messages.h"
#include "../build/generated/win98_message_table.h"
#include "../build/generated/nls_tables.h"
#include "../build/generated/nls_unicode.h"
#include "../build/generated/atom_upper.h"
#include "../build/generated/computer_name_tables.h"
#include "../build/generated/font_charset_tables.h"
#include "../build/generated/font_charset_info.h"
#include "../build/generated/user_menus.h"
#include "../build/generated/user_resources.h"
#include "../build/image-resource/system_assets.h"
#include "../build/serialui-assets/templates.h"
#include "../src/win98_gdi_palette_tables.h"
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    auto read = [&](const char *path, HostWindowsData::Bytes &bytes) {
        std::filesystem::path file(argv[1]);
        std::string rest(path + 3);
        for (char &c : rest) if (c == '\\') c = '/';
        file /= rest;
        std::ifstream stream(file, std::ios::binary);
        if (!stream) return false;
        bytes.assign(std::istreambuf_iterator<char>(stream), {}); return true;
    };
    std::string error;
    if (!HostWindowsData::Load(read, error)) { std::cerr << error << '\n'; return 1; }
    if (memcmp(Win98MessageTable::Data, Expected::Win98MessageTable::Data, sizeof Win98MessageTable::Data)) { std::cerr << "Mismatch: Data\n"; return 1; }
    if (memcmp(OriginalCodePage1252, Expected::OriginalCodePage1252, sizeof OriginalCodePage1252)) { std::cerr << "Mismatch: OriginalCodePage1252\n"; return 1; }
    if (memcmp(OriginalCodePage437, Expected::OriginalCodePage437, sizeof OriginalCodePage437)) { std::cerr << "Mismatch: OriginalCodePage437\n"; return 1; }
    if (memcmp(OriginalCodePage850, Expected::OriginalCodePage850, sizeof OriginalCodePage850)) { std::cerr << "Mismatch: OriginalCodePage850\n"; return 1; }
    if (memcmp(OriginalCodePage866, Expected::OriginalCodePage866, sizeof OriginalCodePage866)) { std::cerr << "Mismatch: OriginalCodePage866\n"; return 1; }
    if (memcmp(OriginalUnicodeComposition, Expected::OriginalUnicodeComposition, sizeof OriginalUnicodeComposition)) { std::cerr << "Mismatch: OriginalUnicodeComposition\n"; return 1; }
    if (memcmp(OriginalAtomUpper, Expected::OriginalAtomUpper, sizeof OriginalAtomUpper)) { std::cerr << "Mismatch: OriginalAtomUpper\n"; return 1; }
    if (memcmp(OriginalComputerAnsiHigh, Expected::OriginalComputerAnsiHigh, sizeof OriginalComputerAnsiHigh)) { std::cerr << "Mismatch: OriginalComputerAnsiHigh\n"; return 1; }
    if (memcmp(OriginalComputerOemBytes, Expected::OriginalComputerOemBytes, sizeof OriginalComputerOemBytes)) { std::cerr << "Mismatch: OriginalComputerOemBytes\n"; return 1; }
    if (memcmp(OriginalGdiAnsiCharset, Expected::OriginalGdiAnsiCharset, sizeof OriginalGdiAnsiCharset)) { std::cerr << "Mismatch: OriginalGdiAnsiCharset\n"; return 1; }
    if (memcmp(OriginalGdiCharsetInfo, Expected::OriginalGdiCharsetInfo, sizeof OriginalGdiCharsetInfo)) { std::cerr << "Mismatch: OriginalGdiCharsetInfo\n"; return 1; }
    if (memcmp(OriginalSystemMenu1, Expected::OriginalSystemMenu1, sizeof OriginalSystemMenu1)) { std::cerr << "Mismatch: OriginalSystemMenu1\n"; return 1; }
    if (memcmp(OriginalSystemMenu2, Expected::OriginalSystemMenu2, sizeof OriginalSystemMenu2)) { std::cerr << "Mismatch: OriginalSystemMenu2\n"; return 1; }
    if (memcmp(Win98UserResources::DefaultIcon10, Expected::Win98UserResources::DefaultIcon10, sizeof Win98UserResources::DefaultIcon10)) { std::cerr << "Mismatch: DefaultIcon10\n"; return 1; }
    if (memcmp(Win98UserResources::DefaultIcon12, Expected::Win98UserResources::DefaultIcon12, sizeof Win98UserResources::DefaultIcon12)) { std::cerr << "Mismatch: DefaultIcon12\n"; return 1; }
    if (memcmp(Win98UserResources::DefaultIcon14, Expected::Win98UserResources::DefaultIcon14, sizeof Win98UserResources::DefaultIcon14)) { std::cerr << "Mismatch: DefaultIcon14\n"; return 1; }
    if (memcmp(Win98UserResources::DefaultIcon16, Expected::Win98UserResources::DefaultIcon16, sizeof Win98UserResources::DefaultIcon16)) { std::cerr << "Mismatch: DefaultIcon16\n"; return 1; }
    if (memcmp(Win98UserResources::DefaultIcon22, Expected::Win98UserResources::DefaultIcon22, sizeof Win98UserResources::DefaultIcon22)) { std::cerr << "Mismatch: DefaultIcon22\n"; return 1; }
    if (memcmp(Win98UserResources::DefaultIcon32, Expected::Win98UserResources::DefaultIcon32, sizeof Win98UserResources::DefaultIcon32)) { std::cerr << "Mismatch: DefaultIcon32\n"; return 1; }
    if (memcmp(Win98UserResources::StripCalls, Expected::Win98UserResources::StripCalls, sizeof Win98UserResources::StripCalls)) { std::cerr << "Mismatch: StripCalls\n"; return 1; }
    if (memcmp(OriginalImageBytes0, Expected::OriginalImageBytes0, sizeof OriginalImageBytes0)) { std::cerr << "Mismatch: OriginalImageBytes0\n"; return 1; }
    if (memcmp(OriginalImageBytes1, Expected::OriginalImageBytes1, sizeof OriginalImageBytes1)) { std::cerr << "Mismatch: OriginalImageBytes1\n"; return 1; }
    if (memcmp(OriginalImageBytes2, Expected::OriginalImageBytes2, sizeof OriginalImageBytes2)) { std::cerr << "Mismatch: OriginalImageBytes2\n"; return 1; }
    if (memcmp(OriginalImageBytes3, Expected::OriginalImageBytes3, sizeof OriginalImageBytes3)) { std::cerr << "Mismatch: OriginalImageBytes3\n"; return 1; }
    if (memcmp(OriginalImageBytes4, Expected::OriginalImageBytes4, sizeof OriginalImageBytes4)) { std::cerr << "Mismatch: OriginalImageBytes4\n"; return 1; }
    if (memcmp(OriginalImageBytes5, Expected::OriginalImageBytes5, sizeof OriginalImageBytes5)) { std::cerr << "Mismatch: OriginalImageBytes5\n"; return 1; }
    if (memcmp(OriginalImageBytes6, Expected::OriginalImageBytes6, sizeof OriginalImageBytes6)) { std::cerr << "Mismatch: OriginalImageBytes6\n"; return 1; }
    if (memcmp(OriginalImageBytes7, Expected::OriginalImageBytes7, sizeof OriginalImageBytes7)) { std::cerr << "Mismatch: OriginalImageBytes7\n"; return 1; }
    if (memcmp(OriginalImageBytes8, Expected::OriginalImageBytes8, sizeof OriginalImageBytes8)) { std::cerr << "Mismatch: OriginalImageBytes8\n"; return 1; }
    if (memcmp(OriginalImageBytes9, Expected::OriginalImageBytes9, sizeof OriginalImageBytes9)) { std::cerr << "Mismatch: OriginalImageBytes9\n"; return 1; }
    if (memcmp(OriginalImageBytes10, Expected::OriginalImageBytes10, sizeof OriginalImageBytes10)) { std::cerr << "Mismatch: OriginalImageBytes10\n"; return 1; }
    if (memcmp(OriginalImageBytes11, Expected::OriginalImageBytes11, sizeof OriginalImageBytes11)) { std::cerr << "Mismatch: OriginalImageBytes11\n"; return 1; }
    if (memcmp(OriginalImageBytes12, Expected::OriginalImageBytes12, sizeof OriginalImageBytes12)) { std::cerr << "Mismatch: OriginalImageBytes12\n"; return 1; }
    if (memcmp(OriginalImageBytes13, Expected::OriginalImageBytes13, sizeof OriginalImageBytes13)) { std::cerr << "Mismatch: OriginalImageBytes13\n"; return 1; }
    if (memcmp(OriginalImageBytes14, Expected::OriginalImageBytes14, sizeof OriginalImageBytes14)) { std::cerr << "Mismatch: OriginalImageBytes14\n"; return 1; }
    if (memcmp(OriginalImageBytes15, Expected::OriginalImageBytes15, sizeof OriginalImageBytes15)) { std::cerr << "Mismatch: OriginalImageBytes15\n"; return 1; }
    if (memcmp(OriginalImageBytes16, Expected::OriginalImageBytes16, sizeof OriginalImageBytes16)) { std::cerr << "Mismatch: OriginalImageBytes16\n"; return 1; }
    if (memcmp(OriginalImageBytes17, Expected::OriginalImageBytes17, sizeof OriginalImageBytes17)) { std::cerr << "Mismatch: OriginalImageBytes17\n"; return 1; }
    if (memcmp(OriginalImageBytes18, Expected::OriginalImageBytes18, sizeof OriginalImageBytes18)) { std::cerr << "Mismatch: OriginalImageBytes18\n"; return 1; }
    if (memcmp(OriginalImageBytes19, Expected::OriginalImageBytes19, sizeof OriginalImageBytes19)) { std::cerr << "Mismatch: OriginalImageBytes19\n"; return 1; }
    if (memcmp(OriginalImageBytes20, Expected::OriginalImageBytes20, sizeof OriginalImageBytes20)) { std::cerr << "Mismatch: OriginalImageBytes20\n"; return 1; }
    if (memcmp(OriginalImageBytes21, Expected::OriginalImageBytes21, sizeof OriginalImageBytes21)) { std::cerr << "Mismatch: OriginalImageBytes21\n"; return 1; }
    if (memcmp(OriginalImageBytes22, Expected::OriginalImageBytes22, sizeof OriginalImageBytes22)) { std::cerr << "Mismatch: OriginalImageBytes22\n"; return 1; }
    if (memcmp(OriginalImageBytes23, Expected::OriginalImageBytes23, sizeof OriginalImageBytes23)) { std::cerr << "Mismatch: OriginalImageBytes23\n"; return 1; }
    if (memcmp(OriginalImageBytes24, Expected::OriginalImageBytes24, sizeof OriginalImageBytes24)) { std::cerr << "Mismatch: OriginalImageBytes24\n"; return 1; }
    if (memcmp(OriginalImageBytes25, Expected::OriginalImageBytes25, sizeof OriginalImageBytes25)) { std::cerr << "Mismatch: OriginalImageBytes25\n"; return 1; }
    if (memcmp(OriginalImageBytes26, Expected::OriginalImageBytes26, sizeof OriginalImageBytes26)) { std::cerr << "Mismatch: OriginalImageBytes26\n"; return 1; }
    if (memcmp(OriginalImageBytes27, Expected::OriginalImageBytes27, sizeof OriginalImageBytes27)) { std::cerr << "Mismatch: OriginalImageBytes27\n"; return 1; }
    if (memcmp(OriginalImageBytes28, Expected::OriginalImageBytes28, sizeof OriginalImageBytes28)) { std::cerr << "Mismatch: OriginalImageBytes28\n"; return 1; }
    if (memcmp(OriginalImageBytes29, Expected::OriginalImageBytes29, sizeof OriginalImageBytes29)) { std::cerr << "Mismatch: OriginalImageBytes29\n"; return 1; }
    if (memcmp(OriginalImageBytes30, Expected::OriginalImageBytes30, sizeof OriginalImageBytes30)) { std::cerr << "Mismatch: OriginalImageBytes30\n"; return 1; }
    if (memcmp(OriginalImageBytes31, Expected::OriginalImageBytes31, sizeof OriginalImageBytes31)) { std::cerr << "Mismatch: OriginalImageBytes31\n"; return 1; }
    if (memcmp(OriginalImageBytes32, Expected::OriginalImageBytes32, sizeof OriginalImageBytes32)) { std::cerr << "Mismatch: OriginalImageBytes32\n"; return 1; }
    if (memcmp(OriginalImageBytes33, Expected::OriginalImageBytes33, sizeof OriginalImageBytes33)) { std::cerr << "Mismatch: OriginalImageBytes33\n"; return 1; }
    if (memcmp(OriginalImageBytes34, Expected::OriginalImageBytes34, sizeof OriginalImageBytes34)) { std::cerr << "Mismatch: OriginalImageBytes34\n"; return 1; }
    if (memcmp(OriginalImageBytes35, Expected::OriginalImageBytes35, sizeof OriginalImageBytes35)) { std::cerr << "Mismatch: OriginalImageBytes35\n"; return 1; }
    if (memcmp(OriginalImageBytes36, Expected::OriginalImageBytes36, sizeof OriginalImageBytes36)) { std::cerr << "Mismatch: OriginalImageBytes36\n"; return 1; }
    if (memcmp(OriginalImageBytes37, Expected::OriginalImageBytes37, sizeof OriginalImageBytes37)) { std::cerr << "Mismatch: OriginalImageBytes37\n"; return 1; }
    if (memcmp(OriginalImageBytes38, Expected::OriginalImageBytes38, sizeof OriginalImageBytes38)) { std::cerr << "Mismatch: OriginalImageBytes38\n"; return 1; }
    if (memcmp(OriginalImageBytes39, Expected::OriginalImageBytes39, sizeof OriginalImageBytes39)) { std::cerr << "Mismatch: OriginalImageBytes39\n"; return 1; }
    if (memcmp(OriginalImageBytes40, Expected::OriginalImageBytes40, sizeof OriginalImageBytes40)) { std::cerr << "Mismatch: OriginalImageBytes40\n"; return 1; }
    if (memcmp(OriginalImageBytes41, Expected::OriginalImageBytes41, sizeof OriginalImageBytes41)) { std::cerr << "Mismatch: OriginalImageBytes41\n"; return 1; }
    if (memcmp(OriginalImageBytes42, Expected::OriginalImageBytes42, sizeof OriginalImageBytes42)) { std::cerr << "Mismatch: OriginalImageBytes42\n"; return 1; }
    if (memcmp(OriginalImageBytes43, Expected::OriginalImageBytes43, sizeof OriginalImageBytes43)) { std::cerr << "Mismatch: OriginalImageBytes43\n"; return 1; }
    if (memcmp(OriginalImageBytes44, Expected::OriginalImageBytes44, sizeof OriginalImageBytes44)) { std::cerr << "Mismatch: OriginalImageBytes44\n"; return 1; }
    if (memcmp(OriginalImageBytes45, Expected::OriginalImageBytes45, sizeof OriginalImageBytes45)) { std::cerr << "Mismatch: OriginalImageBytes45\n"; return 1; }
    if (memcmp(OriginalImageBytes46, Expected::OriginalImageBytes46, sizeof OriginalImageBytes46)) { std::cerr << "Mismatch: OriginalImageBytes46\n"; return 1; }
    if (memcmp(OriginalImageBytes47, Expected::OriginalImageBytes47, sizeof OriginalImageBytes47)) { std::cerr << "Mismatch: OriginalImageBytes47\n"; return 1; }
    if (memcmp(OriginalImageBytes48, Expected::OriginalImageBytes48, sizeof OriginalImageBytes48)) { std::cerr << "Mismatch: OriginalImageBytes48\n"; return 1; }
    if (memcmp(OriginalImageBytes49, Expected::OriginalImageBytes49, sizeof OriginalImageBytes49)) { std::cerr << "Mismatch: OriginalImageBytes49\n"; return 1; }
    if (memcmp(OriginalImageBytes50, Expected::OriginalImageBytes50, sizeof OriginalImageBytes50)) { std::cerr << "Mismatch: OriginalImageBytes50\n"; return 1; }
    if (memcmp(OriginalImageBytes51, Expected::OriginalImageBytes51, sizeof OriginalImageBytes51)) { std::cerr << "Mismatch: OriginalImageBytes51\n"; return 1; }
    if (memcmp(OriginalImageBytes52, Expected::OriginalImageBytes52, sizeof OriginalImageBytes52)) { std::cerr << "Mismatch: OriginalImageBytes52\n"; return 1; }
    if (memcmp(OriginalImageBytes53, Expected::OriginalImageBytes53, sizeof OriginalImageBytes53)) { std::cerr << "Mismatch: OriginalImageBytes53\n"; return 1; }
    if (memcmp(OriginalImageBytes54, Expected::OriginalImageBytes54, sizeof OriginalImageBytes54)) { std::cerr << "Mismatch: OriginalImageBytes54\n"; return 1; }
    if (memcmp(OriginalImageBytes55, Expected::OriginalImageBytes55, sizeof OriginalImageBytes55)) { std::cerr << "Mismatch: OriginalImageBytes55\n"; return 1; }
    if (memcmp(OriginalImageBytes56, Expected::OriginalImageBytes56, sizeof OriginalImageBytes56)) { std::cerr << "Mismatch: OriginalImageBytes56\n"; return 1; }
    if (memcmp(OriginalImageBytes57, Expected::OriginalImageBytes57, sizeof OriginalImageBytes57)) { std::cerr << "Mismatch: OriginalImageBytes57\n"; return 1; }
    if (memcmp(OriginalImageBytes58, Expected::OriginalImageBytes58, sizeof OriginalImageBytes58)) { std::cerr << "Mismatch: OriginalImageBytes58\n"; return 1; }
    if (memcmp(OriginalImageBytes59, Expected::OriginalImageBytes59, sizeof OriginalImageBytes59)) { std::cerr << "Mismatch: OriginalImageBytes59\n"; return 1; }
    if (memcmp(OriginalImageBytes60, Expected::OriginalImageBytes60, sizeof OriginalImageBytes60)) { std::cerr << "Mismatch: OriginalImageBytes60\n"; return 1; }
    if (memcmp(OriginalImageBytes61, Expected::OriginalImageBytes61, sizeof OriginalImageBytes61)) { std::cerr << "Mismatch: OriginalImageBytes61\n"; return 1; }
    if (memcmp(OriginalImageBytes62, Expected::OriginalImageBytes62, sizeof OriginalImageBytes62)) { std::cerr << "Mismatch: OriginalImageBytes62\n"; return 1; }
    if (memcmp(OriginalImageBytes63, Expected::OriginalImageBytes63, sizeof OriginalImageBytes63)) { std::cerr << "Mismatch: OriginalImageBytes63\n"; return 1; }
    if (memcmp(OriginalImageBytes64, Expected::OriginalImageBytes64, sizeof OriginalImageBytes64)) { std::cerr << "Mismatch: OriginalImageBytes64\n"; return 1; }
    if (memcmp(OriginalImageBytes65, Expected::OriginalImageBytes65, sizeof OriginalImageBytes65)) { std::cerr << "Mismatch: OriginalImageBytes65\n"; return 1; }
    if (memcmp(OriginalImageBytes66, Expected::OriginalImageBytes66, sizeof OriginalImageBytes66)) { std::cerr << "Mismatch: OriginalImageBytes66\n"; return 1; }
    if (memcmp(OriginalImageBytes67, Expected::OriginalImageBytes67, sizeof OriginalImageBytes67)) { std::cerr << "Mismatch: OriginalImageBytes67\n"; return 1; }
    if (memcmp(OriginalImageBytes68, Expected::OriginalImageBytes68, sizeof OriginalImageBytes68)) { std::cerr << "Mismatch: OriginalImageBytes68\n"; return 1; }
    if (memcmp(OriginalImageBytes69, Expected::OriginalImageBytes69, sizeof OriginalImageBytes69)) { std::cerr << "Mismatch: OriginalImageBytes69\n"; return 1; }
    if (memcmp(OriginalImageBytes70, Expected::OriginalImageBytes70, sizeof OriginalImageBytes70)) { std::cerr << "Mismatch: OriginalImageBytes70\n"; return 1; }
    if (memcmp(OriginalImageBytes71, Expected::OriginalImageBytes71, sizeof OriginalImageBytes71)) { std::cerr << "Mismatch: OriginalImageBytes71\n"; return 1; }
    if (memcmp(OriginalImageBytes72, Expected::OriginalImageBytes72, sizeof OriginalImageBytes72)) { std::cerr << "Mismatch: OriginalImageBytes72\n"; return 1; }
    if (memcmp(OriginalImageBytes73, Expected::OriginalImageBytes73, sizeof OriginalImageBytes73)) { std::cerr << "Mismatch: OriginalImageBytes73\n"; return 1; }
    if (memcmp(OriginalImageBytes74, Expected::OriginalImageBytes74, sizeof OriginalImageBytes74)) { std::cerr << "Mismatch: OriginalImageBytes74\n"; return 1; }
    if (memcmp(OriginalImageBytes75, Expected::OriginalImageBytes75, sizeof OriginalImageBytes75)) { std::cerr << "Mismatch: OriginalImageBytes75\n"; return 1; }
    if (memcmp(OriginalImageBytes76, Expected::OriginalImageBytes76, sizeof OriginalImageBytes76)) { std::cerr << "Mismatch: OriginalImageBytes76\n"; return 1; }
    if (memcmp(OriginalImageBytes77, Expected::OriginalImageBytes77, sizeof OriginalImageBytes77)) { std::cerr << "Mismatch: OriginalImageBytes77\n"; return 1; }
    if (memcmp(OriginalImageBytes78, Expected::OriginalImageBytes78, sizeof OriginalImageBytes78)) { std::cerr << "Mismatch: OriginalImageBytes78\n"; return 1; }
    if (memcmp(OriginalImageBytes79, Expected::OriginalImageBytes79, sizeof OriginalImageBytes79)) { std::cerr << "Mismatch: OriginalImageBytes79\n"; return 1; }
    if (memcmp(OriginalImageBytes80, Expected::OriginalImageBytes80, sizeof OriginalImageBytes80)) { std::cerr << "Mismatch: OriginalImageBytes80\n"; return 1; }
    if (memcmp(OriginalImageBytes81, Expected::OriginalImageBytes81, sizeof OriginalImageBytes81)) { std::cerr << "Mismatch: OriginalImageBytes81\n"; return 1; }
    if (memcmp(OriginalImageBytes82, Expected::OriginalImageBytes82, sizeof OriginalImageBytes82)) { std::cerr << "Mismatch: OriginalImageBytes82\n"; return 1; }
    if (memcmp(OriginalImageBytes83, Expected::OriginalImageBytes83, sizeof OriginalImageBytes83)) { std::cerr << "Mismatch: OriginalImageBytes83\n"; return 1; }
    if (memcmp(OriginalImageBytes84, Expected::OriginalImageBytes84, sizeof OriginalImageBytes84)) { std::cerr << "Mismatch: OriginalImageBytes84\n"; return 1; }
    if (memcmp(OriginalImageBytes85, Expected::OriginalImageBytes85, sizeof OriginalImageBytes85)) { std::cerr << "Mismatch: OriginalImageBytes85\n"; return 1; }
    if (memcmp(OriginalImageBytes86, Expected::OriginalImageBytes86, sizeof OriginalImageBytes86)) { std::cerr << "Mismatch: OriginalImageBytes86\n"; return 1; }
    if (memcmp(OriginalImageBytes87, Expected::OriginalImageBytes87, sizeof OriginalImageBytes87)) { std::cerr << "Mismatch: OriginalImageBytes87\n"; return 1; }
    if (memcmp(OriginalImageBytes88, Expected::OriginalImageBytes88, sizeof OriginalImageBytes88)) { std::cerr << "Mismatch: OriginalImageBytes88\n"; return 1; }
    if (memcmp(OriginalImageBytes89, Expected::OriginalImageBytes89, sizeof OriginalImageBytes89)) { std::cerr << "Mismatch: OriginalImageBytes89\n"; return 1; }
    if (memcmp(OriginalImageBytes90, Expected::OriginalImageBytes90, sizeof OriginalImageBytes90)) { std::cerr << "Mismatch: OriginalImageBytes90\n"; return 1; }
    if (memcmp(OriginalImageBytes91, Expected::OriginalImageBytes91, sizeof OriginalImageBytes91)) { std::cerr << "Mismatch: OriginalImageBytes91\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::Template119, Expected::OriginalSerialUiAssets::Template119, sizeof OriginalSerialUiAssets::Template119)) { std::cerr << "Mismatch: Template119\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::Template101, Expected::OriginalSerialUiAssets::Template101, sizeof OriginalSerialUiAssets::Template101)) { std::cerr << "Mismatch: Template101\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String462, Expected::OriginalSerialUiAssets::String462, sizeof OriginalSerialUiAssets::String462)) { std::cerr << "Mismatch: String462\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String342, Expected::OriginalSerialUiAssets::String342, sizeof OriginalSerialUiAssets::String342)) { std::cerr << "Mismatch: String342\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String208, Expected::OriginalSerialUiAssets::String208, sizeof OriginalSerialUiAssets::String208)) { std::cerr << "Mismatch: String208\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String209, Expected::OriginalSerialUiAssets::String209, sizeof OriginalSerialUiAssets::String209)) { std::cerr << "Mismatch: String209\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String210, Expected::OriginalSerialUiAssets::String210, sizeof OriginalSerialUiAssets::String210)) { std::cerr << "Mismatch: String210\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String211, Expected::OriginalSerialUiAssets::String211, sizeof OriginalSerialUiAssets::String211)) { std::cerr << "Mismatch: String211\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String213, Expected::OriginalSerialUiAssets::String213, sizeof OriginalSerialUiAssets::String213)) { std::cerr << "Mismatch: String213\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String214, Expected::OriginalSerialUiAssets::String214, sizeof OriginalSerialUiAssets::String214)) { std::cerr << "Mismatch: String214\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String215, Expected::OriginalSerialUiAssets::String215, sizeof OriginalSerialUiAssets::String215)) { std::cerr << "Mismatch: String215\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String216, Expected::OriginalSerialUiAssets::String216, sizeof OriginalSerialUiAssets::String216)) { std::cerr << "Mismatch: String216\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String217, Expected::OriginalSerialUiAssets::String217, sizeof OriginalSerialUiAssets::String217)) { std::cerr << "Mismatch: String217\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String218, Expected::OriginalSerialUiAssets::String218, sizeof OriginalSerialUiAssets::String218)) { std::cerr << "Mismatch: String218\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String219, Expected::OriginalSerialUiAssets::String219, sizeof OriginalSerialUiAssets::String219)) { std::cerr << "Mismatch: String219\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String220, Expected::OriginalSerialUiAssets::String220, sizeof OriginalSerialUiAssets::String220)) { std::cerr << "Mismatch: String220\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String221, Expected::OriginalSerialUiAssets::String221, sizeof OriginalSerialUiAssets::String221)) { std::cerr << "Mismatch: String221\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String222, Expected::OriginalSerialUiAssets::String222, sizeof OriginalSerialUiAssets::String222)) { std::cerr << "Mismatch: String222\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String200, Expected::OriginalSerialUiAssets::String200, sizeof OriginalSerialUiAssets::String200)) { std::cerr << "Mismatch: String200\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String201, Expected::OriginalSerialUiAssets::String201, sizeof OriginalSerialUiAssets::String201)) { std::cerr << "Mismatch: String201\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String202, Expected::OriginalSerialUiAssets::String202, sizeof OriginalSerialUiAssets::String202)) { std::cerr << "Mismatch: String202\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String203, Expected::OriginalSerialUiAssets::String203, sizeof OriginalSerialUiAssets::String203)) { std::cerr << "Mismatch: String203\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String204, Expected::OriginalSerialUiAssets::String204, sizeof OriginalSerialUiAssets::String204)) { std::cerr << "Mismatch: String204\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String205, Expected::OriginalSerialUiAssets::String205, sizeof OriginalSerialUiAssets::String205)) { std::cerr << "Mismatch: String205\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String206, Expected::OriginalSerialUiAssets::String206, sizeof OriginalSerialUiAssets::String206)) { std::cerr << "Mismatch: String206\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String207, Expected::OriginalSerialUiAssets::String207, sizeof OriginalSerialUiAssets::String207)) { std::cerr << "Mismatch: String207\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String240, Expected::OriginalSerialUiAssets::String240, sizeof OriginalSerialUiAssets::String240)) { std::cerr << "Mismatch: String240\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String241, Expected::OriginalSerialUiAssets::String241, sizeof OriginalSerialUiAssets::String241)) { std::cerr << "Mismatch: String241\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String242, Expected::OriginalSerialUiAssets::String242, sizeof OriginalSerialUiAssets::String242)) { std::cerr << "Mismatch: String242\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String243, Expected::OriginalSerialUiAssets::String243, sizeof OriginalSerialUiAssets::String243)) { std::cerr << "Mismatch: String243\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String244, Expected::OriginalSerialUiAssets::String244, sizeof OriginalSerialUiAssets::String244)) { std::cerr << "Mismatch: String244\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String250, Expected::OriginalSerialUiAssets::String250, sizeof OriginalSerialUiAssets::String250)) { std::cerr << "Mismatch: String250\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String251, Expected::OriginalSerialUiAssets::String251, sizeof OriginalSerialUiAssets::String251)) { std::cerr << "Mismatch: String251\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String252, Expected::OriginalSerialUiAssets::String252, sizeof OriginalSerialUiAssets::String252)) { std::cerr << "Mismatch: String252\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String253, Expected::OriginalSerialUiAssets::String253, sizeof OriginalSerialUiAssets::String253)) { std::cerr << "Mismatch: String253\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String254, Expected::OriginalSerialUiAssets::String254, sizeof OriginalSerialUiAssets::String254)) { std::cerr << "Mismatch: String254\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String255, Expected::OriginalSerialUiAssets::String255, sizeof OriginalSerialUiAssets::String255)) { std::cerr << "Mismatch: String255\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String256, Expected::OriginalSerialUiAssets::String256, sizeof OriginalSerialUiAssets::String256)) { std::cerr << "Mismatch: String256\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String257, Expected::OriginalSerialUiAssets::String257, sizeof OriginalSerialUiAssets::String257)) { std::cerr << "Mismatch: String257\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String260, Expected::OriginalSerialUiAssets::String260, sizeof OriginalSerialUiAssets::String260)) { std::cerr << "Mismatch: String260\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String261, Expected::OriginalSerialUiAssets::String261, sizeof OriginalSerialUiAssets::String261)) { std::cerr << "Mismatch: String261\n"; return 1; }
    if (memcmp(OriginalSerialUiAssets::String262, Expected::OriginalSerialUiAssets::String262, sizeof OriginalSerialUiAssets::String262)) { std::cerr << "Mismatch: String262\n"; return 1; }
    if (memcmp(Win98GdiPalette::DefaultColors, Expected::Win98GdiPalette::DefaultColors, sizeof Win98GdiPalette::DefaultColors)) { std::cerr << "Mismatch: DefaultColors\n"; return 1; }
    if (memcmp(Win98GdiPalette::HalftoneColors, Expected::Win98GdiPalette::HalftoneColors, sizeof Win98GdiPalette::HalftoneColors)) { std::cerr << "Mismatch: HalftoneColors\n"; return 1; }
    assert(!memcmp(OriginalCType, Expected::OriginalCType, sizeof OriginalCType));
    assert(!memcmp(OriginalSystemCType, Expected::OriginalSystemCType, sizeof OriginalSystemCType));
    assert(!memcmp(OriginalSystemWideType, Expected::OriginalSystemWideType, sizeof OriginalSystemWideType));
    assert(!memcmp(OriginalUpper, Expected::OriginalUpper, sizeof OriginalUpper));
    assert(!memcmp(OriginalLower, Expected::OriginalLower, sizeof OriginalLower));
    assert(OriginalLocaleData.size() == sizeof Expected::OriginalLocaleData / sizeof *Expected::OriginalLocaleData);
    for (unsigned i = 0; i < OriginalLocaleData.size(); ++i) {
        assert(OriginalLocaleData[i].locale == Expected::OriginalLocaleData[i].locale);
        for (unsigned j = 0; j < sizeof OriginalLocaleTypes / sizeof *OriginalLocaleTypes; ++j)
            assert(!strcmp(OriginalLocaleData[i].values[j], Expected::OriginalLocaleData[i].values[j]));
    }
    for (unsigned i = 0; i < 43; ++i) assert(!strcmp(OriginalCTime[i], Expected::OriginalCTime[i]));
    for (const auto &entry : Expected::OriginalMultimediaMessages) {
        bool found = false;
        for (const auto &actual : OriginalMultimediaMessages) if (actual.id == entry.id) { assert(!strcmp(entry.text, actual.text)); found = true; }
        assert(found);
    }
    assert(HostWindowsData::wide(0x80) == 0x20ac);
    assert(HostWindowsData::wide('A') == 'A');
    assert(!memcmp(OriginalPropertySheetAssets::Template1006, Expected::OriginalPropertySheetAssets::Template1006, sizeof OriginalPropertySheetAssets::Template1006));
    assert(!memcmp(OriginalPropertySheetAssets::Template1020, Expected::OriginalPropertySheetAssets::Template1020, sizeof OriginalPropertySheetAssets::Template1020));
    assert(!memcmp(OriginalPropertySheetAssets::CaptionFormat, Expected::OriginalPropertySheetAssets::CaptionFormat, sizeof OriginalPropertySheetAssets::CaptionFormat));
    Win98FormatMessage::Entry message;
    assert(Win98FormatMessage::Lookup(Win98MessageTable::Data, Win98MessageTable::Bytes, 2, message));
    assert(message.bytes > 4);
    Win98Nls::Table ansi{OriginalCodePage1252, sizeof OriginalCodePage1252};
    assert(ansi.Valid() && ansi.Wide(0x80) == 0x20ac && ansi.Narrow(0x20ac) == 0x80);
    DialogResource::Template serialDialog;
    assert(serialDialog.Parse(OriginalSerialUiAssets::Template119, sizeof OriginalSerialUiAssets::Template119));
    assert(serialDialog.count > 5 && serialDialog.title[0]);
    assert(serialDialog.Parse(OriginalSerialUiAssets::Template101, sizeof OriginalSerialUiAssets::Template101));
    assert(serialDialog.count > 5);
    assert(OriginalCalendars.size() == sizeof Expected::OriginalCalendars / sizeof *Expected::OriginalCalendars);
    for (unsigned i = 0; i < OriginalCalendars.size(); ++i) {
        assert(OriginalCalendars[i].id == Expected::OriginalCalendars[i].id);
        assert(!strcmp(OriginalCalendars[i].maximum, Expected::OriginalCalendars[i].maximum));
    }
    assert(OriginalCalendarEras.size() == sizeof Expected::OriginalCalendarEras / sizeof *Expected::OriginalCalendarEras);
    for (unsigned i = 0; i < OriginalCalendarEras.size(); ++i) {
        assert(OriginalCalendarEras[i].year == Expected::OriginalCalendarEras[i].year);
        assert(!strcmp(OriginalCalendarEras[i].name, Expected::OriginalCalendarEras[i].name));
    }
    // A missing input must reject startup even after a successful import.
    assert(!HostWindowsData::Load([](const char *, HostWindowsData::Bytes &) { return false; }, error));
    assert(error.find("CP_1252.NLS") != std::string::npos);
    for (unsigned size = 0; size < 16; ++size) {
        HostWindowsData::Bytes truncated(size);
        std::vector<HostWindowsData::Resource> rs;
        assert(!HostWindowsData::resources(truncated, rs));
        assert(HostWindowsData::dialog(truncated).empty());
    }
    std::cout << "PASS runtime import, NLS conversion, missing files and truncated resources\n";
}
