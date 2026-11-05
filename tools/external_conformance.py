#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native ReactOS apitests and SDL 1.2 tests for the Win98 comparison runner."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request

import conformance_campaign as campaign
import wine_conformance as wine


def combine_test_entries(body, entries, name):
    if re.findall(r'START_TEST\((\w+)\)', body) != entries or len(set(entries)) != len(entries):
        raise RuntimeError('Pinned multi-entry test list changed')
    for entry in entries:
        body = body.replace('START_TEST('+entry+')', 'static void func_'+entry+'(void)')
    return body+'\nSTART_TEST('+name+') { '+''.join('func_'+entry+'(); ' for entry in entries)+'}\n'


def local_guid_header(definitions, declarations):
    # Define only absent UUIDs, without turning every subsequent SDK GUID
    # declaration into a second definition alongside libuuid. The header is
    # also force-included into cpp_log.cpp, which is built for the GNU target;
    # GNU ld does not fold its selectany COMDAT with the MSVC-target test
    # object's, so only the test unit (_MSC_VER) defines them.
    return ('#include <windows.h>\n'
        '#ifdef _MSC_VER\n'
        '#define NEO98_LOCAL_GUID(n,a,b,c,d,e,f,g,h,i,j,k) '
        'EXTERN_C __declspec(selectany) const GUID n = {a,b,c,{d,e,f,g,h,i,j,k}};\n'
        '#else\n'
        '#define NEO98_LOCAL_GUID(n,...) EXTERN_C const GUID n;\n'
        '#endif\n'
        + '\n'.join(line.replace('DEFINE_GUID(', 'NEO98_LOCAL_GUID(') for line in definitions)
        + '\n#undef NEO98_LOCAL_GUID\n'
        + ''.join('EXTERN_C const GUID '+name+';\n' for name in declarations))


