#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inventory and build external test families without hiding unavailable cases."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from collections import Counter

import wine_conformance as wine

PRIORITY = ['msvcrt', 'kernel32', 'user32', 'gdi32', 'comctl32', 'advapi32',
            'ole32', 'oleaut32', 'shell32', 'shlwapi', 'comdlg32', 'version',
            'ws2_32', 'wininet', 'winmm', 'ddraw', 'dsound']


def make_variables(path):
    text = path.read_text().replace('\\\n', ' ')
    return dict(re.findall(r'^(\w+)\s*=\s*([^\n]*)', text, re.M))


def inventory(source):
    groups = []
    for makefile in sorted(source.glob('dlls/*/tests/Makefile.in')):
        values = make_variables(makefile)
        family = makefile.parent.parent.name
        for filename in values.get('CTESTS', '').split():
            path = makefile.parent / filename
            groups.append({'name': family + '_' + path.stem, 'family': family,
                'source': str(path.relative_to(source)), 'source_sha256': wine.digest(path),
                'imports': values.get('IMPORTS', '').split(),
                'resources': values.get('RC_SRCS', '').split(),
                'other_sources': values.get('C_SRCS', '').split(),
                'makefile': str(makefile.relative_to(source))})
    groups.sort(key=lambda g: (PRIORITY.index(g['family']) if g['family'] in PRIORITY else len(PRIORITY), g['family'], g['name']))
    for i, group in enumerate(groups):
        group['exe'] = 'W%06d.EXE' % i
    return groups


def assessment(group):
    if Path(group.get('source', '')).name == 'generated.c':
        return 'ABI_ONLY'
    return group.get('assessment', 'ASSERTIONS')


def prepare_seh_object(path):
    #.sxdata is SafeSEH linker metadata, not the executable scope tables in
    #.xdata. GNU ld retains it at instead of consuming it, which
    # Win98 rejects. Win98 predates SafeSEH; retain code and.xdata unchanged.
    subprocess.run(['i686-w64-mingw32-objcopy', '--remove-section=.sxdata', str(path)],
                   check=True, capture_output=True)


