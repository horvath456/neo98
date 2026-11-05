# SPDX-License-Identifier: GPL-2.0-or-later
"""Real NE file-dialog hooks, owner Help, selection and native questions."""
import struct
from win16_fixture import build

MODES = ('init', 'init-template', 'hook', 'hook-veto', 'help', 'fileok', 'fileok-veto', 'select', 'double',
         'create-yes', 'create-no', 'overwrite-yes', 'overwrite-no', 'warning-invalid',
         'warning-missing', 'warning-path', 'warning-readonly', 'drive')

def fixture(mode):
    assert mode in MODES
    data, code, fixes, failures = bytearray(256), bytearray(), [], []
    def cell(size=2):
        at=len(data);data.extend(bytes(size));return at
    def text(value):
        at=len(data);data.extend(value.encode('cp1252')+b'\0');return at
    def word(value):code.extend(b'\x68'+struct.pack('<H',value&65535))
    def long(value):word(value>>16);word(value)
    def ptr(at):code.extend(b'\x1e');word(at)
    def call(module,ordinal):
        code.extend(b'\x9a\xff\xff\0\0');fixes.append(struct.pack('<BBHHH',3,1,len(code)-4,module,ordinal))
    def put(at,value):code.extend(b'\xc7\x06'+struct.pack('<HH',at,value&65535))
    def load(at):code.extend(b'\xa1'+struct.pack('<H',at))
    def segment(at,cs=False):code.extend((b'\x8c\xc8' if cs else b'\x8c\xd8')+b'\xa3'+struct.pack('<H',at))
    def procedure(at):
        put(at,0);patch=len(code)-2;segment(at+2,True);return patch
    def far(at,value):put(at,value);segment(at+2)
    def equal(value,label):
        code.extend(b'\x3d'+struct.pack('<H',value)+b'\x0f\x85\0\0');failures.append((len(code)-2,text('FAIL file '+label)))
    def branch(at):code[at:at+2]=struct.pack('<h',len(code)-at-2)
    def jump():code.extend(b'\xe9\0\0');return len(code)-2
    def box(at,end=False):
        word(0);ptr(at);ptr(at);word(0);call(2,1)
        if end:code.extend(b'\xeb\xfe')
    def inc(at):code.extend(b'\xff\x06'+struct.pack('<H',at))
    def post(hwnd,id,lp=0):
        code.extend(b'\xff\x76'+bytes([hwnd]));word(0x111);word(id);long(lp);call(2,110)
    visits,effects,hits,okmsg,selmsg,helpmsg,owner=(cell() for _ in range(7))
    ofn,file,title=cell(0x48),cell(128),cell(64)
    cwd_before,cwd_after=cell(260),cell(260)
    caption=text('File state '+mode);held=text('File pending '+mode);passed=text('PASS file '+mode)
    initial=text('C:\\WFILE');pattern=len(data);data.extend(b'Text\0*.TXT\0\0')
    name={'warning-invalid':'<bad','warning-missing':'missing.txt','warning-path':'C:\\NOFOLDER\\a.txt',
          'warning-readonly':'ro.txt'}.get(mode,'new.txt' if mode.startswith('create-') else 'a.txt')
    data[file:file+len(name)+1]=name.encode()+b'\0'
    code.extend(b'\x8c\xd8\xa3\x00\x00')
    for name,at in (('commdlg_FileNameOK',okmsg),('commdlg_LBSelChangedNotify',selmsg),('commdlg_help',helpmsg)):
        ptr(text(name));call(2,118);code.extend(b'\xa3'+struct.pack('<H',at))
    native_template=None
    if mode=='init-template':
        import win98_c
        original=(win98_c.use_pristine()/'WINDOWS/SYSTEM/COMMDLG.DLL').read_bytes()
        ne=struct.unpack_from('<I',original,0x3c)[0];at=ne+struct.unpack_from('<H',original,ne+0x24)[0]
        shift=struct.unpack_from('<H',original,at)[0];at+=2;template=None
        while True:
            kind,count=struct.unpack_from('<HH',original,at);at+=8
            if not kind:break
            for _ in range(count):
                offset,size,_,identity=struct.unpack_from('<HHHH',original,at);at+=12
                if kind==0x8005 and identity==0x8600:template=original[offset<<shift:(offset+size)<<shift]
        assert template
        native_template=cell();blob=len(data);data.extend(template)
        word(2);long(len(template));call(1,15);code.extend(b'\xa3'+struct.pack('<H',native_template))
        code.extend(b'\x50');call(1,18)
        code.extend(b'\x06\x8e\xc2\x89\xc7\xbe'+struct.pack('<H',blob)+b'\xb9'+struct.pack('<H',len(template))+b'\xfc\xf3\xa4\x07')
        code.extend(b'\xff\x36'+struct.pack('<H',native_template));call(1,19)
    wnd_patch=None
    if mode=='help':
        cls=cell(26);klass=text('FileOwner');wnd_patch=procedure(cls+2);segment(cls+10);far(cls+22,klass)
        ptr(cls);call(2,57);ptr(klass);ptr(klass);long(0)
        for value in (0,0,100,100,0,0):word(value)
        code.extend(b'\x1e');long(0);call(2,41);code.extend(b'\xa3'+struct.pack('<H',owner))
    put(ofn,0x48);far(ofn+8,pattern);put(ofn+0x14,1);far(ofn+0x18,file);put(ofn+0x1c,128)
    far(ofn+0x20,title);put(ofn+0x24,64);far(ofn+0x28,initial);far(ofn+0x2c,caption)
    flags=0x20|0x8|0x4
    if mode=='init-template':flags|=0x80
    if mode=='help':flags|=0x10
    if mode.startswith('create-'):flags|=0x2000
    if mode.startswith('overwrite-'):flags|=0x2
    if mode in ('warning-missing','warning-path'):flags|=0x1800
    if mode=='warning-readonly':flags|=0x8000
    put(ofn+0x30,flags);hook_patch=procedure(ofn+0x40)
    if native_template is not None:load(native_template);code.extend(b'\xa3'+struct.pack('<H',ofn+6))
    if mode=='help':load(owner);code.extend(b'\xa3'+struct.pack('<H',ofn+4))
    code.extend(b'\xbe'+struct.pack('<H',cwd_before)+b'\x31\xd2\xb8\x00\x47\xcd\x21')
    ptr(ofn);call(3,2 if mode.startswith('overwrite-') else 1)
    success=mode in ('fileok','double','create-yes','overwrite-yes')
    equal(int(success),'API result')
    code.extend(b'\xbe'+struct.pack('<H',cwd_after)+b'\x31\xd2\xb8\x00\x47\xcd\x21')
    code.extend(b'\x1e\x07\xbe'+struct.pack('<H',cwd_before)+b'\xbf'+struct.pack('<H',cwd_after)+b'\xb9\x04\x01\xfc\xf3\xa6\x0f\x85\0\0')
    failures.append((len(code)-2,text('FAIL file NOCHANGEDIR restored once')))
    load(visits);equal(1,'Init once')
    load(effects);equal(1 if mode in ('init','init-template','hook','hook-veto','help','fileok','fileok-veto','select','double') else 0,'effect once')
    if mode in ('hook','hook-veto'):
        load(hits);equal(2 if mode=='hook-veto' else 1,'Cancel hook count')
    if mode in ('fileok','fileok-veto','select','double'):
        load(hits);equal(1,'registered hook once')
    if success or mode=='fileok-veto':
        wanted=text('c:\\wfile\\'+('new.txt' if mode=='create-yes' else 'a.txt'))
        code.extend(b'\x1e\x07\xbe'+struct.pack('<H',file)+b'\xbf'+struct.pack('<H',wanted)+b'\xb9'+struct.pack('<H',len('c:\\wfile\\'+('new.txt' if mode=='create-yes' else 'a.txt'))+1)+b'\xfc\xf3\xa6\x0f\x85\0\0')
        failures.append((len(code)-2,text('FAIL file output bytes')))
        load(ofn+0x34);equal(9,'file offset')
    if native_template is not None:
        code.extend(b'\xff\x36'+struct.pack('<H',native_template));call(1,17);equal(0,'caller template released once')
    if mode=='help':code.extend(b'\xff\x36'+struct.pack('<H',owner));call(2,53)
    box(passed,True)
    hook=len(code);code[hook_patch:hook_patch+2]=struct.pack('<H',hook)
    code.extend(b'\x55\x89\xe5\x1e\x16\x1f\x81\x7e\x0c\x10\x01\x0f\x85\0\0');not_init=len(code)-2
    inc(visits)
    if mode in ('init','init-template'):box(held);inc(effects)
    if mode in ('select','double'):
        code.extend(b'\xff\x76\x0e');word(0x460);word(0x407);word(0);long(0);call(2,101)
        post(0x0e,0x460,(2 if mode=='double' else 1)<<16)
    elif mode=='drive':
        code.extend(b'\xff\x76\x0e');word(0x471);word(0x403);word(0);ptr(text('z:'));call(2,101)
        code.extend(b'\x89\xc3\xff\x76\x0e');word(0x471);word(0x40e);code.extend(b'\x53');long(0);call(2,101)
        post(0x0e,0x471,1<<16)
    elif mode not in ('init','init-template'):post(0x0e,0x40e if mode=='help' else 2 if mode in ('hook','hook-veto') else 1)
    code.extend(b'\xb8\x01\x00');init_out=jump();branch(not_init)
    if mode in ('hook','hook-veto'):
        code.extend(b'\x81\x7e\x0c\x11\x01\x0f\x85\0\0');not_command=len(code)-2
        code.extend(b'\x83\x7e\x0a\x02\x0f\x85\0\0');not_cancel=len(code)-2
        inc(hits);load(hits);code.extend(b'\x3d\x01\0\x0f\x85\0\0');second=len(code)-2
        box(held);inc(effects)
        code.extend(b'\xb8'+struct.pack('<H',int(mode=='hook-veto')));cancel_out=jump()
        branch(second);branch(not_command);branch(not_cancel);code.extend(b'\x31\xc0');branch(cancel_out)
    elif mode in ('fileok','fileok-veto','select','double'):
        code.extend(b'\x8b\x46\x0c\x3b\x06'+struct.pack('<H',okmsg if mode.startswith('fileok') else selmsg)+b'\x0f\x85\0\0');not_target=len(code)-2
        inc(hits);box(held);inc(effects);code.extend(b'\xb8'+struct.pack('<H',int(mode=='fileok-veto')));target_out=jump()
        branch(not_target);code.extend(b'\x31\xc0');branch(target_out)
    else:code.extend(b'\x31\xc0')
    branch(init_out);code.extend(b'\x1f\x5d\xca\x0a\x00')
    if wnd_patch is not None:
        code[wnd_patch:wnd_patch+2]=struct.pack('<H',len(code))
        code.extend(b'\x55\x89\xe5\x1e\x16\x1f\x8b\x46\x0c\x3b\x06'+struct.pack('<H',helpmsg)+b'\x0f\x85\0\0');not_help=len(code)-2
        box(held);inc(effects);code.extend(b'\x31\xc0\x31\xd2');owner_out=jump();branch(not_help)
        code.extend(b'\x31\xc0\x31\xd2\x81\x7e\x0c\x81\0\x75\x03\xb8\x01\0');branch(owner_out)
        code.extend(b'\x1f\x5d\xca\x0a\x00')
    for at,label in failures:
        branch(at);code.extend(b'\x36\xa1\x00\x00\x8e\xd8');box(label,True)
    image=bytearray(build(lambda:(bytes(code),struct.pack('<H',len(fixes))+b''.join(fixes)),bytes(data),name='FILESTATE',modules=('KERNEL','USER','COMMDLG')))
    image[0x52:0x54]=struct.pack('<H',0x3000)
    return bytes(image)
