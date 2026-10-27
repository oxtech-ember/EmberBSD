#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Compile the active ksh sources with selected target make and Clang.

The generated directory must contain the normal ksh build's emacs.out and
siglist.out. The check reads it without creating or replacing build outputs.
"""
import argparse
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--make', type=Path, required=True,
                        help='selected target nbmake wrapper using Clang')
    parser.add_argument('--generated-dir', type=Path, required=True,
                        help='ksh object directory with generated headers')
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2],
                        help='OS source tree to check')
    args = parser.parse_args()
    make = args.make.resolve(strict=True)
    source = args.source.resolve(strict=True) / 'bin/ksh'
    generated = args.generated_dir.resolve(strict=True)
    for name in ('emacs.out', 'siglist.out'):
        if not (generated / name).is_file():
            parser.error('generated ksh input is missing: ' + name)
    query = subprocess.run([str(make), '-C', str(source), '-V', '${SRCS}',
                            '-V', '${CC}', '-V', '${CFLAGS}', '-V', '${CPPFLAGS}'],
                           text=True, capture_output=True, check=True)
    lines = query.stdout.splitlines()
    if len(lines) != 4:
        raise SystemExit('target make returned unexpected compiler metadata')
    sources, cc, cflags, cppflags = map(shlex.split, lines)
    if not sources or not cc or not Path(cc[0]).is_absolute():
        raise SystemExit('target make did not select an absolute compiler and sources')
    failures = []
    for name in sources:
        path = source / name
        if path.suffix != '.c' or path.parent != source or not path.is_file():
            raise SystemExit('unexpected ksh source from target make: ' + name)
        command = [*cc, *cflags, *cppflags, '-Werror=deprecated-non-prototype',
                   '-Werror=strict-prototypes', '-fsyntax-only', str(path)]
        result = subprocess.run(command, cwd=generated, text=True, capture_output=True)
        if result.returncode:
            failures.append((name, result.stderr))
    if failures:
        for name, stderr in failures:
            print(name + ':\n' + stderr, end='')
        raise SystemExit('ksh prototype checks failed')
    print(f'PASS: {len(sources)} target ksh source files')


if __name__ == '__main__':
    main()