def legacy_sdk(source, generated, group):
    """Recover declarations/import tables from the pinned SDK, never DLL code."""
    name = group['name']
    guids = {'browseui_autocomplete': [('include/shlguid.h', symbol) for symbol in
                                    ('IID_IACList', 'IID_IACList2', 'IID_IObjMgr', 'CLSID_ACLMulti')],
        'mshtml_dom': [('include/mshtmhst.idl', 'CGID_MSHTML')],
        'mshtml_htmldoc': [('dlls/uuid/uuid.c', 'IID_IProxyManager'), ('include/mshtmhst.idl', 'CGID_MSHTML')],
        'mshtml_script': [('dlls/uuid/uuid.c', 'CLSID_IdentityUnmarshal')],
        'urlmon_url': [('dlls/uuid/uuid.c', 'CLSID_IdentityUnmarshal'), ('dlls/uuid/uuid.c', 'IID_IBindStatusCallbackHolder'),
                       ('include/urlmon.idl', 'IID_IAsyncBindCtx')]}.get(name, [])
    options, extras, notes = [], [], []
    if name == 'ntdll_generated':
        donor = (source/'include/winnt.h').read_text()
        declaration = re.search(r'typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY\s*\{[^}]+\}[^;]+;', donor)[0]
        header = generated/'load-config.h'
        symbols = ('IMAGE_LOAD_CONFIG_DIRECTORY', 'PIMAGE_LOAD_CONFIG_DIRECTORY')
        header.write_text(''.join('#define '+s+' SDK'+s+'\n' for s in symbols)+
                          '#include <windows.h>\n'+
                          ''.join('#undef '+s+'\n' for s in symbols)+declaration+'\n')
        options += ['-include', str(header)]
        notes.append('Legacy IMAGE_LOAD_CONFIG_DIRECTORY declaration copied from pinned winnt.h; ABI-only test')
    if name in ('rpcrt4_ndr_marshall', 'rpcrt4_generated'):
        donor = (source/'include/rpcndr.h').read_text()
        match = re.search(r'typedef struct _MIDL_STUB_MESSAGE\s*\{.*?\}\s*MIDL_STUB_MESSAGE[^;]*;', donor, re.S)
        if not match:
            raise RuntimeError('Missing pinned MIDL_STUB_MESSAGE')
        header = generated/(name+'-stub-message.h')
        symbols = ('_MIDL_STUB_MESSAGE', 'MIDL_STUB_MESSAGE', 'PMIDL_STUB_MESSAGE')
        header.write_text(''.join('#define '+s+' SDK'+s+'\n' for s in symbols)+
                          '#include <windows.h>\n#include <rpc.h>\n#include <rpcndr.h>\n'+
                          ''.join('#undef '+s+'\n' for s in symbols)+
                          '#include <pshpack4.h>\n'+match[0]+'\n#include <poppack.h>\n')
        options += ['-U_WIN32_WINNT', '-D_WIN32_WINNT=0x0500', '-UNTDDI_VERSION',
                    '-DNTDDI_VERSION=0x05000000', '-include', str(header)]
        notes.append('MIDL_STUB_MESSAGE and packing copied from pinned rpcndr.h; Win2000 declarations match upstream NTDDI_VERSION')
    if name == 'ntdll_exception':
        header = generated/'exception-layout.h'
        header.write_text('#include <stddef.h>\n#include <windows.h>\n#include <winternl.h>\n'
                          'typedef char check_teb_peb[offsetof(TEB, ProcessEnvironmentBlock) == 0x30 ? 1 : -1];\n'
                          'typedef char check_tib_seh[offsetof(NT_TIB, ExceptionList) == 0 ? 1 : -1];\n'
                          'typedef char check_peb_image[offsetof(PEB, Reserved3) + sizeof(void *) == 8 ? 1 : -1];\n'
                          'typedef char check_seh_prev[offsetof(EXCEPTION_REGISTRATION_RECORD, prev) == 0 ? 1 : -1];\n')
        options += ['-include', str(header)]
        notes.append('Compile-time x86 offset checks for adapted TEB/PEB/SEH member spellings')
    nt_types = {
        'ntdll_atom': ('ATOM_INFORMATION_CLASS', 'ATOM_BASIC_INFORMATION'),
        'ntdll_file': ('IO_COMPLETION_INFORMATION_CLASS', 'FILE_COMPLETION_INFORMATION'),
        'ntdll_om': ('TIMER_TYPE',),
        'ntdll_info': ('SYSTEM_THREAD_INFORMATION', 'SYSTEM_CPU_INFORMATION', 'SYSTEM_MODULE',
                       'SYSTEM_MODULE_INFORMATION', 'SYSTEM_CACHE_INFORMATION',
                       'SYSTEM_KERNEL_DEBUGGER_INFORMATION'),
    }.get(name)
    if nt_types:
        donor = (source/'include/winternl.h').read_text()
        declarations = []
        for symbol in nt_types:
            match = re.search(r'typedef (?:struct|enum) _'+symbol+r'\s*\{[^}]+\}\s*'+symbol+r'[^;]*;', donor)
            if not match:
                raise RuntimeError('Missing pinned NT type: '+symbol)
            declarations.append(match[0])
        if name == 'ntdll_om':
            declarations.append(re.search(r'^#define FILE_PIPE_FULL_DUPLEX[^\n]+', donor, re.M)[0])
        before, after = '', ''
        if name == 'ntdll_info':
            for symbol in ('_SYSTEM_THREAD_INFORMATION', 'SYSTEM_THREAD_INFORMATION', 'PSYSTEM_THREAD_INFORMATION'):
                before += '#define '+symbol+' SDK'+symbol+'\n'
                after += '#undef '+symbol+'\n'
            after += re.search(r'^#define MAXIMUM_FILENAME_LENGTH[^\n]+', donor, re.M)[0]+'\n'
            for symbol in ('SystemCpuInformation', 'SystemModuleInformation', 'SystemCacheInformation',
                           'SystemKernelDebuggerInformation'):
                value = re.search(r'\b'+symbol+r'\s*=\s*(\d+)', donor)[1]
                after += '#define '+symbol+' '+value+'\n'
        header = generated/(name+'-types.h')
        header.write_text(before+'#include <windows.h>\n#include <winternl.h>\n'+after+'\n'.join(declarations)+'\n')
        options += ['-include', str(header)]
        notes.append('Missing NT declarations copied from pinned winternl.h: '+', '.join(nt_types))
    if name == 'ntdll_port':
        header = generated/'port-types.h'
        symbols = ('_CLIENT_ID', 'CLIENT_ID', 'PCLIENT_ID')
        header.write_text(''.join('#define '+s+' SDK'+s+'\n' for s in symbols)+
                          '#include <windows.h>\n#include <winternl.h>\n'+
                          ''.join('#undef '+s+'\n' for s in symbols))
        options += ['-include', str(header)]
        notes.append('Rename modern SDK CLIENT_ID to preserve the original test-owned declaration')
    if name == 'ole32_usrmarshal':
        donor = (source/'dlls/ole32/usrmarshal.c').read_text()
        declarations = re.findall(
            r'^(?:ULONG|unsigned char \*|void) __RPC_USER (?:HENHMETAFILE|HMETAFILEPICT)_User\w+\([^)]*\)',
            donor, re.M)
        if len(declarations) != 8:
            raise RuntimeError('Expected eight pinned OLE marshal declarations')
        header = generated/'marshal-declarations.h'
        header.write_text('#define COBJMACROS\n#define CONST_VTABLE\n#include <objbase.h>\n#include <objidl.h>\n'+
                          ';\n'.join(declarations)+';\n')
        options += ['-include', str(header)]
        notes.append('Eight missing marshal declarations copied from pinned ole32/usrmarshal.c; no implementation')
    if name == 'browseui_autocomplete':
        donor = (source/'include/shlobj.h').read_text()
        declarations = []
        for interface in ('IObjMgr', 'IACList'):
            match = re.search(r'#define INTERFACE '+interface+r'\n.*?#undef INTERFACE', donor, re.S)
            if not match:
                raise RuntimeError('Missing pinned interface: '+interface)
            declarations.append(match[0])
        header = generated/'autocomplete-interfaces.h'
        header.write_text('#define IObjMgr SDK_IObjMgr\n#define IObjMgrVtbl SDK_IObjMgrVtbl\n'
                          '#define IACList SDK_IACList\n#define IACListVtbl SDK_IACListVtbl\n'
                          '#include <windows.h>\n#include <shlobj.h>\n'
                          '#undef IObjMgr\n#undef IObjMgrVtbl\n#undef IACList\n#undef IACListVtbl\n#undef INTERFACE\n'+
                          '\n'.join(declarations)+'\n'+
                          'extern const IID IID_IACList, IID_IACList2, IID_IObjMgr;\n'
                          'extern const CLSID CLSID_ACLMulti;\n')
        options += ['-include', str(header)]
        notes.append('IObjMgr and IACList C vtables copied from pinned shlobj.h, including IUnknown slots')
    if guids:
        definitions = []
        for path, symbol in guids:
            matches = re.findall(r'DEFINE_GUID\(\s*'+symbol+r'\s*,[^;]+;', (source/path).read_text())
            if len(matches) != 1: raise RuntimeError('Ambiguous pinned GUID: '+symbol)
            definitions.append(matches[0])
        header = generated/(name+('-guids.c' if name == 'browseui_autocomplete' else '-guids.h'))
        header.write_text('#include <initguid.h>\n#include <guiddef.h>\n'+'\n'.join(definitions)+'\n')
        if name == 'browseui_autocomplete':
            extras.append(str(header))
        else:
            options += ['-include', str(header)]
        notes.append('GUID declarations copied from pinned SDK: '+', '.join(s for _, s in guids))
    if name in ('mlang_mlang', 'shdocvw_webbrowser'):
        idl = 'mlang' if name == 'mlang_mlang' else 'htiframe'
        target = generated/(idl+'_i.c')
        subprocess.run(['i686-w64-mingw32-widl', '-I'+str(source/'include'), '-u', '-o', str(target),
            str(source/'include'/(idl+'.idl'))], check=True, capture_output=True)
        extras.append(str(target)); notes.append('GUID object generated from pinned '+idl+'.idl')
    if name in ('jscript_jscript', 'mshtml_script'):
        options += ['-DIActiveScriptParse_Release=IActiveScriptParse32_Release',
                    '-DIActiveScriptParseVtbl=IActiveScriptParse32Vtbl',
                    '-DIActiveScriptParseProcedure2Vtbl=IActiveScriptParseProcedure2_32Vtbl']
        notes.append('Historical unqualified ActiveScript names mapped to explicit 32-bit SDK names')
    if name in ('msxml3_domdoc', 'msxml3_schema'):
        options += ['-DNEO98_SDK_EXTERN_GUIDS']
        notes.append('Use SDK extern GUID declarations; original test defines its own GUIDs')
    if name == 'msxml3_schema':
        donor = (source/'dlls/msxml3/tests/domdoc.c').read_text()
        target = generated/'schema_guids.c'
        definitions = [re.search(r'const (?:CLSID|IID) '+symbol+r'\s*=\s*[^;]+;', donor)[0]
                       for symbol in ('CLSID_DOMDocument2', 'IID_IXMLDOMDocument2')]
        target.write_text('#include <guiddef.h>\n'+'\n'.join(definitions)+'\n')
        extras.append(str(target)); notes.append('Missing GUID definitions copied from pinned domdoc.c')
    if name == 'mapi32_prop':
        options += ['-DUSES_IID_IMAPIPropData']
        notes.append('Enable the SDK IID_IMAPIPropData declaration')
        macros = [line.rstrip() for line in (source/'include/mapiutil.h').read_text().splitlines()
                  if line.startswith('#define IPropData_')]
        header = generated/'mapi_calls.h'; header.write_text('\n'.join(macros)+'\n')
        options += ['-include', str(header)]
        notes.append('IPropData C vtable call macros copied from pinned mapiutil.h')
    if name == 'winspool.drv_info':
        options += ['-U_WIN32_WINNT', '-D_WIN32_WINNT=0x0600', '-UWINVER', '-DWINVER=0x0600']
        notes.append('Expose SDK FORM_INFO_2 declarations; runtime availability remains tested upstream')
    if name in ('dnsapi_name', 'dnsapi_record'):
        symbol = 'DnsValidateName_A' if name == 'dnsapi_name' else 'DnsRecordSetDetach'
        declaration = re.search(r'^.*WINAPI '+symbol+r'\([^;]+;', (source/'include/windns.h').read_text(), re.M)[0]
        header = generated/(name+'-declaration.h')
        header.write_text('#include <windows.h>\n#define '+symbol+' SDK_'+symbol+'\n#include <windns.h>\n#undef '+symbol+'\n'+declaration+'\n')
        options += ['-include', str(header)]
        notes.append('Use pinned WINAPI calling convention for '+symbol+' instead of modern header cdecl declaration')
    if group['family'] in ('hlink', 'd3dx8', 'inetcomm'):
        family = group['family']
        spec = source/'dlls'/family/(family+'.spec')
        exports = []
        for symbol, arguments in re.findall(r'^\S+\s+stdcall\s+(\w+)\(([^)]*)\)', spec.read_text(), re.M):
            widths = {'double': 8, 'int64': 8, 'ptr': 4, 'wstr': 4, 'str': 4, 'long': 4, 'float': 4}
            size = sum(widths[a] for a in arguments.split())
            exports.append(symbol+'@'+str(size))
        definition = generated/(family+'.def')
        definition.write_text('LIBRARY '+family+'.dll\nEXPORTS\n'+'\n'.join(exports)+'\n')
        subprocess.run(['i686-w64-mingw32-dlltool', '-k', '-d', str(definition), '-l', str(generated/('lib'+family+'.a'))],
                       check=True, capture_output=True)
        options += ['-L'+str(generated)]
        notes.append('Import library generated from pinned '+str(spec.relative_to(source))+'; no DLL implementation')
    if notes: group['sdk_adaptations'] = notes
    return options, extras


