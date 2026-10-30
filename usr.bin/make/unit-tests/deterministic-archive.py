#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise library freshness using a deterministic real indexed archive."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
for option in ('make', 'cc', 'ar', 'ranlib', 'work'):
    p.add_argument('--' + option, required=True)
a = p.parse_args()
with tempfile.TemporaryDirectory(dir=a.work) as temp:
    root = Path(temp)
    (root / 'a.c').write_text('int a(void) { return 1; }\n')
    subprocess.run([a.cc, '-c', str(root / 'a.c'), '-o', str(root / 'a.o')], check=True)
    (root / 'Makefile').write_text('.SUFFIXES: .a\n.LIBS: .a\nconsumer: libsample.a\n\t@echo linked >> links\n\t@touch $@\nlibsample.a: a.o\n\t' + a.ar + ' cr $@ $>\n\t' + a.ranlib + ' $@\n\t@echo built >> count\n')
    def make():
        subprocess.run([a.make, '-r', '-f', 'Makefile', 'consumer'], cwd=root, check=True, stdout=subprocess.DEVNULL)
    make()
    make()
    assert (root / 'count').read_text().splitlines() == ['built'], 'unchanged archive rebuilt'
    assert (root / 'links').read_text().splitlines() == ['linked'], 'unchanged consumer relinked'
    (root / 'a.c').write_text('int a(void) { return 2; }\n')
    subprocess.run([a.cc, '-c', str(root / 'a.c'), '-o', str(root / 'a.o')], check=True)
    os.utime(root / 'consumer', (1, 1))
    archive_time = (root / 'libsample.a').stat().st_mtime
    os.utime(root / 'a.o', (archive_time + 2, archive_time + 2))
    make()
    assert (root / 'count').read_text().splitlines() == ['built', 'built'], 'changed member not rebuilt'
    assert (root / 'links').read_text().splitlines() == ['linked', 'linked'], 'consumer not relinked'
    (root / 'libsample.a').unlink()
    subprocess.run([a.ar, 'qcS', str(root / 'libsample.a'), str(root / 'a.o')], check=True)
    os.utime(root / 'a.o', (1, 1))
    make()
    assert (root / 'count').read_text().splitlines() == ['built', 'built', 'built'], 'missing TOC not rebuilt'
print('PASS: deterministic archive freshness')
