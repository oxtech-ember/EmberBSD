#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check that the TDB library excludes executable-only PIE flags."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--nbmake', required=True)
p.add_argument('--source', required=True)
p.add_argument('--python', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]

def flags(*args):
    result = subprocess.run([a.nbmake, *args, 'MKPIE=yes', '-V', '${CFLAGS}',
                             '-V', '${LDFLAGS}'], cwd=root, text=True,
                            capture_output=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == 2, result.stdout
    return [shlex.split(line) for line in lines]

common = ('EMBER_TDB_SOURCE=' + a.source, 'HOST_PYTHON=' + a.python)
library = flags('-C', str(root / 'external/bsd/tdb/lib'), *common)
assert '-fPIE' not in library[0] and '-fpie' not in library[0], library
assert '-pie' not in library[1], library
assert '-Werror' in library[0], library
with tempfile.TemporaryDirectory(prefix='tdb-pie-test-') as tmp:
    makefile = Path(tmp) / 'Makefile'
    makefile.write_text('PROG=pie-probe\n.include <bsd.prog.mk>\n')
    program = flags('-f', str(makefile))
    assert '-fPIE' in program[0] and '-pie' in program[1], program
print('TDB shared-library flags and ordinary program PIE: PASS')