def supervisor(out, groups, cc, flags, assets=None):
    inc = out / 'include'; inc.mkdir(exist_ok=True)
    (inc / 'suite.h').write_text('#define CAMPAIGN_ARGUMENTS 1\n' +
        'static const struct { const char *name, *exe; unsigned long timeout_ms; const char *arguments; } suite[] = {\n' +
        ''.join(' {"%s", "%s", %d, %s},\n' % (g['name'], g['exe'], g['timeout_ms'], json.dumps(g.get('arguments', ''))) for g in groups) + ' {0,0,0,0}\n};\n' +
        '#define CAMPAIGN_INPUT_FILES 1\nstatic const char *input_files[] = {' + ','.join(json.dumps(name) for name in assets or []) + (',' if assets else '') + '0};\n')
    command = [cc, *[f for f in flags if not f.startswith('-I')], '-UWIN32_NO_STATUS', '-I'+str(inc),
        str(wine.SUPPORT/'supervisor.c'), '-o', str(out/'bin/SUITE.EXE'),
        '-Wl,--start-group', '-lmsvcrt-os', '-lkernel32',
        *(['-luser32'] if any(f in flags for f in ('-DCONFORMANCE_SHELL_FIXTURE', '-DCONFORMANCE_TOPMOST_FIXTURE')) else []),
        *(['-ladvapi32'] if any(f in flags for f in ('-DCONFORMANCE_LOGON_FIXTURE', '-DCONFORMANCE_BROWSER_FIXTURE')) else []),
        '-lmingwex', '-lgcc', '-Wl,--end-group']
    subprocess.run(command, check=True, capture_output=True)
    return wine.digest(out/'bin/SUITE.EXE')


def instrument_message_sequences(source):
    start = source.index('static void ok_sequence_(')
    end = source.index('\n}\n', start) + 2
    body = source[start:end]
    begin = '    static const struct message end_of_sequence ='
    if body.count(begin) != 1 or body.count('flush_sequence();') != 2:
        raise RuntimeError('Pinned Wine message sequence body changed')
    def marker(kind):
        return ('{ DWORD neo98_saved_error = GetLastError(); fprintf(stdout, '
                '"NEO98_SEQUENCE_'+kind+' %s:%d %s\\n", file, line, context); '
                'SetLastError(neo98_saved_error); } ')
    # No added newlines: the original source-site numbers remain valid.
    body = body.replace(begin, '    '+marker('BEGIN')+begin.lstrip())
    body = body.replace('flush_sequence();', marker('END')+'flush_sequence();')
    return source[:start]+body+source[end:]


