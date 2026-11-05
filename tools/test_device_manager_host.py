#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Device Manager of SYSDM.CPL in the host, without a window.

RUNDLL32.EXE opens System Properties on the page Device Manager over a clone
of setup's C:. The host plays pointer and key input (NEO98_HOST_INPUT) and
writes the rows of the tree view as the control holds them (step trees=1).

1. The names: every device class is opened with the keyboard. The tree then
   names the port's own display adapter and CD-ROM drive
   (src/neo98_hardware.h) and nothing of the machine Win98 was installed on
   (QEMU, Cirrus Logic).
2. The scroll bar of the tree view: the opened tree is longer than its page
   and has the vertical bar. Home, the wheel (a whole notch and three steps
   of a trackpad, a row each), End, the arrow of the bar, a drag of
   its thumb and Page Down move the first visible row as COMCTL32 moves it.
3. The pages around it, which ended the host with "not ported yet": the page
   Settings of the CD-ROM drive (INT 21h AH=52h), a double click in the tree
   (USER.119 GetMessagePos), Copy on Hardware Profiles (INT 21h AX=3305h),
   Virtual Memory and File System on Performance (INT 21h AX=7303h).
4. Display Properties, page Settings: Advanced opens the sheet of the same
   adapter under its name (USER.271 GlobalGetAtomName).
5. The devices as a whole: the sound card, the
   network adapter and the monitor of the port are in the tree, the floppy
   disk drive Setup found is not, and by connection the monitor hangs below
   the display adapter and the drives below their controllers. The sheet of
   every device opens and its pages are walked with Ctrl+Tab and closed with
   Esc: General says the device works, Resources shows the values of
   src/neo98_hardware.h and no conflict. "Driver File Details" names files
   the folder has. Computer Properties lists every interrupt request, I/O
   range, DMA channel and memory range once and none of an unknown device.
6. Remove: the sound card goes from the tree, the sheet says Close, and a
   second start over the same journal has the card again.
   Disable: "Disable in this hardware profile" on the page General marks
   the sound card, COM1, the network adapter, the keyboard and the mouse
   (the display adapter and the CD-ROM drive have no such box). The next start over the same journal
   shows each as disabled (Code 22) and runs as before: the picture is drawn,
   keys and clicks arrive, the Multimedia and the Network control panel list
   their devices. "Enable Device" and the check box take it back, and a third
   start has them working again.
   Hardware Profiles: Copy (the name typed without a click, the edit control
   has the focus), OK, at the next start two profiles, Rename, Delete, OK,
   at the third start one.
   MIDI: the page MIDI of the Multimedia control panel lists the synthesizer
   under "Single instrument", a click chooses it, OK keeps the choice.
   Print: with the PostScript printer of the CD installed on LPT1, "Print..."
   prints "All devices and system summary"; the pages read back from the
   PDF name every device and the interrupt requests, I/O ranges, memory
   ranges and DMA channels of src/neo98_hardware.h.
7. Elsewhere: the DirectX diagnosis, the Multimedia and the Network control
   panel name the same devices. No text of any of these windows names the
   machine Win98 was installed on.

    build/analysis-venv/bin/python tools/test_device_manager_host.py [--no-build] [--user-clone] [--only PART]

--user-clone runs parts 1, 5 and 7 over a clone of the user's own folder and
journal instead (read only, cloned first), to see what an existing machine
shows. --only names, tree, pages, display, sheets, computer, remove, disable,
profiles, midi, print or elsewhere runs one part. Results in build/device-manager-host/results.json.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import hardware_setup  # noqa: E402
import win98_c  # noqa: E402

COMMAND = 'RUNDLL32.EXE shell32.dll,Control_RunDLL sysdm.cpl,,1'


