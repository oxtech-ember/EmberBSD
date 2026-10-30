#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Run a built target mkdep with real compiler flag and output options."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--mkdep', required=True, type=Path)
parser.add_argument('--clang', required=True, type=Path)
args = parser.parse_args()
env = dict(os.environ, CC=str(args.clang.resolve()))
with tempfile.TemporaryDirectory(prefix='ember-mkdep-flags-') as temp:
    work = Path(temp)
    (work / 'probe.c').write_text('#ifndef __riscv_float_abi_soft\n#error wrong ABI\n#endif\n#include "probe.h"\n')
    (work / 'probe.h').write_text('/* dependency */\n')
    for number, options in enumerate((('-MMD', '-MF', 'unexpected.d'),
                                       ('-MD', '-MFunexpected.d'))):
        output = work / ('result%d.d' % number)
        command = [str(args.mkdep.resolve()), '-f', str(output), '--',
                   '--target=riscv32--netbsd', '-march=rv32imac_zicsr', '-mabi=ilp32',
                   '-mllvm', '-inline-threshold=0', *options, str(work / 'probe.c')]
        subprocess.run(command, cwd=work, env=env, check=True)
        dependencies = output.read_text()
        assert 'probe.c' in dependencies and 'probe.h' in dependencies
        assert not (work / 'unexpected.d').exists()
    target = work / 'target.d'
    subprocess.run([str(args.mkdep.resolve()), '-f', str(target), '--',
                    '--target=riscv32--netbsd', '-march=rv32imac_zicsr', '-mabi=ilp32',
                    '-MT', 'custom.o', str(work / 'probe.c')],
                   cwd=work, env=env, check=True)
    assert 'custom.o:' in target.read_text()
    result = subprocess.run([str(args.mkdep.resolve()), '-f', str(work / 'missing.d'),
                             '--', '-MF'], cwd=work, env=env, capture_output=True, text=True)
    assert result.returncode != 0 and '-MF requires an argument' in result.stderr
    (work / 'one.d').write_text('probe.o: c\n')
    optional = work / 'optional.d'
    subprocess.run([str(args.mkdep.resolve()), '-d', '-o', '-f', str(optional),
                    str(work / 'one.d')], cwd=work, env=env, check=True)
    assert '.OPTIONAL:' in optional.read_text()
print('mkdep preserves paired compiler flags and owns dependency output')