def build_wine(args):
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    # Reuse the verified archive and exactly the same instrumented Wine harness.
    archive = out/'downloads/wine-1.0.tar.gz'
    if not archive.exists() and (wine.DEFAULT_OUT/'downloads/wine-1.0.tar.gz').exists():
        wine.clone(wine.DEFAULT_OUT/'downloads/wine-1.0.tar.gz', archive)
    if not args.retry_failed:
        wine.build(argparse.Namespace(out=out, groups=[], timeout=args.timeout, cc=args.cc))
    else:
        # Cleanup may remove extracted sources. Always reconstruct them from
        # the pinned archive, so a retry cannot consume edited donor headers.
        wine.source_tree(out)
    manifest = json.loads((out/'manifest.json').read_text())
    source = out/'source'
    groups = json.loads((out/'catalog.json').read_text())['groups'] if args.retry_failed else inventory(source)
    rebuild = set(filter(None, getattr(args, 'rebuild', '').split(',')))
    if rebuild - {g['name'] for g in groups}:
        raise RuntimeError('Unknown rebuild groups: '+', '.join(sorted(rebuild - {g['name'] for g in groups})))
    flags = manifest['flags'] + ['-Wno-implicit-function-declaration', '-Wno-incompatible-pointer-types',
        '-Wno-int-conversion', '-Wno-implicit-int', '-Wno-int-to-pointer-cast',
        '-D_WIN32_WINNT=0x0501', '-DWINVER=0x0501']
    flags = list(dict.fromkeys(flags))
    manifest['flags'] = flags
    manifest['support_sha256'] = {p.name: wine.digest(p) for p in wine.SUPPORT.glob('*') if p.is_file()}
    logs = out/'build-logs'; logs.mkdir(exist_ok=True)
    for g in groups:
        if args.retry_failed and g.get('build_status') == 'BUILT' and g['name'] not in rebuild:
            continue
        g['timeout_ms'] = args.timeout*1000
        src = source/g['source']; folder = src.parent
        values = make_variables(folder/'Makefile.in')
        imports = values.get('IMPORTS', '').split()
        extras = []; errors = []
        generated = logs / g['family']; generated.mkdir(exist_ok=True)
        sdk_options, sdk_objects = legacy_sdk(source, generated, g)
        extras += sdk_objects
        if g['name'] == 'rpcrt4_server':
            from build_legacy_widl import build as build_legacy_widl
            widl = build_legacy_widl(archive, out/'build-tools/widl-1.0')
            g['rpc_widl_sha256'] = wine.digest(widl)
            # The upstream makefile builds both sides from server.idl and
            # prefixes server entry points, allowing one EXE to host both.
            for mode, output_option, suffix in (('-h', '-H', '.h'), ('-c', '-C', '_c.c'), ('-s', '-S', '_s.c')):
                target = generated/('server'+suffix)
                command = [str(widl), '-I'+str(source/'include'), '-I'+str(folder),
                           *values.get('EXTRAIDLFLAGS', '').split(), mode, output_option, str(target),
                           str(folder/'server.idl')]
                r = subprocess.run(command, capture_output=True, text=True)
                (generated/('server'+suffix+'.log')).write_text(r.stdout+r.stderr)
                if r.returncode:
                    errors.append('RPC IDL: server'+suffix)
                elif suffix.endswith('.c'):
                    extras.append(str(target))
            g.setdefault('sdk_adaptations', []).append('Client/server RPC stubs generated by pinned Wine 1.0 WIDL from server.idl with upstream EXTRAIDLFLAGS')
        for filename in {'inetcomm': ['mimeole.idl'], 'msxml3': ['xmldom.idl']}.get(g['family'], []):
            command = ['i686-w64-mingw32-widl', '-I'+str(source/'include'), '-h', '-o',
                       str(generated/(Path(filename).stem+'.h')), str(source/'include'/filename)]
            r = subprocess.run(command, capture_output=True, text=True)
            (generated/(filename+'.log')).write_text(r.stdout+r.stderr)
            if r.returncode: errors.append('SDK IDL: '+filename)
        if g['family'] == 'msxml3':
            (generated/'xmldom.h').write_text('#include <msxml.h>\n')
            g.setdefault('sdk_adaptations', []).append('Historical xmldom.h resolved by modern SDK msxml.h to avoid duplicate DOM enums')
        if g['family'] == 'oleaut32':
            command = ['i686-w64-mingw32-widl', '-I'+str(source/'include'), '-t', '-o',
                       str(generated/'stdole2.tlb'), str(source/'include/stdole2.idl')]
            r = subprocess.run(command, capture_output=True, text=True)
            (generated/'stdole2.log').write_text(r.stdout+r.stderr)
            if r.returncode: errors.append('IDL: stdole2.idl')
        for key, options, suffix in [('IDL_I_SRCS', ['-h'], '.h'), ('IDL_TLB_SRCS', ['-t'], '.tlb')]:
            for filename in values.get(key, '').split():
                target = generated / (Path(filename).stem + suffix)
                command = ['i686-w64-mingw32-widl', '-I'+str(folder), '-I'+str(source/'include'), '-L'+str(generated), *options, '-o', str(target), str(folder/filename)]
                r = subprocess.run(command, capture_output=True, text=True)
                (generated/(filename+suffix+'.log')).write_text(r.stdout+r.stderr)
                if r.returncode: errors.append('IDL: '+filename)
        for resource in g['resources']:
            obj = logs/(g['family']+'-'+resource+'.o')
            if not obj.exists():
                r = subprocess.run(['i686-w64-mingw32-windres', '-I'+str(generated), '-I'+str(folder), '-I'+str(source/'include'),
                    str(folder/resource), '-O', 'coff', '-o', str(obj)], capture_output=True, text=True)
                (logs/(g['family']+'-'+resource+'.log')).write_text(r.stdout+r.stderr)
                if r.returncode: errors.append('resource: '+resource)
            if obj.exists(): extras.append(str(obj))
        helpers = {'user32_win': ['msg.c'], 'shell32_shlexec': ['shelllink.c']}.get(g['name'], [])
        helpers += {'comctl32': ['msg.c'], 'dsound': ['capture.c', 'ds3d.c', 'ds3d8.c', 'dsound8.c'],
                    'dinput': ['keyboard.c'], 'winmm': ['wave.c']}.get(g['family'], [])
        for helper in helpers:
            if helper == src.name: continue
            obj = generated/(helper+'.o')
            if not obj.exists():
                command = [args.cc, *flags, '-ffunction-sections', '-I'+str(folder), '-idirafter', str(source/'include'),
                           '-c', str(folder/helper), '-o', str(obj)]
                r = subprocess.run(command, capture_output=True, text=True)
                (generated/(helper+'.log')).write_text(r.stdout+r.stderr)
                if r.returncode: errors.append('helper: '+helper)
            if obj.exists(): extras.append(str(obj))
        libraries = list(dict.fromkeys(['msvcrt-os', *imports, 'kernel32','user32','gdi32','advapi32','uuid','ksguid','mingw32','mingwex','gcc']))
        libraries = [x for x in libraries if x != 'wine']
        aliases = {'kernel32_generated': ['-DReserved1=SystemStatusFlag'], 'gdiplus_pen': ['-DStatus=GpStatus']}.get(g['name'], [])
        if g['name'] == 'user32_msg' and getattr(args, 'message_sequences', False):
            patched = generated/'msg-sequences.c'
            patched.write_text('#line 1 "msg.c"\n'+instrument_message_sequences(src.read_text()))
            g['sequence_instrumentation'] = 'BEGIN/END per ok_sequence_ invocation; original assertions and line numbers unchanged'
            g['translation_unit_sha256'] = wine.digest(patched)
            src = patched
        if g['name'] == 'winmm_capture':
            data = src.read_text()
            adjusted = data.replace('extern GUID KSDATAFORMAT_', 'extern const GUID KSDATAFORMAT_')
            patched = generated/'capture.c'
            patched.write_text('#line 1 "capture.c"\n'+adjusted)
            g['sdk_adaptation'] = 'Two extern GUID declarations made const to match modern mmreg.h; test body unchanged'
            g['translation_unit_sha256'] = wine.digest(patched)
            src = patched
        if g['name'] == 'inetcomm_mimeole':
            patched = generated/'mimeole.c'
            patched.write_text('#line 1 "mimeole.c"\n'+src.read_text().replace('prop.u.pszVal', 'prop.pszVal'))
            g.setdefault('sdk_adaptations', []).append('PROPVARIANT anonymous-union member spelling adapted; comparison unchanged')
            g['translation_unit_sha256'] = wine.digest(patched)
            src = patched
        if g['name'] == 'urlmon_url':
            patched = generated/'url.c'
            patched.write_text('#line 1 "url.c"\n'+src.read_text().replace('extern IID IID_IBindStatusCallbackHolder;',
                'extern const IID IID_IBindStatusCallbackHolder;'))
            g.setdefault('sdk_adaptations', []).append('External GUID declaration made const to match SDK; test body unchanged')
            g['translation_unit_sha256'] = wine.digest(patched)
            src = patched
        if g['name'] == 'ntdll_exception':
            patched = generated/'exception.c'
            adjusted = src.read_text().replace('.Prev', '.prev')
            adjusted = adjusted.replace('pNtCurrentTeb()->Tib.ExceptionList',
                                        '((NT_TIB *)pNtCurrentTeb())->ExceptionList')
            adjusted = adjusted.replace('pNtCurrentTeb()->Peb',
                                        'pNtCurrentTeb()->ProcessEnvironmentBlock')
            adjusted = adjusted.replace('->ProcessEnvironmentBlock->ImageBaseAddress',
                                        '->ProcessEnvironmentBlock->Reserved3[1]')
            patched.write_text('#line 1 "exception.c"\n'+adjusted)
            g.setdefault('sdk_adaptations', []).append(
                'Modern EXCEPTION_REGISTRATION_RECORD.prev and TEB.ProcessEnvironmentBlock spellings; '
                'access the initial NT_TIB through its SDK type; test operations and expectations unchanged')
            g['translation_unit_sha256'] = wine.digest(patched)
            src = patched
        command = [args.cc, *flags, *aliases, *sdk_options, '-include', str(wine.SUPPORT/'wine_sdk.h'), '-ffunction-sections', '-Wl,--gc-sections', '-I'+str(generated), '-I'+str(folder), '-idirafter', str(source/'include'),
            '-DSTANDALONE', str(wine.SUPPORT/'startup.c'), str(src),
            *[str(folder/s) for s in g['other_sources']], *extras,
            '-o', str(out/'bin'/g['exe']), '-Wl,--start-group', *['-l'+x for x in libraries], '-Wl,--end-group']
        compile_output = ''
        if g['name'] == 'rpcrt4_server' and not errors:
            # Clang's GNU x86 target cannot emit these SEH tables. Its MSVC
            # target supplies native x86 SEH; MinGW supplies SDK declarations
            # and import libraries, and our usual startup remains unchanged.
            sysroot = Path(subprocess.check_output([args.cc, '-print-sysroot'], text=True).strip())
            compile_flags = [f for f in flags if not f.startswith(('-Wl,', '-std=')) and f != '-nostdlib']
            g['compile_commands'] = []
            g['seh_compiler'] = subprocess.check_output([args.seh_cc, '--version'], text=True).splitlines()[0]
            for unit in [str(src), *[p for p in extras if p.endswith('.c')]]:
                obj = generated/(Path(unit).stem+'-seh.o')
                compile_command = [args.seh_cc, '--target=i686-pc-windows-msvc',
                    '-isystem', str(sysroot/'i686-w64-mingw32/include'),
                    '-D__MINGW32__', '-D__i386__', '-D_X86_', '-fgnuc-version=4.2.1',
                    '-fms-extensions', '-fno-stack-protector', '-march=i586', '-mno-sse', '-mno-sse2',
                    '-DUSE_COMPILER_EXCEPTIONS', '-DNEO98_SDK_EXTERN_GUIDS', *compile_flags, '-std=gnu99',
                    '-include', str(wine.SUPPORT/'wine_sdk.h'), '-I'+str(generated), '-I'+str(folder),
                    '-idirafter', str(source/'include'), '-DSTANDALONE', '-c', unit, '-o', str(obj)]
                g['compile_commands'].append(compile_command)
                compiled = subprocess.run(compile_command, capture_output=True, text=True)
                compile_output += compiled.stdout+compiled.stderr
                if compiled.returncode:
                    errors.append('Native x86 SEH compilation: '+Path(unit).name)
                    break
                prepare_seh_object(obj)
                command[command.index(unit)] = str(obj)
            g['seh_link_adaptation'] = 'Discard .sxdata SafeSEH linker metadata before GNU ld; retain executable SEH scope tables in .xdata'
            g.setdefault('sdk_adaptations', []).append('Clang MSVC x86 SEH code generation with MinGW SDK/imports; no Wine exception implementation')
        g['build_command'] = command
        r = subprocess.run(command, capture_output=True, text=True) if not errors else subprocess.CompletedProcess(command, 1, '', '')
        (logs/(g['name']+'.log')).write_text(compile_output+r.stdout+r.stderr)
        if r.returncode or errors:
            g.update(build_status='BUILD_ERROR', errors=errors or ['compiler/linker: see build log'])
        else:
            details=wine.pe_details(out/'bin'/g['exe'])
            g.update(build_status='BUILT', sha256=wine.digest(out/'bin'/g['exe']), **details)
            g.pop('errors', None)
            if details['machine'] != 0x14c: raise RuntimeError('Not x86: '+g['name'])
        print(g['name'], g['build_status'], flush=True)
        wine.save(out/'catalog.json', {'source_pin':manifest['pin'], 'groups':groups})
    manifest['groups']=[g for g in groups if g.get('build_status')=='BUILT']
    manifest['supervisor_sha256']=supervisor(out, manifest['groups'], args.cc, flags)
    manifest['supervisor_source_sha256'] = wine.digest(wine.SUPPORT/'supervisor.c')
    wine.save(out/'manifest.json', manifest)
    print('BUILT',len(manifest['groups']),'of',len(groups),flush=True)


