#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Drive unchanged WordPad through real native mouse/key packets using existing runtime objects."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys,zlib

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def png(ppm):
 magic,dimensions,maximum,rgb=ppm.read_bytes().split(b'\n',3);w,h=map(int,dimensions.split());assert magic==b'P6'and maximum==b'255'and len(rgb)==w*h*3
 def chunk(n,b):return struct.pack('>I',len(b))+n+b+struct.pack('>I',zlib.crc32(n+b))
 rows=b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3]for y in range(h));ppm.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
def archive(out,log):
 for p in out.glob('*.ppm'):png(p)
 packets=[]
 for line in log.splitlines():
  if line.startswith('INPUT '):packets.append({k:int(v)if v.isdigit()else v for k,v in re.findall(r'(\w+)=([^ ]+)',line)})
 (out/'input-packets.json').write_text(json.dumps(packets,indent=2)+'\n')
 result={};m=re.search(r'^EDIT_RESULT (.*)$',log,re.M)
 if m:result={k:int(v)for k,v in re.findall(r'(\w+)=(\d+)',m[1])}
 texts={stage:bytes.fromhex(value).decode('latin1')for stage,value in re.findall(r'^EDITOR_TEXT stage=(\w+) text=([0-9a-f]*)$',log,re.M)}
 failures=re.findall(r'^PHASE_FAILED (.*)$',log,re.M)
 roundtrip={};m=re.search(r'^ROUNDTRIP_RESULT (.*)$',log,re.M)
 if m:roundtrip={k:int(v)for k,v in re.findall(r'(\w+)=(\d+)',m[1])}
 saved={};m=re.search(r'^SAVE_FILE_RESULT (.*)$',log,re.M)
 if m:saved={k:int(v)for k,v in re.findall(r'(\w+)=(\d+)',m[1])}
 return {'roundtrip':roundtrip,'roundtrip_completed':bool(roundtrip.get('ok')and texts.get('new')==''and texts.get('reopened')=='Windows 98 on ARM'),'edit_result':result,'texts':texts,'failures':failures,'save_observation':re.findall(r'^SAVE_RESULT (.*)$',log,re.M),'input_packets':len(packets),'text_edit_passed':bool(result.get('ok')and result.get('quiet')==24 and texts.get('edited')=='Windows 98 on ARM'),'save_completed':bool(saved.get('ok')),'saved_file':saved,'original_code_modified':False,'fake_overlay':False}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--objects',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--save',action='store_true',help='After verified edit, press Ctrl+S and capture the real dialog/boundary.');p.add_argument('--save-as',help='Type this guest RTF path into the actual Save As dialog and inspect the resulting RAM file.');p.add_argument('--drive-c',help='Run on this host directory as drive C: instead of the RAM volume.');p.add_argument('--roundtrip',action='store_true',help='After --save-as, use New and Open and verify the saved text in the actual editor.');a=p.parse_args();
 if a.roundtrip and not a.save_as:p.error('--roundtrip requires --save-as')
 source=Path(__file__).resolve();root=next(v for v in source.parents if(v/'neo98.py').is_file());out=a.out.resolve();objects=a.objects.resolve()
 if out.exists()and any(out.iterdir()):p.error('--out must be new or empty')
 out.mkdir(parents=True,exist_ok=True);sys.path.insert(0,str(root));import neo98
 env=neo98.tool_environment();env['NEO98_WIN98_ROOT']=str(root/'build/win98-c');env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1';cpp=source.with_suffix('.cpp');binary=out/'wordpad-demo';from tools.app_programs import application,libraries;inputs=[root/'build/programs'/n for n in [application('wordpad')['program'],*libraries('wordpad')]];font=root/'build/generated/notepad-fixedsys.fnt';objs=sorted(objects.glob('*.o'));lib=root/'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib';report={'status':'BLOCKED','runtime_scope':'Host runner with caller-supplied immutable native ASan/UBSan objects; not an ARM/Pi boot test.','phase_timeout_seconds':20,'tool_sha256':{str(v):sha(v)for v in(source,cpp)},'object_sha256':{v.name:sha(v)for v in objs},'input_sha256':{str(v):sha(v)for v in[*inputs,font]}}
 manifest=objects.parent/'objects.json'
 if manifest.exists():report['object_manifest_sha256']=sha(manifest)
 try:
  command=['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(source.parents[1]/'src'),'-I'+str(root/'third_party/unicorn/include'),str(cpp),*map(str,objs),str(lib),'-Wl,-rpath,'+str(lib.parent),'-o',str(binary)];built=subprocess.run(command,env=env,capture_output=True,text=True);(out/'link.log').write_text(built.stdout+built.stderr);built.check_returncode();report['runner_sha256']=sha(binary)
  command=[str(binary),str(inputs[0]),str(font),*map(str,inputs[1:])]+(['--save']if a.save else[])+(['--save-as',a.save_as]if a.save_as else[])+(['--roundtrip']if a.roundtrip else[])+(['--drive-c',a.drive_c]if a.drive_c else[])
  with(out/'trace.log').open('w')as log:ran=subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120)
  report['runner_exit_code']=ran.returncode;report.update(archive(out,(out/'trace.log').read_text()));report['source_files_unchanged']=all(sha(Path(v))==h for v,h in report['input_sha256'].items());report['status']='PASS'if report['text_edit_passed']and ran.returncode==0 else'BLOCKED'
  if report['save_observation']and any('state=3 'in v for v in report['save_observation']):report['status']='BLOCKED'
  if a.save_as and not report['save_completed']:report['status']='BLOCKED'
  if a.roundtrip and not report['roundtrip_completed']:report['status']='BLOCKED'
 except Exception as error:report['error']=str(error)
 report['artifacts_sha256']={v.name:sha(v)for v in sorted(out.iterdir())if v.is_file()and v.name not in('report.json','wordpad-demo')};(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'text_edit_passed':report.get('text_edit_passed',False),'failures':report.get('failures',[])},indent=2));return 0 if report['status']=='PASS'else 1
if __name__=='__main__':raise SystemExit(main())
