#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A failed debug reader must not become a reusable callback-guard cache."""
import io
import shutil
from collections import Counter
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
import sys
from unittest.mock import patch

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(1, str(Path(__file__).resolve().parent))
import state_frames


class GuardInputs(unittest.TestCase):
    @unittest.skipUnless(sys.platform.startswith('linux'), 'ELF call instructions')
    def test_external_member_call_keeps_inline_caller(self):
        import neo98
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'callee.cpp').write_text('struct Guest { int Call(int); }; int Guest::Call(int n) { return n+1; }\n')
            (root / 'caller.cpp').write_text('''
struct Guest { int Call(int); };
inline int Callback(Guest &g, int n) { return g.Call(n); }
int Invoke(Guest &g, int n) { return Callback(g,n)+3; }
int main(int argc, char **) { Guest g; return Invoke(g,argc); }
''')
            obj = root / 'caller.o'
            env = neo98.tool_environment()
            subprocess.run(['clang++','-O2','-g','-c',str(root/'caller.cpp'),'-o',str(obj)],env=env,check=True)
            binary = root / 'fixture'
            subprocess.run(['clang++','-O2','-g',str(obj),str(root/'callee.cpp'),'-o',str(binary)],env=env,check=True)
            with patch.object(state_frames,'CACHE',root):
                pairs = state_frames.instruction_calls(binary,[obj])
            names = state_frames.plain({n for pair in pairs for n in pair})
            self.assertTrue(any(names[a]=='Callback' and names[b]=='Guest::Call' for a,b in pairs), pairs)

    @unittest.skipUnless(shutil.which('ld64.lld'), 'a Mach-O linker')
    def test_macho_calls_keep_inline_caller_and_tail_jump(self):
        import neo98
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'callee.cpp').write_text('struct Guest { int Call(int); }; int Guest::Call(int n) { return n+1; }\n')
            (root / 'caller.cpp').write_text('''
struct Guest { int Call(int); };
inline int Callback(Guest &g, int n) { return g.Call(n); }
int Invoke(Guest &g, int n) { return Callback(g,n)+3; }
int Tail(Guest &g, int n) { return g.Call(n); }
int main(int argc, char **) { Guest g; return Invoke(g,argc)+Tail(g,argc); }
''')
            env = neo98.tool_environment()
            target = '--target=arm64-apple-macos13'
            for name in ('caller', 'callee'):
                subprocess.run(['clang++',target,'-O2','-g','-c',str(root/f'{name}.cpp'),'-o',str(root/f'{name}.o')],
                               env=env,check=True)
            binary = root / 'fixture'
            subprocess.run(['ld64.lld','-arch','arm64','-platform_version','macos','13.0','13.0','-e','_main',
                            '-undefined','dynamic_lookup',str(root/'caller.o'),str(root/'callee.o'),'-o',str(binary)],
                           env=env,check=True)
            with patch.object(state_frames,'CACHE',root/'cache'):
                pairs = state_frames.instruction_calls(binary,[root/'caller.o'])
            names = state_frames.plain({n for pair in pairs for n in pair})
            found = Counter({(names[a],names[b]): n for (a,b),n in pairs.items()})
            self.assertEqual(found[('Callback','Guest::Call')], 2, pairs)
            self.assertEqual(found[('Tail','Guest::Call')], 2, pairs)
            self.assertEqual([path.suffix for path in (root/'cache').iterdir()], ['.pickle'])

    def test_new_wait_and_changed_count_fail_the_guard(self):
        from collections import Counter
        with tempfile.TemporaryDirectory() as folder:
            listing = Path(folder)/'frames.calls'
            listing.write_text('restart Known sites=1 # reviewed wait\n')
            for callers, counts in [
                ({'Known'}, Counter({('Known',state_frames.SUSPEND):2})),
                ({'Known','New'}, Counter({('Known',state_frames.SUSPEND):1,('New',state_frames.SUSPEND):1})),
            ]:
                with patch.object(state_frames,'LIST',listing), \
                     patch.object(state_frames,'graph',return_value=({state_frames.SUSPEND:callers},counts)):
                    self.assertEqual(state_frames.check(Path('unused')),1)
    def test_failed_debug_reader_does_not_cache_partial_output(self):
        with tempfile.TemporaryDirectory() as folder:
            cache = Path(folder) / 'cache'
            process = SimpleNamespace(stdout=io.StringIO(''), wait=lambda: 9)
            with patch.object(state_frames, 'CACHE', cache), \
                    patch.object(state_frames.subprocess, 'Popen', return_value=process):
                with self.assertRaisesRegex(SystemExit, 'debug-info reader failed.*exit 9'):
                    state_frames.calls_of(Path(folder) / 'object.o')
            self.assertEqual(list(cache.iterdir()), [])

    def test_debug_names_demangle_on_every_platform(self):
        self.assertEqual(state_frames.plain({'_ZN3X8611Application4StepEv'}),
                         {'_ZN3X8611Application4StepEv': 'X86::Application::Step'})

    def test_failed_demangler_cannot_hide_functions(self):
        with patch.object(state_frames.subprocess, 'run',
                          side_effect=subprocess.CalledProcessError(7, ['c++filt'])):
            with self.assertRaises(subprocess.CalledProcessError):
                state_frames.plain({'_ZN3X8611Application4StepEv'})

    def test_duplicate_function_does_not_replace_reviewed_entry(self):
        with tempfile.TemporaryDirectory() as folder:
            listing = Path(folder) / 'frames.calls'
            listing.write_text('transient X86::Application::Example sites=1 # callback\n'
                               'resumable X86::Application::Example sites=2 # replacement\n')
            with patch.object(state_frames, 'LIST', listing):
                with self.assertRaisesRegex(SystemExit, 'duplicate function.*first listed on line 1'):
                    state_frames.read_list()


if __name__ == '__main__':
    unittest.main()
