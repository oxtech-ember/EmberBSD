#!/usr/bin/env python3
# Copyright (c) 2026 EmberBSD contributors.
# SPDX-License-Identifier: BSD-2-Clause
"""Link the real sigsetjmp.S against distant symbols with selected SDK tools.

This checks branch reach, not target execution or signal-mask behavior.
Pass the directory containing the generated target assym.h with --assym.
"""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
for name in ('cc', 'ld', 'readelf', 'sysroot', 'assym', 'work'):
    p.add_argument('--' + name, type=Path, required=True)
p.add_argument('--target', choices=('aarch64--netbsd', 'aarch64_be--netbsd'),
               default='aarch64--netbsd')
p.add_argument('--baseline', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.work.mkdir(parents=True, exist_ok=False)
work = a.work.resolve()
source = root / 'lib/libc/arch/aarch64/gen/sigsetjmp.S'
(work / 'stubs.S').write_text('''\
.section .far,"ax"
.global __setjmp14, __longjmp14, _setjmp, _longjmp
__setjmp14: ret
__longjmp14: ret
_setjmp: ret
_longjmp: ret
''')
(work / 'layout.ld').write_text('''\
SECTIONS { . = 0x10000; .text : { *(.text) } . = 0x210000; .far : { *(.far) } }
''')

def run(args, *, success=True):
    result = subprocess.run([str(v) for v in args], capture_output=True, text=True)
    if (result.returncode == 0) != success:
        raise RuntimeError(f'{args}:\n{result.stdout}{result.stderr}')
    return result.stdout + result.stderr

common = [a.cc, '--target=' + a.target, '--sysroot=' + str(a.sysroot)]
run(common + ['-c', work / 'stubs.S', '-o', work / 'stubs.o'])

def compile_source(src, name):
    obj = work / (name + '.o')
    run(common + ['-I' + str(a.assym), '-I' + str(root / 'sys'),
                  '-c', src, '-o', obj])
    return obj

def link(obj, name, success):
    return run([a.ld, '-shared', '-T', work / 'layout.ld', obj,
                work / 'stubs.o', '-o', work / (name + '.so')], success=success)

if a.baseline:
    error = link(compile_source(a.baseline, 'baseline'), 'baseline', False)
    if not all(s in error for s in ('R_AARCH64_CONDBR19', 'R_AARCH64_TSTBR14')):
        raise RuntimeError('baseline failed without both expected relocation errors: ' + error)
    print('baseline: both short branches rejected')
obj = compile_source(source, 'fixed')
relocs = run([a.readelf, '-r', obj])
if 'R_AARCH64_CONDBR19' in relocs or 'R_AARCH64_TSTBR14' in relocs:
    raise RuntimeError('short external relocation remains: ' + relocs)
for symbol in ('__setjmp14', '__longjmp14', '_setjmp', '_longjmp'):
    if not any('R_AARCH64_JUMP26' in line and symbol in line.split()
               for line in relocs.splitlines()):
        raise RuntimeError('missing JUMP26 for ' + symbol + ': ' + relocs)
link(obj, 'fixed', True)
print('fixed: four JUMP26 relocations and distant link passed')
