#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the rc runner invocation with a non-executable script.
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--sh', required=True, help='absolute selected native shell')
args = parser.parse_args()
if not os.path.isabs(args.sh) or not os.access(args.sh, os.X_OK):
    parser.error('--sh must be an absolute selected executable')
rc = Path(__file__).with_name('target-acceptance-rc').read_text()
line = next(line for line in rc.splitlines() if line.startswith('if /bin/sh ./run-target.sh '))
command = line.removeprefix('if ').removesuffix('; then')
command = command.replace('/bin/sh', args.sh, 1)
with tempfile.TemporaryDirectory() as directory:
    runner = Path(directory) / 'run-target.sh'
    runner.write_text('test "$1" = --target && test "$2" = aarch64 && test "$3" = .\n')
    runner.chmod(0o644)
    subprocess.run([args.sh, '-c', command], cwd=directory, check=True)
print('PASS: rc runs the non-executable target script with its arguments')
