#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The guard of the frames of the host in a saved state.

A state holds no host stack. A thread that stands inside a callback or a
wait is saved only when every frame of the host it has can be built again
(src/x86_state_resume.cpp); in every other frame the save waits until the
thread has left it. Which of the two a function is, is a decision, and this
tool makes sure it was taken for every function that can have such a frame:

  loop runs the guest itself: it calls Application::Step
  entry the first frame of a fiber; a load makes it anew
  resumable a state is taken with its frame on the stack, and the frame is
              built again after a load; the function says so in its code
              (GiveTail, TailOfGate, TakeFresh, PushHostFrame)
  restart waits, and the call is made again from its gate after a load
  transient a state waits until the thread has left the frame; the line
              says why the thread never stays

The list is src/state_rules/frames.calls, one function a line with its
reason behind `#`. The call instructions of the linked program say who calls
whom, with the inline stack of each call from the debug information; the
debug information of the objects adds the functions the compiler inlined.
The build stops

  * for a function that calls Step, one of the loops, or
    Application::Suspend and is not on the list (a new callback or a new
    wait: decide what a save does with it),
  * for a function that hands a tail position on and is not `resumable`,
    and for a `resumable` one that does not,
  * for a line no function stands behind any more.
  * when the number of callback/wait/tail sites in a listed function
    differs from its reviewed sites=N count.

    tools/state_frames.py --binary build/native-app/neo98-host
    tools/state_frames.py --binary... --propose lines for what is missing
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pickle
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import state_layout  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
LIST = ROOT / ('src/state_rules/frames.linux.calls' if sys.platform.startswith('linux')
               else 'src/state_rules/frames.calls')
CACHE = ROOT / 'build/native-app/state-frames-cache'
KINDS = ('loop', 'entry', 'resumable', 'restart', 'transient')
STEP = 'X86::Application::Step'
SUSPEND = 'X86::Application::Suspend'
# What a function does in its code when a state can be taken inside it.
TAIL = {'X86::Application::GiveTail', 'X86::Application::TailOfGate', 'X86::Application::TakeFresh',
        'X86::Application::PushHostFrame', 'X86::Application::ResumeInner', 'X86::Application::ResumeCall',
        'X86::Application::ResumeHostCall'}
DIE = re.compile(r'^0x[0-9a-f]+:(\s+)(DW_TAG_\w+|NULL)')
NAMED = re.compile(r'^\s+(DW_AT_linkage_name|DW_AT_specification|DW_AT_abstract_origin|DW_AT_call_origin|DW_AT_name)\s+\((?:0x[0-9a-f]+ )?"(.*)"\)')


def calls_of(obj):
    """Counts of (caller, callee) sites in one object, including inline
    sites. Keeping each site catches another callback in a known function."""
    CACHE.mkdir(parents=True, exist_ok=True)
    stamp = hashlib.sha256((obj.name + str(Path(__file__).stat().st_mtime_ns)).encode()).hexdigest()[:24]
    cached = CACHE / f'{stamp}.pickle'
    if cached.exists():
        return pickle.loads(cached.read_bytes())
    process = subprocess.Popen([state_layout.DWARFDUMP, '--debug-info', str(obj)], stdout=subprocess.PIPE, text=True,
                               errors='replace')
    pairs = Counter()
    stack = []          # [depth, tag, name or None, name is a linkage name]
    current = None
    for line in process.stdout:
        if 'DW_' not in line and 'NULL' not in line:
            continue
        match = DIE.match(line)
        if match:
            depth = (len(match.group(1)) - 1) // 2
            del stack[depth:]
            if match.group(2) == 'NULL':
                current = None
                continue
            current = [depth, match.group(2), None, False]
            stack.append(current)
            continue
        if current is None or current[1] not in ('DW_TAG_subprogram', 'DW_TAG_inlined_subroutine', 'DW_TAG_call_site'):
            continue
        match = NAMED.match(line)
        if not match:
            continue
        attribute, name = match.groups()
        if current[1] == 'DW_TAG_call_site':
            # Calls come from the instructions (instruction_calls): the
            # compiler leaves out call-site entries, and which ones it
            # leaves out changes between its versions.
            continue
        elif attribute == 'DW_AT_name':
            if current[2] is None:
                current[2] = name
            continue
        elif attribute == 'DW_AT_call_origin':
            continue
        elif current[3]:
            continue
        else:
            current[2], current[3] = name, True
            if current[1] != 'DW_TAG_inlined_subroutine':
                continue
        # A call, or a function inlined here: who has it in its body.
        for outer in reversed(stack[:-1]):
            if outer[1] in ('DW_TAG_subprogram', 'DW_TAG_inlined_subroutine') and outer[2]:
                pairs[outer[2], name] += 1
                break
    status = process.wait()
    if status:
        raise SystemExit(f'state frames: debug-info reader failed for {obj} (exit {status}); no cache written')
    cached.write_bytes(pickle.dumps(pairs))
    return pairs