def fetch(out, name):
    pin = json.loads((wine.SUPPORT/'campaign-pins.json').read_text())[name]
    out.mkdir(parents=True, exist_ok=True)
    archive = out/'source.tar.gz'
    if not archive.exists():
        partial = archive.with_suffix('.part')
        with urllib.request.urlopen(pin['url'], timeout=60) as response, partial.open('wb') as target:
            shutil.copyfileobj(response, target)
        if wine.digest(partial) != pin['sha256']:
            raise RuntimeError('Archive checksum mismatch: '+name)
        partial.replace(archive)
    if wine.digest(archive) != pin['sha256']:
        raise RuntimeError('Archive checksum mismatch: '+name)
    source = out/'source'
    if source.exists(): shutil.rmtree(source)
    source.mkdir()
    with tarfile.open(archive) as tar:
        for member in tar:
            parts = Path(member.name).parts[1:]
            if not parts or '..' in parts or not member.isfile(): continue
            path = source.joinpath(*parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            with tar.extractfile(member) as src, path.open('wb') as dst:
                shutil.copyfileobj(src, dst)
            path.chmod(member.mode & 0o755)
    return pin, source


def flags_for(out):
    original = json.loads((wine.ROOT/'build/conformance-campaign/wine/manifest.json').read_text())
    return [f for f in original['flags'] if not f.startswith('-I')] + ['-I'+str(out/'include')]


def pinned_reactos_sdk(out, source):
    """Generate the upstream SDK headers using its own host preprocessor."""
    folder = out/'build-tools/pinned-sdk'
    folder.mkdir(parents=True, exist_ok=True)
    preprocessor = folder/'hpp'
    commands = [['clang', '-O2', str(source/'sdk/tools/hpp/hpp.c'), '-o', str(preprocessor)]]
    subprocess.run(commands[0], check=True, capture_output=True)
    for name in ('winnt', 'ntdef', 'devioctl', 'wdm', 'ntddk', 'ntifs'):
        command = [str(preprocessor), name+'.template.h', str(folder/(name+'.h'))]
        subprocess.run(command, cwd=source/'sdk/include/xdk', check=True, capture_output=True)
        commands.append(command)
    # ReactOS supplies these interfaces in the NDK rather than winternl.h.
    # Keep one consistent set of pinned NT types instead of mixing SDKs.
    (folder/'winternl.h').write_text('#pragma once\n#include <ndk/ntndk.h>\n')
    metadata = {'hpp_source_sha256': wine.digest(source/'sdk/tools/hpp/hpp.c'),
        'hpp_sha256': wine.digest(preprocessor), 'commands': commands,
        'headers_sha256': {p.name: wine.digest(p) for p in folder.glob('*.h')}}
    wine.save(folder/'build.json', metadata)
    return folder, metadata


def save_build(out, pin, groups, flags, cc, assets=None, extra=None):
    wine.save(out/'catalog.json', {'source_pin': pin, 'groups': groups})
    manifest = {'schema': 1, 'pin': pin, 'flags': flags,
        'compiler': subprocess.check_output([cc, '--version'], text=True).splitlines()[0],
        'support_sha256': {p.name: wine.digest(p) for p in wine.SUPPORT.glob('*') if p.is_file()},
        'groups': [g for g in groups if g['build_status'] == 'BUILT'], **(extra or {})}
    if assets: manifest['assets'] = assets
    manifest['supervisor_sha256'] = campaign.supervisor(out, manifest['groups'], cc, flags, assets)
    manifest['supervisor_source_sha256'] = wine.digest(wine.SUPPORT/'supervisor.c')
    wine.save(out/'manifest.json', manifest)
    wine.save(out/'catalog.json', {'source_pin': pin, 'groups': groups})
    print('BUILT', len(manifest['groups']), 'of', len(groups), flush=True)


def reactos(args):
    out = args.out.resolve()
    previous = {g['name']: g for g in json.loads((out/'catalog.json').read_text())['groups']} if args.retry_failed else {}
    names = set(filter(None, (args.names or '').split(',')))
    if names - previous.keys():
        raise RuntimeError('Unknown selected tests: '+', '.join(sorted(names - previous.keys())))
    pin, source = fetch(out, 'reactos')
    sdk = pinned_reactos_sdk(out, source) if args.pinned_sdk else None
    include = out/'include'; (include/'wine').mkdir(parents=True, exist_ok=True)
    header = (source/'sdk/include/reactos/wine/test.h').read_text()
    anchor = '    tls_data* data=get_tls_data();\n\n    if (data->todo_level)'
    if header.count(anchor) != 1: raise RuntimeError('ReactOS harness instrumentation anchor changed')
    header = header.replace(anchor, '''    tls_data* data=get_tls_data();
    DWORD saved_error = GetLastError();
    fprintf(stdout, "NEO98_CHECK %s:%d %s\\n", data->current_file, data->current_line,
        data->todo_level ? (condition ? "TODO_PASS" : "TODO_FAIL") : (condition ? "PASS" : "FAIL"));
    if (!condition) fflush(stdout);
#ifdef NEO98_FLUSH_CHECKS
    else fflush(stdout);
#endif
    SetLastError(saved_error);

    if (data->todo_level)''')
    # MinGW's C++ headers omit the Windows min macro. This diagnostic
    # buffer-size expression has the same arithmetic without that macro.
    header = header.replace('min( 300, n * 5 )', '((300 < n * 5) ? 300 : n * 5)')
    buffering = 'setvbuf (stdout, NULL, _IONBF, 0);'
    if header.count(buffering) != 1:
        raise RuntimeError('Pinned ReactOS stdout buffering changed')
    # Match the Wine campaign: persist failed assertions immediately, but
    # avoid a guest WriteFile for every character in successful diagnostics.
    # SetProp alone checks all 65,536 atom values. Test operations are unchanged.
    header = header.replace(buffering, '#ifdef NEO98_FLUSH_CHECKS\n    setvbuf (stdout, NULL, _IONBF, 0);\n#else\n    setvbuf (stdout, NULL, _IOFBF, 4096);\n#endif')
    (include/'wine/test.h').write_text(header)
    (include/'campaign_status.h').write_text('#undef WIN32_NO_STATUS\n#include <ntstatus.h>\n#define WIN32_NO_STATUS\n')
    # Replace only aggregate PCH includes. Test bodies and expected values stay
    # upstream. Missing private NT declarations/SEH remain visible build errors.
    (include/'campaign_precomp.h').write_text('''#define OEMRESOURCE
#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <ntstatus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <strsafe.h>
#include <apitest.h>
#include "campaign_nls_constants.h"
#define TEST(x) ok(x, #x "\\n")
#define RTEST(x) ok(x, #x "\\n")
#define CLIPRGN 1
#define ok_rect(p,l,t,r,b) do { ok_int((p)->left,l); ok_int((p)->top,t); ok_int((p)->right,r); ok_int((p)->bottom,b); } while (0)
#ifdef CAMPAIGN_USER32_HELPERS
#include <msgtrace.h>
#include <user32testhelpers.h>
#include <undocuser.h>
#include "resource.h"
#include "campaign_user_constants.h"
#endif
#ifdef CAMPAIGN_GDI_HELPERS
#include "campaign_gdi_constants.h"
#endif
''')
    for output, sources in {
        'campaign_user_constants.h': [('sdk/include/psdk/winuser.h', ('LR_LOADREALSIZE', 'WM_DWMNCRENDERINGCHANGED')),
                                      ('modules/rostests/apitests/user32/precomp.h', ('DESKTOP_ALL_ACCESS',))],
        'campaign_gdi_constants.h': [('win32ss/include/ntgdihdl.h', ('GDI_',)),
                                   ('win32ss/include/ntgdityp.h', ('DEFAULT_BITMAP',)),
                                   ('sdk/include/psdk/wingdi.h', ('LAYOUT_LTR',))]}.items():
        definitions = []
        for filename, prefixes in sources:
            data = (source/filename).read_text().replace('\\\n', ' ')
            for line in data.splitlines():
                match = re.match(r'#define\s+(\w+)', line)
                if match and match[1].startswith(prefixes):
                    definitions += ['#ifndef '+match[1], line, '#endif']
        (include/output).write_text('\n'.join(definitions)+'\n')
    # The upstream GDI PCH declares this stdcall export. Without it Clang
    # accepts an implicit cdecl call and the native import cannot link.
    gdi_sdk = (source/'sdk/include/psdk/wingdi.h').read_text()
    declaration = re.findall(r'^LONG WINAPI GdiGetCharDimensions\([^\n]+;$', gdi_sdk, re.M)
    if len(declaration) != 1:
        raise RuntimeError('Pinned GdiGetCharDimensions declaration changed')
    with (include/'campaign_gdi_constants.h').open('a') as header:
        header.write(declaration[0]+'\n')
    nls_sdk = (source/'sdk/include/psdk/winnls.h').read_text()
    nls_flag = re.findall(r'^#define WC_ERR_INVALID_CHARS[^\n]+$', nls_sdk, re.M)
    if len(nls_flag) != 1:
        raise RuntimeError('Pinned WC_ERR_INVALID_CHARS declaration changed')
    (include/'campaign_nls_constants.h').write_text(
        '#ifndef WC_ERR_INVALID_CHARS\n'+nls_flag[0]+'\n#endif\n')
    api = source/'modules/rostests/apitests'
    flags = flags_for(out) + ['-DWIN32_NO_STATUS', '-include', 'ntstatus.h', '-D_M_IX86=600', '-ffunction-sections', '-Wl,--gc-sections']
    logs = out/'build-logs'; logs.mkdir(exist_ok=True)
    ported = out/'translation-units'; ported.mkdir(exist_ok=True)
    (out/'bin').mkdir(exist_ok=True)
    shared, shared_errors = {}, {}
    for family, resource, helper in [('gdi32', 'resource.rc', api/'gdi32/init.c'),
                                      ('user32', 'user32_apitest.rc', api/'include/msgtrace.c'),
                                      ('comctl32', 'comctl32_apitest.rc', api/'include/msgtrace.c')]:
        folder = api/family
        resource_obj = logs/(family+'-resources.o')
        resource_command = ['i686-w64-mingw32-windres', '-I'+str(folder), '-I'+str(source/'sdk/include'), '-I'+str(source/'sdk/include/reactos'), str(folder/resource), '-O', 'coff', '-o', str(resource_obj)]
        result = subprocess.run(resource_command, cwd=folder, capture_output=True, text=True)
        (logs/(family+'-resources.log')).write_text(result.stdout+result.stderr)
        entries = [str(resource_obj)] if not result.returncode else []
        errors = ['resource: '+resource] if result.returncode else []
        unit = ported/(family+'-helper.c')
        body = helper.read_text().replace('#include "precomp.h"', '#include <campaign_precomp.h>')
        unit.write_text('#line 1 "'+helper.name+'"\n'+body)
        helper_obj = logs/(family+'-helper.o')
        command = [args.cc, *flags, '-include', 'campaign_precomp.h', '-I'+str(folder), '-I'+str(api/'include'),
                   '-I'+str(source/'sdk/include/reactos'), '-c', str(unit), '-o', str(helper_obj)]
        result = subprocess.run(command, capture_output=True, text=True)
        (logs/(family+'-helper.log')).write_text(result.stdout+result.stderr)
        if result.returncode: errors.append('helper: '+str(helper.relative_to(source)))
        else: entries.append(str(helper_obj))
        shared[family] = {'objects': entries, 'helper_source': str(helper.relative_to(source)),
            'helper_sha256': wine.digest(helper), 'resource_source': str((folder/resource).relative_to(source)),
            'resource_sha256': wine.digest(folder/resource), 'helper_command': command, 'resource_command': resource_command}
        shared_errors[family] = errors
    # The socket sources call helpers.c from their original CMake target.
    # Link the same helper instead of dropping those assertions or replacing
    # the server/DNS operations. Its external HTTP dependency stays visible.
    helper = api/'ws2_32/helpers.c'
    helper_header = helper.with_name('ws2_32.h')
    header_body = helper_header.read_text()
    for dependency in ('ndk/rtlfuncs.h', 'ndk/mmfuncs.h'):
        anchor = '#include <'+dependency+'>'
        if header_body.count(anchor) != 1: raise RuntimeError('Pinned socket helper header changed')
        header_body = header_body.replace(anchor, '')
    (include/'campaign_socket_helpers.h').write_text(header_body)
    helper_unit = ported/'ws2_32-helper.c'
    helper_body = helper.read_text().replace('#include "ws2_32.h"', '#include <campaign_socket_helpers.h>')
    if args.socket_loopback:
        for before, after in [('host = gethostbyname("test.winehq.org");', 'host = NULL; /* Numeric local fixture needs no DNS. */'),
                              ('*(u_long*)host->h_addr_list[0]', 'inet_addr("127.0.0.1")'),
                              ('htons(80)', 'htons(32196)')]:
            if helper_body.count(before) != 1: raise RuntimeError('Pinned socket endpoint changed')
            helper_body = helper_body.replace(before, after)
    helper_unit.write_text('#line 1 "helpers.c"\n' + helper_body)
    helper_obj = logs/'ws2_32-helper.o'
    command = [args.cc, *flags, '-I'+str(api/'ws2_32'), '-I'+str(api/'include'),
               '-I'+str(source/'sdk/include/reactos'), '-c', str(helper_unit), '-o', str(helper_obj)]
    result = subprocess.run(command, capture_output=True, text=True)
    (logs/'ws2_32-helper.log').write_text(result.stdout+result.stderr)
    shared['ws2_32'] = {'objects': [str(helper_obj)] if not result.returncode else [],
        'helper_source': str(helper.relative_to(source)), 'helper_sha256': wine.digest(helper),
        'helper_header_sha256': wine.digest(helper_header), 'translation_unit_sha256': wine.digest(helper_unit),
        'unused_helper_includes_removed': ['ndk/rtlfuncs.h', 'ndk/mmfuncs.h'],
        'helper_command': command, 'external_endpoint': 'test.winehq.org:80'}
    shared_errors['ws2_32'] = ['helper: '+str(helper.relative_to(source))] if result.returncode else []
    socket_startup = wine.SUPPORT/'startup.c'
    if args.socket_loopback:
        fixture = wine.SUPPORT/'loopback_http.c'
        obj = logs/'socket-http.o'
        command = [args.cc, *flags, '-UWIN32_NO_STATUS', '-DNEO98_SOCKET_HTTP', '-c', str(fixture), '-o', str(obj)]
        result = subprocess.run(command, capture_output=True, text=True)
        (logs/'socket-http.log').write_text(result.stdout+result.stderr)
        if result.returncode: raise RuntimeError('Socket HTTP fixture build failed')
        shared['ws2_32']['objects'].append(str(obj))
        text = socket_startup.read_text()
        anchor = '    result = main(argc, argv);'
        if text.count(anchor) != 1: raise RuntimeError('Startup fixture anchor changed')
        text = text.replace('void mainCRTStartup(void)',
            'extern int neo98_http_fixture_start(void);\nextern int neo98_http_fixture_stop(void);\nvoid mainCRTStartup(void)')
        text = text.replace(anchor, '    if (!neo98_http_fixture_start()) { fflush(stdout); ExitProcess(252); }\n'+anchor+
                            '\n    if (!neo98_http_fixture_stop()) result = 252;')
        socket_startup = ported/'socket-startup.c'
        socket_startup.write_text(text)
        shared['ws2_32']['external_endpoint'] = None
        shared['ws2_32']['input_fixture'] = {'kind': 'in-guest-loopback-http', 'endpoint': '127.0.0.1:32196',
            'server_sha256': wine.digest(fixture), 'server_command': command,
            'startup_sha256': wine.digest(socket_startup), 'body_bytes': 512,
            'address_input': 'inet_addr numeric loopback; public DNS lookup omitted'}
    groups = []
    for src in sorted(api.rglob('*')):
        if src.suffix not in ('.c', '.cpp'): continue
        data = src.read_bytes().decode('latin1')
        if not re.search(r'\bSTART_TEST\s*\(', data): continue
        relative = src.relative_to(api)
        family = relative.parts[0]
        name = 'ros_' + '_'.join(relative.with_suffix('').parts)
        if names and name not in names:
            groups.append(previous[name]); continue
        if args.families and family not in args.families.split(','):
            groups.append(previous[name]); continue
        if args.only_cpp and src.suffix != '.cpp':
            groups.append(previous[name])
            continue
        if name in previous and previous[name]['build_status'] == 'BUILT' and not args.rebuild_selected:
            if previous[name]['source_sha256'] != wine.digest(src): raise RuntimeError('Changed source: '+name)
            groups.append(previous[name])
            continue
        g = {'name': name, 'family': family, 'source': str(src.relative_to(source)),
             'source_sha256': wine.digest(src), 'exe': 'R%06d.EXE' % len(groups),
             'build_tool_sha256': wine.digest(Path(__file__)),
             'harness_buffering': '4096-byte stdout buffer; failed assertion markers flushed immediately',
             'sdk_headers_sha256': {p.name: wine.digest(p) for p in include.glob('campaign_*.h')},
             'timeout_ms': args.timeout*1000, 'build_status': 'BUILD_ERROR'}
        groups.append(g)
        family_flags = {'gdi32': ['-DCAMPAIGN_GDI_HELPERS'], 'user32': ['-DCAMPAIGN_USER32_HELPERS']}.get(family, [])
        if args.flush_checks:
            family_flags += ['-DNEO98_FLUSH_CHECKS']
            g['harness_flush_checks'] = 'Persist every assertion for crash diagnosis; expectations unchanged'
        legacy_shell_atl = name in {
            'ros_shell32_CFSFolder', 'ros_shell32_CMyComputer', 'ros_shell32_CShellLink',
            'ros_shell32_DragDrop', 'ros_shell32_GetDisplayNameOf', 'ros_shell32_IShellFolderViewCB',
            'ros_shell32_ItemIDList', 'ros_shell32_ShellState', 'ros_shell32_menu',
        }
        legacy_atl = legacy_shell_atl or name in {
            'ros_atl_CRegKey', 'ros_atl_CString', 'ros_atl_CPath',
            'ros_atl_CAtlArray', 'ros_atl_CAtlList', 'ros_atl_CSimpleArray',
            'ros_atl_CSimpleMap', 'ros_atl_CComBSTR', 'ros_shell32_CommandLineToArgvW',
            'ros_atl_CComHeapPtr', 'ros_atl_CAtlFileMapping', 'ros_atl_SubclassWindow',
            'ros_atl_atlconv', 'ros_atl_CComObject', 'ros_atl_CComVariant', 'ros_atl_CComQIPtr',
        }
        legacy_cpp = legacy_atl or name in (
            'ros_shell32_SHAppBarMessage', 'ros_shell32_SHLimitInputEdit',
            'ros_shlwapi_SHPropertyBag', 'ros_shlwapi_IShellFolderHelpers')
        if legacy_cpp and args.seh_cpp:
            if re.search(r'\b(?:dynamic_cast|typeid|throw|try|catch)\b', data):
                raise RuntimeError('Reviewed shell test now requires C++ RTTI or exception handling')
            family_flags += ['-fno-rtti', '-fno-sized-deallocation']
            if legacy_atl:
                # Use upstream ATL's native RaiseException configuration.
                # These selected tests contain no C++ exception/RTTI checks.
                family_flags += ['-D_ATL_NO_EXCEPTIONS', '-fno-threadsafe-statics']
                g['atl_exception_configuration'] = {
                    'define': '_ATL_NO_EXCEPTIONS',
                    'header_sha256': wine.digest(source/'sdk/lib/atl/atlexcept.h'),
                    'scope': 'Upstream ATL RaiseException variant; no C++ throw/catch coverage',
                    'static_initialization': 'Pre-C++11 local static initialization; no concurrent first-use guarantee',
                }
                if re.search(r'\b(?:CAtlBaseModule|CAtlComModule|_AtlBaseModule|_AtlComModule|CreateThread|_beginthreadex?)\b', data):
                    raise RuntimeError('Selected ATL test now exercises module identity or concurrent initialization')
                # These support objects are linked into the test EXE, never
                # a DLL. Their incidental module lookup need not require XP's
                # GetModuleHandleExW. Do not adapt module-identity tests.
                overlay = include/'legacy-atl'
                shutil.copytree(source/'sdk/lib/atl', overlay, dirs_exist_ok=True)
                adaptations = {}
                for header_name, member in (('atlcore.h', 'm_hInst'), ('atlbase.h', 'm_hInstTypeLib')):
                    header = overlay/header_name
                    text = header.read_text()
                    anchor = 'GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)this, &'+member+');'
                    if text.count(anchor) != 1:
                        raise RuntimeError('Pinned ATL module constructor changed')
                    header.write_text(text.replace(anchor, member+' = GetModuleHandleA(NULL);'))
                    adaptations[header_name] = {'source_sha256': wine.digest(source/'sdk/lib/atl'/header_name),
                                                'generated_sha256': wine.digest(header)}
                family_flags += ['-I'+str(overlay)]
                g['atl_executable_module_fixture'] = adaptations
        if family == 'atl':
            family_flags += ['-DHAVE_APITEST', '-DINITGUID', '-DWINETEST_USE_DBGSTR_LONGLONG']
        if legacy_shell_atl:
            family_flags += ['-DUNICODE', '-D_UNICODE', '-D__analysis_noreturn=']
            g['shell_character_configuration'] = 'Original shell32_apitest CMake UNICODE/_UNICODE definitions'
            header = source/'sdk/include/psdk/shlobj.h'
            definitions = []
            for symbol in ('CFSTR_SHELLIDLISTW', 'CFSTR_SHELLIDLISTOFFSETW'):
                matches = re.findall(r'^#\s*define '+symbol+r'\s+L"[^"\n]*"$', header.read_text(), re.M)
                if len(matches) != 1:
                    raise RuntimeError('Pinned shell clipboard constant changed: '+symbol)
                definitions.append('#ifndef '+symbol+'\n'+matches[0]+'\n#endif\n')
            generated = include/'campaign_shell_clipboard.h'
            generated.write_text(''.join(definitions))
            family_flags += ['-include', str(generated)]
            g['shell_clipboard_constants'] = {'source_sha256': wine.digest(header),
                                               'generated_sha256': wine.digest(generated)}
            if src.stem == 'menu':
                header = source/'sdk/include/psdk/shlguid.h'
                definitions = re.findall(r'^DEFINE_GUID\(CLSID_MenuDeskBar,[^\n]+$', header.read_text(), re.M)
                if len(definitions) != 1:
                    raise RuntimeError('Pinned menu GUIDs changed')
                generated = include/'campaign_menu_guid.h'
                generated.write_text(local_guid_header(definitions, ['CLSID_MenuBand']))
                family_flags += ['-include', str(generated), '-Wno-c++11-narrowing']
                g['menu_guid'] = {'source_sha256': wine.digest(header), 'generated_sha256': wine.digest(generated)}
            if src.stem == 'IShellFolderViewCB':
                header = source/'sdk/include/reactos/shlguid_undoc.h'
                definitions = re.findall(r'^DEFINE_GUID\(IID_IShellFolderView,[^\n]+$', header.read_text(), re.M)
                callback_header = source/'sdk/include/psdk/shlguid.h'
                callback = re.findall(r'^DEFINE_GUID\(IID_IShellFolderViewCB,[^\n]+$', callback_header.read_text(), re.M)
                if len(definitions) != 1 or len(callback) != 1:
                    raise RuntimeError('Pinned shell-folder-view GUID changed')
                generated = include/'campaign_shell_view_guid.h'
                generated.write_text(local_guid_header(definitions, ['IID_IShellFolderViewCB']))
                family_flags += ['-include', str(generated)]
                g['shell_view_guid'] = {'source_sha256': wine.digest(header),
                    'callback_source_sha256': wine.digest(callback_header), 'generated_sha256': wine.digest(generated)}
        if name == 'ros_gdi32_StretchBlt':
            family_flags += ['-D__analysis_noreturn=']  # SAL-only annotation in debug.h.
        if name == 'ros_compiler_pseh':
            family_flags += ['-include', 'winternl.h']
        if family == 'imm32':
            family_flags += ['-include', 'windows.h', '-include', 'imm.h']
        if name in ('ros_imm32_imcc', 'ros_imm32_himc'):
            family_flags += ['-include', 'stddef.h', '-include', str(source/'sdk/include/ddk/immdev.h')]
        if name == 'ros_imm32_ImmIsUIMessage':
            header = source/'sdk/include/reactos/undocuser.h'
            definitions = re.findall(r'^#define WM_IME_SYSTEM[^\n]+$', header.read_text(), re.M)
            if len(definitions) != 1: raise RuntimeError('Pinned WM_IME_SYSTEM constant changed')
            generated = include/'campaign_ime_constants.h'
            generated.write_text('#ifndef WM_IME_SYSTEM\n'+definitions[0]+'\n#endif\n')
            family_flags += ['-include', str(generated)]
            g['sdk_flags'] = {'source_sha256': wine.digest(header), 'generated_sha256': wine.digest(generated)}

        if name == 'ros_crt_strlen':
            family_flags += ['-include', 'intrin.h']  # Native x86 EFLAGS intrinsics.
        if name == 'ros_shell32_Control_RunDLLW':
            family_flags += ['-D__analysis_noreturn=']  # SAL annotation only.
        extra_objects = shared.get(family, {}).get('objects', [])
        startup = socket_startup if family == 'ws2_32' else wine.SUPPORT/'startup.c'
        if family in shared: g['shared_inputs'] = shared[family]
        if name == 'ros_compiler_pseh':
            helper = src.parent/'psehtest2.c'
            helper_obj = logs/'psehtest2.o'
            command = [args.cc, *flags, '-c', str(helper), '-o', str(helper_obj)]
            subprocess.run(command, check=True, capture_output=True)
            extra_objects = [*extra_objects, str(helper_obj)]
            g['compiler_helpers'] = {'source_sha256': wine.digest(helper), 'command': command,
                                     'object_sha256': wine.digest(helper_obj)}
        if name == 'ros_kernel32_FormatMessage':
            resources = logs/'format-message-resources'
            resources.mkdir(exist_ok=True)
            message_source = src.with_suffix('.mc')
            resource_commands = [
                ['i686-w64-mingw32-windmc', '-h', str(resources), '-r', str(resources), str(message_source)],
                ['i686-w64-mingw32-windres', '-I'+str(resources), str(resources/'FormatMessage.rc'),
                 '-O', 'coff', '-o', str(resources/'messages.o')]]
            for resource_command in resource_commands:
                subprocess.run(resource_command, cwd=resources, check=True, capture_output=True)
            family_flags += ['-I'+str(resources)]
            extra_objects = [*extra_objects, str(resources/'messages.o')]
            g['message_resources'] = {'source_sha256': wine.digest(message_source),
                'commands': resource_commands,
                'outputs_sha256': {p.name: wine.digest(p) for p in resources.iterdir() if p.is_file()}}
        body, count = re.subn(r'(?m)^[ \t]*#[ \t]*include[ \t]+["<](?:precomp|precompiled)\.h[">]', '#include <campaign_precomp.h>', data)
        if family == 'winspool' and src.stem in ('GetDefaultPrinter', 'GetPrintProcessorDirectory', 'IsValidDevmode'):
            entries = [src.stem+'A', src.stem+'W']
            body = combine_test_entries(body, entries, src.stem)
            g['combined_entries'] = entries
        if name in ('ros_shell32_DragDrop', 'ros_shell32_ItemIDList'):
            operands = ['pidlLast'] if src.stem == 'DragDrop' else ['pidl1Last', 'pidl2Last']
            for operand in operands:
                anchor = ('s_pDesktop->GetUIObjectOf(NULL, ' if src.stem == 'DragDrop' else '')+'1, &'+operand+','
                if body.count(anchor) != 1:
                    raise RuntimeError('Pinned PIDL array argument changed')
                body = body.replace(anchor, anchor.replace('&'+operand, '(LPCITEMIDLIST *)&'+operand))
            g['sdk_const_pointer_arrays'] = operands
            if src.stem == 'ItemIDList':
                entries = ['SHSimpleIDListFromPath', 'ILCreateFromPath', 'PIDL']
                body = combine_test_entries(body, entries, src.stem)
                g['combined_entries'] = entries
        if name == 'ros_shell32_SHAppBarMessage':
            before = 'hThread = CreateThread(NULL, 0, ActionThreadFunc, this, 0, NULL);'
            after = '{ DWORD thread_id; hThread = CreateThread(NULL, 0, ActionThreadFunc, this, 0, &thread_id); }'
            if body.count(before) != 1: raise RuntimeError('Pinned appbar worker creation changed')
            body = body.replace(before, after)
            g['worker_input'] = 'Supply the thread-ID output required by Win98; no CreateThread assertion is changed'
        if name in ('ros_kernel32_MultiByteToWideChar', 'ros_user32_LoadImageGCC'):
            # ReactOS builds expose IsReactOS from their own PSDK header.
            # Keep that original helper, including its Windows-directory
            # test, rather than substituting a hard-coded platform answer.
            helper = source/'sdk/include/psdk/versionhelpers.h'
            header = '#define __REACTOS__\n' + helper.read_text() + '\n#undef __REACTOS__\n'
            (include/'campaign_versionhelpers.h').write_text(header)
            anchor = '#include <versionhelpers.h>'
            if body.count(anchor) != 1: raise RuntimeError('Pinned version helper include changed')
            body = body.replace(anchor, '#include <campaign_versionhelpers.h>')
            g['version_helper'] = {'source_sha256': wine.digest(helper),
                                   'generated_sha256': wine.digest(include/'campaign_versionhelpers.h')}
        if family == 'shlwapi':
            family_flags += ['-include', 'ole2.h']
        if name in ('ros_shell32_She', 'ros_shell32_PathResolve', 'ros_shell32_SHLimitInputEdit'):
            family_flags += ['-include', 'pseh/pseh2.h']
        if name == 'ros_shell32_PathResolve':
            header = source/'sdk/include/psdk/shlobj.h'
            definitions = re.findall(r'^#define (PRF_\w+)[ \t]+([^\n]+)$', header.read_text(), re.M)
            if not definitions: raise RuntimeError('Pinned PathResolve flags missing')
            generated = include/'campaign_pathresolve_flags.h'
            generated.write_text('\n'.join('#ifndef '+key+'\n#define '+key+' '+value+'\n#endif'
                                           for key, value in definitions)+'\n')
            family_flags += ['-include', str(generated)]
            g['sdk_flags'] = {'source_sha256': wine.digest(header), 'generated_sha256': wine.digest(generated)}
        if name == 'ros_comctl32_propsheet':
            # edt1 is a standard dialog ID from the SDK, not a test fixture.
            family_flags += ['-include', 'dlgs.h']
        if name == 'ros_shell32_CommandLineToArgvW':
            # C++ standard headers suppress the Windows min macro. Keep its
            # two integer operands and result; neither operand has side effects.
            anchor = 'min(expected_argc, real_argc)'
            if body.count(anchor) != 1:
                raise RuntimeError('Pinned command-line minimum expression changed')
            body = body.replace(anchor, '(expected_argc < real_argc ? expected_argc : real_argc)')
            g['sdk_min_expression'] = 'Integer Windows min macro expanded; assertions unchanged'
        shell_without_atl = {
            'AddCommas', 'CheckEscapes', 'Control_RunDLLW', 'ExtractIconEx',
            'FindExecutable', 'GUIDFromString', 'Int64ToString', 'OpenAs_RunDLL',
            'PathIsEqualOrSubFolder', 'PathIsTemporary', 'PathResolve',
            'RealShellExecuteEx', 'SHChangeNotify', 'SHParseDisplayName',
            'SHRestricted', 'She', 'ShellExecuteEx', 'ShellExecuteW',
            'SHAppBarMessage', 'SHLimitInputEdit',
            'ShellHook',
        }
        if family == 'shell32' and src.stem in shell_without_atl:
            # This test uses no ATL objects. Its shared header otherwise
            # instantiates an unrelated ATL module requiring newer Windows.
            shell_header = (src.parent/'shelltest.h').read_text()
            for dependency in ('atlbase.h', 'atlcom.h'):
                anchor = '#include <'+dependency+'>'
                if shell_header.count(anchor) != 1: raise RuntimeError('Pinned shell header changed')
                shell_header = shell_header.replace(anchor, '')
            (include/'campaign_shell_functions.h').write_text(shell_header)
            body = re.sub(r'#include ["<]shelltest\.h[">]', '#include <campaign_shell_functions.h>', body)
            g['unused_atl_includes_removed'] = ['atlbase.h', 'atlcom.h']
            g['shell_header_source_sha256'] = wine.digest(src.parent/'shelltest.h')
        if name == 'ros_shell32_AddCommas':
            spec = source/'dll/win32/shell32/shell32.spec'
            if not re.search(r'^203 stdcall -noname AddCommasW\(long wstr\)$', spec.read_text(), re.M):
                raise RuntimeError('Pinned AddCommasW import changed')
            definition = logs/'shell-addcommas.def'
            definition.write_text('LIBRARY SHELL32.DLL\nEXPORTS\nAddCommasW@8 @203 NONAME\n')
            library = logs/'shell-addcommas.a'
            import_command = ['i686-w64-mingw32-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)]
            subprocess.run(import_command, check=True)
            extra_objects = [*extra_objects, str(library)]
            g['pinned_import'] = {'source_sha256': wine.digest(spec), 'command': import_command,
                                  'definition_sha256': wine.digest(definition), 'library_sha256': wine.digest(library)}
        if name == 'ros_shlwapi_SHLoadRegUIString':
            folder = src.parent
            fixture = folder/'shlwapi_resource_dll'
            fixture_obj = logs/'shlwapi-fixture-resources.o'
            fixture_dll = logs/'shlwapi_resource_dll.dll'
            resources_obj = logs/'shlwapi-resources.o'
            helper_obj = logs/'shlwapi-extract.o'
            helper_source = folder/'SHLoadIndirectString.c'
            helper_body = helper_source.read_text()
            function = re.search(r'^BOOL extract_resource\([^\n]+\n\{.*?^\}', helper_body, re.M | re.S)
            if not function: raise RuntimeError('Pinned resource extraction helper changed')
            helper_unit = ported/'shlwapi-extract.c'
            helper_line = helper_body[:function.start()].count('\n') + 1
            helper_unit.write_text('#include <campaign_precomp.h>\n#line '+str(helper_line)+
                ' "SHLoadIndirectString.c"\n'+function.group()+'\n')
            commands = [
                ['i686-w64-mingw32-windres', str(fixture/'shlwapi_resource_dll.rc'), '-O', 'coff', '-o', str(fixture_obj)],
                [args.cc, '-shared', '-nostdlib', '-include', 'windows.h',
                 '-Wl,--entry,_DllMain@12', '-Wl,--no-insert-timestamp',
                 str(fixture/'shlwapi_resource_dll.c'), str(fixture_obj), '-lkernel32', '-o', str(fixture_dll)],
                ['i686-w64-mingw32-windres', '-I'+str(folder), '-I'+str(logs),
                 str(folder/'testdata.rc'), '-O', 'coff', '-o', str(resources_obj)],
                [args.cc, *flags, '-include', 'ole2.h', '-I'+str(api/'include'),
                 '-I'+str(source/'sdk/include/reactos'), '-c', str(helper_unit), '-o', str(helper_obj)]]
            for command in commands:
                result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
                with (logs/'shlwapi-fixture.log').open('a') as log:
                    log.write(result.stdout+result.stderr)
                if result.returncode: raise RuntimeError('Original shlwapi fixture build failed')
            extra_objects = [*extra_objects, str(resources_obj), str(helper_obj)]
            inputs = [fixture/'shlwapi_resource_dll.c', fixture/'shlwapi_resource_dll.rc',
                      folder/'testdata.rc', folder/'SHLoadIndirectString.c', *sorted(folder.glob('*.ico'))]
            g['resource_fixture'] = {'commands': commands, 'helper_translation_sha256': wine.digest(helper_unit),
                'sources': {str(p.relative_to(source)): wine.digest(p) for p in inputs},
                'dll_sha256': wine.digest(fixture_dll)}
        if name in ('ros_shlwapi_SHPropertyBag', 'ros_shlwapi_IShellFolderHelpers',
                    'ros_shlwapi_PathFileExistsDefExtAndAttributesW', 'ros_shell32_ShellState'):
            header = source/'sdk/include/reactos/shlwapi_undoc.h'
            header_body = header.read_text()
            removed = ('SHSendMessageBroadcastW', 'SHAnsiToUnicode')
            for symbol in removed:
                if re.search(r'\b'+symbol+r'\b', body):
                    raise RuntimeError('Test now uses a conflicting SDK declaration')
                header_body, count_removed = re.subn(r'^DWORD WINAPI '+symbol+r'\([^\n]+;(?=\n)', '', header_body, flags=re.M)
                if count_removed != 1: raise RuntimeError('Pinned SDK declaration changed')
            generated = include/'campaign_shlwapi_undoc.h'
            generated.write_text(header_body)
            body = body.replace('#include <shlwapi_undoc.h>', '#include <campaign_shlwapi_undoc.h>')
            g['sdk_conflicts'] = {'unused_declarations_removed': removed,
                'source_sha256': wine.digest(header), 'generated_sha256': wine.digest(generated)}
        if name == 'ros_shell32_ShellState':
            spec = source/'dll/win32/shlwapi/shlwapi.spec'
            if not re.search(r'^491 stdcall -noname SHGetShellKey\(long long long\)$', spec.read_text(), re.M):
                raise RuntimeError('Pinned shell-key ordinal changed')
            definition = logs/'shlwapi-shell-key.def'
            definition.write_text('LIBRARY SHLWAPI.DLL\nEXPORTS\nSHGetShellKey@12 @491 NONAME\n')
            library = definition.with_suffix('.a')
            command = ['i686-w64-mingw32-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)]
            subprocess.run(command, check=True)
            extra_objects = [*extra_objects, str(library)]
            g['pinned_import'] = {'source_sha256': wine.digest(spec), 'command': command,
                'definition_sha256': wine.digest(definition), 'library_sha256': wine.digest(library)}
        if name == 'ros_shlwapi_SHPropertyBag':
            spec = source/'dll/win32/shlwapi/shlwapi.spec'
            exports = []
            for ordinal, symbol, arguments in re.findall(
                    r'^(\d+) stdcall -noname (SH(?:PropertyBag_\w+|CreatePropertyBag\w+))\(([^)]*)\)$',
                    spec.read_text(), re.M):
                size = sum(8 if arg in ('int64', 'double') else 4 for arg in arguments.split())
                exports.append(symbol+'@'+str(size)+' @'+ordinal+' NONAME')
            if not exports: raise RuntimeError('Pinned property-bag imports missing')
            definition = logs/'shlwapi-propertybag.def'
            definition.write_text('LIBRARY SHLWAPI.DLL\nEXPORTS\n'+'\n'.join(exports)+'\n')
            library = definition.with_suffix('.a')
            command = ['i686-w64-mingw32-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)]
            subprocess.run(command, check=True)
            extra_objects = [*extra_objects, str(library)]
            g['pinned_import'] = {'source_sha256': wine.digest(spec), 'command': command,
                'definition_sha256': wine.digest(definition), 'library_sha256': wine.digest(library)}
        if name == 'ros_shlwapi_PathFileExistsDefExtAndAttributesW':
            spec = source/'dll/win32/shlwapi/shlwapi.spec'
            if '511 stdcall -noname PathFileExistsDefExtAndAttributesW(wstr long ptr)' not in spec.read_text().splitlines():
                raise RuntimeError('Pinned path import changed')
            definition = logs/'shlwapi-path.def'
            definition.write_text('LIBRARY SHLWAPI.DLL\nEXPORTS\nPathFileExistsDefExtAndAttributesW@12 @511 NONAME\n')
            library = logs/'shlwapi-path.a'
            command = ['i686-w64-mingw32-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)]
            subprocess.run(command, check=True)
            extra_objects = [*extra_objects, str(library)]
            g['pinned_import'] = {'source_sha256': wine.digest(spec), 'command': command,
                'definition_sha256': wine.digest(definition), 'library_sha256': wine.digest(library)}
        ordinal_helpers = {
            'ros_shell32_Int64ToString': {'Int64ToString': (209, 28), 'LargeIntegerToString': (210, 24)},
            'ros_shell32_PathIsTemporary': {'PathIsTemporaryA': (713, 4), 'PathIsTemporaryW': (714, 4)},
            'ros_shell32_PathIsEqualOrSubFolder': {'PathIsEqualOrSubFolder': (755, 8)},
        }
        if name in ordinal_helpers:
            spec = source/'dll/win32/shell32/shell32.spec'
            exports = []
            for symbol, (ordinal, size) in ordinal_helpers[name].items():
                match = re.search(r'^'+str(ordinal)+r' stdcall -noname '+symbol+r'\(([^)]*)\)$', spec.read_text(), re.M)
                if not match or sum(8 if arg in ('int64', 'double') else 4 for arg in match[1].split()) != size:
                    raise RuntimeError('Pinned shell ordinal signature changed')
                exports.append(symbol+'@'+str(size)+' @'+str(ordinal)+' NONAME')
            definition = logs/(name+'.def')
            definition.write_text('LIBRARY SHELL32.DLL\nEXPORTS\n'+'\n'.join(exports)+'\n')
            library = definition.with_suffix('.a')
            command = ['i686-w64-mingw32-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)]
            subprocess.run(command, check=True)
            extra_objects = [*extra_objects, str(library)]
            g['pinned_import'] = {'source_sha256': wine.digest(spec), 'command': command,
                'definition_sha256': wine.digest(definition), 'library_sha256': wine.digest(library)}
        if name == 'ros_shell32_GUIDFromString':
            spec = source/'dll/win32/shell32/shell32.spec'
            for signature in ('703 stdcall -noname GUIDFromStringA(str ptr)',
                              '704 stdcall -noname GUIDFromStringW(wstr ptr)'):
                if signature not in spec.read_text().splitlines():
                    raise RuntimeError('Pinned GUID import changed')
            definition = logs/'shell-guid.def'
            definition.write_text('LIBRARY SHELL32.DLL\nEXPORTS\nGUIDFromStringA@8 @703 NONAME\nGUIDFromStringW@8 @704 NONAME\n')
            library = logs/'shell-guid.a'
            import_command = ['i686-w64-mingw32-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)]
            subprocess.run(import_command, check=True)
            extra_objects = [*extra_objects, str(library)]
            g['pinned_import'] = {'source_sha256': wine.digest(spec), 'command': import_command,
                'definition_sha256': wine.digest(definition), 'library_sha256': wine.digest(library)}
        if name == 'ros_ws2_32_WSAAsync' and args.winsock_port is not None:
            body, replaced = re.subn(r'(?m)^#define SVR_PORT 5000$',
                                    '#define SVR_PORT '+str(args.winsock_port), body)
            if replaced != 1:
                raise RuntimeError('Pinned WSAAsync port declaration changed')
            g['network_fixture'] = {'original_port': 5000, 'port': args.winsock_port,
                'purpose': 'Identical loopback endpoint on Win98 and NEO98; avoid a host service collision'}
        # These CRT tests include NDK declarations without using an NT API.
        # Avoid importing a second Windows type universe into the MinGW SDK;
        # keep every test function and assertion byte-for-byte unchanged.
        if name in {'ros_crt__mbsncmp', 'ros_crt__mbsstr', 'ros_crt__vsnprintf',
                    'ros_crt__vsnwprintf', 'ros_crt_sprintf'}:
            if re.search(r'\b(?:Nt|Zw|Rtl)[A-Za-z0-9_]+', body):
                raise RuntimeError('Reviewed CRT source gained an NT dependency: '+name)
            body, removed = re.subn(r'(?m)^#include <ndk/(?:mmfuncs|rtlfuncs)\.h>$',
                                   '#include <winternl.h> /* SDK NTSTATUS for SEH assertions. */', body)
            g['unused_ndk_includes_removed'] = removed
        unit = ported/(name+src.suffix)
        unit.write_bytes(('#line 1 "'+src.name+'"\n'+body).encode('latin1'))
        g['pch_replacements'] = count
        g['translation_unit_sha256'] = wine.digest(unit)
        libraries = ['uxtheme', 'msvcrt-os', 'kernel32', 'user32', 'gdi32', 'advapi32', 'ole32', 'oleaut32', 'shell32',
                     'shlwapi', 'comctl32', 'comdlg32', 'ws2_32', 'iphlpapi', 'wininet', 'winmm', 'version', 'uuid', 'ntdll',
                     'imm32', 'opengl32', 'setupapi', 'msimg32', 'mingw32', 'mingwex', 'gcc']
        if family == 'winspool':
            libraries.insert(0, 'winspool')
        command = [args.cc, *flags, *family_flags, '-I'+str(src.parent), '-I'+str(api/'include'), '-I'+str(source/'sdk/include/reactos'),
                   '-DSTANDALONE', str(startup), str(unit),
                   *extra_objects, '-o', str(out/'bin'/g['exe']), '-Wl,--start-group', *['-l'+x for x in libraries], '-Wl,--end-group']
        compiler_output = ''
        if src.suffix == '.cpp':
            cppflags = [f for f in flags if f != '-nostdlib' and not f.startswith(('-std=', '-Wl,--entry,'))]
            command = ['i686-w64-mingw32-g++', *cppflags, '-include', str(wine.SUPPORT/'cpp_sdk.h'),
                *family_flags, '-std=gnu++17', '-march=i586',
                '-static-libstdc++', '-static-libgcc', '-I'+str(src.parent), '-I'+str(api/'include'),
                '-I'+str(source/'sdk/lib/atl'), '-I'+str(source/'sdk/include/reactos'),
                '-I'+str(source/'sdk/lib/pseh/include'),
                '-I'+str(source/'sdk/include/ndk'), '-idirafter', str(source/'sdk/include'),
                '-idirafter', str(source/'sdk/include/psdk'), '-DSTANDALONE',
                str(wine.SUPPORT/'cpp_log.cpp'), str(unit), *extra_objects, '-o', str(out/'bin'/g['exe']),
                '-Wl,--start-group', *['-l'+x for x in libraries], '-Wl,--end-group']
            g['startup'] = 'MinGW C++ CRT: native-reference qualification required'
            g['cpp_log_sha256'] = wine.digest(wine.SUPPORT/'cpp_log.cpp')
            if args.seh_cpp:
                sysroot = Path(subprocess.check_output([args.cc, '-print-sysroot'], text=True).strip())
                cpp_version = subprocess.check_output(['i686-w64-mingw32-g++', '-dumpfullversion'], text=True).strip()
                cpp_include = sysroot/'i686-w64-mingw32/include/c++'/cpp_version
                compile_flags = command[1:command.index('-DSTANDALONE')+1]
                compile_flags = [f for f in compile_flags if not f.startswith('-Wl,') and
                                 f not in ('-static-libstdc++', '-static-libgcc')]
                compile_flags = [str(include/'campaign_status.h') if f == 'ntstatus.h' else f
                                 for f in compile_flags]
                compile_flags += ['-fms-extensions', '-D_USE_NATIVE_SEH', '-D__USE_PSEH2__',
                                  '-include', str(include/'campaign_status.h')]
                g['cpp_seh_compile_commands'] = []
                if legacy_cpp:
                    compile_flags += ['-fno-cxx-exceptions']
                    g['legacy_cpp_codegen'] = 'No C++ throw/catch or RTTI operators; retain native MSVC SEH, use unsized Win98 delete'
                for index, source_file in enumerate((wine.SUPPORT/'cpp_log.cpp', unit)):
                    obj = logs/(name+'-cpp'+str(index)+'.o')
                    compile_command = [args.seh_cc+'++', '--target=i686-pc-windows-msvc',
                                       '-D__MINGW32__', '-D__i386__', '-D_X86_', '-fgnuc-version=4.2.1',
                                       '-isystem', str(cpp_include), '-isystem', str(cpp_include/'i686-w64-mingw32'),
                                       '-isystem', str(sysroot/'i686-w64-mingw32/include'),
                                       *compile_flags, '-c', str(source_file), '-o', str(obj)]
                    if index == 0:
                        # The logging constructor uses the same C++ ABI as
                        # the linked MinGW CRT. The test itself needs native
                        # MSVC SEH; its C main entry keeps the startup ABI.
                        # The logger cannot throw and needs no C++ unwinder.
                        # Test-file exception support remains enabled.
                        compile_command = [args.seh_cc+'++', '--target=i686-w64-windows-gnu',
                            '--sysroot='+str(sysroot), '-fno-exceptions', *compile_flags,
                            '-UINITGUID', '-c', str(source_file), '-o', str(obj)]
                    g['cpp_seh_compile_commands'].append(compile_command)
                    result = subprocess.run(compile_command, capture_output=True, text=True)
                    compiler_output += result.stdout+result.stderr
                    if result.returncode: break
                    campaign.prepare_seh_object(obj)
                    command[command.index(str(source_file))] = str(obj)
                if not result.returncode:
                    result = subprocess.run(command, capture_output=True, text=True)
                g['compiler'] = subprocess.check_output([args.seh_cc+'++', '--version'], text=True).splitlines()[0]
            else:
                result = subprocess.run(command, capture_output=True, text=True)
        elif args.retry_failed:
            sysroot = Path(subprocess.check_output([args.cc, '-print-sysroot'], text=True).strip())
            obj = logs/(name+'.o')
            compile_flags = [f for f in flags+family_flags if not f.startswith(('-Wl,', '-std=')) and f != '-nostdlib']
            compile_flags = [str(include/'campaign_status.h') if f == 'ntstatus.h' else f for f in compile_flags]
            compile_command = [args.seh_cc, '--target=i686-pc-windows-msvc',
                '-D__MINGW32__', '-D__i386__', '-D_X86_', '-fgnuc-version=4.2.1', '-fno-stack-protector',
                '-isystem', str(sysroot/'i686-w64-mingw32/include'),
                *compile_flags, '-std=gnu99', '-march=i586', '-mno-sse', '-mno-sse2', '-fms-extensions', '-D_USE_NATIVE_SEH', '-D__USE_PSEH2__',
                '-include', 'assert.h', '-I'+str(src.parent), '-I'+str(api/'include'),
                '-I'+str(source/'sdk/include/reactos'), '-I'+str(source/'sdk/lib/pseh/include'),
                '-I'+str(source/'sdk/include/ndk'), '-idirafter', str(source/'sdk/include'),
                '-idirafter', str(source/'sdk/include/ddk'),
                '-idirafter', str(source/'sdk/include/psdk'), '-DSTANDALONE', '-c', str(unit), '-o', str(obj)]
            if sdk:
                sdk_folder, sdk_metadata = sdk
                # A duplicate -idirafter overrides -I in Clang's search path.
                index = compile_command.index(str(source/'sdk/include/psdk'))
                del compile_command[index-1:index+1]
                compile_command[1:1] = ['-I'+str(sdk_folder), '-I'+str(source/'sdk/include/psdk'),
                    '-DWIN32_LEAN_AND_MEAN', '-include', 'windows.h']
                # Define base Windows types before winsock2's pack(4) region;
                # otherwise the NDK's original PEB layout assertions fail.
                g['pinned_sdk'] = sdk_metadata
            result = subprocess.run(compile_command, capture_output=True, text=True)
            compiler_output = result.stdout+result.stderr
            g['compile_command'] = compile_command
            g['compiler'] = subprocess.check_output([args.seh_cc, '--version'], text=True).splitlines()[0]
            if name == 'ros_compiler_ms_seh' and not result.returncode:
                helper_folder = src.parent/'ms/seh'
                helpers = []
                for number in range(1, 59):
                    helper = helper_folder/('seh'+str(number).zfill(4)+'.c')
                    helper_obj = logs/(helper.stem+'.o')
                    definitions = ['main='+helper.stem] + [symbol+'='+symbol+'_'+str(number)
                        for symbol in ('test', 'Echo', 'dojump', 'AccessViolation',
                                       'rtlRaiseExceptin', 'rtlRaiseException', 'rtlRaiseStatus')]
                    helper_command = [arg for arg in compile_command if arg != '-DSTANDALONE']
                    helper_command[helper_command.index(str(unit))] = str(helper)
                    helper_command[-1] = str(helper_obj)
                    helper_command[1:1] = ['-D'+definition for definition in definitions]
                    result = subprocess.run(helper_command, capture_output=True, text=True)
                    compiler_output += result.stdout+result.stderr
                    entry = {'source': str(helper.relative_to(source)), 'source_sha256': wine.digest(helper),
                             'command': helper_command}
                    helpers.append(entry)
                    if result.returncode: break
                    campaign.prepare_seh_object(helper_obj)
                    entry['object_sha256'] = wine.digest(helper_obj)
                    extra_objects = [*extra_objects, str(helper_obj)]
                g['compiler_helpers'] = {'units': helpers,
                    'cmake_sha256': wine.digest(helper_folder/'CMakeLists.txt'),
                    'header_sha256': wine.digest(helper_folder/'seh.h')}
            command = [args.cc, *flags, str(startup), str(obj), *extra_objects, '-o', str(out/'bin'/g['exe']),
                       '-Wl,--start-group', *['-l'+x for x in libraries], '-Wl,--end-group']
            if not result.returncode:
                campaign.prepare_seh_object(obj)
                g['seh_link_adaptation'] = 'Discard .sxdata SafeSEH linker metadata before GNU ld; retain .xdata scope tables'
                result = subprocess.run(command, capture_output=True, text=True)
            if (logs/(name+'.log')).exists():
                shutil.copy2(logs/(name+'.log'), logs/(name+'.gcc.log'))
        else:
            result = subprocess.run(command, capture_output=True, text=True)
        (logs/(name+'.log')).write_text(compiler_output+result.stdout+result.stderr)
        g['build_command'] = command
        if result.returncode or shared_errors.get(family):
            g['errors'] = shared_errors.get(family) or ['compiler/linker: see build log']
        else:
            g.update(build_status='BUILT', sha256=wine.digest(out/'bin'/g['exe']), **wine.pe_details(out/'bin'/g['exe']))
            modern_crt = [name for name in g['imports'] if name.lower().startswith(('api-ms-win-crt-', 'ucrtbase', 'libwinpthread'))]
            if modern_crt:
                g['toolchain_dependencies'] = modern_crt
                g['qualification_note'] = 'Modern compiler runtime dependencies: a failed Win98 load does not establish that the tested API is unavailable'
        print(name, g['build_status'], flush=True)
        # A stopped retry must retain the not-yet-visited catalog entries.
        visited = {entry['name'] for entry in groups}
        checkpoint = groups + [entry for old_name, entry in previous.items() if old_name not in visited]
        wine.save(out/'catalog.json', {'source_pin': pin, 'groups': checkpoint})
    assets = {}
    for name in ('verclasstest1.manifest', 'verclasstest2.manifest'):
        shutil.copy2(api/'user32'/name, out/'bin'/name)
        assets[name] = wine.digest(out/'bin'/name)
    save_build(out, pin, groups, flags, args.cc, assets)


def sdl12(args):
    out = args.out.resolve()
    pin, source = fetch(out, 'sdl12')
    objects = out/'objects'
    if objects.exists(): shutil.rmtree(objects)
    objects.mkdir()
    cflags = '-O1 -fno-builtin -D__MSVCRT_VERSION__=0x600 -D__USE_MINGW_ANSI_STDIO=0'
    configure = ['sh', str(source/'configure'), '--host=i686-w64-mingw32', '--disable-shared', '--enable-static',
                 '--disable-assembly', 'CFLAGS='+cflags]
    for name, command in [('configure', configure), ('make', ['make', '-j4'])]:
        with (out/(name+'.log')).open('w') as log:
            subprocess.run(command, cwd=objects, stdout=log, stderr=subprocess.STDOUT, check=True)
    (out/'include').mkdir(exist_ok=True)
    flags = flags_for(out)
    (out/'bin').mkdir(exist_ok=True)
    logs = out/'build-logs'; logs.mkdir(exist_ok=True)
    bridge = out/'include/sdl-entry.c'
    bridge.write_text('''#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
extern int SDL_main(int, char **);
extern void SDL_SetModuleHandle(void *);
int atexit(void (*callback)(void)) {
    typedef _onexit_t (__cdecl *NativeOnExit)(_onexit_t);
    NativeOnExit register_exit = (NativeOnExit)GetProcAddress(GetModuleHandleA("MSVCRT.DLL"), "_onexit");
    return register_exit && register_exit((_onexit_t)callback) ? 0 : -1;
}
static void ended(void) { printf("NEO98_WINE_END sdl_atexit\\n"); fflush(stdout); }
int main(int argc, char **argv) {
    _dup2(_fileno(stdout), _fileno(stderr));
    setvbuf(stderr, NULL, _IONBF, 0);
    if (atexit(ended)) { fprintf(stderr, "SDL bridge: MSVCRT _onexit unavailable\\n"); return 250; }
    SDL_SetModuleHandle(GetModuleHandleA(NULL));
    exit(SDL_main(argc, argv));
}
''')
    test = source/'test'
    assets = {}
    for name in ('icon.bmp', 'moose.dat', 'picture.xbm', 'sail.bmp', 'sample.bmp', 'sample.wav', 'utf8.txt'):
        shutil.copy2(test/name, out/'bin'/name)
        assets[name] = wine.digest(test/name)
    targets = re.search(r'^TARGETS = (.*)$', (test/'Makefile.in').read_text(), re.M)[1].split()
    automatic = {'testblitspeed', 'testerror', 'testfile', 'testhread', 'testiconv', 'testkeys', 'testloadso',
                 'testplatform', 'testsem', 'testtimer', 'testver', 'testvidinfo', 'torturethread'}
    arguments = {'testloadso': 'MSVCRT.DLL strlen', 'testsem': '1', 'testblitspeed': '--seconds 1'}
    groups = []
    for target in targets:
        stem = target.replace('$(EXE)', '')
        src = test/(stem+'.c')
        g = {'name': 'sdl12_'+stem, 'family': 'sdl12', 'exe': 'S%06d.EXE' % len(groups),
             'source': 'test/'+src.name, 'source_sha256': wine.digest(src),
             'timeout_ms': args.timeout*1000 if stem in automatic else 10000,
             'arguments': arguments.get(stem, ''), 'assessment': 'EXIT_SMOKE' if stem in automatic else 'INTERACTIVE_REVIEW',
             'build_status': 'BUILD_ERROR'}
        groups.append(g)
        command = [args.cc, *flags, '-I'+str(objects/'include'), '-I'+str(source/'include'), '-I'+str(test),
            str(wine.SUPPORT/'startup.c'), str(bridge), str(src), '-o', str(out/'bin'/g['exe']), '-Wl,--start-group',
            str(objects/'build/.libs/libSDL.a'), '-lmsvcrt-os', '-lkernel32', '-luser32', '-lgdi32', '-lwinmm',
            '-ladvapi32', '-ldxguid', '-lopengl32', '-lglu32', '-lmingw32', '-lmingwex', '-lgcc', '-Wl,--end-group']
        result = subprocess.run(command, capture_output=True, text=True)
        (logs/(g['name']+'.log')).write_text(result.stdout+result.stderr)
        g['build_command'] = command
        if result.returncode:
            g['errors'] = ['compiler/linker: see build log']
        else:
            g.update(build_status='BUILT', sha256=wine.digest(out/'bin'/g['exe']), **wine.pe_details(out/'bin'/g['exe']))
        print(g['name'], g['build_status'], flush=True)
    save_build(out, pin, groups, flags, args.cc, assets, {
        'configure_command': configure, 'sdl_static_library_sha256': wine.digest(objects/'build/.libs/libSDL.a'),
        'sdl_config_sha256': wine.digest(objects/'include/SDL_config.h'),
        'sdl_entry_sha256': wine.digest(bridge)})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', choices=['reactos', 'sdl12'])
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--cc', default='i686-w64-mingw32-gcc')
    p.add_argument('--seh-cc', default='clang', help='Clang with native MSVC x86 SEH code generation')
    p.add_argument('--seh-cpp', action='store_true', help='Compile failed C++ tests with Clang native x86 SEH and link the MinGW C++ CRT')
    p.add_argument('--pinned-sdk', action='store_true', help='Use generated ReactOS PSDK/NDK declarations for failed C builds')
    p.add_argument('--flush-checks', action='store_true', help='Persist successful assertions too, for crash diagnosis')
    p.add_argument('--socket-loopback', action='store_true', help='Use a paired in-guest HTTP input for WSARecv/ioctlsocket')
    p.add_argument('--timeout', type=int, default=60)
    p.add_argument('--retry-failed', action='store_true', help='Retry ReactOS C tests with Clang native x86 SEH and the pinned SDK headers')
    p.add_argument('--only-cpp', action='store_true', help='With --retry-failed, leave C test results untouched')
    p.add_argument('--families', help='With --retry-failed, preserve all other family results')
    p.add_argument('--names', help='With --retry-failed, preserve all other test results')
    p.add_argument('--rebuild-selected', action='store_true', help='Rebuild already built files in explicitly selected families')
    p.add_argument('--winsock-port', type=int, help='Explicit paired WSAAsync loopback port fixture')
    args = p.parse_args()
    if args.socket_loopback and (args.source != 'reactos' or not args.retry_failed or not args.rebuild_selected or
            not args.names or set(args.names.split(',')) - {'ros_ws2_32_WSARecv', 'ros_ws2_32_ioctlsocket'}):
        p.error('--socket-loopback requires --retry-failed --rebuild-selected and explicit WSARecv/ioctlsocket names')
    if args.seh_cpp and (not args.retry_failed or args.source != 'reactos'):
        p.error('--seh-cpp requires reactos --retry-failed')
    if args.winsock_port is not None and (not 1024 <= args.winsock_port <= 65535 or
            args.names != 'ros_ws2_32_WSAAsync' or not args.rebuild_selected):
        p.error('--winsock-port requires --rebuild-selected --names ros_ws2_32_WSAAsync and port 1024..65535')
    if args.pinned_sdk and (not args.retry_failed or args.source != 'reactos'):
        p.error('--pinned-sdk requires reactos --retry-failed')
    if args.only_cpp and (not args.retry_failed or args.source != 'reactos'):
        p.error('--only-cpp requires reactos --retry-failed')
    if (args.families or args.names or args.rebuild_selected) and (not args.retry_failed or args.source != 'reactos'):
        p.error('--families/--names/--rebuild-selected require reactos --retry-failed')
    if args.rebuild_selected and not (args.families or args.names):
        p.error('--rebuild-selected requires --families or --names')
    {'reactos': reactos, 'sdl12': sdl12}[args.source](args)


if __name__ == '__main__': main()
