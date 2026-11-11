#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import original local Win98 CRT defaults and CP1252 NLS category data.

The output remains in build/crt-locale. No shared asset generator is invoked.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/crt-locale'
EXPECTED = {
 'LOCALE.NLS': '6b3ad917b262fd4a098ac8830703c6f2d20364253399d4c728a2a91b7550e74d',
 'CP_1252.NLS': '6ac031ebf6b0acb242f1791035614b691ecba93360ebb3ec2bed464379771470',
}
CRT_TABLES = {
 '887eb5ce93edb7192ca3e9220f07f9ca0f94db02af5862ebcbdfcb852db99fd1': (0x7803a184, 0x7803c158),
 '2d5670881a6d13a100e37da297f6eeb876f68b63073a28eb5d650625c60f9bf7': (0x780371ac, 0x78039178),
}
# Original KERNEL32 GetLocaleInfoA Windows 98 selects these UTF-16 offsets for
# English-US with LOCALE_NOUSEROVERRIDE. Verified by running its real switch
# and stopping at the common conversion path. Fields2/6 and1013
# require additional providers and are not included.
OFFSETS = {1: 50272, 3: 50492, 4: 50506, 5: 50282, 7: 50550, 8: 50564, 9: 50294, 10: 50304, 11: 50328, 12: 50592, 13: 50364, 14: 50596, 15: 50600, 16: 50604, 17: 50372, 18: 50378, 19: 50612, 20: 50634, 21: 50638, 22: 50686, 23: 50690, 24: 50694, 25: 50386, 26: 50392, 27: 50398, 28: 50402, 29: 50938, 30: 50790, 31: 50806, 32: 50964, 33: 50444, 34: 50460, 35: 50432, 36: 50448, 37: 50436, 38: 50452, 39: 50456, 40: 50794, 41: 50800, 42: 51156, 43: 51170, 44: 51186, 45: 51206, 46: 51224, 47: 51238, 48: 51256, 49: 51270, 50: 51278, 51: 51286, 52: 51294, 53: 51302, 54: 51310, 55: 51318, 56: 51326, 57: 51342, 58: 51360, 59: 51372, 60: 51384, 61: 51392, 62: 51402, 63: 51412, 64: 51426, 65: 51446, 66: 51462, 67: 51480, 68: 51502, 69: 51510, 70: 51518, 71: 51526, 72: 51534, 73: 51542, 74: 51550, 75: 51558, 76: 51566, 77: 51574, 78: 51582, 79: 51590, 80: 50702, 81: 50706, 82: 50408, 83: 50420, 84: 50412, 85: 50416, 86: 50424, 87: 50428, 89: 50500, 90: 50558, 4097: 50476, 4098: 50522, 4099: 50710, 4100: 50316, 4101: 50440, 4102: 50942, 4103: 50646, 4104: 50666, 4105: 50464, 4106: 50368, 4107: 51104, 4108: 50468, 4109: 50472, 4110: 51498, 4111: 51598, 4112: 50382, 4113: 50340, 4114: 50352}

# Locale-record selectors audited against 6,930 original GetLocaleInfoA calls.
# Indexed offsets count UTF-16 words; field 0x100b skips its leading WORD pair.
FIXED_FIELDS = {1: 146, 5: 156, 9: 168, 10: 178, 11: 202, 13: 238, 17: 246, 18: 252, 25: 260, 26: 266, 27: 272, 28: 276, 33: 318, 34: 334, 35: 306, 36: 322, 37: 310, 38: 326, 39: 330, 82: 282, 83: 294, 84: 286, 85: 290, 86: 298, 87: 302, 4100: 190, 4101: 314, 4105: 338, 4106: 242, 4108: 342, 4109: 346, 4112: 256, 4113: 214, 4114: 226}
INDEXED_FIELDS = {3: 1, 4: 3, 7: 5, 8: 7, 12: 8, 14: 9, 15: 10, 16: 11, 19: 12, 20: 13, 21: 14, 22: 17, 23: 18, 24: 19, 29: 27, 30: 23, 31: 26, 32: 29, 40: 24, 41: 25, 42: 31, 43: 32, 44: 33, 45: 34, 46: 35, 47: 36, 48: 37, 49: 38, 50: 39, 51: 40, 52: 41, 53: 42, 54: 43, 55: 44, 56: 45, 57: 46, 58: 47, 59: 48, 60: 49, 61: 50, 62: 51, 63: 52, 64: 53, 65: 54, 66: 55, 67: 56, 68: 58, 69: 59, 70: 60, 71: 61, 72: 62, 73: 63, 74: 64, 75: 65, 76: 66, 77: 67, 78: 68, 79: 69, 80: 20, 81: 21, 89: 2, 90: 6, 4097: 0, 4098: 4, 4099: 22, 4102: 28, 4103: 15, 4104: 16, 4110: 57, 4111: 70, 4107: 30}