def isolated_entry(data):
    """Only split entry points that contain distinct, argument-free test calls."""
    entry = re.search(r'START_TEST\(\s*(\w+)\s*\)\s*\{([^{}]*)\}\s*\Z', data)
    if not entry:
        raise RuntimeError('Entry point is not a flat function-call list')
    calls = re.findall(r'\b(test_\w+)\(\s*\);', entry[2])
    if not calls or len(set(calls)) != len(calls) or re.sub(r'\btest_\w+\(\s*\);', '', entry[2]).strip():
        raise RuntimeError('Refusing to discard setup, arguments or other entry-point logic')
    selected = '\n    int argc; char **argv;\n    argc = winetest_get_mainargs(&argv);\n'
    selected += '\n'.join('    if (argc < 3 || !strcmp(argv[2], "'+call+'")) '+call+'();' for call in calls)
    selected += '\n'
    return entry[1], calls, data[:entry.start(2)]+selected+data[entry.end(2):]


def split_functions(args):
    """Isolate a call-only upstream entry point without changing its assertions."""
    manifest = wine.verified_manifest(args.out)
    original = next(g for g in manifest['groups'] if g['name'] == args.name)
    if manifest.get('fixtures') or manifest.get('assets'):
        raise RuntimeError('Splitting a fixture-dependent bundle needs an explicit preparation review')
    source = args.out/'source'/original['source']
    test_name, calls, data = isolated_entry(source.read_text())
    target = args.destination.resolve()
    target.mkdir(parents=True, exist_ok=False)
    (target/'bin').mkdir()
    unit = target/Path(original['source']).name
    unit.write_text('#line 1 "'+unit.name+'"\n'+data)
    command = list(original['build_command'])
    old_source = str(source.resolve())
    if old_source not in command:
        raise RuntimeError('Original build uses an adapted translation unit; review before splitting')
    command[command.index(old_source)] = str(unit)
    command[command.index('-o')+1] = str(target/'bin'/original['exe'])
    result = subprocess.run(command, capture_output=True, text=True)
    (target/'build.log').write_text(result.stdout+result.stderr)
    result.check_returncode()
    details = wine.pe_details(target/'bin'/original['exe'])
    groups = []
    for index, call in enumerate(calls, 1):
        # Win98 reuses process IDs. Distinct filenames prevent startup.c from
        # truncating a preceding function's EXE.PID.LOG after PID reuse.
        filename = 'NDR%05d.EXE' % index
        shutil.copyfile(target/'bin'/original['exe'], target/'bin'/filename)
        group = dict(original, name=original['name']+'__'+call, arguments=test_name+' '+call,
                     exe=filename, sha256=wine.digest(target/'bin'/filename), **details)
        group['entry_selection'] = {'function': call, 'mode': 'fresh process per upstream function',
                                    'original_source_sha256': wine.digest(source),
                                    'translation_unit_sha256': wine.digest(unit)}
        group['build_command'] = command
        groups.append(group)
    (target/'bin'/original['exe']).unlink()
    manifest['groups'] = groups
    manifest['support_sha256'] = {p.name: wine.digest(p) for p in wine.SUPPORT.glob('*') if p.is_file()}
    manifest['supervisor_sha256'] = supervisor(target, groups, args.cc, manifest['flags'])
    manifest['supervisor_source_sha256'] = wine.digest(wine.SUPPORT/'supervisor.c')
    wine.save(target/'manifest.json', manifest)
    print(target, len(groups), 'isolated upstream functions')


