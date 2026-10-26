#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Run real dependency helpers with the target selected by the make wrapper."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--make', required=True)
parser.add_argument('--work', required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
work = args.work.resolve()
work.mkdir(exist_ok=False)
command = [args.make, '-C', str(source / 'tools')]
for name in ('MKDEP', 'MKDEPCXX', 'MKDEPOBJC', 'MKDEPFLAGS'):
    command.extend(('-V', '${' + name + '}'))
result = subprocess.run(command, text=True, capture_output=True)
if result.returncode:
    raise SystemExit(result.stdout + result.stderr)
c, cxx, objc, flags = result.stdout.splitlines()
(work / 'target.h').write_text('#ifndef __aarch64__\n#error dependency compiler lost AArch64\n#endif\n')
for suffix, selected, language in (('c', c, ''), ('cc', cxx, '__cplusplus'), ('m', objc, '__OBJC__'), ('S', c, '')):
    text = '#include "target.h"\n'
    if language:
        text += '#ifndef ' + language + '\n#error dependency compiler lost language\n#endif\n'
    input_file = work / ('probe.' + suffix)
    input_file.write_text(text)
    words = shlex.split(selected)
    assignment = words.pop(0)
    if not assignment.startswith('CC='):
        raise SystemExit('unexpected dependency compiler binding')
    env = dict(os.environ, CC=assignment[3:])
    output = work / (suffix + '.d')
    command = [*words, '-f', str(output), '--', *shlex.split(flags),
               '-march=armv8-a+nofp+nosimd', str(input_file)]
    result = subprocess.run(command, cwd=work, env=env, text=True, capture_output=True)
    with (work / 'commands.log').open('a') as stream:
        stream.write(repr(command) + '\nCC=' + env['CC'] + '\n' + result.stdout + result.stderr)
    if result.returncode or str(work / 'target.h') not in output.read_text():
        raise SystemExit('dependency target/language check failed; inspect ' + str(work / 'commands.log'))
print('real C, C++, Objective-C and assembly dependencies retain AArch64 and language')