def locale_field_offset(data, base, field):
    if field in FIXED_FIELDS:
        return base + FIXED_FIELDS[field]
    return base + half(data, base + INDEXED_FIELDS[field] * 2) * 2 + (4 if field == 0x100b else 0)

def locale_text(data, at):
    end = at
    while half(data, end): end += 2
    return data[at:end].decode("utf-16le")

def half(data, at): return struct.unpack_from('<H', data, at)[0]
def word(data, at): return struct.unpack_from('<I', data, at)[0]
def pe_bytes(data, address, size):
    pe = word(data, 60)
    optional = pe + 24
    base = word(data, optional + 28)
    sections = optional + half(data, pe + 20)
    for i in range(half(data, pe + 6)):
        at = sections + i * 40
        rva, count, offset = word(data, at+12), word(data, at+16), word(data, at+20)
        if rva <= address-base and address-base-rva+size <= count:
            at = offset + address-base-rva
            return data[at:at+size]
    raise ValueError(f'Unmapped original MSVCRT address {address:08x}')

def calendar_data(data):
    """LOCALE.NLS directory used by KERNEL32 and."""
    calendars = []
    directory = word(data, 8) * 2
    for i in range(word(data, 4)):
        number, offset = struct.unpack_from('<HH', data, directory + 4 * i)
        base = directory + 2 * offset
        eras = []
        at, end = (base + half(data, base + field) * 2 for field in (8, 10))
        for _ in range(half(data, base)):
            if not base <= at < end <= len(data):
                raise ValueError('Invalid calendar era directory')
            month, day, year, step = struct.unpack_from('<4H', data, at)
            name_at = at + 8 + 2 * (len(locale_text(data, at + 8)) + 1)
            eras.append((year, month, day, locale_text(data, name_at)))
            if not step:
                raise ValueError('Empty calendar era record')
            at += step * 2
        calendars.append((number, locale_text(data, base + 2 * half(data, base + 6)), eras))
    return calendars

