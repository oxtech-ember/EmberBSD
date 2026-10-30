#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Check RISC-V ELF selection in the actual private nlist header."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang', required=True, type=Path)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
args = parser.parse_args()
header = args.source.resolve() / 'lib/libc/gen/nlist_private.h'
for width, isa, abi in ((32, 'rv32imac_zicsr', 'ilp32'),
                        (64, 'rv64imac_zicsr', 'lp64')):
    command = [str(args.clang.resolve()), '--target=riscv%d--netbsd' % width,
               '-march=' + isa, '-mabi=' + abi, '-nostdinc', '-dM', '-E',
               '-x', 'c', '-']
    result = subprocess.run(command, input='#include "%s"\n' % header,
                            check=True, capture_output=True, text=True)
    macros = {line.split()[1] for line in result.stdout.splitlines() if line.startswith('#define ')}
    assert 'NLIST_ELF32' in macros
    assert ('NLIST_ELF64' in macros) == (width == 64)
print('RISC-V nlist ELF selection passed for RV32 and RV64')
