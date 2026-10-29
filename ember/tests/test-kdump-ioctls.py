#!/usr/bin/env python3
"""Check generated ioctl lookup and aliases with ordinary synthetic values."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
for name in ('generator', 'shell', 'awk', 'sed', 'host-cc', 'target-cc'):
    p.add_argument('--' + name, required=True)
p.add_argument('--target', default='aarch64--netbsd')
a = p.parse_args()
a.generator = str(Path(a.generator).resolve())
with tempfile.TemporaryDirectory(prefix='kdump-generator-') as t:
    root = Path(t)
    inc = root / 'usr/include'
    inc.mkdir(parents=True)
    fixture = inc / 'fixture.h'
    fixture.write_text("#define _IO(g,n) (((g) << 8) | (n))\n"
                       "#define TEST_EQUAL_A _IO('P', 1)\n"
                       "#define TEST_EQUAL_B _IO('P', 1)\n"
                       "#define TEST_DIFFERENT _IO('P', 2)\n")
    environment = dict(os.environ, AWK=a.awk, CC=a.target_cc + ' --target=' + a.target,
                       SED=a.sed, DESTDIR=str(root), NETBSDSRCDIR=str(root), CPPFLAGS='')
    result = subprocess.run([a.shell, a.generator, str(fixture)], cwd=root,
                            env=environment, capture_output=True, text=True, check=True)
    start = result.stdout.find('#include "ktrace.h"')
    if start < 0:
        raise SystemExit('generated lookup missing')
    code = result.stdout[start:].replace('#include "ktrace.h"', '', 1)
    for name in ('TEST_EQUAL_A', 'TEST_EQUAL_B', 'TEST_DIFFERENT'):
        if code.count('.name = "' + name + '"') != 1 or 'return "' + name + '"' not in code:
            raise SystemExit('generated lookup/table omitted ' + name)
    # Compile the exact emitted lookup/table with a minimal, isolated prologue;
    # unrelated platform header inventory is checked by the real OS build.
    unit = root / 'generated.c'
    unit.write_text('typedef unsigned long u_long;\n#define NULL ((void *)0)\n'
                    'struct ioctlinfo { const char *name; const char *expr; unsigned long value; };\n'
                    '#include "fixture.h"\n' + code)
    for label, cc, flags in [('host', a.host_cc, []), ('target', a.target_cc, ['--target=' + a.target])]:
        r = subprocess.run([cc, *flags, '-ffreestanding', '-std=c11', '-Werror',
                            '-I' + str(inc), '-fsyntax-only', str(unit)], capture_output=True, text=True)
        if r.returncode:
            print(label + ' compile FAIL: ' + r.stderr[-2500:])
            raise SystemExit(1)
        print(label + ' compile PASS; both equal names and distinct name retained')

    equal_order = re.findall(r'if \(val == (TEST_EQUAL_[AB])\)', code)
    if len(equal_order) != 2 or set(equal_order) != {'TEST_EQUAL_A', 'TEST_EQUAL_B'}:
        raise SystemExit('unexpected equal-value lookup order')
    unit.write_text(unit.read_text() + '''
#include <string.h>
int main(void) {
    unsigned found_a = 0, found_b = 0, found_d = 0;
    const struct ioctlinfo *p;
    const char *first = ioctlname(TEST_EQUAL_A);
    const char *distinct = ioctlname(TEST_DIFFERENT);
    if (!first || strcmp(first, FIRST_NAME) != 0) return 1;
    if (!distinct || strcmp(distinct, "TEST_DIFFERENT") != 0) return 2;
    if (ioctlname(_IO('P', 3)) != NULL) return 3;
    for (p = ioctlinfo; p->name; ++p) {
        if (!strcmp(p->name, "TEST_EQUAL_A") && p->value == TEST_EQUAL_A) found_a++;
        if (!strcmp(p->name, "TEST_EQUAL_B") && p->value == TEST_EQUAL_B) found_b++;
        if (!strcmp(p->name, "TEST_DIFFERENT") && p->value == TEST_DIFFERENT) found_d++;
    }
    return found_a != 1 || found_b != 1 || found_d != 1;
}
'''.replace('FIRST_NAME', '"' + equal_order[0] + '"'))
    exe = root / 'lookup-test'
    subprocess.run([a.host_cc, '-std=c11', '-Werror', '-I' + str(inc), str(unit),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('host lookup PASS: first equal name, distinct, unknown, and both aliases')
