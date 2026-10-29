#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Build dynamic alloca regressions with a selected target compiler and header."""
import argparse
import itertools
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
for option in ('cc', 'cxx', 'nm', 'header', 'output'):
    p.add_argument('--' + option, type=Path, required=True)
a = p.parse_args()
source = Path(__file__).with_name('alloca-compat.c')
a.output.mkdir(parents=True, exist_ok=False)
with tempfile.TemporaryDirectory(prefix='ember-alloca-header-') as td:
    include = Path(td)
    shutil.copyfile(a.header, include / 'stdlib.h')
    for language, compiler, standard in [('c', a.cc, 'c11'), ('c++', a.cxx, 'c++17')]:
        for opt, fp, no_builtin in itertools.product(('0', '2'), ('keep', 'omit'), (False, True)):
            name = f'{language.replace("+", "x")}-O{opt}-{fp}' + ('-no-builtin' if no_builtin else '')
            output = a.output / name
            args = [str(compiler), '-x', language, '-std=' + standard, '-O' + opt,
                    '-fno-omit-frame-pointer' if fp == 'keep' else '-fomit-frame-pointer',
                    '-D_NETBSD_SOURCE=1', '-include', str(include / 'stdlib.h'), '-Wall', '-Wextra', '-Werror']
            if no_builtin:
                args.append('-fno-builtin')
            subprocess.run(args + [str(source), '-o', str(output)], check=True)
            symbols = subprocess.run([str(a.nm), '-u', str(output)], check=True,
                                     capture_output=True, text=True).stdout
            if re.search(r'\balloca(?:@\S+)?\s*$', symbols, re.M):
                raise SystemExit(name + ': external alloca reference')
            print('PASS compile and no alloca import:', name, flush=True)
print('Run all generated executables on the target; compilation does not test stack behaviour.')
