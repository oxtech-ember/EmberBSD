#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Compile the NLS regex compiler against a prepared RV32 OS sysroot."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang', required=True, type=Path)
parser.add_argument('--sysroot', required=True, type=Path)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
args = parser.parse_args()
source = args.source.resolve()
libc = source / 'lib/libc'
command = [str(args.clang.resolve()), '--target=riscv32--netbsd',
           '-march=rv32imac_zicsr', '-mabi=ilp32',
           '--sysroot=' + str(args.sysroot.resolve()), '-D_LIBC',
           '-D_LIBC_INTERNAL', '-D_REENTRANT', '-DNLS',
           '-I' + str(libc / 'include'), '-I' + str(libc),
           '-I' + str(source / 'sys'), '-I' + str(libc / 'locale'),
           '-I' + str(libc / 'regex'), '-Werror', '-Wextra',
           '-Wsign-compare', '-fsyntax-only', str(libc / 'regex/regcomp.c')]
subprocess.run(command, check=True)
print('NLS regex compiler passes RV32 signedness checks')