def bundle(args):
    manifest=wine.verified_manifest(args.out)
    groups=[g for g in manifest['groups'] if not args.families or g['family'] in args.families.split(',')]
    if args.names:groups=[g for g in groups if g['name'] in args.names.split(',')]
    if not groups:raise RuntimeError('No built groups selected')
    timeout = getattr(args, 'timeout', None)
    if timeout is not None:
        if timeout <= 0: raise RuntimeError('Batch timeout must be positive')
        groups = [dict(g, timeout_ms=timeout * 1000) for g in groups]
    target=args.destination.resolve();target.mkdir(parents=True,exist_ok=False);(target/'bin').mkdir()
    for g in groups:shutil.copy2(args.out/'bin'/g['exe'],target/'bin'/g['exe'])
    for name in manifest.get('assets', {}): shutil.copy2(args.out/'bin'/name, target/'bin'/name)
    manifest['groups']=groups
    supervisor_flags = list(manifest['flags'])
    if getattr(args, 'wininit_fixture', False) or manifest.get('fixtures', {}).get('wininit'):
        supervisor_flags.append('-DCONFORMANCE_WININIT_FIXTURE')
        manifest.setdefault('fixtures', {})['wininit'] = {
            'contents': '[rename]\r\n',
            'purpose': 'Paired nonempty INI: original MSI.DLL reads before an empty file mapping',
            'phase': 'supervisor, after boot, before each test process'}
    if getattr(args, 'shell_fixture', False) or manifest.get('fixtures', {}).get('shell'):
        supervisor_flags.append('-DCONFORMANCE_SHELL_FIXTURE')
        manifest.setdefault('fixtures', {})['shell'] = {
            'purpose': 'Require the original Explorer taskbar; start Explorer if absent',
            'ready_window': 'visible Shell_TrayWnd, idle process, responsive Progman with DefView/ListView',
            'foreground_stable_ms': 1000, 'valid_foreground_required': True,
            'startup_timeout_ms': 120000, 'idle_timeout_ms': 120000,
            'phase': 'supervisor, before tests; terminate only a shell it started'}
    if getattr(args, 'topmost_fixture', False) or manifest.get('fixtures', {}).get('topmost'):
        supervisor_flags.append('-DCONFORMANCE_TOPMOST_FIXTURE')
        manifest.setdefault('fixtures', {})['topmost'] = {
            'purpose': 'A visible topmost window above the normal Z-order band, as on the Win98 desktop',
            'class': 'STATIC', 'rectangle': [-20, -20, 1, 1],
            'style': 'WS_POPUP|WS_DISABLED', 'exstyle': 'WS_EX_TOPMOST|WS_EX_TOOLWINDOW',
            'show': 'SW_SHOWNOACTIVATE', 'phase': 'supervisor, before tests; destroy on completion'}
    if getattr(args, 'logon_fixture', False) or manifest.get('fixtures', {}).get('logon'):
        supervisor_flags.append('-DCONFORMANCE_LOGON_FIXTURE')
        manifest.setdefault('fixtures', {})['logon'] = {
            'purpose': 'Exercise GetUserNameA with the same explicit Win98 logon value',
            'key': 'HKLM\\System\\CurrentControlSet\\Control',
            'value': 'Current User', 'type': 'REG_SZ', 'data': 'Conformance',
            'phase': 'supervisor, before each test process, in the private system only'}
    if getattr(args, 'browser_fixture', False) or manifest.get('fixtures', {}).get('browser'):
        supervisor_flags.append('-DCONFORMANCE_BROWSER_FIXTURE')
        manifest.setdefault('fixtures', {})['browser'] = {
            'purpose': 'Supply identical per-user IE settings normally created by Active Setup',
            'key': r'HKCU\Software\Microsoft\Internet Explorer\Main',
            'values': {'Start Page': 'http://example.invalid/home', 'Search Page': 'http://example.invalid/search'},
            'phase': 'supervisor, before each test process, in the private system only'}
    manifest['supervisor_sha256']=supervisor(target,groups,args.cc,supervisor_flags,manifest.get('assets'))
    manifest['supervisor_source_sha256'] = wine.digest(wine.SUPPORT/'supervisor.c')
    wine.save(target/'manifest.json',manifest)
    print(target, len(groups), 'groups')


