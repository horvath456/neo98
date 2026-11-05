# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run original SHLWAPI character searches and an independent native ABI fixture."""
from pathlib import Path
import json,subprocess,sys
SOURCE_ROOT=Path(__file__).resolve().parents[1]
ROOT=Path(__file__).resolve().parents[4]if 'runtime' in str(Path(__file__).parent)else SOURCE_ROOT
def test():
 sys.path.insert(0,str(ROOT));import neo98
 out=ROOT/'build/shell-string-search';out.mkdir(exist_ok=True);reports={}
 for script,report in [('test_shell_search','results.json'),('test_shell_search_native','fixture-results.json')]:
  command=[str(neo98.analysis_python()),str(SOURCE_ROOT/'tools'/f'{script}.py')]
  if script=='test_shell_search_native':command+=['--source-root',str(SOURCE_ROOT)]
  run=subprocess.run(command,env=neo98.tool_environment(),capture_output=True,text=True);(out/f'{script}.log').write_text(run.stdout+run.stderr)
  if run.returncode:print(run.stdout+run.stderr)
  run.check_returncode();reports[script]=json.loads((out/report).read_text());print(script+': PASS',flush=True)
 return reports
if __name__=='__main__':print(json.dumps(test(),indent=2))
