#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Compile actual unsigned wide conversion consumers against an OS sysroot."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang', required=True, type=Path)
parser.add_argument('--sysroot', required=True, type=Path)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
args = parser.parse_args()
source = args.source.resolve()
libc = source / 'lib/libc'
for name in ('wcstoul', 'wcstoull', 'wcstoumax'):
    command = [str(args.clang.resolve()), '--target=riscv32--netbsd',
               '-march=rv32imac_zicsr', '-mabi=ilp32',
               '--sysroot=' + str(args.sysroot.resolve()), '-D_LIBC',
               '-D_LIBC_INTERNAL', '-D_REENTRANT', '-I' + str(libc / 'include'),
               '-I' + str(libc), '-I' + str(source / 'sys'), '-I' + str(libc / 'locale'),
               '-Werror', '-Wsign-compare', '-fsyntax-only',
               str(libc / 'locale' / (name + '.c'))]
    subprocess.run(command, check=True)
print('unsigned wide conversion consumers compile for RV32')

# Run the actual template and digit mapper on the host. Replace only the
# OS-private locale include with a C-locale adapter; the conversion is unchanged.
import tempfile
with tempfile.TemporaryDirectory(prefix='ember-wcstoul-') as temp:
    work = Path(temp)
    template = (libc / 'locale/_wcstoul.h').read_text()
    private_include = '#include "setlocale_local.h"'
    if template.count(private_include) != 1:
        raise RuntimeError('unexpected locale include')
    template = template.replace(private_include, '')
    (work / 'template.h').write_text(template)
    (work / 'test.c').write_text(r'''
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <wchar.h>
#include <wctype.h>
#define _DIAGASSERT(x) assert(x)
#define __UNCONST(x) ((void *)(uintptr_t)(const void *)(x))
#define _current_locale() ((locale_t)0)
#define iswspace_l(c, l) ((void)(l), iswspace(c))
#define _FUNCNAME ember_wcstoul
#define __UINT unsigned long
#define __UINT_MAX ULONG_MAX
#include "__wctoint.h"
#include "template.h"
int main(void) {
    wchar_t *end;
    const wchar_t *s;
    s = L"  +0x2az!"; errno = 0;
    assert(ember_wcstoul(s, &end, 0) == 42 && *end == L'z' && errno == 0);
    s = L"?42"; errno = 0;
    assert(ember_wcstoul(s, &end, 10) == 0 && end == s && errno == 0);
    s = L"-1!"; errno = 0;
    assert(ember_wcstoul(s, &end, 10) == ULONG_MAX && *end == L'!' && errno == 0);
    s = L"999999999999999999999999999999999999!"; errno = 0;
    assert(ember_wcstoul(s, &end, 10) == ULONG_MAX && *end == L'!' && errno == ERANGE);
    s = L"123"; errno = 0;
    assert(ember_wcstoul(s, &end, 1) == 0 && errno == EINVAL);
    return 0;
}
''')
    exe = work / 'test'
    subprocess.run([str(args.clang.resolve()), '-fsanitize=address,undefined',
                    '-I' + str(libc / 'locale'), str(work / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('wide conversion behavior passed on host with ASan/UBSan')
