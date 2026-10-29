#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Verify sets.subr preserves parent make overrides and stops on make failure."""
import argparse
import os
import shlex
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for flag in ('source', 'make', 'shell', 'runtime', 'path'):
    p.add_argument('--' + flag, required=True)
a = p.parse_args()
root = Path(a.source).resolve(strict=True)
sets = root / 'distrib/sets'
make = Path(a.make).resolve(strict=True)
shell = Path(a.shell).resolve(strict=True)
runtime = Path(a.runtime).resolve(strict=True)
env = os.environ.copy()
env.update(PATH=a.path, NETBSDSRCDIR=str(root), MACHINE='evbarm',
           MACHINE_ARCH='aarch64', HOST_SH=str(shell))
env.pop('MAKEFLAGS', None)
with tempfile.TemporaryDirectory(prefix='sets-mkvars-') as name:
    tmp = Path(name)
    shellscript = tmp / 'probe.sh'
    shellscript.write_text('rundir="$1"\n. "$rundir/sets.subr"\n'
        'printf "CHILD=%s,%s,%s,%s,%s,%s,%s\\n" '
        '"$MACHINE" "$MACHINE_ARCH" "$MKCXX" "$MKLIBCXX" '
        '"$MKX11" "$EMBER_COMPILER_FAMILY" "$HAVE_COMMON_LIBCXX"\n')
    mk = tmp / 'parent.mk'
    mk.write_text('.include <bsd.own.mk>\nall: .PHONY\n'
        '\t@printf "PARENT=%s,%s,%s,%s,%s,%s\\n" '
        '${MACHINE:Q} ${MACHINE_ARCH:Q} ${MKCXX:Q} ${MKLIBCXX:Q} '
        '${MKX11:Q} ${EMBER_COMPILER_FAMILY:Q}\n'
        '\t@MAKE=${MAKE:Q} ${HOST_SH:Q} ' + shlex.quote(str(shellscript)) + ' ' + shlex.quote(str(sets)) + '\n')
    for cxx, x11 in (('no', 'yes'), ('yes', 'no')):
        args = [str(make), '-C', str(sets), '-m', str(root / 'share/mk'),
                '-f', str(mk), 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
                'MKCXX='+cxx, 'MKLIBCXX='+cxx, 'MKX11='+x11,
                'EMBER_COMPILER_FAMILY=clang',
                'EMBER_EXTERNAL_LLVM_RUNTIME='+str(runtime),
                'EMBER_LLVM_RUNTIME_PHASE=full', 'all']
        r = subprocess.run(args, env=env, text=True, capture_output=True, check=True)
        expected = 'evbarm,aarch64,'+cxx+','+cxx+','+x11+',clang'
        assert 'PARENT='+expected in r.stdout, r.stdout
        assert 'CHILD='+expected+','+cxx in r.stdout, r.stdout
        assert not r.stderr, r.stderr
    target = tmp / 'out'; target.mkdir()
    bad = env | {'MAKE':str(tmp / 'missing-make')}
    r = subprocess.run([str(shell), str(sets / 'makeobsolete'), '-t', str(target), 'base'],
                       cwd=sets, env=bad, text=True, capture_output=True)
    assert r.returncode != 0, (r.stdout, r.stderr)
    assert not list(target.iterdir()), 'failed make created set lists'
    assert 'not an identifier' not in r.stderr, r.stderr
print('sets mkvars overrides and failure: PASS')
