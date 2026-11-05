#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The comparison must distinguish Windows failures from NEO98 regressions."""
import argparse
import contextlib
import io
import json
import struct
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest.mock import patch

import wine_conformance as suite
import conformance_campaign as campaign
from wine_conformance_86box import suite_is_running


class ResultsTest(unittest.TestCase):
    def test_completed_recovery_requires_closed_disk_and_complete_journal(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'vm').mkdir()
            (root/'vm/86box.cfg').write_text('[Hard disks]\nhdd_01_fn = disk.img\n')
            (root/'provenance.json').write_text('{"runtime":"86box-win98"}')
            args = argparse.Namespace(reference=root, out=root, completed=True,
                                      group=None, note='Delayed shutdown acknowledgement')
            with patch.object(suite, 'verified_manifest', return_value={}), \
                 patch.object(campaign.subprocess, 'run') as run, \
                 patch('wine_conformance_86box.fat_image', return_value='private-image'), \
                 patch.object(suite, 'parse_logs') as parse, \
                 patch.object(suite, 'finish_run') as finish:
                run.return_value.stdout = '123\n'
                with self.assertRaisesRegex(RuntimeError, 'still open'):
                    campaign.recover(args)
                parse.assert_not_called()
                run.return_value.stdout = ''
                for done, status, group in [(False, 'PASS', None),
                                            (True, 'INCOMPLETE', None),
                                            (True, 'PASS', 'case')]:
                    args.group = group
                    parse.return_value = {'suite_done': done, 'groups': {'case': {'status': status}}}
                    with self.assertRaisesRegex(RuntimeError, 'DONE journal'):
                        campaign.recover(args)
                finish.assert_not_called()
                args.group = None
                parse.return_value = {'suite_done': True, 'groups': {'case': {'status': 'PASS'}}}
                campaign.recover(args)
                metadata = finish.call_args.args[2]
                self.assertTrue(metadata['recovered_after_shutdown'])
                self.assertFalse(metadata['host_timeout'])
                self.assertFalse(metadata['recovered_after_interruption'])

    def test_pe_metadata_rejects_section_outside_loaded_image(self):
        data = bytearray(0x400)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x80)
        data[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<HHIIIHH', data, 0x84, 0x14c, 1, 0, 0, 0, 224, 0x102)
        optional = 0x98
        struct.pack_into('<H', data, optional, 0x10b)
        for offset, value in ((16, 0x1000), (28, 0x400000), (32, 0x1000),
                              (36, 0x200), (56, 0x2000), (60, 0x200), (92, 16)):
            struct.pack_into('<I', data, optional+offset, value)
        struct.pack_into('<H', data, optional+68, 3)
        section = optional+224
        data[section:section+8] = b'.text\0\0\0'
        struct.pack_into('<IIII', data, section+8, 1, 0x1000, 0x200, 0x200)
        struct.pack_into('<I', data, section+36, 0x60000020)
        data[0x200] = 0xc3
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'probe.exe'
            path.write_bytes(data)
            self.assertEqual(suite.pe_details(path)['machine'], 0x14c)
            # GNU ld's unconsumed SafeSEH metadata produced this invalid RVA.
            struct.pack_into('<I', data, section+12, 0xffc00000)
            path.write_bytes(data)
            with self.assertRaisesRegex(RuntimeError, 'outside SizeOfImage'):
                suite.pe_details(path)

    def test_function_isolation_preserves_assertions_and_full_run(self):
        body = 'static void test_one(void) { ok(value == 7, "unchanged"); }\n'
        name, calls, translated = campaign.isolated_entry(
            body+'START_TEST(example) { test_one(); test_two(); }\n')
        self.assertEqual(name, 'example')
        self.assertEqual(calls, ['test_one', 'test_two'])
        self.assertTrue(translated.startswith(body))
        self.assertIn('argc < 3 || !strcmp(argv[2], "test_one")', translated)

    def test_function_isolation_refuses_to_drop_setup_or_control_flow(self):
        for body in ('initialize(); test_one();', 'test_one(42);',
                     'if (ready) test_one();', 'test_one(); test_one();',
                     'test_one(); cleanup();', 'ok(1, "setup"); test_one();'):
            with self.subTest(body=body), self.assertRaises(RuntimeError):
                campaign.isolated_entry('START_TEST(example) { '+body+' }')

    def test_reference_pid_reuse_does_not_keep_finished_suite_running(self):
        pid = 4294884639
        self.assertTrue(suite_is_running([{'pid': pid, 'executable': r'C:\WTEST\SUITE.EXE'}], pid))
        self.assertTrue(suite_is_running([{'pid': pid, 'executable': 'c:/wtest/suite.exe'}], pid))
        self.assertFalse(suite_is_running([{'pid': pid, 'executable': r'C:\WINDOWS\SYSTEM\WMIEXE.EXE'}], pid))
        self.assertFalse(suite_is_running([{'pid': pid + 1, 'executable': r'C:\WTEST\SUITE.EXE'}], pid))
        self.assertFalse(suite_is_running([{'pid': pid, 'executable': r'C:\OTHER\SUITE.EXE'}], pid))
        self.assertFalse(suite_is_running([], pid))

    def test_dynamic_dependencies_are_included_in_system_provenance(self):
        dlls = suite.tested_dlls({'groups': [{'imports': {'kernel32.dll': []},
                                            'runtime_dlls': ['cfgmgr32.dll']}]})
        self.assertIn('CFGMGR32.DLL', dlls)
        self.assertEqual(dlls.count('KERNEL32.DLL'), 1)

    def test_shell_preparation_has_its_own_watchdog_budget(self):
        self.assertEqual(suite.fixture_timeout_ms({}), 0)
        self.assertEqual(suite.fixture_timeout_ms({'fixtures': {'wininit': {}}}), 0)
        self.assertEqual(suite.fixture_timeout_ms({'fixtures': {'shell': {
            'startup_timeout_ms': 120000, 'idle_timeout_ms': 120000}}}), 240000)
        self.assertEqual(suite.fixture_timeout_ms({'fixtures': {'shell': {
            'startup_timeout_ms': 45000}}}), 45000)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = {'groups': [{'name': 'heap', 'exe': 'KHEAP.EXE', 'sha256': 'abc'}],
                         'supervisor_sha256': 'def'}

    def parsed(self, journal, logs):
        (self.root / 'RUN.TSV').write_text(journal)
        for name, text in logs.items():
            (self.root / name).write_text(text)
        return suite.parse_logs(self.root, self.manifest)['groups']['heap']

    def test_diagnostic_subset_keeps_unselected_groups_out(self):
        manifest = {'groups': [{'name': 'heap'}, {'name': 'file'}]}
        self.assertEqual(suite.select_run_groups(manifest, None), {'heap', 'file'})
        self.assertEqual(suite.select_run_groups(manifest, ' file, file '), {'file'})
        self.assertEqual(len(manifest['groups']), 2)
        for names in ('', ', ', 'heap,typo'):
            with self.assertRaises(RuntimeError):
                suite.select_run_groups(manifest, names)

    def test_source_snapshot_includes_new_runtime_files(self):
        def git(*args, **kwargs):
            return subprocess.run(['git', *args], cwd=self.root, check=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
        git('init', '-q')
        (self.root/'src').mkdir()
        original=self.root/'src/existing.cpp';original.write_text('old\n')
        git('add', 'src')
        git('-c','user.name=Conformance test','-c','user.email=test@invalid',
            '-c','commit.gpgsign=false','commit','-qm','fixture')
        original.write_text('changed\n')
        added=self.root/'src/new header.h';added.write_text('new runtime code\n')
        ignored=self.root/'unrelated';ignored.write_text('not runtime\n')
        patch,hashes=suite.capture_source(self.root)
        self.assertIn(b'+changed\n',patch)
        self.assertIn(b'+new runtime code\n',patch)
        self.assertNotIn(b'not runtime',patch)
        self.assertEqual(hashes,{'src/new header.h':suite.digest(added)})
        git('apply','--reverse','--check','-',input=patch)

    def test_frozen_host_retains_original_source_identity(self):
        host = self.root/'host';host.write_bytes(b'host executable')
        patch = self.root/'source.patch';patch.write_bytes(b'old source snapshot')
        prior = self.root/'results.json'
        record = {'runtime': 'neo98', 'host_sha256': suite.digest(host),
                  'source_patch_sha256': suite.digest(patch), 'git_revision': 'old commit',
                  'git_status': 'old changes', 'untracked_source_sha256': {'src/added.cpp': 'hash'}}
        prior.write_text(json.dumps(record))
        info, data = suite.host_provenance(host, prior)
        self.assertEqual(data, patch.read_bytes())
        self.assertEqual(info['git_revision'], 'old commit')
        self.assertEqual(info['host_provenance']['sha256'], suite.digest(prior))
        self.assertEqual(info['untracked_source_sha256'], record['untracked_source_sha256'])

    def test_frozen_host_rejects_mismatched_binary_or_changed_patch(self):
        host = self.root/'host';host.write_bytes(b'host executable')
        patch = self.root/'source.patch';patch.write_bytes(b'old source snapshot')
        prior = self.root/'results.json'
        record = {'runtime': 'neo98', 'host_sha256': suite.digest(host),
                  'source_patch_sha256': suite.digest(patch), 'git_revision': 'old commit',
                  'git_status': 'old changes', 'untracked_source_sha256': {}}
        prior.write_text(json.dumps(record))
        host.write_bytes(b'other host')
        with self.assertRaisesRegex(RuntimeError, 'executable'):
            suite.host_provenance(host, prior)
        host.write_bytes(b'host executable');patch.write_bytes(b'other source')
        with self.assertRaisesRegex(RuntimeError, 'source patch'):
            suite.host_provenance(host, prior)

    def test_normal_failure_is_not_a_crash(self):
        group = self.parsed('BEGIN\theap\nPID\theap\t42\nEND\theap\tEXIT\t1\nDONE\n', {
            'KHEAP.EXE.42.LOG': 'NEO98_WINE_BEGIN pid=42\n'
                'NEO98_CHECK heap.c:59 PASS\nNEO98_CHECK heap.c:59 PASS\n'
                'NEO98_CHECK heap.c:189 FAIL\nheap.c:200: Tests skipped: unavailable\n'
                'NEO98_WINE_END pid=42 exit=1\n'})
        self.assertEqual(group['status'], 'FAIL')
        self.assertEqual(group['sites']['heap.c:59'], {'PASS': 2})
        self.assertEqual(group['skips'], [{'site': 'heap.c:200', 'reason': 'unavailable'}])

    def test_completed_child_does_not_hide_parent_crash(self):
        group = self.parsed('BEGIN\theap\nPID\theap\t42\nEND\theap\tEXIT\t9\nDONE\n', {
            'KHEAP.EXE.42.LOG': 'NEO98_WINE_BEGIN pid=42\nNEO98_CHECK heap.c:59 PASS\n',
            'KHEAP.EXE.43.LOG': 'NEO98_WINE_BEGIN pid=43\nNEO98_WINE_END pid=43 exit=0\n'})
        self.assertEqual(group['status'], 'CRASH')

    def test_atomic_assertions_after_interleaved_trace_prefixes(self):
        group = self.parsed('', {'KHEAP.EXE.42.LOG':
            'sock.c:589: sock.c:589: NEO98_CHECK sock.c:405 PASS\n'
            'shared.h:2: NEO98_CHECK sock.c:406 FAIL\n'
            'sock.c:405: Test succeeded\n'
            'quoted NEO98_CHECK sock.c:407 PASS\n'
            'NEO98_CHECK sock.c:408 PASS trailing garbage\n'
            'NEO98_CHECK sock.c:409 PA\n'})
        self.assertEqual(group['sites'], {'sock.c:405': {'PASS': 1}, 'sock.c:406': {'FAIL': 1}})

    def test_reparse_preserves_original_and_verifies_raw_evidence(self):
        self.parsed('BEGIN\theap\nPID\theap\t42\nEND\theap\tEXIT\t0\nDONE\n', {
            'KHEAP.EXE.42.LOG': 'NEO98_WINE_BEGIN pid=42\n'
                'heap.c:1: NEO98_CHECK heap.c:2 PASS\nNEO98_WINE_END pid=42 exit=0\n'})
        with contextlib.redirect_stdout(io.StringIO()):
            original = suite.finish_run(self.root, self.manifest, {'runtime': 'neo98', 'host_sha256': 'host'})
        # Simulate the older parser dropping the prefixed check.
        original['groups']['heap']['sites'] = {}
        original['groups']['heap']['counts'] = {}
        suite.save(self.root / 'results.json', original)
        prior_hash = suite.digest(self.root / 'results.json')
        with tempfile.TemporaryDirectory() as parent:
            destination = Path(parent) / 'reparsed'
            args = argparse.Namespace(source=self.root, results=destination)
            with contextlib.redirect_stdout(io.StringIO()):
                result = suite.reparse_run(args)
            self.assertEqual(result['groups']['heap']['sites'], {'heap.c:2': {'PASS': 1}})
            self.assertEqual(result['host_sha256'], 'host')
            self.assertEqual(result['reparsed_from']['results_sha256'], prior_hash)
            self.assertEqual(suite.digest(self.root / 'results.json'), prior_hash)
            self.assertEqual((destination/'KHEAP.EXE.42.LOG').read_bytes(), (self.root/'KHEAP.EXE.42.LOG').read_bytes())
            with self.assertRaises(FileExistsError):
                suite.reparse_run(args)
            args.results = self.root
            with self.assertRaisesRegex(RuntimeError, 'outside'):
                suite.reparse_run(args)
            args.results = Path(parent) / 'changed'
            (self.root/'KHEAP.EXE.42.LOG').write_text('changed')
            with self.assertRaisesRegex(RuntimeError, 'Raw logs differ'):
                suite.reparse_run(args)

    def test_timeout_and_missing_groups_remain_visible(self):
        self.assertEqual(self.parsed('BEGIN\theap\nEND\theap\tTIMEOUT\t248\n', {})['status'], 'TIMEOUT')
        self.assertEqual(self.parsed('', {})['status'], 'NOT_RUN')

    def test_zero_host_exit_after_deadline_is_still_a_timeout(self):
        (self.root/'heap-host.log').write_text('NEO98 HOST deadline 90 s reached\n')
        group = self.parsed('BEGIN\theap\nEND\theap\tHOST_ERROR\t0\nDONE\n', {})
        self.assertEqual(group['status'], 'TIMEOUT')
        self.assertEqual(group['timeout_source'], 'neo98-host-watchdog')

    def test_reference_watchdog_uses_journal_not_stale_process_observation(self):
        self.manifest['groups'].append({'name': 'next', 'exe': 'NEXT.EXE', 'sha256': 'next'})
        self.parsed('BEGIN\theap\nPID\theap\t42\nEND\theap\tEXIT\t0\nBEGIN\tnext\n', {
            'KHEAP.EXE.42.LOG': 'NEO98_WINE_BEGIN pid=42\nNEO98_WINE_END pid=42 exit=0\n'})
        with contextlib.redirect_stdout(io.StringIO()):
            result = suite.finish_run(self.root, self.manifest, {'host_timeout': True, 'host_timeout_group': 'heap'})
        self.assertEqual(result['groups']['heap']['status'], 'PASS')
        self.assertEqual(result['groups']['next']['status'], 'TIMEOUT')
        self.assertEqual(result['last_observed_timeout_group'], 'heap')
        self.assertEqual(result['host_timeout_group'], 'next')

    def test_todo_outcomes_are_preserved(self):
        group = self.parsed('', {'KHEAP.EXE.42.LOG':
            'NEO98_CHECK heap.c:10 TODO_FAIL\nNEO98_CHECK heap.c:11 TODO_PASS\n'})
        self.assertEqual(group['counts'], {'TODO_FAIL': 1, 'TODO_PASS': 1})

    def test_cpp_and_shared_header_skips_remain_visible(self):
        group = self.parsed('', {'KHEAP.EXE.42.LOG':
            'atltypes.cpp:40: Tests skipped: unavailable\n'
            'shared.h:90: Tests skipped: missing extension\n'})
        self.assertEqual([s['site'] for s in group['skips']], ['atltypes.cpp:40', 'shared.h:90'])

    def test_ne_system_module_is_hashed_without_inventing_a_pe_version(self):
        system = self.root/'WINDOWS/SYSTEM'; system.mkdir(parents=True)
        data = bytearray(256); data[:2] = b'MZ'; data[60:64] = (128).to_bytes(4, 'little'); data[128:130] = b'NE'
        path = system/'LEGACY.DLL'; path.write_bytes(data)
        result = suite.installation(self.root, ['LEGACY.DLL'])['LEGACY.DLL']
        self.assertEqual(result['format'], 'NE')
        self.assertIsNone(result['version'])
        self.assertEqual(result['sha256'], suite.digest(path))

    def comparison(self, ref_sites, arm_sites, ref_skips=(), arm_skips=()):
        reference, candidate = self.root / 'ref', self.root / 'arm'
        reference.mkdir(); candidate.mkdir()
        for directory, sites, skips in [(reference, ref_sites, ref_skips), (candidate, arm_sites, arm_skips)]:
            suite.save(directory / 'manifest.json', self.manifest)
            suite.save(directory / 'results.json', {
                'runtime': '86box-win98' if directory == reference else 'neo98',
                'info': ['INFO\tos=4.10.2222\tplatform=1\tcsd= A '], 'groups': {'heap': {
                'status': 'FAIL', 'sites': sites, 'skips': list(skips)}}})
        args = argparse.Namespace(reference=reference, candidate=candidate, report=self.root / 'comparison.json')
        with contextlib.redirect_stdout(io.StringIO()):
            suite.compare(args)
        return args, json.loads(args.report.read_text())

    def test_native_failures_and_mixed_sites_do_not_become_regressions(self):
        _, report = self.comparison(
            {'heap.c:1': {'PASS': 2}, 'heap.c:2': {'FAIL': 1},
             'heap.c:3': {'PASS': 1, 'FAIL': 1}, 'heap.c:4': {'PASS': 1},
             'heap.c:5': {'PASS': 3}},
            {'heap.c:1': {'FAIL': 2}, 'heap.c:2': {'FAIL': 1},
             'heap.c:3': {'FAIL': 2}, 'heap.c:5': {'PASS': 1}},
            arm_skips=[{'site': 'heap.c:99', 'reason': 'missing API'}])
        self.assertEqual([r['site'] for r in report['regressions']], ['heap.c:1'])
        self.assertEqual([r['site'] for r in report['unreached']], ['heap.c:4'])
        self.assertEqual([r['site'] for r in report['count_changes']], ['heap.c:5'])
        self.assertEqual(report['additional_skips'], [{'group': 'heap', 'site': 'heap.c:99', 'count': 1}])

    def test_different_exes_cannot_be_compared(self):
        args, _ = self.comparison({}, {})
        altered = json.loads((args.candidate / 'manifest.json').read_text())
        altered['groups'][0]['sha256'] = 'different'
        suite.save(args.candidate / 'manifest.json', altered)
        with self.assertRaisesRegex(RuntimeError, 'identical test binaries'):
            suite.compare(args)

    def test_comparison_keeps_partial_native_results_out_of_requirements(self):
        args, _ = self.comparison({'heap.c:1': {'PASS': 1}}, {'heap.c:1': {'FAIL': 1}})
        reference = json.loads((args.reference/'results.json').read_text())
        reference['groups']['heap']['status'] = 'CRASH'
        suite.save(args.reference/'results.json', reference)
        with contextlib.redirect_stdout(io.StringIO()):
            suite.compare(args)
        report = json.loads(args.report.read_text())
        self.assertEqual(report['regressions'], [])
        self.assertEqual(report['incomplete_reference'], ['heap'])

    def test_different_input_assets_cannot_be_compared(self):
        args, _ = self.comparison({}, {})
        altered = json.loads((args.candidate / 'manifest.json').read_text())
        altered['assets'] = {'sample.bmp': 'different'}
        suite.save(args.candidate / 'manifest.json', altered)
        with self.assertRaisesRegex(RuntimeError, 'identical test binaries'):
            suite.compare(args)

    def test_campaign_does_not_require_partial_native_results(self):
        batch = self.root/'batch'
        reference = batch/'reference'; reference.mkdir(parents=True)
        suite.save(reference/'manifest.json', self.manifest)
        native = {'runtime': '86box-win98', 'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
                  'suite_done': True, 'groups': {'heap': {
                      'status': 'CRASH', 'sites': {'heap.c:1': {'PASS': 1}}, 'skips': []}}}
        suite.save(reference/'results.json', native)
        campaign.summarize(self.root)
        result = json.loads((self.root/'summary.json').read_text())['groups']['heap']
        self.assertEqual(result['required_sites'], 0)
        self.assertEqual(result['candidate'], 'NOT_RUN')
        native['groups']['heap']['status'] = 'FAIL'
        suite.save(reference/'results.json', native)
        campaign.summarize(self.root)
        result = json.loads((self.root/'summary.json').read_text())['groups']['heap']
        self.assertEqual(result['required_sites'], 1)
        self.assertEqual(result['unreached'], ['heap.c:1'])

    def test_campaign_does_not_accept_a_zero_assertion_smoke_as_contract(self):
        batch = self.root/'batch'
        reference = batch/'reference'; reference.mkdir(parents=True)
        suite.save(reference/'manifest.json', self.manifest)
        suite.save(reference/'results.json', {'runtime': '86box-win98',
            'info': ['INFO\tos=4.10.2222\tplatform=1\t'], 'groups': {'heap': {
                'status': 'PASS', 'sites': {}, 'skips': []}}})
        campaign.summarize(self.root)
        result = json.loads((self.root/'summary.json').read_text())['groups']['heap']
        self.assertEqual(result['qualification'], 'NO_COMPLETE_REFERENCE_ASSERTIONS')

    def test_native_selection_keeps_failures_and_interactive_reviews(self):
        reference = {'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
                     'groups': {'heap': {'status': 'FAIL'}}}
        group = {'name': 'heap'}
        self.assertIsNone(suite.reference_skip_reason(reference, group))
        for status in ('CRASH', 'LOAD_ERROR', 'NOT_RUN', 'TIMEOUT'):
            reference['groups']['heap']['status'] = status
            self.assertIn(status, suite.reference_skip_reason(reference, group))
        group['assessment'] = 'INTERACTIVE_REVIEW'
        self.assertIsNone(suite.reference_skip_reason(reference, group))
        reference['info'] = []
        self.assertIn('did not identify', suite.reference_skip_reason(reference, group))

    def test_reference_overlay_adds_only_absent_verified_modules(self):
        ref = self.root/'ref'; originals = ref/'system-provenance/WINDOWS/SYSTEM'; originals.mkdir(parents=True)
        system = self.root/'guest/WINDOWS/SYSTEM'; system.mkdir(parents=True)
        (system/'existing.dll').write_bytes(b'keep installed version')
        (originals/'ADDON.DLL').write_bytes(b'original reference DLL')
        reference = {'windows_dlls': {'EXISTING.DLL': {'sha256': 'other-version'},
            'ADDON.DLL': {'sha256': suite.digest(originals/'ADDON.DLL')}}}
        overlay = suite.reference_dll_overlay(ref/'results.json', reference, self.root/'guest')
        self.assertEqual(set(overlay), {'ADDON.DLL'})
        self.assertEqual((system/'existing.dll').read_bytes(), b'keep installed version')
        (originals/'ADDON.DLL').write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError, 'recorded hash'):
            suite.reference_dll_overlay(ref/'results.json', reference, self.root/'guest')

    def test_explicit_reference_version_is_verified_and_does_not_mutate_source(self):
        ref = self.root/'ref'; originals = ref/'system-provenance/WINDOWS/SYSTEM'; originals.mkdir(parents=True)
        system = self.root/'guest/WINDOWS/SYSTEM'; system.mkdir(parents=True)
        (system/'riched20.dll').write_bytes(b'old installed version')
        dll = originals/'RICHED20.DLL'; dll.write_bytes(b'captured original version')
        reference = {'windows_dlls': {'RICHED20.DLL': {'sha256': suite.digest(dll)}}}
        self.assertEqual(suite.reference_dll_overlay(ref/'results.json', reference, self.root/'guest'), {})
        overlay = suite.reference_dll_overlay(ref/'results.json', reference, self.root/'guest', ['riched20.dll'])
        self.assertEqual(overlay, {'RICHED20.DLL': dll})
        self.assertEqual((system/'riched20.dll').read_bytes(), b'old installed version')
        with self.assertRaisesRegex(RuntimeError, 'not captured'):
            suite.reference_dll_overlay(ref/'results.json', reference, self.root/'guest', ['MISSING.DLL'])
        dll.write_bytes(b'tampered')
        with self.assertRaisesRegex(RuntimeError, 'recorded hash'):
            suite.reference_dll_overlay(ref/'results.json', reference, self.root/'guest', ['RICHED20.DLL'])

    def test_selection_skip_is_not_a_successful_run(self):
        group = self.parsed('END\theap\tSKIP\t0\nDONE\n', {})
        self.assertEqual(group['status'], 'SKIP')
        self.assertEqual(group['sites'], {})

    def test_qualified_contract_keeps_improvements_and_reports_missing_checks(self):
        baseline = {'supervisor_sha256': 'def', 'groups': {'heap': {
            'sha256': 'abc', 'required': {'heap.c:1': {'PASS': 1}, 'heap.c:2': {'PASS': 1}},
            'advisory': {'heap.c:3': {'FAIL': 1}}, 'skips': {}}}}
        candidate = {'suite_done': True, 'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
            'groups': {'heap': {'status': 'PASS', 'sites': {
                'heap.c:1': {'PASS': 1}, 'heap.c:3': {'PASS': 1}}, 'skips': []}}}
        report = suite.evaluate_baseline(baseline, candidate, self.manifest)
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['groups']['heap']['checks']['heap.c:2']['status'], 'MISSING')
        self.assertEqual(len(report['improvements']), 1)
        candidate['groups']['heap']['sites']['heap.c:2'] = {'PASS': 1}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'PASS')
        candidate['groups']['heap']['status'] = 'TIMEOUT'
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')

    def test_alternative_needs_every_check_and_never_waives_a_failure(self):
        baseline = {'supervisor_sha256': 'def', 'groups': {'heap': {
            'sha256': 'abc', 'required': {'heap.c:1': {'PASS': 1}}, 'advisory': {}, 'skips': {},
            'alternatives': {'heap.c:1': {'reason': 'Successful alternative branch',
                'requires': ['heap.c:2', 'heap.c:3']}}}}}
        candidate = {'suite_done': True, 'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
            'groups': {'heap': {'status': 'PASS', 'sites': {'heap.c:2': {'PASS': 1}}, 'skips': []}}}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')
        candidate['groups']['heap']['sites']['heap.c:3'] = {'PASS': 1}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'PASS')
        candidate['groups']['heap']['sites']['heap.c:1'] = {'FAIL': 1}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')

    def test_qualification_rejects_partial_reference(self):
        ref = self.root / 'ref'; ref.mkdir()
        suite.save(ref / 'manifest.json', self.manifest)
        suite.save(ref / 'results.json', {'runtime': '86box-win98', 'suite_done': False})
        with self.assertRaisesRegex(RuntimeError, 'completed original'):
            suite.qualify(argparse.Namespace(reference=ref, baseline=self.root / 'baseline.json'))

    def test_gate_rejects_new_skips_and_incomplete_batches(self):
        baseline = {'supervisor_sha256': 'def', 'groups': {'heap': {
            'sha256': 'abc', 'required': {'heap.c:1': {'PASS': 1}},
            'advisory': {}, 'skips': {'heap.c:9': 1}}}}
        candidate = {'suite_done': True, 'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
            'groups': {'heap': {'status': 'PASS', 'sites': {'heap.c:1': {'PASS': 1}},
                'skips': [{'site': 'heap.c:9'}]}}}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'PASS')
        candidate['groups']['heap']['skips'].append({'site': 'heap.c:9'})
        report = suite.evaluate_baseline(baseline, candidate, self.manifest)
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['groups']['heap']['additional_skips'], {'heap.c:9': 1})
        candidate['groups']['heap']['skips'].clear()
        candidate['suite_done'] = False
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')

    def test_gate_rejects_changed_binary_and_non_win98_environment(self):
        baseline = {'supervisor_sha256': 'def', 'groups': {'heap': {
            'sha256': 'abc', 'required': {}, 'advisory': {}, 'skips': {}}}}
        candidate = {'suite_done': True, 'info': ['INFO\tos=5.1.2600\tplatform=2\t']}
        with self.assertRaisesRegex(RuntimeError, 'not a Win98'):
            suite.evaluate_baseline(baseline, candidate, self.manifest)
        self.manifest['supervisor_sha256'] = 'different'
        with self.assertRaisesRegex(RuntimeError, 'qualified test binaries'):
            suite.evaluate_baseline(baseline, candidate, self.manifest)

    def test_repeated_passes_and_mixed_native_outcomes_cannot_hide_lost_coverage(self):
        baseline = {'supervisor_sha256': 'def', 'groups': {'heap': {
            'sha256': 'abc', 'required': {'heap.c:1': {'PASS': 12}},
            'advisory': {'heap.c:2': {'PASS': 8, 'FAIL': 2}}, 'skips': {}}}}
        candidate = {'suite_done': True, 'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
            'groups': {'heap': {'status': 'FAIL', 'sites': {
                'heap.c:1': {'PASS': 1}, 'heap.c:2': {'PASS': 3, 'FAIL': 7}}, 'skips': []}}}
        report = suite.evaluate_baseline(baseline, candidate, self.manifest)
        self.assertEqual(report['status'], 'FAIL')
        self.assertEqual(report['groups']['heap']['checks']['heap.c:1']['status'], 'COUNT_SHORTFALL')
        self.assertEqual(report['groups']['heap']['mixed_checks']['heap.c:2']['status'], 'COUNT_SHORTFALL')
        candidate['groups']['heap']['sites']['heap.c:1'] = {'PASS': 12}
        candidate['groups']['heap']['sites']['heap.c:2'] = {'PASS': 8, 'FAIL': 2}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'PASS')
        candidate['groups']['heap']['sites']['heap.c:2'] = {'PASS': 10}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'PASS')
        baseline['groups']['heap']['variable_counts'] = {'heap.c:1': 'One check per mounted drive'}
        candidate['groups']['heap']['sites']['heap.c:1'] = {'PASS': 2}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'PASS')
        candidate['groups']['heap']['sites']['heap.c:1'] = {'PASS': 2, 'FAIL': 1}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')
        candidate['groups']['heap']['sites']['heap.c:1'] = {}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')

    def test_variable_events_still_require_both_complete_clients(self):
        baseline = {'supervisor_sha256': 'def', 'groups': {'heap': {
            'sha256': 'abc', 'required': {'event': {'PASS': 10},
                'backpressure': {'PASS': 1}, 'complete': {'PASS': 2}},
            'advisory': {}, 'skips': {},
            'variable_counts': {'event': 'Events can coalesce'},
            'alternatives': {'backpressure': {'reason': 'Send buffer need not fill',
                'requires': ['event', 'complete']}}}}}
        candidate = {'suite_done': True, 'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
            'groups': {'heap': {'status': 'PASS', 'sites': {
                'event': {'PASS': 3}, 'complete': {'PASS': 2}}, 'skips': []}}}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'PASS')
        sites = candidate['groups']['heap']['sites']
        sites['complete'] = {'PASS': 1}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')
        sites['complete'] = {'PASS': 2}
        sites['backpressure'] = {'FAIL': 1}
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')

    def test_bounded_callbacks_require_exact_reviewed_improvements(self):
        rule = {'maximum_deficit': 4, 'requires': ['reject', 'error'], 'reason': 'Native accepts invalid handles'}
        baseline = {'supervisor_sha256': 'def', 'groups': {'heap': {
            'sha256': 'abc', 'required': {'callback': {'PASS': 221}},
            'advisory': {}, 'skips': {}, 'bounded_counts': {'callback': rule}}}}
        sites = {'callback': {'PASS': 217}, 'reject': {'PASS': 1}, 'error': {'PASS': 1}}
        candidate = {'suite_done': True, 'info': ['INFO\tos=4.10.2222\tplatform=1\t'],
            'groups': {'heap': {'status': 'PASS', 'sites': sites, 'skips': []}}}
        report = suite.evaluate_baseline(baseline, candidate, self.manifest)
        self.assertEqual(report['status'], 'PASS')
        self.assertEqual(report['groups']['heap']['checks']['callback']['bounded_count_review'], rule)
        for counts in ({'PASS': 216}, {'PASS': 217, 'FAIL': 1}, {}):
            sites['callback'] = counts
            self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')
        sites['callback'] = {'PASS': 217}
        for counts in ({}, {'FAIL': 1}, {'PASS': 2}, {'PASS': 1, 'FAIL': 1}):
            sites['error'] = counts
            self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')
        sites['error'] = {'PASS': 1}
        candidate['suite_done'] = False
        self.assertEqual(suite.evaluate_baseline(baseline, candidate, self.manifest)['status'], 'FAIL')

    def test_qualification_keeps_only_unmixed_native_passes(self):
        ref = self.root / 'ref'; ref.mkdir()
        manifest = {**self.manifest, 'pin': {'revision':
            '36def4af0ca85a1d0e66b5207056775bcb3b09ff'}}
        suite.save(ref / 'manifest.json', manifest)
        suite.save(ref / 'results.json', {'runtime': '86box-win98', 'suite_done': True,
            'info': ['INFO\tos=4.10.2222\tplatform=1\t'], 'groups': {'heap': {
                'status': 'FAIL', 'skips': [], 'sites': {'heap.c:1': {'PASS': 2},
                    'heap.c:2': {'PASS': 1, 'FAIL': 1}, 'heap.c:3': {'FAIL': 1},
                    'heap.c:4': {'TODO_PASS': 1}}}}})
        path = self.root / 'baseline.json'
        with contextlib.redirect_stdout(io.StringIO()):
            suite.qualify(argparse.Namespace(reference=ref, baseline=path))
        group = json.loads(path.read_text())['groups']['heap']
        self.assertEqual(set(group['required']), {'heap.c:1'})
        self.assertEqual(set(group['advisory']), {'heap.c:2', 'heap.c:3', 'heap.c:4'})

    def test_group_selection_and_dos_filenames(self):
        self.assertEqual(suite.groups_argument('heap,gdi_pen,user_fmt'), ['heap', 'gdi_pen', 'user_fmt'])
        for invalid in ['heap,heap', 'unknown', '../heap']:
            with self.assertRaises(argparse.ArgumentTypeError):
                suite.groups_argument(invalid)
        names = [suite.group_executable(name) for name in suite.GROUP_SOURCES]
        self.assertEqual(len(names), len(set(names)))

    def test_qualification_subset_records_incomplete_exclusions(self):
        ref=self.root/'ref';ref.mkdir();baseline=self.root/'baseline.json'
        manifest={**self.manifest,'pin':{'revision':'36def4af0ca85a1d0e66b5207056775bcb3b09ff'}}
        manifest['groups']=[*manifest['groups'],{'name':'file','sha256':'other'}]
        suite.save(ref/'manifest.json',manifest)
        suite.save(ref/'results.json',{'runtime':'86box-win98','suite_done':True,
            'info':['INFO\tos=4.10.2222\tplatform=1\t'],'groups':{
                'heap':{'status':'PASS','skips':[],'sites':{'heap.c:1':{'PASS':2}}},
                'file':{'status':'TIMEOUT','skips':[],'sites':{}}}})
        with self.assertRaisesRegex(RuntimeError,'Incomplete reference'):
            suite.qualify(argparse.Namespace(reference=ref,baseline=baseline))
        with contextlib.redirect_stdout(io.StringIO()):
            suite.qualify(argparse.Namespace(reference=ref,baseline=baseline,names='heap'))
        data=json.loads(baseline.read_text())
        self.assertEqual(data['reference_selection'],{'selected':['heap'],'excluded':{'file':'TIMEOUT'}})
        self.assertEqual(set(data['groups']),{'heap'})
        candidate=json.loads((ref/'results.json').read_text())
        self.assertEqual(suite.evaluate_baseline(data,candidate,manifest)['status'],'PASS')
        changed=json.loads(json.dumps(manifest));changed['groups'][0]['sha256']='wrong'
        with self.assertRaisesRegex(RuntimeError,'qualified test binaries'):
            suite.evaluate_baseline(data,candidate,changed)
        with self.assertRaisesRegex(RuntimeError,'Incomplete reference'):
            suite.qualify(argparse.Namespace(reference=ref,baseline=baseline,names='file'))

    def test_reactos_qualification_has_no_wine_alternatives_and_binds_assets(self):
        ref = self.root / 'ref'; ref.mkdir()
        pin = json.loads((suite.SUPPORT / 'campaign-pins.json').read_text())['reactos']
        manifest = {**self.manifest, 'pin': pin, 'assets': {'test.manifest': 'asset-hash'}}
        result = {'runtime': '86box-win98', 'suite_done': True,
            'info': ['INFO\tos=4.10.2222\tplatform=1\t'], 'groups': {'heap': {
                'status': 'PASS', 'skips': [], 'sites': {'heap.c:1': {'PASS': 1}}}}}
        suite.save(ref / 'manifest.json', manifest)
        suite.save(ref / 'results.json', result)
        path = self.root / 'baseline.json'
        with contextlib.redirect_stdout(io.StringIO()):
            suite.qualify(argparse.Namespace(reference=ref, baseline=path))
        baseline = json.loads(path.read_text())
        self.assertIsNone(baseline['alternatives_sha256'])
        self.assertEqual(baseline['groups']['heap']['alternatives'], {})
        self.assertEqual(suite.evaluate_baseline(baseline, result, manifest)['status'], 'PASS')
        with self.assertRaisesRegex(RuntimeError, 'input assets'):
            suite.evaluate_baseline(baseline, result, {**manifest, 'assets': {}})
        manifest['pin'] = {**pin, 'revision': 'unknown'}
        suite.save(ref / 'manifest.json', manifest)
        with self.assertRaisesRegex(RuntimeError, 'recognized pinned source'):
            suite.qualify(argparse.Namespace(reference=ref, baseline=path))

    def test_qualification_rejects_abi_and_empty_pass_contracts(self):
        ref = self.root / 'ref'; ref.mkdir()
        pin = json.loads((suite.SUPPORT / 'pin.json').read_text())
        for source, counts in [('generated.c', {'PASS': 1}), ('heap.c', {'FAIL': 1})]:
            manifest = {**self.manifest, 'pin': pin,
                'groups': [{**self.manifest['groups'][0], 'source': source}]}
            suite.save(ref / 'manifest.json', manifest)
            suite.save(ref / 'results.json', {'runtime': '86box-win98', 'suite_done': True,
                'info': ['INFO\tos=4.10.2222\tplatform=1\t'], 'groups': {'heap': {
                    'status': 'FAIL', 'skips': [], 'sites': {'heap.c:1': counts}}}})
            with self.assertRaisesRegex(RuntimeError, 'No qualifying runtime assertions'):
                suite.qualify(argparse.Namespace(reference=ref, baseline=self.root / 'baseline.json'))

    def test_mixed_bundle_keeps_abi_hash_without_qualifying_its_assertions(self):
        ref = self.root / 'ref'; ref.mkdir()
        pin = json.loads((suite.SUPPORT / 'pin.json').read_text())
        manifest = {**self.manifest, 'pin': pin, 'groups': [
            {**self.manifest['groups'][0], 'source': 'heap.c'},
            {'name': 'layout', 'source': 'generated.c', 'sha256': 'layout-hash'}]}
        result = {'runtime': '86box-win98', 'suite_done': True,
                  'info': ['INFO\tos=4.10.2222\tplatform=1\t'], 'groups': {
                      'heap': {'status': 'PASS', 'sites': {'heap.c:1': {'PASS': 1}}, 'skips': []},
                      'layout': {'status': 'PASS', 'sites': {'generated.c:1': {'PASS': 100}}, 'skips': []}}}
        suite.save(ref / 'manifest.json', manifest); suite.save(ref / 'results.json', result)
        path = self.root / 'baseline.json'
        suite.qualify(argparse.Namespace(reference=ref, baseline=path))
        baseline = json.loads(path.read_text())
        self.assertEqual(set(baseline['groups']), {'heap'})
        self.assertEqual(set(baseline['non_runtime_groups']), {'layout'})
        report = suite.evaluate_baseline(baseline, result, manifest)
        self.assertEqual(report['status'], 'PASS')
        self.assertEqual(set(report['groups']), {'heap'})
        manifest['groups'][1]['sha256'] = 'changed'
        with self.assertRaisesRegex(RuntimeError, 'qualified test binaries'):
            suite.evaluate_baseline(baseline, result, manifest)

    def test_completed_reference_skips_are_visible_but_do_not_qualify(self):
        ref = self.root / 'ref'; ref.mkdir()
        pin = json.loads((suite.SUPPORT / 'pin.json').read_text())
        manifest = {**self.manifest, 'pin': pin, 'groups': [
            {**self.manifest['groups'][0], 'source': 'heap.c'},
            {'name': 'unavailable', 'source': 'format.c', 'sha256': 'format-hash'}]}
        unavailable = {'status': 'PASS', 'sites': {},
                       'skips': [{'site': 'format.c:1', 'text': 'API unavailable'}]}
        result = {'runtime': '86box-win98', 'suite_done': True,
                  'info': ['INFO\tos=4.10.2222\tplatform=1\t'], 'groups': {
                      'heap': {'status': 'PASS', 'sites': {'heap.c:1': {'PASS': 1}}, 'skips': []},
                      'unavailable': unavailable}}
        suite.save(ref / 'manifest.json', manifest); suite.save(ref / 'results.json', result)
        path = self.root / 'baseline.json'
        suite.qualify(argparse.Namespace(reference=ref, baseline=path))
        baseline = json.loads(path.read_text())
        self.assertEqual(set(baseline['groups']), {'heap'})
        self.assertEqual(baseline['non_runtime_groups']['unavailable']['assessment'],
                         'NO_REFERENCE_ASSERTIONS')
        self.assertEqual(suite.evaluate_baseline(baseline, result, manifest)['status'], 'PASS')
        for replacement in ({**unavailable, 'status': 'TIMEOUT'}, {**unavailable, 'skips': []}):
            result['groups']['unavailable'] = replacement
            suite.save(ref / 'results.json', result)
            with self.assertRaisesRegex(RuntimeError, 'Incomplete reference group'):
                suite.qualify(argparse.Namespace(reference=ref, baseline=path))
        manifest['groups'] = manifest['groups'][1:]
        result['groups'] = {'unavailable': unavailable}
        suite.save(ref / 'manifest.json', manifest); suite.save(ref / 'results.json', result)
        with self.assertRaisesRegex(RuntimeError, 'No qualifying runtime assertions'):
            suite.qualify(argparse.Namespace(reference=ref, baseline=path))

    def test_invalid_harness_runs_remain_visible_without_counting_as_os_attempts(self):
        for name in ('wine', 'reactos', 'sdl12'):
            (self.root / name).mkdir()
            suite.save(self.root / name / 'catalog.json', {'source_pin': {'revision': 'fixture'},
                'groups': [{'name': name + '_test', 'build_status': 'BUILT', 'sha256': 'fixture'}]})
            suite.save(self.root / name / 'manifest.json', {'compiler': 'fixture'})
        folder = self.root / 'broken-startup'
        (folder / '001/reference').mkdir(parents=True)
        suite.save(folder / '001/reference/results.json', {})
        suite.save(folder / 'invalid-harness.json', {'reason': 'Recursive startup helper'})
        args = argparse.Namespace(root=self.root, report=self.root / 'report.json')
        with contextlib.redirect_stdout(io.StringIO()):
            campaign.export_report(args)
        report = json.loads(args.report.read_text())
        self.assertEqual(report['excluded_runs']['broken-startup']['reason'], 'Recursive startup helper')
        self.assertEqual(report['totals']['with_reference_attempt'], 0)
        self.assertEqual(report['totals']['built'], 3)
        suite.save(folder / 'invalid-harness.json', {})
        with self.assertRaisesRegex(RuntimeError, 'explicit reason'):
            campaign.export_report(args)

    def test_old_binary_pass_does_not_count_as_coverage_of_a_rebuild(self):
        sources = {'wine': {'counts': {'BUILT': 1, 'BUILD_ERROR': 2}, 'groups': [
            {'name': 'heap', 'build_status': 'BUILT', 'sha256': 'new'}]}}
        runs = {'heap': [{'sha256': 'old', 'reference': 'PASS', 'candidate': 'PASS'}]}
        totals = campaign.coverage_totals(sources, runs)
        self.assertEqual(totals['with_reference_attempt'], 0)
        self.assertEqual(totals['with_neo98_attempt'], 0)
        self.assertEqual(totals['not_run'], ['heap'])
        runs['heap'].append({'sha256': 'new', 'reference': 'FAIL', 'candidate': 'TIMEOUT'})
        totals = campaign.coverage_totals(sources, runs)
        self.assertEqual(totals['with_completed_reference'], 1)
        self.assertEqual(totals['with_neo98_attempt'], 1)
        self.assertEqual(totals['not_run'], [])


if __name__ == '__main__':
    unittest.main()