def host(binary, out, name, program, command, steps, deadline, tree, journal=None, drives=(), prepare=None, extra=(),
         keep=False, environment=None):
    """One run of neo98-host over a clone of `tree`: (exit code, log text, run folder).
    `prepare(run)` works on the clone run/c and its journal run/c.registry before the start;
    `extra` are more arguments of the host, "{out}" the run folder. With `keep` the clone
    stays in run/c for the caller to read and to remove."""
    run = out / name
    shutil.rmtree(run, ignore_errors=True)
    run.mkdir(parents=True)
    subprocess.run(['cp', '-c', '-R', str(Path(tree).resolve()), str(run / 'c')], check=True)
    if journal and Path(journal).is_file():
        shutil.copy(journal, run / 'c.registry')
    if prepare:
        prepare(run)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE=str(deadline),
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(run)),
               NEO98_SETTINGS_DIR=str(run / 'settings'), NEO98_LOG_DIR=str(run / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS', 'NEO98_TRACE_CALLS_FROM', 'NEO98_CPU'):
        env.pop(key, None)
    # The test camera of the host, none unless a run asks for it: the tree
    # does not depend on whether this Mac has a camera.
    env['NEO98_TEST_CAMERA'] = 'none'
    if environment:
        env.update(environment)
    line = [str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', program, '--command', command, '--quiet',
            '--system-tree', str(run / 'c'), '--drive-c', str(run / 'c'), '--registry', str(run / 'c.registry'),
            '--crash-report-dir', str(run / 'reports')]
    for drive in drives:
        line += ['--drive-readonly', drive]
    line += [argument.replace('{out}', str(run)) for argument in extra]
    import arm64_run
    arm64_run.ensure_idle()
    with (run / 'log.txt').open('wb') as log:
        process = subprocess.Popen(line, cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)
        try:
            code = process.wait(deadline + 120)
        except subprocess.TimeoutExpired:
            arm64_run.end(process)
            code = 'timeout'
    text = (run / 'log.txt').read_text(errors='replace')
    if not keep:
        shutil.rmtree(run / 'c', ignore_errors=True)
    return code, text, run


def failures(text):
    """Lines that say the host gave up or met a call it does not carry.
    CONFIGMG answers an unknown function with CR_FAILURE and goes on."""
    return [line for line in text.splitlines()
            if ('NEO98 HOST failed' in line or 'not ported yet' in line or 'no native provider' in line)
            and 'answering CR_FAILURE' not in line]


def trees(text):
    """The dumps of trees=1 in order: [{'rows', 'first', 'page', 'bar', 'items': [(row, depth, selected, text)]}]."""
    dumps = []
    for line in text.splitlines():
        match = re.match(r'NEO98 HOST tree hwnd=[0-9a-f]+ rows=(\d+) first=(\d+) page=(-?\d+) bar=(\d)$', line)
        if match:
            dumps.append({'rows': int(match[1]), 'first': int(match[2]), 'page': int(match[3]), 'bar': match[4] == '1',
                          'items': []})
            continue
        match = re.match(r'NEO98 HOST tree row=(\d+) depth=(\d+) selected=(\d) text=(.*)$', line)
        if match and dumps:
            dumps[-1]['items'].append((int(match[1]), int(match[2]), match[3] == '1', match[4]))
    return dumps


class Script:
    """Steps at rising times."""

    def __init__(self, start=3.0):
        self.steps, self.time = ['0:wait=|System Properties'], start

    def add(self, action, pause=0.4):
        self.steps.append(f'{self.time:.1f}:{action}')
        self.time += pause
        return self

    def expand_all(self):
        # Down, Right over every row: Right opens a closed class.
        self.add('click=155,157')
        for _ in range(45):
            self.add('key=28', 0.15).add('key=27', 0.15)
        return self


def names_script():
    script = Script().expand_all()
    script.add('trees=1').add('shot={out}/tree.ppm')
    return script


# What no window may say: the machine Win98 was installed on.
STALE = re.compile('qemu|cirrus|seabios|red hat|pcnet|bochs|virtio|unknown monitor', re.I)
KINDS = {'Q': 'Interrupt Request', 'D': 'Direct Memory Access', 'P': 'Input/Output Range', 'M': 'Memory Range'}
# The devnode of a row of the tree, for the rows whose resources are compared.
DEVNODES = {'NEO98 Sound Card': 'ROOT\\*NEO9810\\0000', 'NEO98 Ethernet Adapter': 'ROOT\\NET\\0001',
            'NEO98 Display Adapter': 'ROOT\\*NEO9800\\0000', 'Communications Port (COM1)': 'ROOT\\*PNP0500\\0000',
            'Printer Port (LPT1)': 'ROOT\\*PNP0400\\0000', 'System timer': 'ROOT\\*PNP0100\\0000',
            'System board': 'ROOT\\*PNP0C01\\0000', 'Direct memory access controller': 'ROOT\\*PNP0200\\0000',
            'Programmable interrupt controller': 'ROOT\\*PNP0000\\0000', 'Standard PS/2 Port Mouse': 'ROOT\\*PNP0F0E\\0000',
            'System CMOS/real time clock': 'ROOT\\*PNP0B00\\0000', 'System speaker': 'ROOT\\*PNP0800\\0000',
            'Numeric data processor': 'ROOT\\*PNP0C04\\0000',
            'Standard 101/102-Key or Microsoft Natural Keyboard': 'ROOT\\*PNP0303\\0000'}


def resources():
    """Neo98Hardware::DeviceResources of src/neo98_hardware.h: {devnode: [(kind, first, last)]}."""
    text = (ROOT / 'src/neo98_hardware.h').read_text()
    out = {}
    for device, body in re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*\{((?:\s*\{[^{}]*\},?)+)\}\}', text):
        out[device.replace('\\\\', '\\')] = [(kind, int(first, 0), int(last, 0)) for kind, first, last in
                                              re.findall(r"\{'(\w)',\s*(\w+),\s*(\w+)\}", body)]
    return out


def setting(kind, first, last):
    """A resource as the Device Manager writes it."""
    if kind in 'QD':
        return f'{first:02d}'
    return f'{first:04X} - {last:04X}' if kind == 'P' else f'{first:08X} - {last:08X}'


def dumps(text):
    """The dumps of tops=2 and lists=1 in order. A dump is {'windows': [{'title', 'pages', 'texts'}] of the
    visible top-level windows front to back, 'lists': [[row]] of the visible list views and list boxes};
    a str between them is the title a wait= of the script reached."""
    out, top, rows = [], None, None
    for line in text.splitlines():
        match = re.match(r'NEO98 HOST input wait=\|(.+?) reached', line)
        if match:
            out.append(match[1])
            top = rows = None
            continue
        if line.startswith('NEO98 HOST tops '):
            out.append({'windows': [], 'lists': []})
            top = rows = None
            continue
        last = out[-1] if out and isinstance(out[-1], dict) else None
        if last is None:
            continue
        match = re.match(r'NEO98 HOST (top|child) id=\w+ visible=(\d) .*? class=(\S+) title=(.*)$', line)
        if match and match[1] == 'top':
            top = None
            if match[2] == '1':
                top = {'title': match[4], 'pages': [], 'texts': []}
                last['windows'].append(top)
        elif match and top is not None and match[2] == '1' and match[4]:
            (top['pages'] if match[3] == '#32770' else top['texts']).append(match[4])
        match = re.match(r'NEO98 HOST list(?:box)? hwnd=\w+ rows=\d+ .*visible=(\d)$', line)
        if match:
            rows = None
            if match[1] == '1':
                rows = []
                last['lists'].append(rows)
        match = re.match(r'NEO98 HOST list(?:box)? row=\d+ text=(.*)$', line)
        if match and rows is not None:
            rows.append(match[1])
    return out


def said(text):
    """Every text a window showed in the dumps of a log, for the search for the installation machine."""
    lines = []
    for line in text.splitlines():
        match = re.match(r'NEO98 HOST (?:top|child) id=\w+ visible=1 .*? title=(.+)$', line) or \
            re.match(r'NEO98 HOST (?:list|listbox|tree) row=\d+ (?:depth=\d+ selected=\d )?text=(.+)$', line)
        if match:
            lines.append(match[1])
    return lines


def check_names(dump, check, label):
    hardware = hardware_setup.names()
    texts = [text for _row, _depth, _selected, text in dump['items']]
    check(f'{label}: the tree is filled', len(texts) > 30 and texts[0] == 'Computer')
    for part, name in (('display adapter', 'DisplayName'), ('CD-ROM drive', 'CdName'), ('sound card', 'SoundName'),
                       ('network adapter', 'NetName'), ('monitor', 'MonitorName')):
        check(f'{label}: the {part} is {hardware[name]}', hardware[name] in texts)
    check(f'{label}: the sound card stands under its class',
          'Sound, video and game controllers' in texts
          and texts[texts.index('Sound, video and game controllers') + 1:][:1] == [hardware['SoundName']])
    check(f'{label}: no floppy disk drive', not [text for text in texts if 'floppy' in text.lower()])
    stale = [text for text in texts if STALE.search(text)]
    check(f'{label}: no device of the installation machine', not stale)
    return texts