def run_batches(args):
    """Resume completed batches; never overwrite evidence or silently retry it."""
    manifest = wine.verified_manifest(args.out)
    selected = [g for g in manifest['groups'] if g['family'] not in args.exclude_families.split(',')]
    if args.names:
        selected = [g for g in selected if g['name'] in args.names.split(',')]
    root = args.destination.resolve()
    root.mkdir(parents=True, exist_ok=True)
    plan_path = root/'plan.json'
    selection = {'exclude_families': args.exclude_families, 'names': args.names, 'batch_size': args.batch_size}
    if args.reference_dlls: selection['reference_dlls'] = True
    if plan_path.exists():
        plan = json.loads(plan_path.read_text())
        if plan['selection'] != selection:
            raise RuntimeError('Resume with the original selection and batch size: '+str(plan_path))
        current = {g['name']: g for g in selected}
        if any(g['name'] not in current or current[g['name']]['sha256'] != g['sha256'] for g in plan['groups']):
            raise RuntimeError('Planned binaries changed: use a new result directory')
        selected = [current[g['name']] for g in plan['groups']]
    else:
        wine.save(plan_path, {'selection': selection, 'source_pin': manifest['pin'],
            'groups': [{k: g[k] for k in ('name', 'exe', 'sha256')} for g in selected]})
    for index in range(0, len(selected), args.batch_size):
        groups = selected[index:index + args.batch_size]
        path = root / ('%03d-%s' % (index // args.batch_size + 1, groups[0]['family']))
        if not path.exists():
            bundle(argparse.Namespace(out=args.out, destination=path, families=None,
                names=','.join(g['name'] for g in groups), cc=args.cc))
        else:
            old = wine.verified_manifest(path)
            if [(g['name'], g['sha256']) for g in old['groups']] != [(g['name'], g['sha256']) for g in groups]:
                raise RuntimeError('Batch selection changed: ' + str(path))
        common = [sys.executable, '-u', str(wine.ROOT/'tools/wine_conformance.py'), '--out', str(path)]
        for kind, command in [('reference', ['reference', '--agent', str(args.agent), '--emulator', str(args.emulator),
                '--source-vm', str(args.source_vm), '--system-root', str(args.win98_root), '--boot-timeout', '240']),
                ('neo98', ['neo98', '--win98-root', str(args.win98_root), '--discard-sessions',
                    '--reference-results', str(path/'reference/results.json'), *(['--reference-dlls'] if args.reference_dlls else [])])]:
            result = path/kind
            if not (result/'results.json').exists():
                if result.exists(): raise RuntimeError('Unfinished run needs inspection: '+str(result))
                print('RUN', path.name, kind, flush=True)
                with (path/(kind+'.log')).open('w') as log:
                    run = subprocess.run([*common, *command, '--results', str(result)], stdout=log, stderr=subprocess.STDOUT)
                print('END', path.name, kind, run.returncode, flush=True)
                if not (result/'results.json').exists(): raise RuntimeError('No results: '+str(result))
            if kind == 'reference' and args.discard_vms and (result/'vm').exists():
                shutil.copy2(result/'vm/86box.cfg', result/'private-86box.cfg')
                shutil.rmtree(result/'vm')
        ref = json.loads((path/'reference/results.json').read_text())
        if any(line.startswith('INFO\tos=4.10.2222\tplatform=1\t') for line in ref.get('info', [])):
            wine.compare(argparse.Namespace(reference=path/'reference', candidate=path/'neo98', report=path/'comparison.json'))
        summarize(root)


def summarize(root):
    report = {'schema': 1, 'groups': {}, 'counts': {}}
    policy = json.loads((wine.SUPPORT/'alternatives.json').read_text())
    for path in sorted(root.glob('*/reference/results.json')):
        batch = path.parent.parent
        ref = json.loads(path.read_text())
        arm_path = batch/'neo98/results.json'
        arm = json.loads(arm_path.read_text()) if arm_path.exists() else {'groups': {}}
        manifest = json.loads((batch/'reference/manifest.json').read_text())
        definitions = {g['name']: g for g in manifest['groups']}
        if arm_path.exists():
            other_manifest = json.loads((batch/'neo98/manifest.json').read_text())
            identity = lambda m: ({g['name']: g['sha256'] for g in m['groups']}, m['supervisor_sha256'], m.get('assets', {}))
            if identity(manifest) != identity(other_manifest):
                raise RuntimeError('Reference/candidate inputs differ: '+str(batch))
        native = ref.get('runtime') == '86box-win98' and any(
            line.startswith('INFO\tos=4.10.2222\tplatform=1\t') for line in ref.get('info', []))
        for name, group in ref['groups'].items():
            other = arm['groups'].get(name, {'status': 'NOT_RUN', 'sites': {}, 'skips': []})
            observed = wine.host_observations(batch/'neo98'/(name+'-host.log'))
            recorded_status = other['status']
            if recorded_status == 'HOST_ERROR' and observed['deadline_reached']:
                other = {**other, 'status': 'TIMEOUT'}
            complete = native and group['status'] in ('PASS', 'FAIL')
            mode = assessment(definitions[name])
            required = {s: c for s, c in group['sites'].items() if complete and mode == 'ASSERTIONS' and set(c) == {'PASS'}}
            imported = {dll.upper() for dll in definitions[name].get('imports', {})}
            reference_dlls, candidate_dlls = ref.get('windows_dlls', {}), arm.get('windows_dlls', {})
            different_dlls = sorted(dll for dll in imported if arm_path.exists() and reference_dlls.get(dll) != candidate_dlls.get(dll))
            accepted = []
            if manifest.get('pin', {}).get('revision') == policy['source_commit']:
                legacy = next((key for key, (family, stem) in wine.GROUP_SOURCES.items()
                    if definitions[name].get('source') == 'dlls/%s/tests/%s.c' % (family, stem)), None)
                for site, rule in policy['groups'].get(legacy, {}).items():
                    if site in required and site not in other['sites'] and all(
                            set(other['sites'].get(s, {})) == {'PASS'} for s in rule['requires']):
                        accepted.append(site)
            report['groups'][name] = {
                'reference': group['status'], 'candidate': other['status'],
                'candidate_recorded_status': recorded_status, 'host_observations': observed,
                'candidate_not_attempted': arm.get('not_attempted', {}).get(name),
                'reference_sites': len(group['sites']), 'required_sites': len(required),
                'required_invocations': sum(counts['PASS'] for counts in required.values()),
                'sha256': definitions[name]['sha256'],
                'assessment': mode,
                'imported_dll_differences': different_dlls,
                'candidate_missing_dll_files': [dll for dll in different_dlls if dll in reference_dlls and dll not in candidate_dlls],
                'smoke_regression': native and mode == 'EXIT_SMOKE' and group['status'] == 'PASS' and other['status'] not in ('PASS', 'NOT_RUN'),
                'failed': [s for s in required if other['sites'].get(s, {}).get('FAIL')],
                'unreached': [s for s in required if s not in other['sites'] and s not in accepted],
                'count_changes': [{'site': s, 'reference': counts, 'candidate': other['sites'][s]}
                    for s, counts in required.items() if s in other['sites'] and
                    other['sites'][s].get('PASS', 0)+other['sites'][s].get('FAIL', 0) != counts['PASS']],
                'accepted_alternatives': accepted,
                'native_failure_neo98_pass': [site for site, counts in group['sites'].items()
                    if complete and other['status'] in ('PASS', 'FAIL') and counts.get('FAIL')
                    and set(other['sites'].get(site, {})) == {'PASS'}],
                'reference_skips': group['skips'], 'candidate_skips': other['skips'],
                'additional_skips': dict(Counter(s['site'] for s in other['skips']) - Counter(s['site'] for s in group['skips'])),
                'batch': str(batch),
                'diagnostics': [{'reason': json.loads(p.read_text())['reason'], 'path': str(p.relative_to(batch))}
                    for p in sorted((batch/'neo98'/(name+'-reports')).glob('*/report.json'))],
                'qualification': 'REVIEW' if required else 'NO_COMPLETE_REFERENCE_ASSERTIONS'}
    for side in ('reference', 'candidate'):
        report['counts'][side] = dict(Counter(g[side] for g in report['groups'].values()))
    wine.save(root/'summary.json', report)


def coverage_totals(sources, runs):
    built = {g['name']: g['sha256'] for source in sources.values() for g in source['groups']
             if g['build_status'] == 'BUILT'}
    current = {name: [r for r in history if r.get('sha256') == built.get(name)]
               for name, history in runs.items() if name in built}
    attempted = {n for n, history in current.items() if any(r['reference'] != 'NOT_RUN' for r in history)}
    completed = {n for n, history in current.items() if any(r['reference'] in ('PASS', 'FAIL') for r in history)}
    arm_attempted = {n for n, history in current.items() if any(r['candidate'] not in ('NOT_RUN', 'SKIP') for r in history)}
    return {'built': len(built), 'with_reference_attempt': len(attempted),
        'with_completed_reference': len(completed), 'with_neo98_attempt': len(arm_attempted),
        'not_run': sorted(set(built) - attempted),
        'build_errors': sum(s['counts'].get('BUILD_ERROR', 0) for s in sources.values())}


def export_report(args):
    root = args.root.resolve()
    report = {'schema': 1, 'generated_at_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'report_tool_sha256': wine.digest(Path(__file__)), 'sources': {}, 'runs': {}, 'batches': {}, 'totals': {}}
    if (root/'font-environment.json').exists():
        report['font_environment'] = json.loads((root/'font-environment.json').read_text())
    for source in ('wine', 'reactos', 'sdl12'):
        catalog = json.loads((root/source/'catalog.json').read_text())
        records = []
        for group in catalog['groups']:
            record = {k: v for k, v in group.items() if k not in ('build_command', 'compile_command')}
            if group['build_status'] == 'BUILD_ERROR':
                log = root/source/'build-logs'/(group['name']+'.log')
                lines = log.read_text(errors='replace').splitlines() if log.exists() else []
                record['build_diagnostics'] = [s.replace(str(root), '$BUILD') for s in lines
                    if 'error:' in s or 'undefined reference' in s or 'cannot find' in s][:4]
            records.append(record)
        source_manifest = json.loads((root/source/'manifest.json').read_text())
        report['sources'][source] = {'pin': catalog['source_pin'], 'groups': records,
            'compiler': source_manifest['compiler'], 'manifest_sha256': wine.digest(root/source/'manifest.json'),
            'counts': dict(Counter(g['build_status'] for g in records))}
    for folder in sorted(root.iterdir()):
        if not folder.is_dir() or not list(folder.glob('*/reference/results.json')): continue
        invalid = folder / 'invalid-harness.json'
        if invalid.exists():
            evidence = json.loads(invalid.read_text())
            if not evidence.get('reason'):
                raise RuntimeError('Invalid-harness marker requires an explicit reason: ' + str(invalid))
            report.setdefault('excluded_runs', {})[folder.name] = evidence
            continue
        summarize(folder)
        summary = json.loads((folder/'summary.json').read_text())
        for name, result in summary['groups'].items():
            report['runs'].setdefault(name, []).append(result)
        for path in sorted(folder.glob('*/reference/results.json')):
            batch = path.parent.parent
            native = json.loads(path.read_text())
            if (batch/'neo98/results.json').exists() and any(
                    line.startswith('INFO\tos=4.10.2222\tplatform=1\t') for line in native.get('info', [])):
                wine.compare(argparse.Namespace(reference=batch/'reference', candidate=batch/'neo98',
                    report=batch/'comparison.json', quiet=True))
            entry = {}
            for kind in ('reference', 'neo98'):
                result_file = batch/kind/'results.json'
                if not result_file.exists(): continue
                result = json.loads(result_file.read_text())
                manifest = json.loads((batch/kind/'manifest.json').read_text())
                entry[kind] = {key: result.get(key) for key in ('runtime', 'info', 'suite_done', 'host_sha256',
                    'git_revision', 'source_patch_sha256', 'emulator_sha256', 'private_config_sha256', 'source_config_sha256',
                    'windows_dlls', 'windows_core16_sha256', 'reference_dll_overlay', 'host_timeout', 'host_timeout_group', 'infrastructure_error', 'recovery_note')}
                entry[kind].update(results_sha256=wine.digest(result_file),
                                   supervisor_sha256=manifest['supervisor_sha256'])
            report['batches'][str(batch.relative_to(root))] = entry
    report['coverage_basis'] = 'Current catalog EXE hashes; historical binaries remain in runs but do not count as current coverage'
    report['totals'] = coverage_totals(report['sources'], report['runs'])
    wine.save(args.report, report)
    print(json.dumps(report['totals'], indent=2))


def recover(args):
    """Collect a manually interrupted reference only after its disk is closed."""
    import configparser
    from wine_conformance_86box import fat_image
    destination = args.reference.resolve()
    if (destination/'results.json').exists(): raise RuntimeError('Results already exist')
    manifest = wine.verified_manifest(args.out)
    config = configparser.ConfigParser()
    config.read(destination/'vm/86box.cfg')
    disks = [(destination/'vm'/value).resolve() for section in config.sections()
             for key, value in config.items(section) if re.match(r'hdd_\d+_fn$', key)]
    if len(disks) != 1 or not disks[0].is_relative_to(destination/'vm'):
        raise RuntimeError('Recovery requires one private disk')
    if subprocess.run(['lsof', '-t', str(disks[0])], capture_output=True, text=True).stdout.strip():
        raise RuntimeError('Private VM disk is still open')
    for pattern in ('RUN.TSV', '*.LOG'):
        subprocess.run(['mcopy', '-i', fat_image(disks[0]), '::/WTEST/'+pattern, str(destination)],
                       capture_output=True, check=True)
    parsed = wine.parse_logs(destination, manifest)
    unfinished = [name for name, group in parsed['groups'].items()
                  if group['status'] == 'INCOMPLETE']
    completed = getattr(args, 'completed', False)
    if completed and (not parsed['suite_done'] or unfinished or args.group):
        raise RuntimeError('Completed recovery requires a DONE journal and no unfinished group')
    if args.group and args.group not in unfinished:
        raise RuntimeError('Selected group was not interrupted according to the guest journal: '+str(unfinished))
    timed_out = args.group or (unfinished[0] if len(unfinished) == 1 else None)
    provenance = destination/'provenance.json'
    extra = json.loads(provenance.read_text()) if provenance.exists() else {
        'runtime': '86box-win98', 'private_config_sha256': wine.digest(destination/'vm/86box.cfg'),
        'windows_dlls': wine.installation(destination/'system-provenance', wine.tested_dlls(manifest)),
        'windows_core16_sha256': wine.core_hashes(destination/'system-provenance')}
    extra.update(recovered_after_interruption=not completed, recovered_after_shutdown=completed,
                 recovery_note=args.note, host_timeout=not completed, host_timeout_group=timed_out)
    wine.finish_run(destination, manifest, extra)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,default=wine.ROOT/'build/conformance-campaign/wine')
    sub=p.add_subparsers(dest='action',required=True)
    b=sub.add_parser('wine-build');b.add_argument('--cc',default='i686-w64-mingw32-gcc');b.add_argument('--seh-cc',default='clang');b.add_argument('--timeout',type=int,default=60);b.add_argument('--retry-failed',action='store_true');b.add_argument('--rebuild',default='',help='Also rebuild these comma-separated built groups on a retry');b.set_defaults(func=build_wine)
    b.add_argument('--message-sequences', action='store_true', help='Mark each user32_msg sequence invocation without changing its assertions')
    b=sub.add_parser('split-functions');b.add_argument('--name',required=True);b.add_argument('--destination',type=Path,required=True);b.add_argument('--cc',default='i686-w64-mingw32-gcc');b.set_defaults(func=split_functions)
    b=sub.add_parser('bundle');b.add_argument('--destination',type=Path,required=True);b.add_argument('--families');b.add_argument('--names');b.add_argument('--cc',default='i686-w64-mingw32-gcc');b.add_argument('--wininit-fixture', action='store_true');b.add_argument('--shell-fixture', action='store_true');b.add_argument('--topmost-fixture', action='store_true');b.add_argument('--logon-fixture', action='store_true');b.add_argument('--browser-fixture', action='store_true');b.add_argument('--timeout', type=int, help='Per-group seconds for this new supervisor, used identically on both systems');b.set_defaults(func=bundle)
    b = sub.add_parser('run')
    for name in ('destination', 'agent', 'emulator', 'source-vm', 'win98-root'):
        b.add_argument('--'+name, type=Path, required=True)
    b.add_argument('--batch-size', type=int, default=20)
    b.add_argument('--exclude-families', default='')
    b.add_argument('--names')
    b.add_argument('--discard-vms', action='store_true')
    b.add_argument('--reference-dlls', action='store_true', help='Compare with missing original DLL files added only to private NEO98 guest copies')
    b.add_argument('--cc', default='i686-w64-mingw32-gcc')
    b.set_defaults(func=run_batches)
    b = sub.add_parser('report')
    b.add_argument('--root', type=Path, default=wine.ROOT/'build/conformance-campaign')
    b.add_argument('--report', type=Path, required=True)
    b.set_defaults(func=export_report)
    b = sub.add_parser('recover')
    b.add_argument('--reference', type=Path, required=True)
    b.add_argument('--group')
    b.add_argument('--completed', action='store_true', help='Recover a DONE journal after a delayed emulator shutdown; do not label it a test timeout')
    b.add_argument('--note', required=True)
    b.set_defaults(func=recover)
    a=p.parse_args();a.func(a)

if __name__=='__main__':main()
