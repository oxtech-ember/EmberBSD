#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check native PPP programs select the common prepared source."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('make', 'source', 'tooldir'):
    p.add_argument('--' + name, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
for directory in ('chat', 'pppd', 'pppdump', 'pppstats',
                  'plugins/minconn', 'plugins/passwordfd'):
    base = [a.make, '-r', '-m', str(root / 'share/mk'), '-C',
            str(root / 'external/bsd/ppp/usr.sbin' / directory), 'MAKECONF=/dev/null',
            '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root), 'MACHINE=evbarm',
            'MACHINE_ARCH=aarch64', 'USETOOLS=yes', 'TOOLDIR=' + a.tooldir]
    def run(*args):
        return subprocess.run([*base, *args], capture_output=True, text=True)
    for goal in ('obj', 'includes'):
        assert run('-n', goal).returncode == 0, (directory, goal)
    for goal in ('all', 'dependall', 'install'):
        result = run('-n', goal)
        assert result.returncode != 0 and 'Set EMBER_PPP_SOURCE' in result.stderr, (directory, goal)
    result = run('EMBER_PPP_SOURCE=' + a.source, '-V', '${DIST}')
    assert result.returncode == 0 and result.stdout.strip() == a.source, directory
    assert run('EMBER_PPP_SOURCE=/missing/ppp', '-V', '${DIST}').returncode != 0
    if directory.startswith('plugins/'):
        result = run('EMBER_PPP_SOURCE=' + a.source, '-V', '${.PATH}')
        assert result.returncode == 0 and str(Path(a.source) / 'pppd/plugins') in result.stdout, directory
        result = run('EMBER_PPP_SOURCE=' + a.source, '-V', '${CPPFLAGS}')
        assert result.returncode == 0 and '-I' + a.source in result.stdout, directory
print('PPP source routing: PASS')
