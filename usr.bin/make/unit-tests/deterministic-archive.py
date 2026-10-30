#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise library freshness using a deterministic real indexed archive."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

p = argparse.ArgumentParser()
for option in ('make', 'cc', 'ar', 'ranlib', 'work'):
    p.add_argument('--' + option, required=True)
a = p.parse_args()
with tempfile.TemporaryDirectory(dir=a.work) as temp:
    root = Path(temp)
    (root / 'a.c').write_text('int a(void) { return 1; }\n')
    subprocess.run([a.cc, '-c', str(root / 'a.c'), '-o', str(root / 'a.o')], check=True)
    (root / 'Makefile').write_text('.SUFFIXES: .a\n.LIBS: .a\nconsumer: libsample.a\n\t@echo linked >> links\n\t@touch $@\nlibsample.a: a.o\n\t' + a.ar + ' crD $@ $>\n\t' + a.ranlib + ' -D $@\n\t@echo built >> count\n')
    def make():
        subprocess.run([a.make, '-r', '-f', 'Makefile', 'consumer'], cwd=root, check=True, stdout=subprocess.DEVNULL)
    make()
    archive = (root / 'libsample.a').read_bytes()
    assert archive[:8] == b'!<arch>\n', 'invalid archive'
    name = archive[8:24].rstrip()
    if name.startswith(b'#1/'):
        name = archive[68:68 + int(name[3:])].rstrip(b'\0')
    assert name in (b'__.SYMDEF', b'__.SYMDEF SORTED', b'/'), 'missing archive index'
    assert int(archive[24:36].strip() or b'0') == 0, 'index is not deterministic'
    make()
    assert (root / 'count').read_text().splitlines() == ['built'], 'unchanged archive rebuilt'
    assert (root / 'links').read_text().splitlines() == ['linked'], 'unchanged consumer relinked'
    old_time = time.time() - 10
    os.utime(root / 'libsample.a', (old_time, old_time))
    os.utime(root / 'consumer', (old_time, old_time))
    (root / 'a.c').write_text('int a(void) { return 2; }\n')
    subprocess.run([a.cc, '-c', str(root / 'a.c'), '-o', str(root / 'a.o')], check=True)
    os.utime(root / 'a.o', (old_time + 2, old_time + 2))
    make()
    assert (root / 'count').read_text().splitlines() == ['built', 'built'], 'changed member not rebuilt'
    assert (root / 'links').read_text().splitlines() == ['linked', 'linked'], 'consumer not relinked'
    make()
    assert (root / 'count').read_text().splitlines() == ['built', 'built'], 'archive rebuilt again'
    assert (root / 'links').read_text().splitlines() == ['linked', 'linked'], 'consumer relinked again'
    (root / 'libsample.a').unlink()
    subprocess.run([a.ar, 'qcS', str(root / 'libsample.a'), str(root / 'a.o')], check=True)
    os.utime(root / 'a.o', (1, 1))
    make()
    assert (root / 'count').read_text().splitlines() == ['built', 'built', 'built'], 'missing TOC not rebuilt'
print('PASS: deterministic archive freshness')
