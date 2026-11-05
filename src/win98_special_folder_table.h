// SPDX-License-Identifier: GPL-2.0-or-later
// SHELL32 and original RT_STRING resources.
#pragma once
namespace Win98SpecialFolders {
struct Entry {unsigned id,root;const char *value,*long_name,*short_name;};
inline constexpr Entry Table[]={
 {0x0u,0x0u,"","",""},
 {0x12u,0x0u,"","",""},
 {0x11u,0x0u,"","",""},
 {0x1u,0x0u,"","",""},
 {0x3u,0x0u,"","",""},
 {0x4u,0x0u,"","",""},
 {0xau,0x0u,"","",""},
 {0x14u,0x80000001u,"Fonts","Fonts","Fonts"},
 {0x10u,0x80000001u,"Desktop","Desktop","Desktop"},
 {0x7u,0x80000001u,"Startup","Start Menu\\Programs\\StartUp","StrtMenu\\Programs\\Startup"},
 {0x2u,0x80000001u,"Programs","Start Menu\\Programs","StrtMenu\\Programs"},
 {0xbu,0x80000001u,"Start Menu","Start Menu","StrtMenu"},
 {0x8u,0x80000001u,"Recent","Recent","Recent"},
 {0x9u,0x80000001u,"SendTo","SendTo","SendTo"},
 {0x5u,0x80000001u,"Personal","My Documents","MyDocs"},
 {0x6u,0x80000001u,"Favorites","Favorites","Favorite"},
 {0x13u,0x80000001u,"NetHood","NetHood","NetHood"},
 {0x1bu,0x80000001u,"PrintHood","PrintHood","PrtHood"},
 {0x15u,0x80000001u,"Templates","ShellNew","ShellNew"},
 {0x18u,0x80000002u,"Common Startup","All Users\\Start Menu\\Programs\\StartUp","allusers\\StrtMenu\\Programs\\Startup"},
 {0x17u,0x80000002u,"Common Programs","All Users\\Start Menu\\Programs","allusers\\Programs"},
 {0x16u,0x80000002u,"Common Start Menu","All Users\\Start Menu","allusers\\StrtMenu"},
 {0x19u,0x80000002u,"Common Desktop","All Users\\Desktop","allusers\\Desktop"},
 {0x1fu,0x80000002u,"Common Favorites","All Users\\Favorites","allusers\\Favorite"},
 {0x1au,0x80000001u,"AppData","Application Data","AppData"},
 {0x1du,0x80000001u,"AltStartup","Start Menu\\Programs\\StartUp","StrtMenu\\Programs\\Startup"},
 {0x1eu,0x80000002u,"Common AltStartup","Start Menu\\Programs\\StartUp","StrtMenu\\Programs\\Startup"},
 {0x20u,0x80000001u,"Cache","Temporary Internet Files","Cache"},
 {0x21u,0x80000001u,"Cookies","Cookies","Cookies"},
 {0x22u,0x80000001u,"History","History","History"},
};
}
