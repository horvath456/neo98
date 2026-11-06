#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""clear prediction addresses without touching four MiB per discard."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: the Unicorn, FEX and ARM64 engines run on macOS only; Linux runs x86 code natively')  # macOS only; skipped elsewhere
        _sys.exit(77)
import subprocess
from native_app import ROOT
import neo98
import build_fex_host as fex

out = fex.ROOT / 'build/callret-reset'
out.mkdir(parents=True, exist_ok=True)
fex.libraries()
binary = out / 'test-callret-reset'
subprocess.run(['clang++', *fex.compile_flags(), '-O1', str(fex.ROOT / 'tools/test_fex_callret_reset.cpp'),
                '-o', str(binary)], env=neo98.tool_environment(), check=True)
subprocess.run([str(binary)], check=True)
