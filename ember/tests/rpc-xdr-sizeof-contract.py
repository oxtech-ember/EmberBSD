#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run actual xdr_sizeof with OS RPC headers on the selected native toolchain.

Select --cc, --cflags-json (an argv array), and a new --work directory.
--baseline accepts the previous source: it must fail the callback cast diagnostic.
--sanitize additionally runs ASan/UBSan; this is not a target or Valgrind result.
Only namespace/RCS identifiers and host integer aliases are adapted for native
execution. The XDR layout, callback types and implementation come from the OS.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--cflags-json', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if not args.cc.is_absolute() or not args.cc.is_file():
        parser.error('--cc must select one existing absolute compiler')
    flags = json.loads(args.cflags_json.read_text())
    if not isinstance(flags, list) or not all(isinstance(v, str) for v in flags):
        parser.error('--cflags-json must contain a string array')
    args.work.mkdir(parents=True, exist_ok=False)
    work = args.work.resolve()
    (work / 'rpc').mkdir()
    for name in ('types.h', 'xdr.h'):
        shutil.copyfile(root / 'common/include/rpc' / name, work / 'rpc' / name)
    shutil.copyfile(root / 'include/netconfig.h', work / 'netconfig.h')
    (work / 'namespace.h').write_text('/* Keep public symbols in this native test. */\n')
    (work / 'host.h').write_text("""#include <sys/types.h>
#include <stdint.h>
#ifndef __RCSID
#define __RCSID(s)
#endif
#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#ifndef __NetBSD__
typedef int64_t longlong_t;
typedef uint64_t u_longlong_t;
#endif
""")
    version = subprocess.check_output([str(args.cc), '--version'], text=True)
    warning = '-Wcast-function-type-mismatch' if 'clang' in version else '-Wcast-function-type'
    common = [str(args.cc), *flags, '-std=gnu11', '-O1', '-Wall', '-Wextra',
              '-Wno-unused-parameter', '-Werror', warning, '-I' + str(work),
              '-include', str(work / 'host.h')]
    source = (root / 'lib/libc/rpc/xdr_sizeof.c').read_text()
    fixture = root / 'ember/tests/rpc-xdr-sizeof-fixture.c'
    commands = []

    def run(argv, name):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=90)
        (work / (name + '.log')).write_text(result.stdout + result.stderr)
        commands.append(dict(argv=argv, status=result.returncode))
        (work / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        return result

    def check(name, text, extra=(), failure=False):
        src, binary = work / (name + '.c'), work / name
        src.write_text(text)
        result = run(common + list(extra) + [str(src), str(fixture), '-o', str(binary)], name + '-compile')
        if result.returncode:
            raise RuntimeError(result.stderr)
        result = run([str(binary)], name + '-execute')
        if result.returncode != (1 if failure else 0):
            raise RuntimeError(name + ': unexpected fixture result ' + str(result.returncode))
        print(name + ': PASS', flush=True)

    check('production', source)
    for callback in ('x_getlong', 'x_getbytes'):
        start = source.index('\n' + callback + '(XDR *xdrs __unused,')
        pos = source.index('return (FALSE);', start)
        mutant = source[:pos] + source[pos:].replace('return (FALSE);', 'return (TRUE);', 1)
        check(callback + '-mutation', mutant, failure=True)
    if args.baseline:
        src = work / 'baseline.c'
        src.write_bytes(args.baseline.read_bytes())
        result = run(common + ['-c', str(src), '-o', str(work / 'baseline.o')], 'baseline-compile')
        if result.returncode == 0 or warning[2:] not in result.stderr:
            raise RuntimeError('baseline did not reproduce ' + warning)
        print('baseline cast rejection: PASS', flush=True)
    if args.sanitize:
        check('sanitized', source, ['-fsanitize=address,undefined', '-fno-sanitize-recover=all'])


if __name__ == '__main__':
    main()
