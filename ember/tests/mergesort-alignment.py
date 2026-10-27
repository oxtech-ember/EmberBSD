#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the actual mergesort and run its native alignment fixture.

Use --cc, --cflags-json (argv array), and a new --work directory. --baseline
is the old merge.c, which must fail Clang's null-subtraction diagnostic.
--sanitize runs ASan/UBSan additionally; it is not a target or Valgrind result.
The adapter changes only libc symbol/RCS/assert plumbing and supplies the
native host's missing mergesort_r declaration. The algorithm is unmodified.
"""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True, type=Path)
    parser.add_argument('--cflags-json', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if not args.cc.is_absolute() or not args.cc.is_file():
        parser.error('--cc must select one existing absolute compiler')
    flags = json.loads(args.cflags_json.read_text())
    if not isinstance(flags, list) or not all(isinstance(v, str) for v in flags):
        parser.error('--cflags-json must contain a string array')
    args.work.mkdir(parents=True, exist_ok=False)
    work = args.work.resolve()
    root = Path(__file__).resolve().parents[2]
    (work / 'namespace.h').write_text('/* Retain public symbols in this native test. */\n')
    (work / 'host.h').write_text("""#include <assert.h>
#include <stdlib.h>
#ifndef __RCSID
#define __RCSID(s)
#endif
#ifndef _DIAGASSERT
#define _DIAGASSERT(e) assert(e)
#endif
int mergesort(void *, size_t, size_t, int (*)(const void *, const void *));
int mergesort_r(void *, size_t, size_t,
    int (*)(const void *, const void *, void *), void *);
""")
    common = [str(args.cc), *flags, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
              '-I' + str(work), '-include', str(work / 'host.h')]
    source = root / 'lib/libc/stdlib/merge.c'
    fixture = root / 'ember/tests/mergesort-alignment-fixture.c'
    commands = []

    def run(argv, name):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=90)
        (work / (name + '.log')).write_text(result.stdout + result.stderr)
        commands.append(dict(argv=argv, status=result.returncode))
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        return result

    def check(name, extra):
        binary = work / name
        compiled = run(common + extra + [str(source), str(fixture), '-o', str(binary)], name + '-compile')
        if compiled.returncode:
            raise RuntimeError(compiled.stderr)
        result = run([str(binary)], name + '-execute')
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        print(name + ': PASS', flush=True)

    check('production', [])
    if args.baseline:
        result = run(common + ['-c', str(args.baseline), '-o', str(work / 'baseline.o')], 'baseline-compile')
        if result.returncode == 0 or 'null-pointer-subtraction' not in result.stderr:
            raise RuntimeError('baseline did not reproduce null-pointer-subtraction')
        print('baseline null-pointer subtraction: PASS', flush=True)
    if args.sanitize:
        check('sanitized', ['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])


if __name__ == '__main__':
    main()
