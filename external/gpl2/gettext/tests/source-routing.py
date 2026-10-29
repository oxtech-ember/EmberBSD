#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check common gettext and libxml source selection with native bmake."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('make', 'source', 'libxml2-source', 'tooldir', 'shell', 'gmake', 'od', 'pkg-config'):
    p.add_argument('--' + name, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
base = [a.make, '-r', '-m', str(root / 'share/mk'), 'MAKECONF=/dev/null',
        '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root), 'MACHINE=evbarm',
        'MACHINE_ARCH=aarch64', 'USETOOLS=yes', 'TOOLDIR=' + a.tooldir]

def run(directory, *args):
    return subprocess.run([*base, '-C', str(root / directory), *args],
                          capture_output=True, text=True)

for goal in ('obj', 'includes'):
    assert run('external/gpl2/gettext', '-n', goal).returncode == 0, goal
for directory in ('external/bsd/libxml2/gnu', 'external/gpl2/gettext/gnu'):
    for goal in ('obj', 'includes'):
        assert run(directory, 'EMBER_GETTEXT_SOURCE=' + a.source,
                   'EMBER_LIBXML2_SOURCE=' + a.libxml2_source, '-n', goal).returncode == 0, (directory, goal)
for goal in ('all', 'dependall', 'install'):
    result = run('external/gpl2/gettext', '-n', goal)
    assert result.returncode and 'EMBER_GETTEXT_SOURCE' in result.stderr, goal
flags = ['EMBER_GETTEXT_SOURCE=' + a.source, 'EMBER_LIBXML2_SOURCE=' + a.libxml2_source,
         'HOST_SH=' + a.shell, 'HOST_GMAKE=' + a.gmake,
         'HOST_OD=' + a.od, 'HOST_PKG_CONFIG=' + a.pkg_config, 'EMBER_HOST_GNU_TRIPLE=host-test']
gettext = run('external/gpl2/gettext/gnu', *flags, '-V', '${GNU_SOURCE}',
              '-V', '${GNU_CONFIGURE_ENV}', '-V', '${GNU_CONFIGURE_DEPENDENCY}')
xml = run('external/bsd/libxml2/gnu', *flags, '-V', '${GNU_SOURCE}')
assert gettext.returncode == 0 and xml.returncode == 0, gettext.stderr + xml.stderr
lines = gettext.stdout.splitlines()
assert len(lines) == 3 and lines[0] == a.source
assert 'EMBER_GETTEXT_PLAIN_STYLE=yes' in lines[1]
assert lines[2].endswith('/gnu-build/.libs/libxml2.a')
assert xml.stdout.strip() == a.libxml2_source
print('gettext and libxml2 source routing: PASS')
