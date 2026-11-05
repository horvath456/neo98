#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Extract the local Win98 kernel's computer-name byte conversion tables."""
from pathlib import Path
import hashlib,json,argparse
ROOT=Path(__file__).resolve().parents[1]
def generate(root=ROOT,original=None):
 import pefile
 root=Path(root);original=Path(original)if original else root/'build/programs/KERNEL32.DLL';data=original.read_bytes();sha=hashlib.sha256(data).hexdigest()
 if sha!='9c8d574f84bfbbb6521dac54d0bfaf7a4c8fee07e468525cdb9da7db418e6c7e':raise RuntimeError('Unexpected original KERNEL32 for computer-name tables')
 pe=pefile.PE(data=data);tables=[('OriginalComputerAnsiHigh',0xbffca480,128),('OriginalComputerOemBytes',0xbffca500,160)];parts=['// Local Win98 original conversion bytes; generated, do not redistribute.'];records=[]
 for name,address,count in tables:
  raw=pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase,count);assert len(raw)==count
  parts.append('static const unsigned char '+name+'[] = {\n'+','.join('0x%02x'%b for b in raw)+'\n};')
  records.append({'name':name,'address':hex(address),'bytes':count,'sha256':hashlib.sha256(raw).hexdigest()})
 out=root/'build/generated';out.mkdir(parents=True,exist_ok=True);header=out/'computer_name_tables.h';text='\n'.join(parts)+'\n'
 if not header.exists()or header.read_text()!=text:header.write_text(text)
 report={'source_sha256':sha,'tables':records,'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest()};(out/'computer-name-provenance.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,default=ROOT);parser.add_argument('--original',type=Path);a=parser.parse_args();print(json.dumps(generate(a.root,a.original),indent=2))
