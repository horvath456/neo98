# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Standalone guest imports for the implemented clipboard family."""
IMPORTS={
 'KERNEL32':{'GlobalAlloc':8,'GlobalLock':4,'GlobalUnlock':4,'GlobalFree':4,'GlobalSize':4,'GetLastError':0,'SetLastError':4,'CreateThread':24,'CreateEventA':16,'SetEvent':4,'WaitForSingleObject':8,'CloseHandle':4,'ExitProcess':4,'ExitThread':4,'GetExitCodeThread':8},
 'USER32':{'OpenClipboard':4,'CloseClipboard':0,'EmptyClipboard':0,'GetClipboardData':4,'SetClipboardData':8,'CountClipboardFormats':0,'EnumClipboardFormats':4,'IsClipboardFormatAvailable':4,'GetPriorityClipboardFormat':8,'GetClipboardOwner':0,'GetOpenClipboardWindow':0,'GetClipboardViewer':0,'GetClipboardSequenceNumber':0,'SetClipboardViewer':4,'ChangeClipboardChain':8,'RegisterClipboardFormatA':4,'GetClipboardFormatNameA':12,'GetClipboardFormatNameW':12,'RegisterClassA':4,'CreateWindowExA':48,'DestroyWindow':4,'IsWindow':4,'DefWindowProcA':16,'SendMessageA':16,'GetWindowTextA':12}
}


def test():
    import json,subprocess,sys
    from pathlib import Path
    root=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(root))
    import neo98
    out=root/'build/clipboard-family';out.mkdir(exist_ok=True)
    reports={}
    for name,report in (('clipboard','results.json'),('clipboard_public','public-results.json'),('clipboard_native','native/results.json')):
        result=subprocess.run([str(neo98.analysis_python()),str(root/'tools'/f'test_{name}.py')],capture_output=True,text=True,env=neo98.tool_environment())
        (out/f'component-{name}.log').write_text(result.stdout+result.stderr)
        if result.returncode:print(result.stdout+result.stderr)
        result.check_returncode();reports[name]=json.loads((out/report).read_text());print(name+': PASS',flush=True)
    return reports

if __name__=='__main__':
    import json
    print(json.dumps(test(),indent=2))