def generate():
    OUT.mkdir(exist_ok=True)
    provenance = json.loads((ROOT/'build/generated/provenance.json').read_text())['source']
    files, hashes = {}, {}
    for name in ('MSVCRT.DLL', *EXPECTED):
        path = ROOT/'build/programs'/name if name.endswith('.DLL') else OUT/name
        if not path.exists():
            win98_c.copy(f'::/WINDOWS/SYSTEM/{name}', str(path))
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        valid = digest in CRT_TABLES if name == 'MSVCRT.DLL' else digest == EXPECTED[name]
        if not valid:
            raise ValueError(f'Unexpected original {name}; audit this version before importing')
        files[name] = data
        hashes[name] = digest
    crt, locale, cp = (files[n] for n in ('MSVCRT.DLL','LOCALE.NLS','CP_1252.NLS'))
    ctype_address, ctime_address = CRT_TABLES[hashes['MSVCRT.DLL']]
    text = ['// Original local Win98 locale data. Do not redistribute.']
    text += ['static const struct {unsigned type; const char *text;} OriginalLocaleUS[] = {']
    fields = {}
    for field, at in OFFSETS.items():
        end=at
        while half(locale,end):end+=2
        value=locale[at:end].decode('utf-16le')
        fields[field]=value
        text.append('{'+str(field)+','+json.dumps(value)+'},')
    text += ['};']
    ctype=list(struct.unpack('<257H',pe_bytes(crt,ctype_address,514)))
    system=[0]+[half(cp,word(cp,15*4)*2+c*2) for c in range(256)]
    def narrow(ch):
        at=word(cp,5*4)*2
        first=half(cp,at+(ch>>8)*2)
        second=half(cp,at+(first+((ch>>4)&15))*2)
        return half(cp,at+(second+(ch&15))*2)&255
    # Import defaults only for records using our available CP1252 table.
    # Encode with its original best-fit mapping, not Python's replacement rule.
    locales = {}
    for i in range(word(locale, 0)):
        lcid, base = word(locale, 12 + i * 8), word(locale, 16 + i * 8) * 2
        if locale_text(locale, locale_field_offset(locale, base, 0x1004)) != '1252':
            continue
        values = []
        for field in OFFSETS:
            value = locale_text(locale, locale_field_offset(locale, base, field))
            values.append(bytes(narrow(ord(c)) for c in value))
        locales[lcid] = values
    text.append('static const unsigned OriginalLocaleTypes[] = {' + ','.join(map(str, OFFSETS)) + '};')
    text.append('static const struct {unsigned locale; const char *values[' + str(len(OFFSETS)) + '];} OriginalLocaleData[] = {')
    for lcid, values in locales.items():
        # Fixed-width octal escapes preserve ANSI bytes in every host compiler.
        strings = ['"' + ''.join('\\%03o' % c for c in value) + '"' for value in values]
        text.append('{' + str(lcid) + ',{' + ','.join(strings) + '}},')
    text.append('};')
    # The lists EnumTimeFormats, EnumDateFormats and EnumCalendarInfo walk:
    # times, short dates, year-months, long dates, each from its own offset
    # of the record to the next one.
    text.append('static const struct {unsigned locale; unsigned counts[4]; const char *values[4][12];} OriginalLocaleFormats[] = {')
    for i in range(word(locale, 0)):
        lcid, base = word(locale, 12 + i * 8), word(locale, 16 + i * 8) * 2
        if lcid not in locales:
            continue
        lists = []
        for head in (0x2c, 0x34, 0x38, 0x3a):
            at, end, values = base + half(locale, base + head) * 2, base + half(locale, base + head + 2) * 2, []
            while at < end and half(locale, at):
                value = locale_text(locale, at)
                at += (len(value) + 1) * 2
                values.append('"' + ''.join('\\%03o' % narrow(ord(c)) for c in value) + '"')
            assert 0 < len(values) <= 12
            lists.append(values)
        text.append('{%d,{%s},{%s}},' % (lcid, ','.join(str(len(v)) for v in lists), ','.join('{' + ','.join(v) + '}' for v in lists)))
    text.append('};')
    wide=[0]+[system[narrow(c)+1] for c in range(256)]
    calendars = calendar_data(locale)
    text.append('static const struct {unsigned id; const char *maximum;} OriginalCalendars[] = {')
    for number, maximum, eras in calendars:
        text.append('{%d,%s},' % (number, json.dumps(maximum)))
    text.append('};')
    text.append('static const struct {unsigned calendar,year,month,day; const char *name;} OriginalCalendarEras[] = {')
    for number, maximum, eras in calendars:
        for year, month, day, name in eras:
            encoded = '"' + ''.join('\\%03o' % narrow(ord(c)) for c in name) + '"'
            text.append('{%d,%d,%d,%d,%s},' % (number, year, month, day, encoded))
    text.append('};')
    text.append('static const struct {unsigned locale,standard,alternate;} OriginalLocaleCalendars[] = {')
    for i in range(word(locale, 0)):
        lcid, base = word(locale, 12 + i * 8), word(locale, 16 + i * 8) * 2
        standard = int(locale_text(locale, locale_field_offset(locale, base, 0x1009)))
        alternate = half(locale, base + 2 * half(locale, base + 0x3c))
        text.append('{%d,%d,%d},' % (lcid, standard, alternate))
    text.append('};')
    for name,values in [('OriginalCType',ctype),('OriginalSystemCType',system),('OriginalSystemWideType',wide)]:
        text.append(f'static const unsigned short {name}[] = {{')
        text.extend(','.join(str(x) for x in values[i:i+16])+',' for i in range(0,len(values),16))
        text.append('};')
    for name, index in [('OriginalUpper', 6), ('OriginalLower', 7)]:
        at=word(cp,index*4)*2
        text.append(f'static const unsigned char {name}[] = {{')
        text.extend(','.join(str(x) for x in cp[at+i:at+i+16])+',' for i in range(0,256,16))
        text.append('};')
    text.append('static const char *const OriginalCTime[] = {')
    for i in range(43):
        at=word(pe_bytes(crt,ctime_address+i*4,4),0)
        value=pe_bytes(crt,at,128).split(b'\0')[0].decode('ascii')
        text.append(json.dumps(value)+',')
    text.append('};')
    path=OUT/'crt_locale_data.h';contents='\n'.join(text)+'\n'
    if not path.exists() or path.read_text()!=contents:path.write_text(contents)
    report={'source_sha256':hashes,'locale':0x409,'locales':list(locales),'codepage':1252,'fields':len(fields),
            'calendar_count':len(calendars),'calendar_directory':word(locale,8)*2,
            'original_getlocaleinfo_body':'bff820c6','field_selection_join':'bff83641',
            'ctype1_codepage_header_index':15,'header_sha256':hashlib.sha256(contents.encode()).hexdigest(),
            'boundary':'Original defaults for CP1252 locales, without user registry overrides. Wide Latin1 ctype data uses original CP1252 reverse mapping, matching the Win98 CRT ANSI fallback. Other codepages and locale-specific formatting/CRT selection are not asserted.'}
    (OUT/'us-fields.json').write_text(json.dumps({f'{k:x}': {'text': v, 'file_offset': OFFSETS[k]} for k,v in fields.items()},indent=2)+'\n')
    (OUT/'assets.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
if __name__ == '__main__':print(json.dumps(generate()))
