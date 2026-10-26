#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check UTC declarations without leaking the native NetBSD namespace."""

import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--host-prefix', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--expect-rejection', action='store_true')
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    prefix = args.host_prefix.resolve(strict=True)
    work = args.work.absolute()
    work.mkdir()
    env = dict(os.environ, PATH=os.pathsep.join(str(prefix / p)
               for p in ('bin', 'sbin', 'gnu/bin')))
    for name in ('CC', 'CFLAGS', 'CPPFLAGS', 'LDFLAGS'):
        env.pop(name, None)
    compat = source / 'tools/compat'
    cc = [str(prefix / 'bin/clang'), '--no-default-config', '-std=c11', '-Werror']
    control = work / 'control.c'
    control.write_text('#include "compat_parsedate_features.h"\n'
                       '#ifdef _NETBSD_SOURCE\n#error native namespace leaked\n#endif\n'
                       '#include "parsedate-control.c"\n')

    def compile(arguments):
        result = subprocess.run(cc + arguments, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        with (work / 'compile.log').open('ab') as log:
            log.write(result.stdout)
        return result

    inputs = ['-I', str(compat), '-I', str(compat / 'probes'), str(control)]
    result = compile(inputs + ['-o', str(work / 'control')])
    if args.expect_rejection:
        if result.returncode == 0 or b'native namespace leaked' not in result.stdout:
            raise RuntimeError('the old header did not reproduce the namespace leak')
        print('Baseline: UTC feature header leaks the forbidden namespace')
        return
    result.check_returncode()
    subprocess.run([str(work / 'control')], env=env, check=True)
    result = compile(['-D_NETBSD_SOURCE=1', *inputs, '-o', str(work / 'forbidden')])
    if result.returncode == 0 or b'not permitted' not in result.stdout:
        raise RuntimeError('caller-defined forbidden namespace was not rejected')

    fake = work / 'fake'
    (fake / 'sys').mkdir(parents=True)
    (fake / 'sys/types.h').write_text('''#ifndef FAKE_TYPES_H
#define FAKE_TYPES_H
#if defined(_NETBSD_SOURCE) || _POSIX_C_SOURCE != 200112L || _XOPEN_SOURCE != 600
#error base types were not restricted
#endif
#define RESTRICTED_TYPES_READY 1
typedef long time_t;
#endif
''')
    (fake / 'time.h').write_text('''#ifndef FAKE_TIME_H
#define FAKE_TIME_H
#include <sys/types.h>
#if !defined(RESTRICTED_TYPES_READY) || !defined(_NETBSD_SOURCE)
#error time header was included before the scoped native API request
#endif
struct tm { int unused; };
typedef void *timezone_t;
time_t timegm(struct tm *) __asm__("__timegm50");
time_t mktime_z(timezone_t, struct tm *) __asm__("__mktime_z50");
#endif
''')
    netbsd = work / 'netbsd.c'
    netbsd.write_text('#include "compat_parsedate_features.h"\n'
                      '#ifdef _NETBSD_SOURCE\n#error native namespace leaked\n#endif\n'
                      '#include <time.h>\n'
                      'time_t convert(struct tm *t) { return timegm(t) + mktime_z((timezone_t)0, t); }\n')
    result = compile(['-D__NetBSD__', '-nostdinc', '-I', str(fake), '-I', str(compat),
                      '-c', str(netbsd), '-o', str(work / 'netbsd.o')])
    result.check_returncode()
    symbols = subprocess.check_output([str(prefix / 'bin/llvm-nm'), '-u',
                                       str(work / 'netbsd.o')], env=env, text=True)
    if {line.split()[-1] for line in symbols.splitlines()} != {'__timegm50', '__mktime_z50'}:
        raise RuntimeError('native time ABI aliases were not retained')
    print('UTC features: native control, namespace refusal and NetBSD header/ABI model passed')


if __name__ == '__main__':
    main()