def demangle(names):
    # The names come from the debug information, without the underscore
    # of a Mach-O symbol; c++filt on macOS strips one unless told not to.
    if not names:
        return []
    # A symbol of the state layout keeps the Mach-O underscore on every
    # platform (MachineState::SymbolOf, the step wstate=chains); LLVM's
    # c++filt reads "__Z" as well, GNU c++filt only "_Z".
    names = [name[1:] if name.startswith('__Z') else name for name in names]
    return subprocess.run(['c++filt', '-n'], input='\n'.join(names), capture_output=True, text=True,
                          check=True).stdout.split('\n')[:len(names)]


def plain(names):
    """Mangled names as the list writes a function: without its parameters,
    a lambda without its number."""
    names = sorted(names)
    out = dict(zip(names, demangle(names)))
    # The demangler of LLVM (c++filt on macOS) cannot read some parameter
    # packs. The list drops the parameters anyway: the name cut before
    # them, with an empty parameter list, reads the same.
    unread = [name for name in names if name.startswith('_Z') and out[name] == name]
    candidates = [(name, name[:end] + 'v') for name in unread for end in range(len(name) - 1, max(3, len(name) - 200), -1)]
    for (name, candidate), readable in zip(candidates, demangle([candidate for _, candidate in candidates])):
        if out[name] == name and readable != candidate:
            out[name] = readable
    result = {}
    for mangled in names:
        readable = out[mangled]
        text = readable.replace('(anonymous namespace)', '{anon}')
        text = re.sub(r"'lambda\d*'", 'lambda', text)
        # Parameter lists and template arguments go, innermost first.
        before = None
        while before != text:
            before = text
            text = re.sub(r'\([^()]*\)', '', text)
            text = re.sub(r'<[^<>]*>', '', text)
        text = re.sub(r'\{lambda#\d+\}', 'lambda', text)
        text = re.sub(r'\$_\d+', 'lambda', text)
        text = re.sub(r'\s*(const|\[clone[^\]]*\])\s*$', '', text).strip()
        text = re.sub(r'^.* (?=[\w{}:~]+$)', '', text)  # a return type in front
        result[mangled] = text.replace('::operator', '::call') if text.endswith('::operator') else text
    return result


def read_list():
    listed = {}
    if not LIST.exists():
        return listed
    for number, line in enumerate(LIST.read_text().splitlines(), 1):
        text = line.split('#', 1)[0].strip()
        if not text:
            continue
        match = re.match(r'^(' + '|'.join(KINDS) + r')\s+(\S.*?)$', text)
        if not match:
            raise SystemExit(f'{LIST}:{number}: not a line of the list: {line}')
        if '#' not in line or not line.split('#', 1)[1].strip():
            raise SystemExit(f'{LIST}:{number}: a line says why (# ...): {line}')
        name, _, sites = match.group(2).rpartition(' sites=')
        if not name:
            name, sites = match.group(2), None
        if sites is not None and not sites.isdigit():
            raise SystemExit(f'{LIST}:{number}: invalid site count: {sites}')
        if name in listed:
            raise SystemExit(f'{LIST}:{number}: duplicate function {name} (first listed on line {listed[name][1]})')
        listed[name] = (match.group(1), number, int(sites) if sites is not None else None)
    return listed


def long_frames(text):
    """Callback functions seen for at least 200 ms in an unsavable thread.
    A resumable function may still contain a transient branch. Keep its
    classification instead of hiding that branch from the measurement."""
    found = re.findall(r'^NEO98 HOST state long-frame ms=(\d+) (.*?) thread (\d+) function=(\S+)$', text, re.M)
    names = plain({function for _, _, _, function in found})
    kinds = {name: record[0] for name, record in read_list().items()}
    return [dict(ms=int(ms), program=program, thread=int(thread), function=names[function],
                 kind=kinds[names[function]])
            for ms, program, thread, function in found
            if kinds.get(names[function]) in ('transient', 'resumable')]


