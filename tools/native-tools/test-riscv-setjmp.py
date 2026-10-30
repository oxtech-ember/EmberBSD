#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Preprocess and assemble the real RISC-V setjmp code for selected ABIs."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang', required=True, type=Path)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
args = parser.parse_args()
source = args.source.resolve()
assembly = [source / 'lib/libc/arch/riscv/gen' / name
            for name in ('__setjmp14.S', '_setjmp.S')]
cases = ((32, 'rv32imac_zicsr', 'ilp32', False),
         (32, 'rv32imafdc_zicsr', 'ilp32d', True),
         (64, 'rv64imac_zicsr', 'lp64', False),
         (64, 'rv64imafdc_zicsr', 'lp64d', True))
with tempfile.TemporaryDirectory(prefix='ember-riscv-setjmp-') as temp:
    include = Path(temp)
    (include / 'machine').symlink_to(source / 'sys/arch/riscv/include')
    # These generated offsets affect operands, not the code-selection macros.
    names = sorted(set(re.findall(r'\bJB_[A-Z0-9]+\b',
                                  ''.join(asm.read_text() for asm in assembly))))
    names.append('SIG_SETMASK')
    (include / 'assym.h').write_text(''.join('#define %s 0\n' % name for name in names))
    for asm in assembly:
        for width, isa, abi, hard in cases:
            command = [str(args.clang.resolve()), '--target=riscv%d--netbsd' % width,
                       '-march=' + isa, '-mabi=' + abi, '-nostdinc', '-I' + str(include),
                       '-I' + str(source / 'sys'), '-I' + str(source / 'lib/libc/arch/riscv'),
                       '-x', 'assembler-with-cpp', str(asm)]
            preprocessed = subprocess.run(command + ['-E'], check=True, capture_output=True, text=True).stdout
            for instruction in ('frcsr', 'fsd', 'fscsr', 'fld'):
                present = bool(re.search(r'\b' + instruction + r'\b', preprocessed))
                if present != hard:
                    raise RuntimeError('%s %s: unexpected %s selection' % (isa, abi, instruction))
            subprocess.run(command + ['-c', '-o', str(include / (asm.stem + '-' + abi + '.o'))], check=True)
print('RISC-V setjmp soft and double ABI preprocessing and assembly passed')
