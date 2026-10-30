#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Compare real ARM target macros from compile and dependency recipes."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--make', required=True, type=Path)
parser.add_argument('--clang', required=True, type=Path)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
args = parser.parse_args()
source = args.source.resolve()
with tempfile.TemporaryDirectory(prefix='ember-dependency-isa-') as temp:
    work = Path(temp)
    (work / 'probe.c').write_text('/* no includes required */\n')
    (work / 'Makefile').write_text('SRCS=probe.c\n.include <bsd.own.mk>\n'
                                   '.include <bsd.sys.mk>\n.include <bsd.dep.mk>\n')
    for float_abi, thumb in (('softfp', '-mthumb'), ('hard', '-marm')):
        flags = '-march=armv7-a -mfpu=neon -mfloat-abi=' + float_abi + ' ' + thumb
        command = [str(args.make.resolve()), '-m', str(source / 'share/mk'), '-n',
                   'MAKECONF=/dev/null', '_SRC_TOP_=', 'MACHINE=evbarm',
                   'MACHINE_ARCH=earmv7hf', 'MKSOFTFLOAT=no',
                   'NETBSDSRCDIR=' + str(source), 'EXTERNAL_TOOLCHAIN=/selected/llvm',
                   'TOOLDIR=' + str(work / 'tools'), 'DESTDIR=' + str(work / 'dest'), 'CFLAGS=' + flags,
                   'USETOOLS=yes', 'MKDEP=selected-mkdep', 'CFLAGS=' + flags,
                   'probe.d', 'probe.o']
        result = subprocess.run(command, cwd=work, check=True, capture_output=True, text=True)
        lines = [shlex.split(line) for line in result.stdout.splitlines()]
        dep = next(line for line in lines if line and line[0] == 'selected-mkdep')
        compile_cmd = next(line for line in lines if '-c' in line and 'probe.c' in line)
        def macros(recipe):
            isa = [flag for flag in recipe if flag.startswith('-m')]
            cmd = [str(args.clang.resolve()), '--target=armv7--netbsdelf-eabihf',
                   *isa, '-dM', '-E', '-x', 'c', '-']
            result = subprocess.run(cmd, input='', capture_output=True, text=True, check=True)
            keys = ('__ARM_FP', '__ARM_PCS_VFP', '__thumb__', '__ARM_NEON')
            return {key: next((line for line in result.stdout.splitlines()
                               if line.startswith('#define ' + key + ' ')), None)
                    for key in keys}
        assert macros(dep) == macros(compile_cmd), (float_abi, thumb, dep, compile_cmd)
print('ARM dependency and compilation ISA macros agree for softfp/thumb and hard/arm')