def sheet_steps(script, pages=4):
    """The open sheet page by page with Ctrl+Tab, a dump of each, then Esc."""
    for _ in range(pages):
        script.add('tops=2', 0.2).add('lists=1', 0.3)
        script.add('keydown=a2', 0.1).add('key=09', 0.1).add('keyup=a2', 0.6)
    script.add('key=1b', 1.0)


def select(script, row):
    """The row of the opened tree by the keyboard: Home and Down."""
    script.add('click=300,157', 0.3).add('key=24', 0.2)
    for _ in range(row):
        script.add('key=28', 0.12)


def sheets(binary, out, tree, journal, devices, check, report, label, environment=None):
    """Part 5: every device's sheet, page by page. `devices` is [(row, name)]."""
    hardware, table = hardware_setup.names(), resources()
    script = Script().expand_all()
    for row, name in devices:
        select(script, row)
        script.add('click=133,431', 0.5).add(f'wait=|{name} Properties', 1.0)
        sheet_steps(script)
    script.add('tops=1')
    code, text, run = host(binary, out, 'sheets', 'C:\\WINDOWS\\RUNDLL32.EXE', COMMAND, script.steps,
                           int(script.time) + 60, tree, journal, environment=environment)
    seen, name = {}, None
    for entry in dumps(text):
        if isinstance(entry, str):
            name = entry[:-len(' Properties')] if entry.endswith(' Properties') and entry != 'System Properties' else None
            if name:
                seen.setdefault(name, {'pages': [], 'texts': [], 'lists': []})
            continue
        front = entry['windows'][0] if entry['windows'] else None
        if name and front and front['title'] == f'{name} Properties':
            record = seen[name]
            record['pages'] += [page for page in front['pages'] if page not in record['pages']]
            record['texts'] += front['texts']
            record['lists'] += [rows for rows in entry['lists'] if rows not in record['lists']]
    report[f'{label}sheets'] = {name: record['pages'] for name, record in seen.items()}
    names = [name for _row, name in devices]
    missing = [name for name in names if name not in seen]
    check(f'{label}the sheet of every device opened ({len(names)})', not missing)
    if missing:
        print('    missing:', missing)
    check(f'{label}every sheet has General and Driver, reached with Ctrl+Tab',
          seen and all({'General', 'Driver'} <= set(record['pages']) for record in seen.values()))
    broken = [name for name, record in seen.items() if 'This device is working properly.' not in record['texts']]
    check(f'{label}every device is working properly', seen and not broken)
    if broken:
        print('    not working:', broken)
    wrong = []
    for name, record in seen.items():
        expected = table.get(DEVNODES.get(name, ''))
        if expected is None:
            continue
        # The page has its own order of the kinds.
        rows = sorted(f'{KINDS[kind]} | {setting(kind, first, last)}' for kind, first, last in expected)
        if 'Resources' not in record['pages'] or rows not in [sorted(shown) for shown in record['lists']] \
                or 'No conflicts.' not in record['texts']:
            wrong.append(name)
    compared = [name for name in seen if DEVNODES.get(name, '') in table]
    check(f'{label}Resources shows the values of src/neo98_hardware.h without conflict ({len(compared)} devices)',
          len(compared) >= 10 and not wrong)
    if wrong:
        print('    wrong:', wrong)
    without = [name for name in (hardware['MonitorName'], hardware['CdName']) if name in seen]
    check(f'{label}a monitor and a drive have no page Resources',
          without and not [name for name in without if 'Resources' in seen[name]['pages']])
    final = [entry for entry in dumps(text) if isinstance(entry, dict)][-1:]
    check(f'{label}Esc closed every sheet, System Properties is in front at the end',
          final and final[0]['windows'] and final[0]['windows'][0]['title'] == 'System Properties')
    check(f'{label}no call the host does not carry (sheets)', not failures(text))
    stale = [line for line in said(text) if STALE.search(line)]
    check(f'{label}no sheet names the installation machine', not stale)
    return seen


def driver_files(binary, out, tree, journal, devices, pages, check, label, folder):
    """Driver File Details of some devices: the files are in the folder."""
    script = Script().expand_all()
    for row, name in devices:
        select(script, row)
        script.add('click=133,431', 0.5).add(f'wait=|{name} Properties', 1.0)
        script.add('keydown=a2', 0.1)
        for _ in range(pages[name].index('Driver')):
            script.add('key=09', 0.3)
        script.add('keyup=a2', 0.5).add('click=247,483', 0.5).add('wait=|Driver File Details', 0.8)
        script.add('trees=1', 0.3).add('key=1b', 0.8).add('key=1b', 1.0)
    code, text, run = host(binary, out, 'files', 'C:\\WINDOWS\\RUNDLL32.EXE', COMMAND, script.steps,
                           int(script.time) + 40, tree, journal)
    listed = [[item[3] for item in dump['items']] for dump in trees(text) if dump['items'] and dump['items'][0][3] != 'Computer']
    check(f'{label}Driver File Details lists files for {len(devices)} devices', len(listed) == len(devices) and all(listed))
    absent = []
    for files in listed:
        for file in files:
            path = file.split(' (')[0]
            if win98_c.path(path, folder) is None:
                absent.append(file)
    check(f'{label}every driver file named is in the folder', listed and not absent)
    if absent:
        print('    absent:', absent)
    check(f'{label}no call the host does not carry (driver files)', not failures(text))
    return listed


def computer(binary, out, tree, journal, check, report, label):
    """Computer Properties: the four lists of View Resources."""
    table = resources()
    script = Script()
    script.add('click=155,157').add('click=133,431', 0.5).add('wait=|Computer Properties', 1.5)
    for radio in ('127,192', '127,210', '265,192', '265,210'):   # IRQ, I/O, DMA, memory
        script.add(f'click={radio}', 1.2).add('tops=2', 0.2).add('lists=1', 0.4)
    script.add('shot={out}/computer.ppm').add('key=1b', 1.0).add('tops=1')
    code, text, run = host(binary, out, 'computer', 'C:\\WINDOWS\\RUNDLL32.EXE', COMMAND, script.steps,
                           int(script.time) + 20, tree, journal)
    lists = [entry['lists'][0] for entry in dumps(text)
             if isinstance(entry, dict) and entry['lists'] and entry['windows']
             and entry['windows'][0]['title'] == 'Computer Properties']
    report[f'{label}computer'] = lists
    check(f'{label}Computer Properties showed its four lists', len(lists) == 4)
    if len(lists) == 4:
        for kind, rows, title in zip('QPDM', lists, ('interrupt requests', 'I/O ranges', 'DMA channels', 'memory ranges')):
            expected = sorted(setting(k, first, last) for entries in table.values() for k, first, last in entries if k == kind)
            shown = sorted(row.split(' | ')[0] for row in rows)
            # A devnode the folder does not have (the network adapter without its INF) has no row.
            check(f'{label}the {title} are the devices\' own, each once ({len(shown)})',
                  shown and len(set(shown)) == len(shown) and set(shown) <= set(expected)
                  and len(shown) >= len(expected) - 3)
            check(f'{label}no {title} of an unknown device', not [row for row in rows if not row.split(' | ', 1)[1:] or
                                                                 re.search('unknown|reserved', row, re.I)])
    check(f'{label}no call the host does not carry (computer)', not failures(text))
    return run


