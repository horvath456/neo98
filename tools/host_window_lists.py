# SPDX-License-Identifier: GPL-2.0-or-later
"""Parse host window listings for shell regression tests."""
import re

def tops(text):
    """The window lists of a log: one list of dicts per tops= step, with the foreground id."""
    lists = []
    for line in text.split('\n'):
        if match := re.match(r'NEO98 HOST tops foreground=([0-9a-f]+)', line):
            lists.append({'foreground': match[1], 'windows': []})
        elif match := re.match(r'NEO98 HOST top id=([0-9a-f]+) visible=(\d) frame=(-?\d+),(-?\d+),(-?\d+),(-?\d+) '
                               r'style=([0-9a-f]+) process=(\d+) props=(\d) class=(.*?) title=(.*)$', line):
            if lists:
                lists[-1]['windows'].append({
                    'id': match[1], 'visible': match[2] == '1', 'frame': [int(match[i]) for i in range(3, 7)],
                    'style': int(match[7], 16), 'process': int(match[8]), 'props': match[9] == '1',
                    'class': match[10], 'title': match[11]})
    return lists


def window(listing, name):
    return next((w for w in listing['windows'] if w['class'] == name), None)
