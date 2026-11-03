#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check release man settings do not request nonexistent host-tool manuals."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--make', type=Path, required=True)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--heimdal-source', type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
env = dict(os.environ, PATH=f'{args.prefix}/bin:{args.prefix}/gnu/bin',
           SHELL=str(args.prefix / 'bin/mksh'))

def variables(directory):
    command = [str(args.make), '-C', str(source / directory), 'MKMAN=yes',
               f'EMBER_HEIMDAL_SOURCE={args.heimdal_source}', '-V', 'MAN', '-V', 'MKMAN']
    return subprocess.run(command, env=env, check=True, stdout=subprocess.PIPE,
                          text=True).stdout.splitlines()

for name in ('asn1_compile', 'compile_et', 'slc'):
    result = variables('tools/' + name)
    if result != ['', 'yes']:
        raise SystemExit(f'{name}: unexpected MAN/MKMAN: {result!r}')
result = variables('usr.bin/true')
if result != ['true.1', 'yes']:
    raise SystemExit(f'target manual settings changed: {result!r}')
print('host tools omit manuals with MKMAN=yes; target manuals retained: PASS')