DISABLED = 'This device is disabled  (Code 22.)'
WORKING = 'This device is working properly.'


def general(text):
    """{device: the status line of the page General} for every sheet a script opened and dumped."""
    out, name = {}, None
    for entry in dumps(text):
        if isinstance(entry, str):
            name = entry[:-len(' Properties')] if entry.endswith(' Properties') and entry != 'System Properties' else None
            continue
        front = entry['windows'][0] if entry['windows'] else None
        if name and front and front['title'] == f'{name} Properties' and name not in out:
            out[name] = next((line for line in front['texts'] if line.startswith('This device')), '')
    return out


def not_blank(path):
    """A screenshot with more than a handful of colours: the display drew the desktop and its windows."""
    try:
        body = Path(path).read_bytes().split(b'\n', 3)[3]
    except (OSError, IndexError):
        return False
    return len({body[i:i + 3] for i in range(0, len(body), 3 * 997)}) > 4


def disable(binary, out, pristine, rows, check, hardware):
    """"Disable in this hardware profile": marked, kept over a start, taken back, and the port runs on."""
    program = 'C:\\WINDOWS\\RUNDLL32.EXE'
    names = [hardware['SoundName'], 'Communications Port (COM1)', hardware['NetName'],
             'Standard 101/102-Key or Microsoft Natural Keyboard', 'Standard PS/2 Port Mouse']
    # SYSDM.CPL offers the box for neither of these.
    without = [hardware['DisplayName'], hardware['CdName']]
    at = {text: row for row, _depth, _chosen, text in rows}
    script = Script().expand_all()
    for name in names:
        select(script, at[name])
        script.add('click=133,431', 0.5).add(f'wait=|{name} Properties', 1.0)
        script.add('button=Disable in this hardware profile', 0.6).add('button=OK', 2.0)
    for name in without:
        select(script, at[name])
        script.add('click=133,431', 0.5).add(f'wait=|{name} Properties', 1.0).add('tops=2', 0.3).add('key=1b', 1.0)
    script.add('tops=2', 0.3).add('trees=1')
    code, text, first = host(binary, out, 'disable', program, COMMAND, script.steps, int(script.time) + 50,
                             pristine, win98_c.journal(pristine))
    left = trees(text)
    final = [entry for entry in dumps(text) if isinstance(entry, dict) and entry['windows']]
    boxes = {entry['windows'][0]['title']: '&Disable in this hardware profile' in entry['windows'][0]['texts']
             for entry in dumps(text) if isinstance(entry, dict) and entry['windows']}
    check('the display adapter and the CD-ROM drive have no "Disable in this hardware profile"',
          [boxes.get(f'{name} Properties') for name in without] == [False, False])
    check('Disable: five devices ticked and confirmed, every device is still in the tree',
          left and [item[3] for item in left[-1]['items']] == [item[3] for item in rows]
          and final and final[-1]['windows'][0]['title'] == 'System Properties')
    check('Disable needs no call the host does not carry (CM_Set_HW_Prof_Flags, SHELL.157)', not failures(text))

    # The next start: all six disabled; two are taken back.
    script = Script().expand_all()
    for name in names:
        select(script, at[name])
        script.add('click=133,431', 0.5).add(f'wait=|{name} Properties', 1.0).add('tops=2', 0.3)
        if name == hardware['SoundName']:
            script.add('button=Enable Device', 2.0).add('tops=2', 0.3)
            script.add('key=1b', 1.0)
        elif name == 'Communications Port (COM1)':
            script.add('button=Disable in this hardware profile', 0.6).add('button=OK', 2.0)
        else:
            script.add('key=1b', 1.0)
    script.add('shot={out}/disabled.ppm').add('tops=1')
    code, text, second = host(binary, out, 'disable-next-start', program, COMMAND, script.steps, int(script.time) + 50,
                              pristine, first / 'c.registry')
    status = general(text)
    check('at the next start the five are disabled (Code 22) and their sheets open',
          [status.get(name) for name in names] == [DISABLED] * len(names))
    if [status.get(name) for name in names] != [DISABLED] * len(names):
        print('    status:', status)
    after = [entry for entry in dumps(text) if isinstance(entry, dict) and entry['windows']
             and entry['windows'][0]['title'] == f'{hardware["SoundName"]} Properties']
    check('"Enable Device" starts the sound card at once, without a restart',
          len(after) > 1 and WORKING in after[-1]['windows'][0]['texts'])
    check('with keyboard and mouse disabled the picture is drawn and keys and clicks arrive',
          not_blank(second / 'disabled.ppm') and len(status) == len(names))
    check('no call the host does not carry (disabled devices)', not failures(text))

    # Sound and network of the port with their devices disabled.
    lines = []
    for name, command, title in (('disable-multimedia', 'RUNDLL32.EXE shell32.dll,Control_RunDLL mmsys.cpl',
                                  'Multimedia Properties'),
                                 ('disable-network', 'RUNDLL32.EXE shell32.dll,Control_RunDLL netcpl.cpl', 'Network')):
        steps = [f'0:wait=|{title}', '4:tops=2', '4.4:lists=1']
        code, text, run = host(binary, out, name, program, command, steps, 25, pristine, first / 'c.registry')
        lines += said(text)
        check(f'no call the host does not carry ({title}, devices disabled)', not failures(text))
    check('with the sound card disabled Multimedia still offers the playback device',
          hardware['WaveOutName'] in lines)
    check('with the network adapter disabled Network still lists it with TCP/IP',
          f'TCP/IP -> {hardware["NetName"]}' in lines)

    # The third start: what was taken back works, the rest is still disabled.
    script = Script().expand_all()
    for name in names[:3]:
        select(script, at[name])
        script.add('click=133,431', 0.5).add(f'wait=|{name} Properties', 1.0).add('tops=2', 0.3).add('key=1b', 1.0)
    code, text, third = host(binary, out, 'disable-third-start', program, COMMAND, script.steps, int(script.time) + 50,
                             pristine, second / 'c.registry')
    status = general(text)
    check('at the third start sound card and COM1 work, the network adapter is still disabled',
          [status.get(name) for name in names[:3]] == [WORKING, WORKING, DISABLED])
    check('no call the host does not carry (third start)', not failures(text))