def graph(binary):
    objects = state_layout.source_objects(binary)
    jobs = max(1, int(os.environ.get('NEO98_NATIVE_JOBS', '6')))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        parts = list(pool.map(calls_of, [obj for _, obj in sorted(objects.items())]))
    pairs = Counter()
    for part in parts:
        pairs.update(part)
    # The emitted instructions; the linked image gives every function one
    # address for the inline stack lookup.
    pairs |= instruction_calls(binary, objects.values())
    names = plain({name for pair in pairs for name in pair})
    callers = defaultdict(set)
    sites = Counter()
    for caller, callee in pairs:
        if names[caller] != names[callee]:
            callers[names[callee]].add(names[caller])
            sites[names[caller], names[callee]] += pairs[caller, callee]
    return callers, sites


# A direct call or jump to the start of a function: x86-64 and arm64.
BRANCH = re.compile(r'^\s*([0-9a-f]+):\s+(?:(?:callq?|jmpq?|bl|b|b\.\w+)\s+|cbn?z\s+\w+,\s*|tbn?z\s+\w+,\s*#\w+,\s*)'
                    r'(?:0x)?[0-9a-f]+ <([^>]+)>')


def instruction_calls(binary, objects):
    """Counts of (caller, callee) sites read from the call and jump
    instructions of the linked program. The caller is the innermost function
    at the instruction, inlined ones included."""
    CACHE.mkdir(parents=True, exist_ok=True)
    objects = sorted(set(objects))
    digest = hashlib.sha256(binary.read_bytes())
    digest.update(Path(__file__).read_bytes())
    digest.update('\n'.join(str(obj) for obj in objects).encode())
    cached = CACHE / ('calls-' + digest.hexdigest() + '.pickle')
    if cached.exists():
        return pickle.loads(cached.read_bytes())
    tools = Path(state_layout.DWARFDUMP).parent

    def tool(name):
        return str(tools / name) if (tools / name).exists() else name

    macho = binary.read_bytes()[:4] in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe')
    # Mach-O symbols carry a leading underscore the debug information has not.
    symbol = (lambda name: name[1:] if name.startswith('_') else name) if macho else (lambda name: name)
    nm = tool('llvm-nm') if macho else 'nm'
    symbols = set()
    for obj in objects:
        for line in subprocess.check_output([nm, '--defined-only', str(obj)], text=True).splitlines():
            row = line.split()
            # ltmp marks the start of a section in a Mach-O object.
            if len(row) == 3 and row[1] in 'tTwW' and not re.match(r'ltmp\d+$', row[2]):
                symbols.add(symbol(row[2]))
    objdump = tool('llvm-objdump') if macho else 'objdump'
    process = subprocess.Popen([objdump, '-d', '--no-show-raw-insn', str(binary)],
                               stdout=subprocess.PIPE, text=True, errors='replace')
    calls = []
    caller = None
    for line in process.stdout:
        match = re.match(r'^([0-9a-f]+) <([^>]+)>:$', line)
        if match:
            caller = symbol(match[2])
        elif caller in symbols:
            match = BRANCH.match(line)
            if match and '+' not in match[2] and '@' not in match[2]:
                calls.append((int(match[1], 16), symbol(match[2])))
    if process.wait():
        raise SystemExit('state frames: instruction reader failed')
    symbolize = [tool('llvm-symbolizer'), '--no-debuginfod', '--obj=' + str(binary), '--no-demangle',
                 '--inlines', '--output-style=JSON']
    if macho:
        # The program keeps only a map of its objects; a dSYM bundle gathers
        # their debug information at the linked addresses.
        bundle = CACHE / ('calls-' + digest.hexdigest() + '.dSYM')
        result = subprocess.run([tool('dsymutil'), '--num-threads', os.environ.get('NEO98_NATIVE_JOBS', '6'),
                                 str(binary), '-o', str(bundle)],
                                capture_output=True, text=True, errors='replace')
        if result.returncode:
            raise SystemExit('state frames: dsymutil failed\n' + result.stdout + result.stderr)
        symbolize.append('--dsym-hint=' + str(bundle))
    try:
        output = subprocess.check_output(symbolize, input=''.join(f'0x{pc:x}\n' for pc, _ in calls), text=True)
    finally:
        if macho:
            shutil.rmtree(bundle, ignore_errors=True)
    records = [json.loads(line) for line in output.splitlines()]
    if len(records) != len(calls):
        raise SystemExit('state frames: incomplete inline stack lookup')
    pairs = Counter()
    for (_, target), record in zip(calls, records):
        frames = record.get('Symbol', [])
        if not frames or not frames[0].get('FunctionName'):
            raise SystemExit('state frames: a call has no debug function')
        pairs[frames[0]['FunctionName'], target] += 1
    cached.write_bytes(pickle.dumps(pairs))
    return pairs


