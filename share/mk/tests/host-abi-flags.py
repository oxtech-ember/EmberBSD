#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check selected host ABI flags compose after OS-local host flags."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-prefix', required=True, type=Path)
p.add_argument('--work-dir', required=True, type=Path)
a = p.parse_args()
for path in (a.host_prefix, a.work_dir):
    if not path.is_absolute() or not path.is_dir():
        p.error('select existing absolute directories')

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='host-abi-flags-', dir=a.work_dir) as temporary:
    osroot = Path(temporary) / 'os'
    tool = osroot / 'tools/probe'
    source = osroot / 'crypto/probe'
    tool.mkdir(parents=True)
    source.mkdir(parents=True)
    shutil.copyfile(root / 'tools/Makefile.host', osroot / 'tools/Makefile.host')
    (tool / 'Makefile').write_text('''HOSTPROGNAME= host-abi-probe
HOST_SRCDIR= crypto/probe
.include "${.CURDIR}/../Makefile.host"
''')

    def write_source(local_cflags):
        lines = ['.include <bsd.hostinit.mk>',
                 'HOSTPROG= host-abi-probe',
                 'HOST_CPPFLAGS+= -DLOCAL_BEFORE_ASSIGN',
                 'HOST_CPPFLAGS= -DLOCAL_ASSIGN --sysroot=/target-only -Wp,-iremap,/target-only',
                 'HOST_CPPFLAGS+= -DLOCAL_AFTER_ASSIGN']
        if local_cflags:
            lines += ['HOST_CFLAGS+= -DLOCAL_CFLAGS_BEFORE',
                      'HOST_CFLAGS= -O3 -DLOCAL_CFLAGS_ASSIGN',
                      'HOST_CFLAGS+= -DLOCAL_CFLAGS_AFTER']
        lines += ['HOST_CXXFLAGS= -DLOCAL_CXXFLAGS',
                  'HOST_LDFLAGS= -Wl,-dead_strip']
        (source / 'Makefile').write_text('\n'.join(lines) + '\n')

    env = dict(os.environ, PATH=str(a.host_prefix / 'bin') + ':' +
               str(a.host_prefix / 'gnu/bin'), TMPDIR=str(a.work_dir))
    mv = shutil.which('mv', path=env['PATH'])
    if mv is None:
        raise SystemExit('selected SDK-only PATH does not provide mv')
    command = [str(a.host_prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
               '-C', str(tool), 'NETBSDSRCDIR=' + str(root),
               'MAKECONF=/dev/null', 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
               'USETOOLS=yes', 'TOOLDIR=' + str(a.host_prefix),
               'HOST_CC=' + str(a.host_prefix / 'bin/clang'),
               'HOST_CXX=' + str(a.host_prefix / 'bin/clang++'),
               'HOST_AR=' + str(a.host_prefix / 'bin/llvm-ar'),
               'HOST_RANLIB=' + str(a.host_prefix / 'bin/llvm-ranlib'),
               'HOST_LD=' + str(a.host_prefix / 'bin/ld64.lld')]

    def query(*abi_flags):
        argv = command + list(abi_flags)
        for name in ('HOST_CPPFLAGS', '_HOST_SELECTED_CPPFLAGS',
                     '_HOST_SELECTED_CFLAGS', '_HOST_SELECTED_CXXFLAGS',
                     '_HOST_SELECTED_LDFLAGS'):
            argv += ['-V', '${' + name + '}']
        result = subprocess.run(argv, cwd=osroot, env=env, text=True,
                                capture_output=True)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        return result.stdout.splitlines()

    write_source(local_cflags=True)
    selected = query(
        'EMBER_HOST_ABI_CPPFLAGS=-DABI_CPP --sysroot=/selected-host -Wp,-iremap,/selected-host',
        'EMBER_HOST_ABI_CFLAGS=-O2 -DABI_C',
        'EMBER_HOST_ABI_CXXFLAGS=-DABI_CXX',
        'EMBER_HOST_ABI_LDFLAGS=-Wl,-dead_strip -DABI_LD')
    if len(selected) != 5:
        raise SystemExit('unexpected bmake variable query output: ' + repr(selected))
    raw_cpp, cpp, c, cxx, ld = selected
    if '--sysroot=/target-only' in raw_cpp or '-Wp,-iremap,/target-only' in raw_cpp:
        raise SystemExit('target-only source flags escaped Makefile.host filtering')
    if ('-DLOCAL_ASSIGN' not in cpp or '-DLOCAL_AFTER_ASSIGN' not in cpp or
            '-DLOCAL_BEFORE_ASSIGN' in cpp or '-DABI_CPP' not in cpp or
            '--sysroot=/selected-host' not in cpp or
            '-Wp,-iremap,/selected-host' not in cpp):
        raise SystemExit('selected C++ flags did not compose after source flags: ' + cpp)
    if '-DLOCAL_CFLAGS_ASSIGN' not in c or '-DLOCAL_CFLAGS_AFTER' not in c or '-DABI_C' not in c:
        raise SystemExit('selected C flags did not compose after source flags: ' + c)
    if '-DLOCAL_CXXFLAGS' not in cxx or '-DABI_CXX' not in cxx:
        raise SystemExit('selected C++ ABI flags were lost: ' + cxx)
    if '-Wl,-dead_strip' not in ld or '-DABI_LD' not in ld:
        raise SystemExit('selected link ABI flags were lost: ' + ld)

    write_source(local_cflags=False)
    empty = query('EMBER_HOST_ABI_CPPFLAGS=', 'EMBER_HOST_ABI_CFLAGS=',
                  'EMBER_HOST_ABI_CXXFLAGS=', 'EMBER_HOST_ABI_LDFLAGS=')
    if len(empty) != 5:
        raise SystemExit('unexpected empty-ABI query output: ' + repr(empty))
    if '-DLOCAL_ASSIGN' not in empty[1] or '-DLOCAL_AFTER_ASSIGN' not in empty[1]:
        raise SystemExit('empty selected flags erased local host CPPFLAGS: ' + empty[1])
    if '-O' not in empty[2]:
        raise SystemExit('empty selected CFLAGS erased the OS host default: ' + empty[2])
    # Kernel/library Makefiles also use bsd.sys.mk's .c.lo suffix rule.
    libdir = osroot / 'crypto/libvers'
    libdir.mkdir(parents=True)
    (libdir / 'probe.c').write_text('''
#ifndef LOCAL_CPP
#error missing source-local CPPFLAGS
#endif
#ifndef ABI_CPP
#error missing selected ABI CPPFLAGS
#endif
#ifndef ABI_C
#error missing selected ABI CFLAGS
#endif
int probe(void) { return 0; }
''')
    (libdir / 'Makefile').write_text('''
.include <bsd.own.mk>
HOST_CPPFLAGS= -DLOCAL_CPP --sysroot=/target-only -Wp,-iremap,/target-only
HOST_CFLAGS= -DLOCAL_C
all: probe.lo
.include <bsd.sys.mk>
''')
    lib_command = [str(a.host_prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
                   '-C', str(libdir), 'MAKECONF=/dev/null',
                   'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes',
                   'TOOLDIR=' + str(a.host_prefix),
                   'MV=' + mv,
                   'HOST_CC=' + str(a.host_prefix / 'bin/clang'),
                   'EMBER_HOST_ABI_CPPFLAGS=-DABI_CPP --sysroot=/selected-host',
                   'EMBER_HOST_ABI_CFLAGS=-DABI_C', 'probe.lo']
    result = subprocess.run(lib_command, cwd=osroot, env=env, text=True,
                            capture_output=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    (libdir / 'probe.lo').unlink()
    legacy_command = [str(a.host_prefix / 'bin/bmake'), '-r', '-n',
                      '-m', str(root / 'share/mk'), '-C', str(libdir),
                      'MAKECONF=/dev/null', 'MACHINE=evbarm',
                      'MACHINE_ARCH=aarch64', 'USETOOLS=yes',
                      'TOOLDIR=' + str(a.host_prefix), '-W', 'probe.c',
                      'HOST_CC=' + str(a.host_prefix / 'bin/clang'), 'probe.lo']
    legacy = subprocess.run(legacy_command, cwd=osroot, env=env, text=True,
                            capture_output=True)
    if legacy.returncode or '--sysroot=/target-only' not in legacy.stdout:
        raise SystemExit('legacy HOST_CPPFLAGS lost source sysroot behavior: ' +
                         legacy.stdout + legacy.stderr)

print('PASS: native host ABI flags compose with local host and GNU flags')
