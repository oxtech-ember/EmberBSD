#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Check RISC-V float rounding against a prepared OS header sysroot."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang', required=True, type=Path)
parser.add_argument('--nm', required=True, type=Path)
parser.add_argument('--sysroot', required=True, type=Path)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
args = parser.parse_args()
source = args.source.resolve()
implementation = source / 'lib/libc/arch/riscv/gen/flt_rounds.c'
cases = ((32, 'rv32imac_zicsr', 'ilp32', False, False),
         (32, 'rv32imafdc_zicsr', 'ilp32', False, True),
         (32, 'rv32imafdc_zicsr', 'ilp32d', True, False),
         (64, 'rv64imac_zicsr', 'lp64', False, False),
         (64, 'rv64imafdc_zicsr', 'lp64', False, True),
         (64, 'rv64imafdc_zicsr', 'lp64d', True, False))
with tempfile.TemporaryDirectory(prefix='ember-riscv-flt-rounds-') as temp:
    work = Path(temp)
    for width, isa, abi, hard, inline_frrm in cases:
        obj = work / (isa + '-' + abi + '.o')
        command = [str(args.clang.resolve()), '--target=riscv%d--netbsd' % width,
                   '-march=' + isa, '-mabi=' + abi, '--sysroot=' + str(args.sysroot.resolve()),
                   '-I' + str(source / 'lib/libc/include'), '-I' + str(source / 'lib/libc'),
                   '-Werror', '-Wmissing-prototypes', '-c', str(implementation), '-o', str(obj)]
        subprocess.run(command, check=True)
        symbols = subprocess.run([str(args.nm.resolve()), '-u', str(obj)],
                                 check=True, capture_output=True, text=True).stdout
        if ('_fpgetround' in symbols) != hard:
            raise RuntimeError('unexpected fpgetround dependency for ' + abi)
        assembly = subprocess.run(command[:-4] + ['-S', '-o', '-', str(implementation)],
                                  check=True, capture_output=True, text=True).stdout
        if ('frrm' in assembly) != inline_frrm:
            raise RuntimeError('unexpected hardware rounding read for ' + isa + ' ' + abi)
    # Execute the real soft branch on the host. Only the libc namespace header
    # is replaced; this branch does not depend on target header definitions.
    (work / 'namespace.h').write_text('/* no names needed by this branch */\n')
    (work / 'check.c').write_text('int __flt_rounds(void);\nint main(void) { return __flt_rounds() != 1; }\n')
    exe = work / 'rounds'
    subprocess.run([str(args.clang.resolve()), '-D__riscv_float_abi_soft=1', '-I' + str(work),
                    str(implementation), str(work / 'check.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('RISC-V float rounding soft/double references and soft result passed')
