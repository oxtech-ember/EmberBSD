#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check native ls declarations using the real make-selected compiler flags."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--make', required=True)
parser.add_argument('--ls', type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
command = [args.make, '-C', str(source / 'tools/ls'),
           '-V', '${HOST_CC}', '-V', '${HOST_CPPFLAGS}', '-V', '${HOST_CFLAGS}']
result = subprocess.run(command, text=True, capture_output=True)
if result.returncode:
    raise SystemExit(result.stdout + result.stderr)
compiler, cppflags, cflags = result.stdout.splitlines()
command = [*shlex.split(compiler), *shlex.split(cppflags), *shlex.split(cflags),
           '-Werror=implicit-function-declaration', '-fsyntax-only', str(source / 'bin/ls/util.c')]
result = subprocess.run(command, text=True, capture_output=True)
if result.returncode:
    raise SystemExit('native ls declaration check failed:\n' + result.stdout + result.stderr)
if args.ls:
    with tempfile.TemporaryDirectory(prefix='native-ls-') as directory:
        for name in ('plain', '\u00e9', '\u754c', 'A\tB'):
            (Path(directory) / name).touch()
        env = dict(os.environ, LC_ALL='C.UTF-8')
        result = subprocess.run([str(args.ls.resolve()), '-1q', directory],
                                env=env, text=True, capture_output=True)
        if result.returncode or set(result.stdout.splitlines()) != {'plain', '\u00e9', '\u754c', 'A?B'}:
            raise SystemExit('native ls UTF-8/control display failed:\n' + result.stdout + result.stderr)
print('native ls declarations and requested runtime checks passed')
