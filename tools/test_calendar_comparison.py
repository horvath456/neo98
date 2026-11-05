# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
import json
from pathlib import Path
import tempfile
import unittest
from compare_calendar_contract import LOCALES, DATES, compare
from wine_conformance import digest


class CalendarComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)

    def fixture(self,name,changed=False,success_error='12345678',failure_error='00000057',missing=False):
        folder=self.root/name;folder.mkdir();lines=[]
        for mode in ('NUMBER','STRING'):
            for locale in LOCALES:
                for calendar in range(14):
                    count=4 if calendar in range(1,13) and locale!=0xffff else 0
                    value='value=2029' if mode=='NUMBER' else 'text=2029'
                    if changed and mode=='NUMBER' and locale==0x409 and calendar==1:value='value=2030'
                    lines.append(f'test.c:1: CALENDAR_{mode} locale={locale:04x} calendar={calendar} n={count} error={success_error if count else failure_error} {value}')
        for locale in LOCALES:
            for date in DATES:
                for alt in (0,1):
                    lines.append(f'test.c:2: CALENDAR_DATE locale={locale:04x} date={date} alt={alt} n=3 error={success_error} text=99')
        log=folder/'TEST.LOG';log.write_text('\n'.join(lines[:-1] if missing else lines)+'\n')
        (folder/'manifest.json').write_text(json.dumps({'groups':[{'name':'calendar_contract','sha256':'same-exe'}]}))
        (folder/'results.json').write_text(json.dumps({'groups':{'calendar_contract':{'status':'PASS',
            'processes':[{'log':log.name,'sha256':digest(log)}]}}}))
        return folder

    def test_success_last_error_does_not_hide_output_comparison(self):
        report=compare(self.fixture('ref'),self.fixture('arm',success_error='00000057'))
        self.assertEqual(report['status'],'PASS');self.assertEqual(len(report['checks']),336)

    def test_wrong_number_fails(self):
        self.assertEqual(compare(self.fixture('ref'),self.fixture('arm',changed=True))['status'],'FAIL')

    def test_failure_last_error_is_compared(self):
        self.assertEqual(compare(self.fixture('ref'),self.fixture('arm',failure_error='00000006'))['status'],'FAIL')

    def test_missing_case_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'incomplete calendar matrix'):
            compare(self.fixture('ref'),self.fixture('arm',missing=True))

    def test_changed_log_is_rejected(self):
        arm=self.fixture('arm');(arm/'TEST.LOG').write_text('changed')
        with self.assertRaisesRegex(ValueError,'changed calendar log'):compare(self.fixture('ref'),arm)
