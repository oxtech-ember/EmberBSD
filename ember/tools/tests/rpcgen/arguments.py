#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Exercise separate CPP executable/arguments with actual rpcgen and Clang."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ('rpcgen', 'cpp', 'make', 'source', 'work'):
    parser.add_argument('--' + name, required=True, type=Path)
a = parser.parse_args()
a.work.mkdir(parents=True, exist_ok=False)
source = a.work / 'protocol input.x'
source.write_text('''#if !defined(__NetBSD__) || !defined(__x86_64__)
#error target preprocessor flags were lost
#endif
#if __SIZEOF_POINTER__ != 8
#error wrong target pointer size
#endif
%static const char *rpcgen_text = RPCGEN_TEXT;
struct payload { int value; };
''')
compiler = a.work / 'compiler with spaces'
compiler.symlink_to(a.cpp.resolve())
tools = a.work / 'tools'
(tools / 'bin').mkdir(parents=True)
(tools / 'bin/nbrpcgen').symlink_to(a.rpcgen.resolve())
records = []


def invoke(name, argv, cpp, *, expected=0):
    result = subprocess.run(argv, env=dict(os.environ, RPCGEN_CPP=str(cpp)),
                            capture_output=True, timeout=30)
    (a.work / (name + '.stdout')).write_bytes(result.stdout)
    (a.work / (name + '.stderr')).write_bytes(result.stderr)
    records.append(dict(case=name, argv=argv, cpp=str(cpp), exit=result.returncode))
    assert result.returncode == expected, (name, result.returncode, result.stderr.decode())
    if expected == 0:
        output = Path(argv[argv.index('-o') + 1]).read_text()
        assert 'struct payload' in output, name
        assert 'rpcgen_text = "two words";' in output, (name, output)
    elif '-o' in argv:
        assert not Path(argv[argv.index('-o') + 1]).exists(), name


arguments = ['-E', '-x', 'c', '--target=x86_64--netbsd', '-DRPCGEN_TEXT="two words"']
command = [str(a.rpcgen)] + [word for value in arguments for word in ('-X', value)]
invoke('separate-arguments', command + ['-B', '-h', str(source), '-o', str(a.work / 'separate.h')], compiler)
invoke('missing-argument', [str(a.rpcgen), '-X'], compiler, expected=1)
invoke('preprocessor-error', command + ['-X', '-invalid-cpp-option', '-h', str(source), '-o', str(a.work / 'failed.h')], compiler, expected=1)
invoke('argument-limit', [str(a.rpcgen)] + ['-X', '-E'] * 25 + ['-h', str(source)], compiler, expected=1)
# Query the actual common make owner, rather than duplicating TOOL_RPCGEN.
fixture = a.work / 'probe.mk'
fixture.write_text('.include <sys.mk>\n.include <bsd.own.mk>\n')
base = [str(a.make), '-r', '-m', str(a.source / 'share/mk'), '-f', str(fixture),
        'MAKECONF=/dev/null', '_SRC_TOP_=', 'USETOOLS=yes', 'MACHINE=amd64',
        'MACHINE_ARCH=x86_64', 'MACHINE_GNU_PLATFORM=x86_64--netbsd',
        'TOOLDIR=' + str(tools), 'TOOLCHAIN_MISSING=yes',
        'EXTERNAL_TOOLCHAIN=' + str(a.cpp.parent.parent), 'EMBER_COMPILER_FAMILY=clang']
for name, variables in [('clang-mapping', []), ('single-executable', ['CPP=' + str(a.cpp)])]:
    result = subprocess.run(base + variables + ['-V', '${TOOL_RPCGEN}'], capture_output=True, text=True, check=True)
    words = shlex.split(result.stdout.strip())
    assert words[0] == 'RPCGEN_CPP=' + str(a.cpp), (name, words)
    expected = [str(tools / 'bin/nbrpcgen')]
    if name == 'clang-mapping':
        expected += ['-X', '--driver-mode=cpp', '-X', '--target=x86_64--netbsd']
    assert words[1:] == expected, (name, words)
    if name == 'clang-mapping':
        invoke(name, words[1:] + ['-X', arguments[-1], '-B', '-h', str(source), '-o', str(a.work / 'mapped.h')], a.cpp)
    records.append(dict(case=name, make=base + variables, argv=words))
assembly = a.work / 'assembly input.S'
assembly.write_text('#ifndef __ASSEMBLER__\n#error assembler language lost\n#endif\n.word 17\n')
argv = [str(a.cpp), '--driver-mode=cpp', '--target=x86_64--netbsd', '-x', 'assembler-with-cpp', str(assembly)]
result = subprocess.run(argv, capture_output=True, text=True, check=True)
assert '.word 17' in result.stdout
records.append(dict(case='explicit-assembler-language', argv=argv, exit=result.returncode))
(a.work / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
print('PASS: real Clang .x input, separate arguments, spaces, make mapping and error controls')