def check(binary, propose=False, propose_sites=False):
    callers, sites = graph(binary)
    listed = read_list()
    kind = {name: entry[0] for name, entry in listed.items()}
    wanted = {}   # function -> what it calls that puts it on the list
    for name in callers.get(STEP, ()):
        wanted.setdefault(name, set()).add('Step')
    for name, how in kind.items():
        if how == 'loop':
            for caller in callers.get(name, ()):
                wanted.setdefault(caller, set()).add(name.rsplit('::', 1)[-1])
    for name in callers.get(SUSPEND, ()):
        wanted.setdefault(name, set()).add('Suspend')
    tails = set()
    for helper in TAIL:
        tails |= callers.get(helper, set())
    tails -= TAIL
    for name in tails:
        wanted.setdefault(name, set()).add('a tail position or a record')
    watched = {STEP, SUSPEND, *TAIL, *(name for name, how in kind.items() if how == 'loop')}
    counts_of_sites = Counter()
    for (caller, callee), count in sites.items():
        if callee in watched:
            counts_of_sites[caller] += count
    if propose_sites:
        for line in LIST.read_text().splitlines():
            content, mark, reason = line.partition('#')
            if content.strip():
                match = re.match(r'^(\w+\s+)(.*?)(?:\s+sites=\d+)?\s*$', content)
                name = match[2]
                line = f'{match[1]}{name} sites={counts_of_sites[name]}  #{reason}'
            print(line)
        return 0
    problems = []
    missing = sorted(name for name in wanted if name not in kind)
    for name in missing:
        problems.append(f'{name} calls {", ".join(sorted(wanted[name]))} and is not on the list')
    for name in sorted(callers.get(STEP, ())):
        if kind.get(name) not in (None, 'loop', 'entry'):
            problems.append(f'{name} runs the guest (Step) and is listed as {kind[name]}, not as loop')
    for name in sorted(tails):
        if name in kind and kind[name] not in ('resumable', 'loop', 'entry'):
            problems.append(f'{name} hands a tail position on or keeps a record and is listed as {kind[name]}')
    for name, how in sorted(kind.items()):
        if listed[name][2] != counts_of_sites[name]:
            problems.append(f'{name} has {counts_of_sites[name]} callback/wait/tail sites, '
                            f'listed {listed[name][2]}; review the changed sites and update sites=')
        if how == 'resumable' and name not in tails and name not in TAIL:
            problems.append(f'{name} is listed as resumable and neither hands a tail position on nor keeps a record')
        if name not in wanted and name != STEP and name not in TAIL:
            problems.append(f'{name} (line {listed[name][1]}) calls nothing of the list any more: the line goes')
    if propose:
        for name in missing:
            print(f'transient  {name}  # ({", ".join(sorted(wanted[name]))})')
        return 0
    counts = defaultdict(int)
    for how in kind.values():
        counts[how] += 1
    print('state frames: ' + ', '.join(f'{counts[how]} {how}' for how in KINDS) + f'; {len(problems)} to decide')
    for problem in problems[:60]:
        print(f'state frames: {problem}', file=sys.stderr)
    if len(problems) > 60:
        print(f'state frames: and {len(problems) - 60} more', file=sys.stderr)
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True)
    parser.add_argument('--propose', action='store_true')
    parser.add_argument('--propose-sites', action='store_true', help='print the reviewed list with this build\'s site counts')
    parser.add_argument('--list', help='another list than src/state_rules/frames.calls (the test of the guard)')
    arguments = parser.parse_args()
    if arguments.list:
        global LIST
        LIST = Path(arguments.list)
    return check(Path(arguments.binary), arguments.propose, arguments.propose_sites)


if __name__ == '__main__':
    sys.exit(main())
