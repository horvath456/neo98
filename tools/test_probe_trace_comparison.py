# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import json
from pathlib import Path
import tempfile
import unittest
from compare_probe_traces import compare
from wine_conformance import digest


class ProbeTraceComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)

    def fixture(self,name,lines=('RESULT one','RESULT two'),binary='exe',status='PASS'):
        p=self.root/name;p.mkdir();log=p/'TEST.LOG'
        log.write_text(''.join(f'test.c:1: {line}\n' for line in lines))
        (p/'manifest.json').write_text(json.dumps({'supervisor_sha256':'supervisor',
            'groups':[{'name':'probe','sha256':binary}]}))
        (p/'results.json').write_text(json.dumps({'groups':{'probe':{'status':status,
            'processes':[{'log':log.name,'sha256':digest(log)}]}}}))
        return p

    def test_matching_ordered_trace(self):
        self.assertEqual(compare(self.fixture('ref'),self.fixture('arm'),'probe','RESULT ',2)['status'],'PASS')

    def test_reordering_is_a_difference(self):
        self.assertEqual(compare(self.fixture('ref'),self.fixture('arm',('RESULT two','RESULT one')),
                                 'probe','RESULT ',2)['status'],'FAIL')

    def test_missing_trace_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'incomplete probe traces'):
            compare(self.fixture('ref'),self.fixture('arm',('RESULT one',)),'probe','RESULT ',2)

    def test_different_exe_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'different probe EXE'):
            compare(self.fixture('ref'),self.fixture('arm',binary='other'),'probe','RESULT ',2)

    def test_same_exe_with_different_preparation_is_rejected(self):
        reference=self.fixture('ref');candidate=self.fixture('arm')
        path=candidate/'manifest.json';original=json.loads(path.read_text())
        for key,value in [('supervisor_sha256','different'),('fixtures',{'shell':True}),('assets',{'input':'changed'})]:
            path.write_text(json.dumps({**original,key:value}))
            with self.assertRaisesRegex(ValueError,'supervisor/fixtures'):
                compare(reference,candidate,'probe','RESULT ',2)

    def test_changed_log_is_rejected(self):
        candidate=self.fixture('arm');(candidate/'TEST.LOG').write_text('changed')
        with self.assertRaisesRegex(ValueError,'changed probe log'):
            compare(self.fixture('ref'),candidate,'probe','RESULT ',2)

    def test_incomplete_process_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'incomplete or failed probe'):
            compare(self.fixture('ref'),self.fixture('arm',status='TIMEOUT'),'probe','RESULT ',2)

    def review(self):
        return {'group':'probe','prefix':'RESULT ','binary_sha256':'exe','differences':[
            {'reference':'RESULT one','candidate':'RESULT improved','reason':'Preserve full result.'}]}

    def test_reviewed_difference_remains_visible(self):
        result=compare(self.fixture('ref'),self.fixture('arm',('RESULT improved','RESULT two')),
                       'probe','RESULT ',2,self.review())
        self.assertEqual(result['status'],'PASS_WITH_REVIEWED_DIFFERENCES')
        self.assertEqual(result['checks'][0]['status'],'REVIEWED_DIFFERENCE')
        self.assertEqual(result['checks'][0]['reason'],'Preserve full result.')

    def test_review_does_not_hide_another_difference(self):
        result=compare(self.fixture('ref'),self.fixture('arm',('RESULT improved','RESULT wrong')),
                       'probe','RESULT ',2,self.review())
        self.assertEqual(result['status'],'FAIL')

    def test_review_must_match_binary(self):
        review=self.review();review['binary_sha256']='old'
        with self.assertRaisesRegex(ValueError,'another probe'):
            compare(self.fixture('ref'),self.fixture('arm'),'probe','RESULT ',2,review)

    def test_unused_review_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'stale reviewed difference'):
            compare(self.fixture('ref'),self.fixture('arm'),'probe','RESULT ',2,self.review())
