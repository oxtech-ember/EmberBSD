#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check native bmake debug splitting of a target GNU-produced ELF."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('host-prefix', 'work-dir'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
host = a.host_prefix
root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='gnu-debug-', dir=a.work_dir) as tmp:
    work = Path(tmp)
    (work / 'sample.c').write_text('int sample(void) { return 7; }\n')
    env = dict(os.environ, PATH=str(host / 'bin') + ':' + str(host / 'gnu/bin'))
    subprocess.run([str(host / 'bin/clang'), '--target=aarch64--netbsd', '-g', '-c',
                    str(work / 'sample.c'), '-o', str(work / 'input.o')], check=True, env=env)
    (work / 'Makefile').write_text('''PROGS= sample
SRCS.sample=
MKDEBUG= yes
MKMAN= no
OBJCOPY= %s
.include <bsd.prog.mk>
sample: input.o
\tcp input.o sample
sample.debug: sample __progdebug
''' % (host / 'bin/llvm-objcopy'))
    command = [str(host / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
               'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=no', 'sample.debug']
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stderr + result.stdout)
    for name in ('sample', 'sample.debug'):
        check = subprocess.run([str(host / 'bin/llvm-readelf'), '-h', str(work / name)],
                               env=env, capture_output=True, text=True)
        if check.returncode or 'AArch64' not in check.stdout:
            raise SystemExit(name + ': missing AArch64 ELF')
    sections = subprocess.check_output([str(host / 'bin/llvm-readelf'), '-S', str(work / 'sample')],
                                       env=env, text=True)
    if '.gnu_debuglink' not in sections:
        raise SystemExit('missing standard debug link')
print('PASS: GNU target ELF uses native bmake debug splitting')
