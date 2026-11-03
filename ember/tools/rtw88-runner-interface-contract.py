#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native Wi-Fi source contract regression, AI-assisted.
"""Check runner invocation, selected flags and test-runner propagation."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
runners = ('rtw8723d-core-phy-order', 'rtw88-executor',
           'rtw8723d-rx-batch', 'rtw88-sdio-rx-fault')
with tempfile.TemporaryDirectory(prefix='rtw-runner-interface-') as temporary:
    work = Path(temporary)
    cc = work / 'cc'
    cc.write_text('#!' + sys.executable + '''\nimport os, pathlib, sys
with open(os.environ['CC_LOG'], 'a') as f: f.write(' '.join(sys.argv[1:]) + '\\n')
for arg in sys.argv[1:]:
    if arg.endswith('.c') and not pathlib.Path(arg).is_file():
        raise SystemExit('missing source input: ' + arg)
out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
out.write_text('#!/bin/sh\\nexit 0\\n')
out.chmod(0o755)
''')
    cc.chmod(0o755)
    runner = work / 'runner'
    runner.write_text('#!' + sys.executable + '''\nimport os, sys
with open(os.environ['RUN_LOG'], 'a') as f: f.write(sys.argv[1] + '\\n')
os.execv(sys.argv[1], sys.argv[1:])
''')
    runner.chmod(0o755)
    shell = os.environ.get('TEST_SH', '/bin/sh')
    for name in runners:
        for direct in (False, True):
            out = work / (name + ('-direct' if direct else '-default'))
            cc_log, run_log = work / 'cc.log', work / 'run.log'
            cc_log.unlink(missing_ok=True)
            run_log.unlink(missing_ok=True)
            env = dict(os.environ, CC=str(cc), HOST_CC=str(cc), PYTHON=sys.executable,
                       HOST_PYTHON=sys.executable, TEST_RUNNER=str(runner),
                       RTW88_TEST_CFLAGS='-DRTW88_RUNNER_MARKER=1',
                       CC_LOG=str(cc_log), RUN_LOG=str(run_log), TMPDIR=str(work))
            if direct:
                env.pop('CC')
                env.pop('PYTHON')
            else:
                env.pop('HOST_CC')
                env.pop('HOST_PYTHON')
            args = [shell, str(root / 'ember/tools' / (name + '-contract.sh'))]
            if direct:
                args += [str(root), str(out)]
            subprocess.run(args, env=env, check=True, stdout=subprocess.DEVNULL)
            count = 2 if name == 'rtw8723d-rx-batch' else 1
            commands = cc_log.read_text().splitlines()
            invocations = run_log.read_text().splitlines()
            assert len(commands) == count and all('-DRTW88_RUNNER_MARKER=1' in line for line in commands)
            assert len(invocations) == count
            assert (out / 'test').is_file() if direct else not any(work.glob('rtw88-fixture.*'))
print('Wi-Fi runner interfaces: 4 runners, default/direct, flags and TEST_RUNNER PASS')