def profiles(binary, out, pristine, check):
    """Hardware Profiles: Copy, Rename and Delete, each kept by OK over a start."""
    program = 'C:\\WINDOWS\\RUNDLL32.EXE'

    def rows(text):
        return [entry['lists'][0] for entry in dumps(text) if isinstance(entry, dict) and entry['lists']]

    script = Script()
    script.add('click=262,96', 1.5).add('tops=2', 0.2).add('lists=1', 0.3)
    script.add('click=148,336', 0.5).add('wait=|Copy Profile', 1.0).add('tops=2', 0.3)
    script.add('type=Docked', 0.8).add('key=0d', 2.0).add('tops=2', 0.2).add('lists=1', 0.3).add('click=346,481', 4.0)
    code, text, first = host(binary, out, 'profiles', program, COMMAND, script.steps, int(script.time) + 15,
                             pristine, win98_c.journal(pristine))
    lists = rows(text)
    # The dump of Copy Profile: the window with the focus is its visible edit control.
    focus = [match[1] for match in re.finditer(r'NEO98 HOST tops foreground=(\w+) focus=(\w+)\n', text)
             if re.search(rf'NEO98 HOST top id={match[1]} visible=1 .* title=Copy Profile\n', text)
             and re.search(rf'NEO98 HOST child id={match[2]} visible=1 .* class=Edit ', text)]
    check('Hardware Profiles lists the profile of Setup', lists[:1] == [['Original Configuration']])
    check('Copy Profile opens with the focus in its edit control', bool(focus))
    check('Copy adds the profile under the typed name',
          len(lists) > 1 and lists[1] == ['Docked', 'Original Configuration'])
    check('OK ends the program (profiles copied)', code == 0 and not failures(text))

    script = Script()
    script.add('click=262,96', 1.5).add('tops=2', 0.2).add('lists=1', 0.3)
    script.add('click=150,217', 0.5).add('click=268,336', 0.5).add('wait=|Rename Profile', 1.0)
    script.add('type=Travel', 0.8).add('key=0d', 2.0).add('tops=2', 0.2).add('lists=1', 0.3)
    script.add('click=150,230', 0.5).add('click=388,336', 1.5).add('tops=2', 0.3).add('key=0d', 2.0)
    script.add('tops=2', 0.2).add('lists=1', 0.3).add('click=346,481', 4.0)
    code, text, second = host(binary, out, 'profiles-next-start', program, COMMAND, script.steps,
                              int(script.time) + 15, pristine, first / 'c.registry')
    lists = rows(text)
    check('at the next start there are two profiles', lists[:1] == [['Docked', 'Original Configuration']])
    check('Rename gives the copy its new name', len(lists) > 1 and lists[1] == ['Original Configuration', 'Travel'])
    asked = [entry['windows'][0]['title'] for entry in dumps(text) if isinstance(entry, dict) and entry['windows']]
    check('Delete asks and removes the copy', 'Confirm Profile Delete' in asked and lists[-1:] == [['Original Configuration']])
    if 'Confirm Profile Delete' not in asked:
        print('    windows:', asked)
    check('OK ends the program (profile renamed and deleted)', code == 0 and not failures(text))

    script = Script()
    script.add('click=262,96', 1.5).add('tops=2', 0.2).add('lists=1', 0.3).add('tops=1')
    code, text, third = host(binary, out, 'profiles-third-start', program, COMMAND, script.steps,
                             int(script.time) + 10, pristine, second / 'c.registry')
    check('at the third start one profile is left', rows(text)[:1] == [['Original Configuration']])
    check('no call the host does not carry (profiles)', not failures(text))


def midi(binary, out, pristine, check, hardware):
    """Multimedia, page MIDI: the synthesizer under "Single instrument"."""
    program, command = 'C:\\WINDOWS\\RUNDLL32.EXE', 'RUNDLL32.EXE shell32.dll,Control_RunDLL mmsys.cpl'

    def edits(text):
        """The text of the visible edit control of the page in each dump, and whether the tree is enabled."""
        found = []
        for block in text.split('NEO98 HOST tops ')[1:]:
            edit = re.search(r'child id=\w+ visible=1 .* style=(\w+) .* class=Edit title=(.*)', block)
            tree = re.search(r'child id=\w+ visible=1 .* style=(\w+) .* class=SysTreeView32 ', block)
            if edit and tree:
                found.append((edit[2], not int(tree[1], 16) & 0x08000000))
        return found

    # Ctrl+Tab twice: Audio, Video, MIDI. The first row of the tree, OK.
    steps = ['0:wait=|Multimedia Properties', '3:keydown=a2', '3.1:key=09', '3.6:key=09', '4:keyup=a2', '6:tops=2',
             '6.5:click=150,196', '7.5:tops=2', '8:shot={out}/midi.ppm', '8.5:button=OK']
    code, text, first = host(binary, out, 'midi', program, command, steps, 25, pristine, win98_c.journal(pristine))
    found = edits(text)
    check('Multimedia, MIDI: "Single instrument" and its list are enabled', len(found) == 2 and found[0][1])
    check(f'a click on the first row of the list chooses {hardware["MidiOutName"]}',
          len(found) == 2 and found[1][0] == hardware['MidiOutName'])
    check('OK ends the program (MIDI)', code == 0 and not failures(text))
    steps = steps[:6]
    code, text, second = host(binary, out, 'midi-next-start', program, command, steps, 20, pristine,
                              first / 'c.registry')
    found = edits(text)
    check('at the next start the page shows the chosen instrument',
          len(found) == 1 and found[0] == (hardware['MidiOutName'], True))
    check('no call the host does not carry (MIDI)', not failures(text))


