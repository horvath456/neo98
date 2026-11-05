#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""a new integer host address must not silently become a scalar."""
from pathlib import Path
import subprocess
import tempfile
import state_layout as layout
from native_app import ROOT
import neo98


def main():
    with tempfile.TemporaryDirectory(dir=layout.ROOT / 'build') as folder:
        source, obj = Path(folder) / 'address.cpp', Path(folder) / 'address.o'
        source.write_text('''namespace Example {
using Address = __UINTPTR_TYPE__;
typedef __UINTPTR_TYPE__ Other;
using Alias = Address;
struct State { Alias interior; Other second; unsigned long long count; };
State state;
}
unsigned long scratch() { using Local = __UINTPTR_TYPE__; return Local(&Example::state); }
''')
        subprocess.run(['clang++', '-std=c++17', '-g', '-O0', '-c', str(source), '-o', str(obj)],
                       env=neo98.tool_environment(), check=True)
        def read():
            unit, table = layout.Unit(str(source), obj), {}
            reached = set()
            for name, symbol, kind in unit.variables:
                reached |= layout.reach(table, layout.describe(unit, kind, table))
            return table, reached
        table, reached = read()
        problems = layout.address_problems(table, reached)
        assert len(problems) == 2 and all('Example::' in p for p in problems), problems
        assert table['int8']['kind'] == 'scalar'
        saved = layout.ADDRESSES.copy()
        try:
            layout.ADDRESSES.update(('Example::Address', 'Example::Other'))
            table, reached = read()
            assert not layout.address_problems(table, reached)
            assert table['address']['kind'] == 'address' and 'address' in reached
        finally:
            layout.ADDRESSES.clear()
            layout.ADDRESSES.update(saved)
    print('PASS: stored using/typedef aliases require address rules; aliases preserve relocation; counters and local arithmetic do not')


if __name__ == '__main__':
    main()
