#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the GNU grep OS producer and compatibility entry points."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--host-triple', required=True)
a = p.parse_args()
prefix = a.prefix.resolve(strict=True)
source = a.source.resolve(strict=True)
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temporary:
    base = [str(prefix / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'), '-C',
            str(root / 'external/gpl2/grep/bin'), 'MAKECONF=/dev/null', '_SRC_TOP_=',
            'NETBSDSRCDIR=' + str(root), 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
            'USETOOLS=yes', 'TOOLDIR=' + str(Path(temporary) / 'tools'),
            'HOST_SH=' + str(prefix / 'bin/mksh'), 'HOST_GMAKE=' + str(prefix / 'bin/gmake'),
            'EMBER_HOST_GNU_TRIPLE=' + a.host_triple]
    env = dict(os.environ, PATH=str(prefix / 'bin') + ':' + str(prefix / 'gnu/bin'))
    for nls in ('yes', 'no'):
        result = subprocess.run([*base, 'EMBER_GREP_SOURCE=' + str(source), 'MKNLS=' + nls,
                                 '-V', '${GNU_SOURCE}', '-V', '${GNU_CONFIGURE_ARGS}',
                                 '-V', '${GNU_MAKE_DIRS}'], env=env, capture_output=True,
                                text=True, check=True).stdout.splitlines()
        assert result[0] == str(source), result
        flags = result[1].split()
        assert '--disable-perl-regexp' in flags, flags
        assert ('--with-base-libintl' in flags) == (nls == 'yes'), flags
        assert ('--disable-nls' in flags) == (nls == 'no'), flags
        assert result[2].split() == ['lib', 'src', 'doc'], result
    for target in ('obj', 'includes'):
        result = subprocess.run([*base, '-n', target], env=env, capture_output=True, text=True)
        assert result.returncode == 0, (target, result.stderr)
    for target in ('all', 'dependall', 'install'):
        result = subprocess.run([*base, '-n', target], env=env, capture_output=True, text=True)
        assert result.returncode != 0 and 'Set EMBER_GREP_SOURCE' in result.stderr
for name, option in (('egrep', '-E'), ('fgrep', '-F')):
    wrapper = (root / 'external/gpl2/grep/bin' / (name + '.sh')).read_text()
    assert wrapper.splitlines()[-1] == 'exec /usr/bin/grep ' + option + ' "$@"'
print('GNU grep target source routing: PASS')
