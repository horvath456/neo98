// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Win98 GDI device information.
// Driver loading, profile storage, pointer translation and display hardware are providers.
namespace Win98DcInformation {
using U = unsigned;
inline int SignedWord(U value) { return int(value & 32767) - int(value & 32768); }
template<class E> U Create(E &e, U driver, U device, U output, U mode, bool information) {
    if (device && e.Profile(device)) {
        U comma = e.Buffer();
        while (e.Read8(comma) != ',') {
            if (!e.Valid()) return 0;
            ++comma;
        }
        e.Write8(comma, 0);
        driver = e.Buffer();
        if (!output || !e.Read8(output)) output = comma + 1;
    }
    return e.Valid() ? e.Create16(driver, device, output, mode, information) : 0;
}
// The Win32 direct path deliberately differs from GDI16 for high-color
// NUMBRUSHES/NUMPENS/NUMCOLORS and does not add RC_BITMAP64 to RASTERCAPS.
template<class E> U Caps32(E &e, U signature, U flags, U index) {
    if (index > 108) return 0;
    signature &= 0x5fff;
    if (signature < 0x4f4d || signature > 0x4f51) return 0;
    if (signature == 0x4f51) return index == 2 ? 5 : 0;
    if (flags & 0x8000) return e.BitmapCaps(index);
    if (SignedWord(e.Word(0)) < 0x300 && index > 102) return 0;
    U value = U(SignedWord(e.Word(index)));
    if (index == 2) { if ((value & 65535) == 11) value = 2; }
    else if (e.Word(12) >= 9 && index >= 16 && index <= 24 && !(index & 4)) value = ~0u;
    return value;
}
template<class E> U Caps16(E &e, U signature, U index) {
    index &= 65535;
    if (index > 108) return 0;
    if ((signature & 0x5fff) == 0x4f51) return index == 2 ? 5 : 0;
    if (SignedWord(e.Word(0)) < 0x300 && index > 102) return 0;
    U value = e.Word(index);
    if (index == 38) value |= 32;
    else if (index == 2) { if (value == 11) value = 2; }
    else if (e.Word(12) > 8 && index >= 16 && index <= 24 && !(index & 4)) value = 0x800;
    return value;
}
// Original GDI32 public GetDeviceCaps Windows 98 sends printer
// physical size/offset/scaling pairs through Escape 12, 13 and 14.
template<class E> U Query(E &e,U index) {
    if(index>=110 && index<=115){
        U pair[2]={};
        return e.Escape(12+(index-110)/2,pair)>0 ? pair[index&1] : 0;
    }
    return e.Direct(index);
}
// Original DC initial attribute stores, GDI. Native handles,
// driver references and the surrounding allocation sequence remain providers.
template<class E> void InitialAttributes(E &e) {
    e.Write16(0x4e,13); e.Write16(0x50,2); e.Write16(0x72,1);
    e.Write16(0x74,0); e.Write16(0x76,0x4120);
    e.Write16(0x78,0); e.Write16(0x7a,0);
    e.Write16(0x84,1); e.Write16(0x86,1);
    e.Write16(0x8c,1); e.Write16(0x8e,1);
    e.Write16(0x94,1); e.Write16(0x96,3);
    e.Write16(0x98,1); e.Write16(0x9a,1); e.Write8(0xb2,1);
}
// GDI after the handle-to-record provider returns its signature.
inline bool DrawingAllowed(U signature) { signature&=0x5fff;return signature!=0x4f4e && signature>=0x4f4d && signature<=0x4f51; }
inline U Signature(bool information) { return information ? 0x4f4e : 0x4f4d; }
} // namespace Win98DcInformation
