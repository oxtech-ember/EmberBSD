#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Compile the NLS regex engine against a prepared RV32 OS sysroot."""
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
           '-Wno-unused-parameter', '-Wsign-compare', '-fsyntax-only', str(libc / 'regex/regexec.c')]
subprocess.run(command, check=True)
print('NLS regex engine passes RV32 signedness checks')

# Exercise the engine's actual sentinel definitions for both signed and
# unsigned wint_t and for either signedness of plain char.
import tempfile
engine = (libc / 'regex/engine.c').read_text()
start = engine.index('#define\tWOUT\t')
stop = engine.index('/* sflags */', start)
definitions = engine[start:stop]
with tempfile.TemporaryDirectory(prefix='ember-regex-sentinel-') as temp:
    work = Path(temp)
    for wint in ('int32_t', 'uint32_t'):
        for sentinel in (-1, -129):
            test = work / 'sentinel.c'
            test.write_text('#include <stdint.h>\n#include <assert.h>\n'
                            'typedef ' + wint + ' wint_t;\n#define OUT (' + str(sentinel) + ')\n'
                            + definitions + r'''
int main(void) {
    wint_t special[] = { WOUT, BOL, EOL, BOLEOL, NOTHING, BOW, EOW, BADCHAR, NWBND };
    wint_t chars[] = { 0, 65, 255, 0x10ffff };
    for (unsigned i = 0; i < sizeof(special)/sizeof(special[0]); ++i)
        assert(NONCHAR(special[i]));
    for (unsigned i = 0; i < sizeof(chars)/sizeof(chars[0]); ++i)
        assert(!NONCHAR(chars[i]));
    return 0;
}
''')
            exe = work / 'test'
            subprocess.run([str(args.clang.resolve()), '-Werror', '-Wall', '-Wextra',
                            '-fsanitize=address,undefined', str(test), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
print('regex sentinel classification passed signed/unsigned host cases with ASan/UBSan')
