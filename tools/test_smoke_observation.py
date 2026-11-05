#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Focused checks for independent visual and liveness smoke observations."""

import smoke_matrix


def row(kind, visual):
    return {'id': 'julietta.exe', 'class': kind, 'visual_class': visual,
            'detail': 'same visible window'}


baseline = {'revision': 'before', 'programs': [row('window-empty', 'window-empty')]}
current = {'programs': [row('timeout', 'window-empty')]}
change = smoke_matrix.compare(baseline, current)
assert not change['regressions']
assert not change['progress']
assert change['liveness_changes'] == [{
    'id': 'julietta.exe', 'from': 'window-empty', 'to': 'timeout',
    'visual_class': 'window-empty', 'detail': 'same visible window'}]

different = {'programs': [row('timeout', 'window-content')]}
change = smoke_matrix.compare(baseline, different)
assert len(change['regressions']) == 1
assert not change['liveness_changes']

legacy = {'revision': 'before', 'programs': [
    {'id': 'julietta.exe', 'class': 'window-empty', 'detail': 'old result'}]}
change = smoke_matrix.compare(legacy, current)
assert len(change['liveness_changes']) == 1

# Older timeout rows retain their window observations, but lack visual_class.
legacy_timeout = {'programs': [{'id': 'julietta.exe', 'class': 'timeout',
                              'windows': [{'colours': 1}]}]}
change = smoke_matrix.compare(legacy_timeout, baseline)
assert len(change['liveness_changes']) == 1
assert not change['progress'] and not change['regressions']
# Missing observations do not establish that the old window was empty.
legacy_timeout['programs'][0]['windows'] = [{}]
change = smoke_matrix.compare(legacy_timeout, baseline)
assert not change['liveness_changes']

timeout_log = """\
PROBE started=1 state=0 code=0 calls=0 callbacks=1256 windows=2 error=
PROBE_STOP reason=time-limit calls=0
PROBE_WINDOW hwnd=1025 parent=0 owner=0 visible=1 x=0 y=0 w=300 h=480 class=4d6b text= canvas=1 drawn=1 client_x=3 client_y=22 client_w=294 client_h=455 visible_pixels=133770 colours=1
"""
observed = smoke_matrix.classify({}, timeout_log, 0, None)
assert observed['class'] == 'timeout'
assert observed['visual_class'] == 'window-empty'
assert observed['stop'] == 'time-limit'

print('SMOKE OBSERVATION PASS')
