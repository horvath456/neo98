#!/usr/bin/env python3
"""Extract the RebarBandSite class's installation rows from original BROWSEUI REGINST."""
from pathlib import Path
import argparse,csv,hashlib,json,re
import pefile
SHA='598c799e974e39fc27437554b66d69cb3619126ddc8ae9eaaca5c6e4b2d4eced'
def extract(path):
 raw=path.read_bytes();digest=hashlib.sha256(raw).hexdigest()
 if SHA:assert digest==SHA,'Unexpected BROWSEUI version'
 pe=pefile.PE(data=raw);resources=[]
 for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
  for n in t.directory.entries:
   for l in n.directory.entries:
    if str(t.name)=='REGINST'and str(n.name)=='REGINST'and l.id==1033:resources.append(pe.get_data(l.data.struct.OffsetToData,l.data.struct.Size))
 assert len(resources)==1;text=resources[0].decode('cp1252');strings={}
 for line in text.split('[Strings]',1)[1].splitlines():
  if '='in line:
   k,v=line.split('=',1);strings[k.strip().upper()]=v.strip().strip('"')
 assert strings['CLSID_REBARBANDSITE']=='{ECD4FC4D-521C-11D0-B792-00A0C90312E1}'
 strings['_SYS_MOD_PATH']='C:\\WINDOWS\\SYSTEM\\BROWSEUI.DLL'
 def expand(s):return re.sub(r'%([^%]+)%',lambda m:strings[m[1].upper()],s)
 rows=[]
 for line in text.splitlines():
  if line.startswith(('HKCR,','HKLM,'))and '%CLSID_RebarBandSite%'in line:
   fields=next(csv.reader([line]));root,key,name,flags,value=map(expand,fields);assert flags in ('','0x00020000')
   rows.append({'root':0 if root=='HKCR'else 2,'key':key,'name':name,'type':2 if flags else 1,'value':value})
 assert len(rows)==4
 return {'source_sha256':digest,'resource_sha256':hashlib.sha256(resources[0]).hexdigest(),'resource':'REGINST/REGINST/1033','module_path_substitution':strings['_SYS_MOD_PATH'],'rows':rows}
def header(report):
 q=lambda s:json.dumps(s,ensure_ascii=True)
 s='// SPDX-License-Identifier: GPL-2.0-or-later\n// Generated from original BROWSEUI REGINST by import_browseui_registration.py.\n#pragma once\nnamespace Win98BrowseUiRegistration {\nstruct Value { unsigned root;const char*key;const char*name;unsigned type;const char*value; };\ninline constexpr Value Values[]={\n'
 for v in report['rows']:s+=' {'+','.join([str(v['root']),q(v['key']),q(v['name']),str(v['type']),q(v['value'])])+'},\n'
 return s+'''};
// Native installation provider for these four original data rows.
template<class Store> unsigned Install(Store&store){
 for(const auto&v:Values){unsigned key=0;bool created=false;unsigned error=store.Open(store.roots[v.root],v.key,true,key,created);if(error)return error;unsigned bytes=0;while(v.value[bytes])++bytes;error=store.Set(key,v.name,v.type,v.value,bytes+1);if(error)return error;}return 0;
}
}
'''
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dll',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();report=extract(a.dll);a.out.mkdir(parents=True,exist_ok=True);(a.out/'browseui-registration.json').write_text(json.dumps(report,indent=2)+'\n');(a.out/'win98_browseui_registration.h').write_text(header(report));print('Extracted 4 original RebarBandSite installation rows.')
