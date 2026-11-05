# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run original MenuHelp comparisons and the independent native ABI fixture."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def test():
 import json,subprocess,sys
 sys.path.insert(0,str(ROOT));import neo98
 out=ROOT/'build/menu-help';out.mkdir(exist_ok=True);reports={}
 for script,report in [('test_menu_help','results.json'),('test_menu_help_native','fixture-results.json')]:
  run=subprocess.run([str(neo98.analysis_python()),str(ROOT/'tools'/f'{script}.py')],env=neo98.tool_environment(),capture_output=True,text=True);(out/f'{script}.log').write_text(run.stdout+run.stderr)
  if run.returncode:print(run.stdout+run.stderr)
  run.check_returncode();reports[script]=json.loads((out/report).read_text());print(script+': PASS',flush=True)
 return reports
