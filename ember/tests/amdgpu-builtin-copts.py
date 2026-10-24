#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted), native config and make command regression.
"""Check built-in AMDGPU options without compiling a target object."""
import argparse
from pathlib import Path
import shlex
import subprocess


def run(command, cwd):
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path, help='Native config or nbconfig')
    parser.add_argument('make', type=Path, help='Native BSD make or nbmake')
    parser.add_argument('work', type=Path, help='New directory outside the source tree')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    for tool in (args.config, args.make):
        if not tool.is_absolute() or not tool.is_file():
            parser.error('Supply absolute paths to existing native tools')
    work = args.work.resolve()
    if work == source or source in work.parents:
        parser.error('Keep generated files outside the source tree')
    work.mkdir()
    config = work / 'AMDGPU_COPTS'
    config.write_text('include "' + str(source / 'sys/arch/amd64/conf/GENERIC') +
                      '"\nident "AMDGPU_COPTS"\n'
                      'amdgpu* at pci? dev ? function ?\n'
                      'amdgpufb* at amdgpufbbus?\n')
    obj = work / 'obj'
    log = run([str(args.config), '-s', str(source / 'sys'), '-b', str(obj),
               str(config)], source / 'sys/arch/amd64/conf')
    (work / 'config.log').write_text(log)
    # A sentinel compiler makes accidental execution fail. make -n emits the
    # production rule from the generated kernel Makefile; no compiler is run.
    marker = 'AMDGPU_COPTS_COMMAND_ONLY'
    base = [str(args.make), '-m', str(source / 'share/mk'), 'USETOOLS=no',
            'MACHINE=amd64', 'MACHINE_ARCH=x86_64', 'MKCTF=no', 'CC=' + marker]
    cases = {'amdgpu_gfx_v9_0': False, 'amdgpu_dcn10_resource': True,
             'amdgpufb': False}
    for name, floating in cases.items():
        output = run(base + ['-n', name + '.o'], obj)
        (work / (name + '.command')).write_text(output)
        tokens = shlex.split(output)
        starts = [i for i, value in enumerate(tokens) if value == marker]
        if not starts:
            raise AssertionError('Missing compiler command: ' + name)
        command = tokens[starts[-1]:]
        if '&&' in command:
            command = command[:command.index('&&')]
        if '-c' not in command or not any(Path(v).name == name + '.c' for v in command):
            raise AssertionError('Wrong compiler input: ' + name)
        if '-fwrapv' not in command:
            raise AssertionError('Missing signed wrapping semantics: ' + name)
        signed = [v for v in command if v in ('-fwrapv', '-fno-wrapv',
                                             '-fstrict-overflow', '-fno-strict-overflow')]
        if signed[-1] not in ('-fwrapv', '-fno-strict-overflow'):
            raise AssertionError('Signed wrapping semantics were overridden: ' + name)
        for flag in ('-mhard-float', '-msse', '-msse2'):
            if (flag in command) != floating:
                raise AssertionError('Wrong floating-point option ' + flag + ': ' + name)
        if floating:
            for enabled, disabled in (('-mhard-float', '-msoft-float'),
                                      ('-msse', '-mno-sse'), ('-msse2', '-mno-sse2')):
                effective = [v for v in command if v in (enabled, disabled)]
                if effective[-1] != enabled:
                    raise AssertionError('Floating-point option was overridden: ' + enabled)
        print('PASS: ' + name + ' has wrapping arithmetic and its expected float options')


if __name__ == '__main__':
    main()
