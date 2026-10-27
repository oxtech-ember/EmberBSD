#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted), program link option regression.
"""Check that a static root program gets no dynamic linker option."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def run(command, cwd):
    # Keep the host MAKECONF, MAKEFLAGS and object settings out of the probe.
    env = {'PATH': os.environ.get('PATH', '/usr/bin:/bin')}
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def link_command(make, source, work, ldstatic):
    # A sentinel compiler makes accidental execution fail. make -n emits the
    # production link rule from bsd.prog.mk; no compiler is run.
    marker = 'STATIC_PROG_LINK_ONLY'
    output = run([str(make), '-m', str(source / 'share/mk'), 'MAKECONF=/dev/null',
                  'USETOOLS=no',
                  'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'MKCTF=no',
                  'MKDYNAMICROOT=yes', 'MKDEBUG=no', 'MKMAN=no',
                  # Target startup files are inputs, not link options.
                  'LIBCRT0=', 'LIBCRTI=', 'LIBCRTBEGIN=', 'LIBCRTEND=', 'LIBC=',
                  'CC=' + marker, 'LDSTATIC=' + ldstatic, '-n', 'probe'], work)
    for line in reversed(output.splitlines()):
        tokens = shlex.split(line)
        if marker not in tokens:
            continue
        command = tokens[tokens.index(marker):]
        if '&&' in command:
            command = command[:command.index('&&')]
        if '-o' in command and command[command.index('-o') + 1] == 'probe':
            return command
    raise AssertionError('Missing link command:\n' + output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('make', type=Path, help='Native BSD make or nbmake')
    parser.add_argument('work', type=Path, help='New directory outside the source tree')
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    if not args.make.is_absolute() or not args.make.is_file():
        parser.error('Supply an absolute path to an existing native make')
    work = args.work.resolve()
    if work == source or source in work.parents:
        parser.error('Keep generated files outside the source tree')
    work.mkdir()
    # A subdirectory keeps bsd.init.mk from reading a Makefile.inc above it.
    probe = work / 'probe'
    probe.mkdir()
    (probe / 'probe.c').write_text('int main(void) { return 0; }\n')
    (probe / 'Makefile').write_text('PROG=\tprobe\nBINDIR=\t/sbin\nMAN=\n'
                                    '.include <bsd.prog.mk>\n')
    option = '-Wl,-dynamic-linker=/libexec/ld.elf_so'
    dynamic = link_command(args.make, source, probe, '')
    static = link_command(args.make, source, probe, '-static')
    (work / 'commands.txt').write_text(shlex.join(dynamic) + '\n' +
                                       shlex.join(static) + '\n')
    if option not in dynamic:
        raise AssertionError('Dynamic root program lost its interpreter')
    if '-static' not in static:
        raise AssertionError('Static link lost -static')
    if any(value.startswith('-Wl,-dynamic-linker') for value in static):
        raise AssertionError('Static root program requests an interpreter')
    print('PASS: dynamic and static /sbin program link options')


if __name__ == '__main__':
    main()
