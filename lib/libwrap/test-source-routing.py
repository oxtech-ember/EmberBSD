#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check native TCP wrappers consumers select the common source."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
for name in ('make', 'source', 'tooldir'):
    p.add_argument('--' + name, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
for directory in ('lib/libwrap', 'usr.sbin/tcpdchk', 'usr.sbin/tcpdmatch', 'usr.sbin/rtadvd'):
    base = [a.make, '-r', '-m', str(root / 'share/mk'), '-C', str(root / directory),
            'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root),
            'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes', 'TOOLDIR=' + a.tooldir]
    def run(*args):
        return subprocess.run([*base, *args], capture_output=True, text=True)
    assert run('-n', 'obj').returncode == 0, directory
    assert (run('-n', 'includes').returncode != 0) == (directory == 'lib/libwrap'), directory
    for goal in ('all', 'dependall', 'install'):
        result = run('-n', goal)
        assert result.returncode != 0 and 'Set EMBER_TCP_WRAPPERS_SOURCE' in result.stderr, (directory, goal)
    result = run('EMBER_TCP_WRAPPERS_SOURCE=' + a.source, '-V', '${TCPWRAPPERS_SRCDIR}')
    assert result.returncode == 0 and result.stdout.strip() == a.source, directory
    assert run('EMBER_TCP_WRAPPERS_SOURCE=/missing/tcp-wrappers', '-V', '${TCPWRAPPERS_SRCDIR}').returncode != 0
    if directory == 'lib/libwrap':
        with tempfile.TemporaryDirectory(prefix='tcp-wrappers-includes-') as tmp:
            dest = Path(tmp)
            header = dest / 'usr/include/tcpd.h'
            header.parent.mkdir(parents=True)
            header.write_text('stale header\n')
            future = time.time() + 3600
            os.utime(header, (future, future))
            flags = ('EMBER_TCP_WRAPPERS_SOURCE=' + a.source, 'DESTDIR=' + str(dest),
                     'INSTPRIV=-U -M ' + str(dest / 'METALOG') + ' -D ' + str(dest) +
                     ' -N ' + str(root / 'etc'))
            result = run(*flags, 'includes')
            assert result.returncode == 0, result.stderr
            assert header.read_bytes() == (Path(a.source) / 'tcpd.h').read_bytes()
            stamp = header.stat().st_mtime_ns
            result = run(*flags, 'includes')
            assert result.returncode == 0 and header.stat().st_mtime_ns == stamp, result.stderr
print('TCP wrappers source routing: PASS')
