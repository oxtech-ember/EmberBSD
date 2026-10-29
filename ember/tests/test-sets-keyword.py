#!/usr/bin/env python3
"""Check conditional and explicit set exclusions using real sets.subr."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
for name in ('os', 'prefix', 'nbawk'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--subr', type=Path)
a = p.parse_args()
osroot, prefix = a.os.resolve(), a.prefix.resolve()
subr = (a.subr or osroot / 'distrib/sets/sets.subr').resolve()
with tempfile.TemporaryDirectory(prefix='set-keyword-regression-') as t:
    root = Path(t)
    fixture = root / 'entries'
    fixture.write_text('./kept comp-c-bin definitely_disabled\n./kept comp-c-bin\n./removed comp-c-bin\n-./removed\n./removed comp-c-bin definitely_disabled\n./removed comp-c-bin\n./accepted comp-c-bin\n')
    harness = root / 'run.sh'
    harness.write_text('rundir="$NETBSDSRCDIR/distrib/sets"\n. "$SUBR"\nlist_kernel_configs() { :; }\nprint_set_lists() { cat "$INPUT"; }\nlist_set_files\n')
    env = dict(os.environ, PATH=str(prefix/'bin') + ':' + str(prefix/'gnu/bin'),
               LC_ALL='C', NETBSDSRCDIR=str(osroot), MAKE=str(prefix/'bin/bmake'),
               AWK=str(a.nbawk.resolve()), HOST_SH=str(prefix/'bin/mksh'),
               MACHINE='evbarm', MACHINE_ARCH='aarch64', MACHINE_CPU='aarch64',
               SUBR=str(subr), INPUT=str(fixture), MKCOMPAT='no', MKKMOD='no')
    r = subprocess.run([str(prefix/'bin/mksh'), str(harness)], env=env,
                       text=True, capture_output=True)
    if r.returncode:
        raise SystemExit('fixture execution failed: ' + r.stderr[-2000:])
    names = {line.split()[0] for line in r.stdout.splitlines() if line.strip()}
    expected = {'./kept', './accepted'}
    print('PASS' if names == expected else 'FAIL', 'actual=', sorted(names), 'expected=', sorted(expected))
    raise SystemExit(names != expected)