def print_report(binary, out, pristine, rows, check, hardware):
    """"Print..." with a printer installed: the report "All devices and system summary"."""
    import print_pdf

    def prepare(run):
        from test_support import require_printer_inputs
        require_printer_inputs()
        subprocess.run([sys.executable, str(ROOT / 'tools/install_printer.py'), '--drive-c', str(run / 'c'),
                        '--registry', str(run / 'c.registry'), '--tree', str(run / 'c')], check=True,
                       stdout=subprocess.DEVNULL)

    script = Script()
    script.add('click=403,431', 1.0).add('wait=|Print', 0.5).add('tops=2', 0.3)
    script.add('button=All devices and system summary', 0.5).add('button=OK', 25.0).add('tops=1')
    code, text, run = host(binary, out, 'print', 'C:\\WINDOWS\\RUNDLL32.EXE', COMMAND, script.steps,
                           int(script.time) + 15, pristine, win98_c.journal(pristine), prepare=prepare,
                           extra=['--printer-output', '{out}/printed'])
    shown = [line for line in said(text) if 'Default Printer' in line]
    check('Print... names the installed printer on LPT1', shown and 'on LPT1:' in shown[0])
    job = re.search(r'PRINTER_JOB bytes=(\d+) file=(\S+) pdf=(\S+)', text)
    check('the report is printed as one job', job and int(job[1]) > 20000 and Path(job[2]).is_file())
    check('no call the host does not carry (print)', not failures(text))
    if not job or job[3] == '-' or not print_pdf.ghostscript():
        print('skip the text of the report: no PDF (Ghostscript is not installed)')
        return
    pages = print_pdf.pages(Path(job[3]), run / 'printed/text')
    lines = [line.strip() for page in pages for line in page.splitlines() if line.strip()]
    table = resources()

    def section(title, until):
        """The lines between two headings of the report."""
        first = next((i for i, line in enumerate(lines) if title in line), None)
        if first is None:
            return []
        rest = lines[first + 1:]
        last = next((i for i, line in enumerate(rest) if until in line), len(rest))
        return [line for line in rest[:last] if ' - ' in line and not line.startswith('System Resource Report')]

    names = {node: name for name, node in DEVNODES.items()}
    expected = {kind: sorted((first, last, names.get(device, '?')) for device, entries in table.items()
                             for k, first, last in entries if k == kind) for kind in 'QPDM'}

    def parsed(rows, hexadecimal):
        found = []
        for row in rows:
            match = re.match(r'([0-9A-F]+)h?(?:-([0-9A-F]+)h)? - (.+)$', row)
            if match:
                base = 16 if hexadecimal else 10
                found.append((int(match[1], base), int(match[2] or match[1], base), match[3]))
        return sorted(found)

    def same(found, kind):
        """Every value once and from the table, under the name of its device where the test knows the devnode.
        A device the folder does not have (the network adapter without its INF) has no row."""
        values = [(first, last) for first, last, _name in found]
        wanted_names = {(first, last): name for first, last, name in expected[kind] if name != '?'}
        return (bool(found) and len(set(values)) == len(values)
                and set(values) <= {(first, last) for first, last, _name in expected[kind]}
                and all(wanted_names.get((first, last), name) == name for first, last, name in found)
                and len(found) >= len(expected[kind]) - 3)

    check('the report has several pages and names the port as its BIOS',
          len(pages) >= 3 and f'BIOS Version: {hardware["BiosVersion"]}' in lines)
    check('its IRQ summary is the table of src/neo98_hardware.h',
          same(parsed(section('IRQ SUMMARY', 'IO PORT SUMMARY'), False), 'Q'))
    check('its I/O port summary is the table',
          same(parsed(section('IO PORT SUMMARY', 'UPPER MEMORY'), True), 'P'))
    check('its memory summary is the table',
          same(parsed(section('UPPER MEMORY', 'DMA USAGE'), True), 'M'))
    check('its DMA summary is the table', same(parsed(section('DMA USAGE', 'MEMORY SUMMARY'), False), 'D'))
    devices = {line[len('Device: '):] for line in lines if line.startswith('Device: ')}
    wanted = {name for _row, depth, _chosen, name in rows if depth == 2}
    check(f'every device of the tree has its part of the report ({len(wanted)})', wanted <= devices)
    if not wanted <= devices:
        print('    missing:', sorted(wanted - devices))
    check('the report names nothing of the installation machine', not [line for line in lines if STALE.search(line)])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--user-clone', action='store_true', help='parts 1, 5 and 7 over a clone of the user\'s folder')
    parser.add_argument('--only', action='append', default=[],
                        choices=['names', 'tree', 'pages', 'display', 'sheets', 'computer', 'remove', 'disable', 'profiles',
                                 'midi', 'print', 'elsewhere'])
    arguments = parser.parse_args()
    from test_support import printer_inputs_reason, require_printer_inputs
    printer_reason = printer_inputs_reason()
    if not arguments.user_clone and set(arguments.only) == {'print'}:
        require_printer_inputs()
    live = win98_c.live_folder()
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('device-manager-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    checks, report = {}, {}
    hardware = hardware_setup.names()

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    def wanted(part):
        return not arguments.only or part in arguments.only

    program = 'C:\\WINDOWS\\RUNDLL32.EXE'
    folder = live if arguments.user_clone else pristine
    journal = win98_c.journal(folder)
    label = 'user: ' if arguments.user_clone else ''
    rows = None
    if arguments.user_clone or (arguments.only and 'tree' not in arguments.only):
        script = names_script()
        code, text, run = host(binary, out, 'names', program, COMMAND, script.steps, int(script.time) + 10,
                               folder, journal)
        found = trees(text)
        check(f'{label}the tree was read', bool(found))
        if found:
            rows = found[-1]['items']
            report['devices'] = check_names(found[-1], check, label.strip(': ') or 'setup')
            if arguments.user_clone:
                print('\n'.join('    ' + text for text in report['devices']))
        check(f'{label}no call the host does not carry (names)', not failures(text))
    if not arguments.user_clone and wanted('tree'):
        # Parts 1 and 2: the names and the scroll bar.
        script = names_script()
        script.add('key=24').add('trees=1')                      # 1 Home
        script.add('move=250,300').add('wheel=-1').add('trees=1')  # 2 a notch down
        script.add('scroll=-13.4,0,begin', 0.1).add('scroll=-13.4,0', 0.1).add('scroll=-13.4,0', 0.8)
        script.add('trees=1')                                    # 3 three steps of a trackpad, a row each
        script.add('key=23').add('trees=1')                      # 4 End
        script.add('key=24').add('trees=1')                      # 5 Home again
        script.add('click=434,401').add('trees=1')               # 6 the arrow down of the bar
        script.add('key=24').add('down=434,172', 0.3).add('move=434,230', 0.3).add('move=434,290', 0.3)
        script.add('up=434,290').add('trees=1').add('shot={out}/dragged.ppm')  # 7 the thumb dragged down
        # Pixel/row rounding can shift the grab offset on release. Drag into
        # the top arrow to test the clamped endpoint, not an exact inverse.
        script.add('down=434,290', 0.3).add('move=434,230', 0.3).add('move=434,150', 0.3)
        script.add('up=434,150').add('trees=1')                    # 8 thumb back at the top
        script.add('click=300,157').add('key=24').add('key=22').add('trees=1')  # 9 Page Down from the first row
        code, text, run = host(binary, out, 'tree', program, COMMAND, script.steps, int(script.time) + 10,
                               pristine, win98_c.journal(pristine))
        found = trees(text)
        check('the tree was read ten times', len(found) == 10)
        check('no call the host does not carry (tree)', not failures(text))
        if len(found) == 10:
            rows = found[0]['items']
            report['devices'] = check_names(found[0], check, 'setup')
            total, page = found[0]['rows'], found[0]['page']
            report['scroll'] = [{'rows': d['rows'], 'first': d['first'], 'page': d['page'], 'bar': d['bar']} for d in found]
            check('the opened tree is longer than its page and has the bar', total > page > 5 and found[0]['bar'])
            check('the selection at the end is in the page', found[0]['first'] == total - page)
            check('Home shows the first row', found[1]['first'] == 0)
            check('a notch of the wheel scrolls three rows', found[2]['first'] == 3)
            check('three steps of the trackpad scroll a row each', found[3]['first'] == 6)
            check('End shows the last page', found[4]['first'] == total - page)
            check('the arrow of the bar scrolls one row', found[5]['first'] == 0 and found[6]['first'] == 1)
            check('a drag of the thumb scrolls', 3 <= found[7]['first'] <= total - page)
            check('dragging the thumb back returns to the first row', found[8]['first'] == 0)
            selected = [row for row, _depth, chosen, _text in found[9]['items'] if chosen]
            check('Page Down selects the last row of the page', selected == [page - 1] and found[9]['first'] == 0)

    if not arguments.user_clone and wanted('pages'):
        # Part 3: the pages that ended the host.
        script = Script()
        script.add('click=122,175', 1.0).add('click=200,193', 0.6).add('click=133,431', 0.5)
        script.add('wait=|NEO98 CD-ROM Properties', 1.0).add('click=179,162', 2.0)
        script.add('shot={out}/cd-settings.ppm').add('close=*', 1.5)
        script.add('dblclick=200,193', 1.5).add('close=*', 1.5)      # the properties again, by a double click
        script.add('click=262,96', 1.5).add('click=148,336', 0.5).add('wait=|Copy Profile', 1.0).add('key=1b', 1.5)
        script.add('click=346,96', 1.5).add('click=379,421', 0.5).add('wait=|Virtual Memory', 1.0)
        script.add('shot={out}/virtual-memory.ppm').add('key=1b', 1.5)
        script.add('click=154,421', 0.5).add('wait=|File System Properties', 1.0).add('close=*', 1.5)
        script.add('click=109,96', 1.5).add('trees=1').add('windows=1')
        code, text, run = host(binary, out, 'pages', program, COMMAND, script.steps, int(script.time) + 25,
                               pristine, win98_c.journal(pristine))
        reached = re.findall(r'NEO98 HOST input wait=\|(.+?) reached', text)
        report['dialogs'] = reached
        for title in ('NEO98 CD-ROM Properties', 'Copy Profile', 'Virtual Memory', 'File System Properties'):
            check(f'the dialog "{title}" opened', title in reached)
        check('no call the host does not carry (pages)', not failures(text))
        check('System Properties is still there at the end', 'title=System Properties' in text and trees(text))
    if not arguments.user_clone and wanted('display'):
        # The same adapter in the display properties: the page Settings names
        # it, and Advanced opens its sheet under that name (USER.271).
        sheet = f'{hardware["DisplayName"]} Properties'
        steps = ['0:wait=|Display Properties', '3:tops=2', '4:click=402,442', f'5:wait=|{sheet}', '6:tops=2',
                 '7:shot={out}/adapter.ppm', '8:close=*', '10:windows=1']
        code, text, run = host(binary, out, 'display', program, 'RUNDLL32.EXE shell32.dll,Control_RunDLL desk.cpl,,3',
                               steps, 40, pristine, win98_c.journal(pristine))
        check(f'Display Properties, Advanced opens "{sheet}"', sheet in re.findall(r'wait=\|(.+?) reached', text))
        check('Display Properties names the monitor and the adapter of the port',
              any(hardware['MonitorName'] in line and hardware['DisplayName'] in line for line in said(text)))
        check('no call the host does not carry (display)', not failures(text))
        check('Display Properties is still there at the end', 'title=Display Properties' in text)
        check('Display Properties names nothing of the installation machine',
              not [line for line in said(text) if STALE.search(line)])

    if wanted('sheets') and rows:
        # Part 5: every device, by type and by connection.
        devices = [(row, text) for row, depth, _chosen, text in rows if depth == 2]
        seen = sheets(binary, out, folder, journal, devices, check, report, label)
        some = [(row, text) for row, text in devices
                if text in (hardware['DisplayName'], hardware['SoundName'], hardware['NetName'],
                            'Communications Port (COM1)') and text in seen
                and 'Driver' in seen[text]['pages']]
        report[f'{label}driver files'] = driver_files(binary, out, folder, journal, some,
                                                      {name: record['pages'] for name, record in seen.items()},
                                                      check, label, folder)
        script = Script()
        script.add('click=250,130', 1.0).expand_all()
        script.add('trees=1').add('shot={out}/connection.ppm')
        code, text, run = host(binary, out, 'connection', program, COMMAND, script.steps, int(script.time) + 10,
                               folder, journal)
        found = trees(text)
        order = [(depth, text) for _row, depth, _chosen, text in found[-1]['items']] if found else []
        report[f'{label}connection'] = ['  ' * depth + text for depth, text in order]

        def below(child, parent):
            at = [i for i, (_depth, text) in enumerate(order) if text == child]
            return bool(at) and any(order[i][0] == order[j][0] - 1 and order[i][1] == parent
                                    for j in at for i in [max(k for k in range(j) if order[k][0] < order[j][0])])
        check(f'{label}by connection the monitor hangs below the display adapter',
              below(hardware['MonitorName'], hardware['DisplayName']))
        check(f'{label}by connection the CD-ROM drive hangs below a hard disk controller',
              below(hardware['CdName'], 'Standard IDE/ESDI Hard Disk Controller'))
        check(f'{label}by connection every device of the list by type is there',
              {text for _row, text in devices} <= {text for _depth, text in order})
    if wanted('computer') or (arguments.user_clone and not arguments.only):
        computer(binary, out, folder, journal, check, report, label)

    if not arguments.user_clone and wanted('remove') and rows:
        # Part 6: Remove holds until the next start.
        sound = next(row for row, _depth, _chosen, text in rows if text == hardware['SoundName'])
        script = Script().expand_all()
        select(script, sound)
        script.add('click=313,431', 0.5).add('wait=|Confirm Device Removal', 1.0).add('tops=2', 0.5)
        script.add('key=0d', 3.0).add('tops=2', 0.5).add('trees=1')
        code, text, run = host(binary, out, 'remove', program, COMMAND, script.steps, int(script.time) + 15,
                               pristine, win98_c.journal(pristine))
        asked = [entry for entry in dumps(text) if isinstance(entry, dict) and entry['windows']]
        check('Remove asks with "Confirm Device Removal" for the sound card',
              asked and asked[0]['windows'][0]['title'] == 'Confirm Device Removal'
              and hardware['SoundName'] in asked[0]['windows'][0]['texts'])
        gone = trees(text)
        check('Enter confirms and the sound card is gone from the tree',
              gone and hardware['SoundName'] not in [item[3] for item in gone[-1]['items']]
              and len(asked) > 1 and asked[-1]['windows'][0]['title'] == 'System Properties')
        check('the sheet says Close and has no Cancel (PSM_CANCELTOCLOSE)',
              len(asked) > 1 and 'Close' in asked[-1]['windows'][0]['texts']
              and re.search(r'style=58010000 .*title=Cancel', text))
        check('no call the host does not carry (remove)', not failures(text))
        script = names_script()
        code, text, again = host(binary, out, 'remove-next-start', program, COMMAND, script.steps, int(script.time) + 10,
                                 pristine, run / 'c.registry')
        back = trees(text)
        check('at the next start over the same journal the sound card is back',
              back and hardware['SoundName'] in [item[3] for item in back[-1]['items']]
              and len(back[-1]['items']) == len(rows))
        check('no call the host does not carry (next start)', not failures(text))

    if not arguments.user_clone and wanted('disable') and rows:
        disable(binary, out, pristine, rows, check, hardware)
    if not arguments.user_clone and wanted('profiles'):
        profiles(binary, out, pristine, check)
    if not arguments.user_clone and wanted('midi'):
        midi(binary, out, pristine, check, hardware)
    if not arguments.user_clone and wanted('print') and rows:
        if printer_reason:
            print('SKIP print: ' + printer_reason, flush=True)
            report.setdefault('skipped', {})['print'] = printer_reason
        else:
            print_report(binary, out, pristine, rows, check, hardware)

    if wanted('elsewhere'):
        # Part 7: the same devices outside the Device Manager.
        everything = []
        dxdiag = 'C:\\WINDOWS\\SYSTEM\\DXDIAG.EXE'
        if win98_c.path(dxdiag, folder) is None:
            dxdiag = 'C:\\Program Files\\DirectX\\Setup\\DXDIAG.EXE'
        if win98_c.path(dxdiag, folder) is None:
            print('skip the DirectX diagnosis: not in', folder)
        else:
            # Every page with "Next Page". The tool of DirectX 7 and later asks
            # first whether it may look for signatures on the Internet: No.
            steps = ['0:wait=|DirectX Diagnostic Tool', '4:button=No', '30:tops=2', '30.5:lists=1']
            time = 31.0
            for page in range(11):
                steps += [f'{time:.1f}:button=Next Page', f'{time + 2.5:.1f}:tops=2', f'{time + 2.8:.1f}:lists=1']
                if page in (3, 4):
                    steps.append(f'{time + 3.1:.1f}:shot={{out}}/dxdiag-{page}.ppm')
                time += 3.5
            code, text, run = host(binary, out, 'dxdiag', dxdiag, 'DXDIAG.EXE', steps, int(time) + 25, folder, journal)
            lines = said(text)
            everything += lines
            report[f'{label}dxdiag'] = [line for line in lines if 'NEO98' in line or '.drv' in line or '.vxd' in line.lower()]
            check(f'{label}the DirectX diagnosis names the display adapter and the monitor',
                  hardware['DisplayName'] in lines and hardware['MonitorName'] in lines)
            check(f'{label}the DirectX diagnosis names a display driver file of the folder',
                  hardware['DisplayDriverFile'] in lines and any(re.match(r'4\.\d+\.\d+\.\d+', line) for line in lines))
            check(f'{label}the DirectX diagnosis lists the MIDI synthesizer', hardware['MidiOutName'] in lines)
            check(f'{label}no call the host does not carry (DirectX diagnosis)', not failures(text))
        steps = ['0:wait=|Multimedia Properties', '4:tops=2', '4.4:lists=1', '5:shot={out}/audio.ppm']
        code, text, run = host(binary, out, 'multimedia', program, 'RUNDLL32.EXE shell32.dll,Control_RunDLL mmsys.cpl',
                               steps, 25, folder, journal)
        lines = said(text)
        everything += lines
        check(f'{label}Multimedia, Audio: the playback device is {hardware["WaveOutName"]}', hardware['WaveOutName'] in lines)
        check(f'{label}no call the host does not carry (Multimedia)', not failures(text))
        steps = ['0:wait=|Network', '4:tops=2', '4.4:lists=1', '5:shot={out}/network.ppm']
        code, text, run = host(binary, out, 'network', program, 'RUNDLL32.EXE shell32.dll,Control_RunDLL netcpl.cpl',
                               steps, 25, folder, journal)
        lines = said(text)
        everything += lines
        check(f'{label}Network lists {hardware["NetName"]} with TCP/IP bound to it',
              hardware['NetName'] in lines and f'TCP/IP -> {hardware["NetName"]}' in lines)
        check(f'{label}no call the host does not carry (Network)', not failures(text))
        stale = sorted({line for line in everything if STALE.search(line)})
        check(f'{label}none of these windows names the installation machine ({len(everything)} texts)',
              len(everything) > 60 and not stale)
        if stale:
            print('    stale:', stale[:8])
    passed = all(checks.values())
    report['checks'] = checks
    name = 'results-user.json' if arguments.user_clone else 'results.json' if not arguments.only else 'results-part.json'
    (out / name).write_text(json.dumps(report, indent=2) + '\n')
    print(f'device manager host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
