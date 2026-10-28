#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise GNU configure with an actual restricted native-routing PATH."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('host-prefix', 'native-routing', 'work-dir'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(dir=a.work_dir) as tmp:
    work = Path(tmp)
    src = work / 'source'
    src.mkdir()
    conf = src / 'configure'
    conf.write_text("printf 'all:; @:\\n' > Makefile\n")
    makefile = work / 'Makefile'
    makefile.write_text('''GNU_SOURCE= %s
GNU_OBJDIR= %s
GNU_BUILD_TRIPLE= host
GNU_HOST_TRIPLE= target
HOST_SH= %s
HOST_GMAKE= %s
.include "%s"
all: ${GNU_OBJDIR}/Makefile
''' % (src, work / 'build', a.host_prefix / 'bin/mksh',
       a.host_prefix / 'bin/gmake', root / 'share/mk/bsd.gnutarget.mk'))
    env = dict(os.environ, PATH=str(a.native_routing), TMPDIR=str(a.work_dir))
    cmd = [str(a.host_prefix / 'bin/bmake'), '-r', '-f', str(makefile), 'all']
    result = subprocess.run(cmd, cwd=work, env=env, text=True, capture_output=True)
    if result.returncode:
        raise SystemExit('restricted PATH configure failed: ' + result.stdout + result.stderr)
    (work / 'build/Makefile').unlink()
    conf.write_text('exit 1\n')
    failed = subprocess.run(cmd, cwd=work, env=env, text=True, capture_output=True)
    if failed.returncode == 0 or (work / 'build/Makefile').exists():
        raise SystemExit('failed configure did not fail cleanly')
print('GNU target restricted PATH: PASS')
